#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace strata {

// Time-based windows on message stamps in integer nanoseconds (exact at epoch-sized
// stamps, where a double loses ~0.2 us). Window w covers [t0 + w*P, t0 + (w+1)*P), t0
// being the first stamp. advance(t) is called before a message stamped t is integrated
// and returns how many windows close first (0 while t stays in the open window). The
// clock re-anchors at t and returns kReset when t falls more than half a period behind
// the newest stamp seen (a bag loop, however short; a clock reset) or lies more than
// kMaxGapWindows past the open window (an unset stamp followed by a real one). Smaller
// reorderings stay in the open window. ROS-free so it can be unit-tested.
class WindowClock {
 public:
  static constexpr int kReset = -1;
  // About 2.3 days at 0.2 s windows, 11.6 days at 1 s: a longer silence is a clock jump.
  static constexpr std::int64_t kMaxGapWindows = 1000000;

  explicit WindowClock(double period_s)
      : period_ns_(std::max<std::int64_t>(1, std::llround(period_s * 1e9))) {}

  int advance(std::int64_t t_ns) {
    if (!anchored_ || t_ns < newest_ns_ - period_ns_ / 2) {
      const bool reset = anchored_;
      anchored_ = true; t0_ns_ = t_ns; newest_ns_ = t_ns; index_ = 0;
      return reset ? kReset : 0;
    }
    if (t_ns > newest_ns_) newest_ns_ = t_ns;
    if (t_ns < t0_ns_) return 0;                      // slightly before the anchor
    const std::int64_t idx = (t_ns - t0_ns_) / period_ns_;
    if (idx <= index_) return 0;
    if (idx - index_ > kMaxGapWindows) {
      t0_ns_ = t_ns; newest_ns_ = t_ns; index_ = 0;
      return kReset;
    }
    const std::int64_t closed = idx - index_;
    index_ = idx;
    return static_cast<int>(closed);                  // <= kMaxGapWindows
  }
  std::int64_t periodNs() const { return period_ns_; }
  std::int64_t index() const { return index_; }

 private:
  std::int64_t period_ns_;
  bool anchored_{false};
  std::int64_t t0_ns_{0}, newest_ns_{0}, index_{0};
};

}  // namespace strata
