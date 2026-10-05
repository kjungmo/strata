#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <vector>
namespace strata {

// Per-window input-rate statistics from the stamps of the messages the node actually
// integrated. Dropped messages (DDS best effort, TF failures, an overloaded node) show
// up as gaps between consecutive stamps: a gap of g nominal intervals hides
// round(g) - 1 messages. The nominal interval is the sensor's (if given) or the 20th
// percentile of recent intervals, which stays at one interval as long as at least
// about a fifth of consecutive messages arrive back to back; with heavier loss, pass
// the sensor rate. ROS-free so it can be unit-tested.
class RateMonitor {
 public:
  struct WindowStats {
    int scans{0};               // messages integrated in the window
    double duration_s{0.0};     // last stamp of the previous window to last stamp of this one
    double rate_hz{0.0};        // scans / duration_s
    double nominal_interval_s{0.0};
    long dropped{0};            // estimated messages lost inside the window
    bool complete{true};        // false for the first window after a start or reset (no lead-in gap)
  };

  explicit RateMonitor(double expected_rate_hz = 0.0, std::size_t history = 256,
                       std::size_t window_history = 32)
      : fixed_interval_(expected_rate_hz > 0.0 ? 1.0 / expected_rate_hz : 0.0),
        history_(history), window_history_(window_history) {}

  void record(double t) {
    // A stamp far behind the last one (a bag loop, a clock reset) starts the
    // interval history again instead of ignoring every later stamp.
    if (has_last_ && t < last_ - kBackJumpS) {
      has_last_ = false; intervals_.clear(); window_intervals_.clear();
    }
    if (!has_last_) partial_ = true;   // this window has no gap leading into it
    if (has_last_ && t > last_) {
      const double iv = t - last_;
      intervals_.push_back(iv);
      if (intervals_.size() > history_) intervals_.pop_front();
      window_intervals_.push_back(iv);
    }
    if (!has_last_ || t > last_) last_ = t;
    has_last_ = true;
    ++scans_;
  }

  WindowStats closeWindow() {
    WindowStats s;
    s.scans = scans_;
    s.complete = !partial_;
    s.nominal_interval_s = nominalInterval();
    for (double iv : window_intervals_) {
      s.duration_s += iv;
      if (s.nominal_interval_s > 0.0)
        s.dropped += std::max(0L, std::lround(iv / s.nominal_interval_s) - 1);
    }
    s.rate_hz = s.duration_s > 0.0 ? s.scans / s.duration_s : 0.0;
    if (s.duration_s > 0.0 || s.scans > 0) {
      windows_.push_back(s);
      if (windows_.size() > window_history_) windows_.pop_front();
    }
    scans_ = 0;
    partial_ = false;
    window_intervals_.clear();
    return s;
  }

  // Forget the stamp history (the window clock re-anchored); window statistics stay.
  void reset() { has_last_ = false; intervals_.clear(); window_intervals_.clear(); }
  long recentDropped() const { long d = 0; for (const auto& w : windows_) d += w.dropped; return d; }
  long recentScans() const { long n = 0; for (const auto& w : windows_) n += w.scans; return n; }

  // dropped / (dropped + integrated) over the recent windows.
  double recentDropFraction() const {
    long d = 0, n = 0;
    for (const auto& w : windows_) { d += w.dropped; n += w.scans; }
    return (d + n) > 0 ? static_cast<double>(d) / static_cast<double>(d + n) : 0.0;
  }
  // Mean duration of the recent complete windows.
  double meanWindowDuration() const {
    double sum = 0.0; int n = 0;
    for (const auto& w : windows_) if (w.complete && w.duration_s > 0.0) { sum += w.duration_s; ++n; }
    return n > 0 ? sum / n : 0.0;
  }
  // Coefficient of variation of the recent window durations (complete windows only).
  double windowDurationCv() const {
    const double m = meanWindowDuration();
    double ss = 0.0; int n = 0;
    for (const auto& w : windows_) if (w.complete && w.duration_s > 0.0) { ss += (w.duration_s - m) * (w.duration_s - m); ++n; }
    return (n > 1 && m > 0.0) ? std::sqrt(ss / (n - 1)) / m : 0.0;
  }
  double nominalInterval() const {
    if (fixed_interval_ > 0.0) return fixed_interval_;
    if (intervals_.empty()) return 0.0;
    std::vector<double> v(intervals_.begin(), intervals_.end());
    const std::size_t k = v.size() / 5;   // 20th percentile
    std::nth_element(v.begin(), v.begin() + static_cast<long>(k), v.end());
    return v[k];
  }
  std::size_t windowsSeen() const { return windows_.size(); }

 private:
  static constexpr double kBackJumpS = 1.0;
  double fixed_interval_;
  std::size_t history_, window_history_;
  bool has_last_{false};
  double last_{0.0};
  int scans_{0};
  bool partial_{false};
  std::deque<double> intervals_;
  std::vector<double> window_intervals_;
  std::deque<WindowStats> windows_;
};

}  // namespace strata
