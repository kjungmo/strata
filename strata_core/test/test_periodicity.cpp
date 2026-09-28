#include <gtest/gtest.h>
#include "strata_core/periodicity.hpp"
using namespace strata_core;

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

// ---- Regression: uncentred FreMEn amplitude (constant cells read as Periodic) ----
// A cell occupied in every touched window has no temporal variation, so its
// mean-removed (centred) Fourier amplitude must be zero whatever the sampling.

TEST(Periodicity, AlwaysOccupiedHasZeroAmplitudeAtEveryLengthT8) {
  PeriodicityModel m({8, 3});
  const CellId c = 11;
  for (int n = 1; n <= 200; ++n) {
    m.gather(c, true, n);
    EXPECT_LT(m.amplitude(c), 1e-9) << "n=" << n;   // n=10..13 read ~0.37-0.44 when uncentred
  }
}

TEST(Periodicity, AlwaysOccupiedHasZeroAmplitudeAtEveryLengthT24) {
  PeriodicityModel m({24, 2});
  const CellId c = 12;
  for (int n = 1; n <= 400; ++n) {
    m.gather(c, true, n);
    EXPECT_LT(m.amplitude(c), 1e-9) << "n=" << n;   // n=29..40 exceed 0.3 when uncentred
  }
}

TEST(Periodicity, ConstantCellUnderUnevenPhaseCoverageHasZeroAmplitude) {
  // Revisit loop: the cell is seen in 8 of every 48 windows (always the same 8
  // phases of T=24). Uncentred amplitude is 2 sin(8pi/24)/(8 sin(pi/24)) = 1.659.
  PeriodicityModel m({24, 2});
  const CellId c = 13;
  for (int w = 1; w <= 48 * 20; ++w)
    if ((w % 48) < 8) m.gather(c, true, w);
  EXPECT_LT(m.amplitude(c), 1e-9);
  EXPECT_GT(m.predict(c, 5), 0.99);                 // still predicted occupied
}

TEST(Periodicity, SquareWaveDetectedAtNonMultiplesOfPeriod) {
  PeriodicityModel m({8, 3});
  const CellId c = 14;
  for (int w = 1; w <= 100; ++w) {
    m.gather(c, ((w - 1) % 8) < 4, w);
    if (w >= 8) { EXPECT_GT(m.amplitude(c), 0.3) << "n=" << w; }
  }
}
