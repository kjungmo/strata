#include <cmath>
#include <cstdint>
#include <functional>
#include <gtest/gtest.h>
#include "strata/tf_wait.hpp"
using strata::RosClockWatch;
using strata::TfWait;
using strata::TfWaitRule;

namespace {
using Clock = std::function<double(double)>;   // ROS seconds as a function of steady seconds
std::int64_t ns(double s) { return static_cast<std::int64_t>(std::llround(s * 1e9)); }

struct Result { double waited, ros_waited; };

// A node that started at steady 0 and has read the clock every 10 ms since (earlier
// waits, the diagnostics timer) runs one wait for a transform that never arrives,
// starting at steady `at` and polling every millisecond.
Result waitAt(const Clock& ros, double at) {
  const TfWaitRule rule;   // 0.1 s budget, 1 s frozen, 1 s cap
  RosClockWatch watch;
  for (int k = 0; k * 0.01 < at; ++k) watch.observe(k * 0.01, ns(ros(k * 0.01)));
  TfWait w(rule, watch, at, ns(ros(at)));
  for (int ms = 1;; ++ms) {
    const double now = at + ms * 1e-3;
    if (w.stop(now, ns(ros(now)))) return Result{now - at, ros(now) - ros(at)};
  }
}
double coarse(double steady, double rtf, double step) {   // /clock steps of `step` sim s
  return 100.0 + step * std::floor(steady * rtf / step);
}
}  // namespace

TEST(TfWait, LiveClockWaitsTheBudget) {
  const Result r = waitAt([](double t) { return 100.0 + t; }, 5.0);
  EXPECT_NEAR(r.waited, 0.1, 1.5e-3);
}

TEST(TfWait, SlowSimClockGetsTheBudgetDownToATenth) {
  const Result fifth = waitAt([](double t) { return 100.0 + 0.2 * t; }, 5.0);
  EXPECT_NEAR(fifth.ros_waited, 0.1, 1e-3);
  EXPECT_NEAR(fifth.waited, 0.5, 6e-3);
  const Result tenth = waitAt([](double t) { return 100.0 + 0.1 * t; }, 5.0);
  EXPECT_GE(tenth.ros_waited, 0.1 - 1e-3);
  EXPECT_LE(tenth.waited, 1.0 + 1e-3);
}

TEST(TfWait, CoarseClockAtHalfSpeedIsWaitedOn) {
  // 10 Hz of sim time at 0.5x: the clock steps every 0.2 s of wall time. A wait that
  // starts just after a step sees no change for 0.2 s and must not take that for frozen.
  const Clock c = [](double t) { return coarse(t, 0.5, 0.1); };
  const Result r = waitAt(c, 5.001);
  EXPECT_NEAR(r.ros_waited, 0.1, 1e-9);
  EXPECT_NEAR(r.waited, 0.199 + 0.02, 2e-3);   // the step at 5.2, then the 20 ms grace
}

TEST(TfWait, CoarseClockAtAFifthIsWaitedOn) {
  const Clock c = [](double t) { return coarse(t, 0.2, 0.1); };   // a step every 0.5 s
  const Result r = waitAt(c, 5.001);
  EXPECT_NEAR(r.ros_waited, 0.1, 1e-9);
  EXPECT_NEAR(r.waited, 0.499 + 0.02, 2e-3);
}

TEST(TfWait, ClockFrozenFromTheStartIsNotWaitedOnOnceKnown) {
  const Clock c = [](double) { return 0.0; };   // use_sim_time, no /clock
  EXPECT_LE(waitAt(c, 0.0).waited, 1.0 + 1e-3);    // the first wait may run to the cap
  EXPECT_NEAR(waitAt(c, 2.0).waited, 1e-3, 1e-9);   // after 1 s unchanged: give up at once
}

TEST(TfWait, ClockThatStopsIsRecognisedWithinOneSecond) {
  const Clock c = [](double t) { return 100.0 + std::fmin(t, 5.0); };
  const Result first = waitAt(c, 5.02);
  EXPECT_NEAR(first.waited, 0.98, 2e-3);        // stops when the clock has been still 1 s
  EXPECT_NEAR(first.ros_waited, 0.0, 1e-9);
  EXPECT_NEAR(waitAt(c, 6.5).waited, 1e-3, 1e-9);
}

TEST(TfWait, ClockThatJumpsBackCountsTheBudgetFromTheJump) {
  // A bag loop: at steady 5.0 the clock jumps back 100 s and runs on.
  const Clock c = [](double t) { return t < 5.0 ? 200.0 + t : 100.0 + t; };
  const Result r = waitAt(c, 4.95);
  EXPECT_NEAR(r.waited, 0.15, 2e-3);            // 0.05 s before the jump + 0.1 s after
}
