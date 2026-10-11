#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
namespace strata {

// What the node tells /diagnostics about its input: are messages arriving, and can
// they be placed in the map frame? ROS-free so every state can be unit-tested; times
// are passed in as steady seconds.
//
// The counts are per input topic (all sensor frames together) and are published in
// every status from the first one, so a status never has to wait for a window close
// to show that the TF lookup is failing.

// Text from outside the node (a tf2 exception, a frame id from a message header) as
// one line for a status value or message: valid UTF-8 (a byte that is not part of a
// well-formed sequence becomes '?'), control characters and line separators become a
// space, runs of spaces collapse, no space at either end, and at most max_bytes bytes
// (cut at a character boundary, the last three bytes then being "...").
inline std::string oneLine(const std::string& in, std::size_t max_bytes = 120) {
  std::string out;
  for (std::size_t i = 0; i < in.size();) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
    bool ok = len > 0 && i + len <= in.size();
    for (std::size_t k = 1; ok && k < len; ++k) ok = (static_cast<unsigned char>(in[i + k]) & 0xC0) == 0x80;
    const unsigned char c1 = ok && len > 1 ? static_cast<unsigned char>(in[i + 1]) : 0;
    const unsigned char c2 = ok && len > 2 ? static_cast<unsigned char>(in[i + 2]) : 0;
    if (ok && len == 2) ok = c >= 0xC2;                                                // overlong
    if (ok && len == 3) ok = !(c == 0xE0 && c1 < 0xA0) && !(c == 0xED && c1 >= 0xA0);  // overlong, surrogates
    if (ok && len == 4) ok = c <= 0xF4 && !(c == 0xF0 && c1 < 0x90) && !(c == 0xF4 && c1 >= 0x90);
    std::string piece;
    if (!ok) {
      piece = "?"; i += 1;
    } else {
      const bool blank = (len == 1 && (c <= 0x20 || c == 0x7F)) ||   // C0 controls, space, DEL
                         (len == 2 && c == 0xC2 && c1 <= 0x9F) ||    // C1 controls (U+0080..U+009F)
                         (len == 3 && c == 0xE2 && c1 == 0x80 && (c2 == 0xA8 || c2 == 0xA9));  // U+2028, U+2029
      piece = blank ? std::string(" ") : in.substr(i, len);
      i += len;
    }
    if (piece == " " && (out.empty() || out.back() == ' ')) continue;
    out += piece;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (out.size() > max_bytes) {
    std::size_t cut = max_bytes > 3 ? max_bytes - 3 : 0;   // first byte to drop: not inside a character
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
    out.resize(cut);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    out += "...";
  }
  return out;
}

// Messages the input callback handled, since the node started.
struct InputTally {
  std::int64_t received{0};                     // every message: TF failed or resolved
  std::int64_t integrated{0};                   // messages that went into the map
  std::int64_t tf_failures{0};                  // messages dropped for a failed TF lookup
  std::int64_t tf_failures_since_integrate{0};  // the running streak; 0 once one integrates
  // Why the last lookup of the running streak failed (the tf2 exception) and for which
  // sensor frame, each as one line (oneLine). Set exactly while the streak runs: both
  // are cleared when a message integrates, so a status that integrates never shows an
  // error that is over. Failures between integrated messages stay in the node log.
  std::string last_tf_error;
  std::string failed_frame;

