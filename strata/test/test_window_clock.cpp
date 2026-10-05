#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include "strata/window_clock.hpp"
using strata::WindowClock;

static std::int64_t ns(double s) { return static_cast<std::int64_t>(std::llround(s * 1e9)); }

TEST(WindowClock, FirstMessageAnchorsAndClosesNothing) {
  WindowClock c(1.0);
  EXPECT_EQ(c.advance(ns(100.3)), 0);
  EXPECT_EQ(c.advance(ns(101.29)), 0);           // still inside [100.3, 101.3)
  EXPECT_EQ(c.advance(ns(101.3)), 1);            // exactly one period later: one window closes
  EXPECT_EQ(c.index(), 1);
}
TEST(WindowClock, GapClosesEverySkippedWindow) {
  WindowClock c(0.5);
  c.advance(0);
  EXPECT_EQ(c.advance(ns(2.6)), 5);              // windows 0..4 close; the message opens window 5
  EXPECT_EQ(c.advance(ns(2.7)), 0);
}
TEST(WindowClock, EpochStampsCloseExactlyEveryTenMessages) {
  // 50 Hz, P = 0.2 s, nanosecond stamps near 1.7e9 s: no boundary message is floored
  // into the previous window (in double seconds most of them would be).
  WindowClock c(0.2);
  const std::int64_t t0 = 1700000000LL * 1000000000LL + 123456789LL;
  c.advance(t0);
  for (int i = 1; i <= 500; ++i)
    EXPECT_EQ(c.advance(t0 + i * 20000000LL), i % 10 == 0 ? 1 : 0) << "message " << i;
}
TEST(WindowClock, SmallReorderStaysAndLargeJumpReanchors) {
  WindowClock c(1.0);
  c.advance(ns(10.0)); c.advance(ns(12.5));
  EXPECT_EQ(c.index(), 2);
  EXPECT_EQ(c.advance(ns(12.2)), 0);             // 0.3 s behind the newest stamp: kept
  EXPECT_EQ(c.advance(ns(5.0)), WindowClock::kReset);   // far back: bag loop
  EXPECT_EQ(c.index(), 0);
  EXPECT_EQ(c.advance(ns(6.0)), 1);
}
TEST(WindowClock, ShortBagLoopReanchors) {
  WindowClock c(1.0);                            // a 1.5 s loop, shorter than two periods
  int resets = 0, closed = 0;
  for (int loop = 0; loop < 5; ++loop)
    for (int i = 0; i < 15; ++i) {
      const int k = c.advance(ns(100.0 + 0.1 * i));
      if (k == WindowClock::kReset) ++resets; else closed += k;
    }
  EXPECT_EQ(resets, 4);                          // every restart of the loop
  EXPECT_EQ(closed, 5);                          // one window per pass through the loop
}
TEST(WindowClock, HugeForwardJumpReanchors) {
  WindowClock c(0.2);
  c.advance(0);                                  // an unset (zero) stamp...
  EXPECT_EQ(c.advance(ns(1.7e9)), WindowClock::kReset);   // ...then wall time: no 8.5e9 windows
  EXPECT_EQ(c.index(), 0);
  EXPECT_EQ(c.advance(ns(1.7e9 + 0.2)), 1);      // windows resume at the new anchor
}
