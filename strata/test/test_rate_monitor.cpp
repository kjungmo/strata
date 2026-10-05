#include <gtest/gtest.h>
#include "strata/rate_monitor.hpp"
using strata::RateMonitor;

TEST(RateMonitor, SteadyStreamHasNoDrops) {
  RateMonitor m;
  for (int w = 0; w < 5; ++w) {
    for (int i = 0; i < 10; ++i) m.record(0.1 * (w * 10 + i));
    const auto s = m.closeWindow();
    EXPECT_EQ(s.complete, w > 0);                // the first window has no lead-in gap
    if (w > 0) { EXPECT_EQ(s.scans, 10); EXPECT_NEAR(s.duration_s, 1.0, 1e-9); EXPECT_NEAR(s.rate_hz, 10.0, 1e-6); }
    EXPECT_EQ(s.dropped, 0);
  }
  EXPECT_NEAR(m.recentDropFraction(), 0.0, 1e-12);
  EXPECT_NEAR(m.nominalInterval(), 0.1, 1e-9);
  EXPECT_NEAR(m.windowDurationCv(), 0.0, 1e-9);
}
TEST(RateMonitor, FortyPercentLossIsEstimated) {
  RateMonitor m;
  for (int i = 0; i < 1000; ++i) {
    if (i % 5 == 1 || i % 5 == 3) continue;     // the e2e drop pattern: 2 of every 5 lost
    m.record(0.02 * i);
    if (i % 50 == 49) m.closeWindow();
  }
  EXPECT_NEAR(m.nominalInterval(), 0.02, 1e-9);  // a third of the gaps are still one interval
  EXPECT_NEAR(m.recentDropFraction(), 0.4, 0.01);
}
TEST(RateMonitor, ExpectedRateOverridesTheEstimate) {
  RateMonitor m(10.0);
  for (int i = 0; i < 20; ++i) m.record(0.2 * i);   // every other message lost, no clean gap
  const auto s = m.closeWindow();
  EXPECT_NEAR(s.nominal_interval_s, 0.1, 1e-12);
  EXPECT_EQ(s.dropped, 19);
  EXPECT_NEAR(m.recentDropFraction(), 19.0 / 39.0, 1e-12);
}
TEST(RateMonitor, BackJumpRestartsTheIntervals) {
  RateMonitor m;
  for (int i = 0; i < 10; ++i) m.record(100.0 + 0.1 * i);
  m.closeWindow();
  for (int i = 0; i < 10; ++i) m.record(0.1 * i);   // bag loop: stamps restart near zero
  const auto s = m.closeWindow();
  EXPECT_EQ(s.scans, 10);
  EXPECT_EQ(s.dropped, 0);
  EXPECT_NEAR(s.duration_s, 0.9, 1e-9);
}