  void tfFailure(const std::string& sensor_frame, const std::string& error) {
    ++received; ++tf_failures; ++tf_failures_since_integrate;
    failed_frame = oneLine(sensor_frame, 64);
    if (failed_frame.empty()) failed_frame = "(empty frame_id)";
    last_tf_error = oneLine(error, 120);
    if (last_tf_error.empty()) last_tf_error = "(no error text)";   // never empty while the streak runs
  }
  // The TF lookup resolved. The message may still be dropped (a zero stamp with time
  // windows), which does not end a failure streak.
  void tfResolved() { ++received; }
  void integratedOne() {
    ++integrated; tf_failures_since_integrate = 0;
    last_tf_error.clear(); failed_frame.clear();
  }
};

enum InputLevel : int { kInputOk = 0, kInputWarn = 1, kInputError = 2 };   // DiagnosticStatus levels

enum class InputKind {
  kFlowing,          // nothing to say about the input: the window statistics stand
  kWaitingForInput,  // no message yet, within startup_timeout_s
  kNoInputYet,       // no message yet, startup_timeout_s passed
  kInputLost,        // messages arrived, then stopped for input_timeout_s
  kWaitingForTf,     // messages arrive, none integrated yet, within startup_timeout_s
  kTfMissing,        // ... and startup_timeout_s passed
  kTfFailing,        // was integrating; lookups fail, nothing integrated for kTfNoteAfterS
  kTfLost,           // ... and for input_timeout_s
};

struct InputState {
  InputTally tally;
  double since_input_s{0.0};      // since the last message (since start before the first)
  double since_integrate_s{0.0};  // since the last integrated message (since start before the first)
  double input_timeout_s{5.0};
  double startup_timeout_s{30.0};
};

struct InputVerdict {
  InputKind kind{InputKind::kFlowing};
  int level{kInputOk};
  std::string message;
  // true: the verdict replaces the level and message found so far. false: a note at
  // level OK that only adds to the message (kWaitingForTf, kTfFailing).
  bool overrides{false};
};

// A failure streak is named in the message once nothing has integrated for more than
// this long (steady seconds, counted from the last integrated message, not from the
// first failure). One failed lookup leaves a gap of two message intervals, so with
// input faster than 2 Hz it stays below this and shows in tf_failures_since_integrate
// only; at 2 Hz or slower a single failed lookup is named until the next message
// integrates.
constexpr double kTfNoteAfterS = 1.0;

// The messages name the lookup that fails (global frame -> the frame id of the last
// message that failed), not its cause: the transform may be absent, too old or too
// new for the stamp, or the frame id may be unknown. The cause is last_tf_error.
inline InputVerdict judgeInput(const InputState& s, const std::string& topic,
                               const std::string& global_frame) {
  InputVerdict v;
  char buf[480];
  const InputTally& n = s.tally;
  const bool any_input = n.received > 0, any_integrated = n.integrated > 0;
  const long long streak = static_cast<long long>(n.tf_failures_since_integrate);
  const char* frame = n.failed_frame.c_str();
  if (!any_input) {
    // Drivers can take tens of seconds to start: wait startup_timeout_s before ERROR.
    v.overrides = true;
    if (s.since_input_s > s.startup_timeout_s) {
      std::snprintf(buf, sizeof(buf), "no input on %s yet after %.0f s", topic.c_str(), s.since_input_s);
      v.kind = InputKind::kNoInputYet; v.level = kInputError; v.message = buf;
    } else {
      v.kind = InputKind::kWaitingForInput; v.level = kInputOk; v.message = "waiting for input on " + topic;
    }
  } else if (s.since_input_s > s.input_timeout_s) {
    std::snprintf(buf, sizeof(buf), "no input on %s for %.1f s (sensor, driver or link down?)",
                  topic.c_str(), s.since_input_s);
    v.kind = InputKind::kInputLost; v.level = kInputError; v.message = buf; v.overrides = true;
  } else if (streak > 0 && s.since_integrate_s > (any_integrated ? s.input_timeout_s : s.startup_timeout_s)) {
    // Messages arrive but none can be placed: the pose source (localizer, TF tree) is
    // missing, stale, or does not know this sensor frame.
    std::snprintf(buf, sizeof(buf), "messages arrive on %s but the TF lookup %s -> %s has "
                  "failed %lld times; nothing integrated for %.1f s", topic.c_str(),
                  global_frame.c_str(), frame, streak, s.since_integrate_s);
    v.kind = any_integrated ? InputKind::kTfLost : InputKind::kTfMissing;
    v.level = any_integrated ? kInputError : kInputWarn; v.message = buf; v.overrides = true;
  } else if (streak > 0 && !any_integrated) {
    // The same condition inside the startup grace: a localizer may still be coming up,
    // so the level stays OK, but the message says what is (not) happening.
    std::snprintf(buf, sizeof(buf), "no usable TF %s -> %s yet: %lld message(s) received on %s, "
                  "0 integrated, %lld TF lookup(s) failed (WARN after startup_timeout_s = %.0f s)",
                  global_frame.c_str(), frame, static_cast<long long>(n.received), topic.c_str(), streak,
                  s.startup_timeout_s);
    v.kind = InputKind::kWaitingForTf; v.level = kInputOk; v.message = buf;
  } else if (streak > 0 && s.since_integrate_s > kTfNoteAfterS) {
    std::snprintf(buf, sizeof(buf), "TF lookup %s -> %s failing: last %lld message(s) on %s not "
                  "integrated, nothing integrated for %.1f s (ERROR after input_timeout_s = %.0f s)",
                  global_frame.c_str(), frame, streak, topic.c_str(), s.since_integrate_s, s.input_timeout_s);
    v.kind = InputKind::kTfFailing; v.level = kInputOk; v.message = buf;
  }
  return v;
}

// Fold the verdict into the level and message found so far ("ok", or the window and
// stamp warnings). An overriding verdict replaces both. A note leaves the level alone
// and replaces only a bare "ok"; after another warning it is appended.
inline void applyInput(const InputVerdict& v, int& level, std::string& message) {
  if (v.kind == InputKind::kFlowing) return;
  if (v.overrides) { level = v.level; message = v.message; return; }
  message = message == "ok" ? v.message : message + "; " + v.message;
}

}  // namespace strata
