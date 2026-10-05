#include <gtest/gtest.h>
#include "strata/window_clock.hpp"
using strata::WindowClock;

TEST(WindowClock, FirstMessageAnchorsAndClosesNothing) {
  WindowClock c(1.0);
  EXPECT_EQ(c.advance(100.3), 0);
  EXPECT_EQ(c.advance(101.29), 0);              // still inside [100.3, 101.3)
  EXPECT_EQ(c.advance(101.3), 1);               // exactly one period later: one window closes
  EXPECT_EQ(c.index(), 1);
}
TEST(WindowClock, GapClosesEverySkippedWindow) {
  WindowClock c(0.5);
  c.advance(0.0);
  EXPECT_EQ(c.advance(2.6), 5);                 // windows 0..4 close; the message opens window 5
  EXPECT_EQ(c.advance(2.7), 0);
}
TEST(WindowClock, ExactMultiplesAreNotFlooredBack) {
  WindowClock c(0.2);
  c.advance(0.02);
  int closed = 0;
  for (int i = 1; i <= 100; ++i) closed += c.advance(0.02 + i * 0.02);   // 50 Hz for 2 s
  EXPECT_EQ(closed, 10);                         // one window per 10 messages, no off-by-one
}
TEST(WindowClock, SmallReorderStaysAndLargeJumpReanchors) {
  WindowClock c(1.0);
  c.advance(10.0); c.advance(12.5);
  EXPECT_EQ(c.index(), 2);
  EXPECT_EQ(c.advance(11.8), 0);                // slightly behind the open window: kept in it
  EXPECT_EQ(c.advance(5.0), WindowClock::kReset);   // far back: bag loop
  EXPECT_EQ(c.index(), 0);
  EXPECT_EQ(c.advance(6.0), 1);
}
