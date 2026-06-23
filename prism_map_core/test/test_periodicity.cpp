#include <gtest/gtest.h>
#include "prism_map_core/periodicity.hpp"
using namespace prism_map_core;

TEST(Periodicity, ConstantOccupiedHasHighMeanLowAmplitude) {
  PeriodicityModel m({/*period*/8, /*harmonics*/2});
  const CellId c = 1;
  for (int t = 0; t < 32; ++t) m.gather(c, true, t);
  EXPECT_GT(m.predict(c, 100), 0.9);
  EXPECT_LT(m.amplitude(c), 0.2);
}

TEST(Periodicity, ConstantFreeHasLowPrediction) {
  PeriodicityModel m({8, 2});
  const CellId c = 2;
  for (int t = 0; t < 32; ++t) m.gather(c, false, t);
  EXPECT_LT(m.predict(c, 5), 0.1);
}

TEST(Periodicity, SquareWaveIsDetectedAndPredicted) {
  PeriodicityModel m({8, 3});
  const CellId c = 3;
  // occupied for first half of each period of 8, free for second half, 4 periods
  for (int t = 0; t < 32; ++t) m.gather(c, (t % 8) < 4, t);
  EXPECT_GT(m.amplitude(c), 0.3);           // clearly periodic
  EXPECT_GT(m.predict(c, 8 + 1), 0.5);      // phase 1 -> occupied region
  EXPECT_LT(m.predict(c, 8 + 6), 0.5);      // phase 6 -> free region
}

TEST(Periodicity, UnknownCellReturnsHalf) {
  PeriodicityModel m({8, 2});
  EXPECT_DOUBLE_EQ(m.predict(999, 0), 0.5);
  EXPECT_FALSE(m.has(999));
}
