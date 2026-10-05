#include <algorithm>
#include <functional>
#include <gtest/gtest.h>
#include "strata/tf_wait.hpp"
using strata::TfWaitRule;

namespace {
struct Stop { double steady, ros; };
// Polls the rule every millisecond of steady time against a ROS-clock model
// (ROS seconds elapsed as a function of steady seconds elapsed), as the node does.
Stop waitWith(const std::function<double(double)>& ros_of_steady) {
  const TfWaitRule rule;
  for (int ms = 0; ms <= 5000; ++ms) {
    const double steady = ms * 1e-3;
    if (rule.stop(steady, ros_of_steady(steady))) return {steady, ros_of_steady(steady)};
  }
  return {1e9, 1e9};
}
}  // namespace

TEST(TfWait, LiveClockWaitsTheBudget) {
  const Stop s = waitWith([](double t) { return t; });
  EXPECT_NEAR(s.steady, 0.1, 1.5e-3);
}

TEST(TfWait, SlowSimClockStillGetsTheBudgetInRosTime) {
  // A bag at -r 0.2: 0.1 s of ROS time takes 0.5 s, and the wait gives it all of it.
  const Stop fifth = waitWith([](double t) { return 0.2 * t; });
  EXPECT_NEAR(fifth.ros, 0.1, 1e-3);
  EXPECT_NEAR(fifth.steady, 0.5, 6e-3);
  // At -r 0.1 the hard cap (1 s of steady time) and the budget coincide.
  const Stop tenth = waitWith([](double t) { return 0.1 * t; });
  EXPECT_GE(tenth.ros, 0.1 - 1e-3);
  EXPECT_LE(tenth.steady, 1.0 + 1e-3);
}

TEST(TfWait, FrozenClockGivesUpAfterTheBudgetOfSteadyTime) {
  const Stop s = waitWith([](double) { return 0.0; });   // use_sim_time, no /clock
  EXPECT_NEAR(s.steady, 0.1, 1.5e-3);
}

TEST(TfWait, ClockThatStartsThenFreezesStopsAtTheHardCap) {
  const Stop s = waitWith([](double t) { return std::min(t, 0.03); });
  EXPECT_NEAR(s.steady, 1.0, 1.5e-3);
  EXPECT_NEAR(s.ros, 0.03, 1e-9);
}
