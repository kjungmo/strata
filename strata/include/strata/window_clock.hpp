#pragma once
#include <climits>
#include <cmath>
namespace strata {

// Time-based windows: window w covers message time [t0 + w*P, t0 + (w+1)*P), where t0
// is the stamp of the first message. advance(t) is called before a message stamped t
// is integrated and returns how many windows close first (0 while t stays in the open
// window). A stamp more than one period behind the open window (a bag loop, a clock
// reset) re-anchors the clock at t and returns kReset; slightly out-of-order stamps
// are kept in the open window. ROS-free so it can be unit-tested.
class WindowClock {
 public:
  static constexpr int kReset = -1;
  explicit WindowClock(double period_s) : period_(period_s) {}
  int advance(double t) {
    if (!anchored_) { anchored_ = true; t0_ = t; index_ = 0; return 0; }
    const double rel = (t - t0_) / period_;
    if (rel < static_cast<double>(index_) - 1.0) { t0_ = t; index_ = 0; return kReset; }
    // Small slack so a stamp computed as an exact multiple of the period is not
    // floored into the previous window by rounding.
    const long idx = static_cast<long>(std::floor(rel + 1e-9));
    if (idx <= index_) return 0;
    const long closed = idx - index_;
    index_ = idx;
    return closed > INT_MAX ? INT_MAX : static_cast<int>(closed);
  }
  double period() const { return period_; }
  long index() const { return index_; }
 private:
  double period_;
  bool anchored_{false};
  double t0_{0.0};
  long index_{0};
};

}  // namespace strata
