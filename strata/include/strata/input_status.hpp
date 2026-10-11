#pragma once
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

// Messages the input callback handled, since the node started.
struct InputTally {
  std::int64_t received{0};                     // every message: TF failed or resolved
  std::int64_t integrated{0};                   // messages that went into the map
  std::int64_t tf_failures{0};                  // messages dropped for a failed TF lookup
  std::int64_t tf_failures_since_integrate{0};  // the running streak; 0 once one integrates

  void tfFailure() { ++received; ++tf_failures; ++tf_failures_since_integrate; }
  // The TF lookup resolved. The message may still be dropped (a zero stamp with time
  // windows), which does not end a failure streak.
  void tfResolved() { ++received; }
  void integratedOne() { ++integrated; tf_failures_since_integrate = 0; }
};

enum InputLevel : int { kInputOk = 0, kInputWarn = 1, kInputError = 2 };   // DiagnosticStatus levels

enum class InputKind {
  kFlowing,          // nothing to say about the input: the window statistics stand
  kWaitingForInput,  // no message yet, within startup_timeout_s
  kNoInputYet,       // no message yet, startup_timeout_s passed
  kInputLost,        // messages arrived, then stopped for input_timeout_s
  kWaitingForTf,     // messages arrive, none integrated yet, within startup_timeout_s
  kTfMissing,        // ... and startup_timeout_s passed
  kTfFailing,        // was integrating; every lookup has failed for kTfNoteAfterS
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

// A failure streak this long (steady seconds) is named in the message. Shorter ones
// (one late transform) show in tf_failures_since_integrate only, so the message does
// not flicker.
constexpr double kTfNoteAfterS = 1.0;

inline InputVerdict judgeInput(const InputState& s, const std::string& topic,
                               const std::string& global_frame) {
  InputVerdict v;
  char buf[320];
  const InputTally& n = s.tally;
  const bool any_input = n.received > 0, any_integrated = n.integrated > 0;
  const long long streak = static_cast<long long>(n.tf_failures_since_integrate);
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
    // Messages arrive but none can be placed: the pose source (localizer, TF tree) is missing.
    std::snprintf(buf, sizeof(buf), "messages arrive on %s but the TF lookup %s -> sensor has "
                  "failed %lld times; nothing integrated for %.1f s", topic.c_str(),
                  global_frame.c_str(), streak, s.since_integrate_s);
    v.kind = any_integrated ? InputKind::kTfLost : InputKind::kTfMissing;
    v.level = any_integrated ? kInputError : kInputWarn; v.message = buf; v.overrides = true;
  } else if (streak > 0 && !any_integrated) {
    // The same condition inside the startup grace: a localizer may still be coming up,
    // so the level stays OK, but the message says what is (not) happening.
    std::snprintf(buf, sizeof(buf), "waiting for TF %s -> sensor: %lld message(s) received on %s, "
                  "0 integrated, %lld TF lookup(s) failed (WARN after startup_timeout_s = %.0f s)",
                  global_frame.c_str(), static_cast<long long>(n.received), topic.c_str(), streak,
                  s.startup_timeout_s);
    v.kind = InputKind::kWaitingForTf; v.level = kInputOk; v.message = buf;
  } else if (streak > 0 && s.since_integrate_s > kTfNoteAfterS) {
    std::snprintf(buf, sizeof(buf), "TF lookup %s -> sensor failing: last %lld message(s) on %s not "
                  "integrated, nothing integrated for %.1f s (ERROR after input_timeout_s = %.0f s)",
                  global_frame.c_str(), streak, topic.c_str(), s.since_integrate_s, s.input_timeout_s);
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
