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

// ---- Noise-calibrated periodicity test ----
// Null: o_w ~ iid Bernoulli(m), independent of the window phase. The test
// declares a cell Periodic only if some harmonic has amplitude >= a_min AND its
// Chernoff false-alarm bound, times H (Bonferroni over harmonics), is <= alpha.
// The bound guarantees a rate <= alpha; at the shipped alpha = 0.1 the measured
// rate must also meet the 0.01 target. Seeds 800000+ are test-only; they are
// disjoint from the paper's evaluation seeds (12345 + small offsets) and from the
// calibration seeds (20260928 + offsets).
#include <random>
#include "strata_core/layered_map.hpp"

namespace {
const double kAlpha = LayeredMapParams{}.periodic_false_alarm;   // shipped level
constexpr double kTarget = 0.01;

double noiseRate(int T, int H, double m, int n, int trials, unsigned seed,
                 double a_min, double alpha) {
  PeriodicityModel pm({T, H});
  std::mt19937 rng(seed);
  std::bernoulli_distribution coin(m);
  int hits = 0;
  for (int r = 0; r < trials; ++r) {
    const CellId id = static_cast<CellId>(r);
    for (int w = 1; w <= n; ++w) pm.gather(id, coin(rng), w);
    if (pm.isPeriodic(id, a_min, alpha)) ++hits;
    pm.erase(id);
  }
  return static_cast<double>(hits) / trials;
}
}  // namespace

TEST(PeriodicitySignificance, ChernoffBoundIsAValidTailBound) {
  // x e^{1-x} with x = 2 dchi: 1 at x <= 1, decreasing beyond, e.g. alpha/H at
  // dchi ~ 3.1 for alpha = 0.1, H = 3.
  EXPECT_DOUBLE_EQ(PeriodicityModel::tailBound(0.0), 1.0);
  EXPECT_DOUBLE_EQ(PeriodicityModel::tailBound(0.5), 1.0);
  double prev = 1.0;
  for (double d = 0.6; d < 20.0; d += 0.1) {
    const double b = PeriodicityModel::tailBound(d);
    EXPECT_LT(b, prev + 1e-15); EXPECT_GT(b, 0.0); prev = b;
    EXPECT_NEAR(b, 2.0 * d * std::exp(1.0 - 2.0 * d), 1e-12);
  }
}

TEST(PeriodicitySignificance, BernoulliNoiseRarelyPeriodicT8) {
  double pooled = 0.0; int cells = 0;
  unsigned seed = 800000u;
  for (double m : {0.1, 0.25, 0.5, 0.75, 0.9}) {
    for (int n : {8, 9, 12, 16, 24, 33, 64, 100}) {
      const double r = noiseRate(8, 3, m, n, 3000, seed++, 0.3, kAlpha);
      EXPECT_LE(r, kAlpha) << "m=" << m << " n=" << n;          // guaranteed level
      EXPECT_LE(r, 1.5 * kTarget) << "m=" << m << " n=" << n;   // target, binomial slack
      pooled += r; ++cells;
    }
  }
  EXPECT_LE(pooled / cells, kTarget);
}

TEST(PeriodicitySignificance, BernoulliNoiseRarelyPeriodicShippedT24) {
  double pooled = 0.0; int cells = 0;
  unsigned seed = 810000u;
  for (double m : {0.1, 0.3, 0.5, 0.8}) {
    for (int n : {24, 30, 48, 100, 240}) {
      const double r = noiseRate(24, 2, m, n, 3000, seed++, 0.3, kAlpha);
      EXPECT_LE(r, 1.5 * kTarget) << "m=" << m << " n=" << n;
      pooled += r; ++cells;
    }
  }
  EXPECT_LE(pooled / cells, kTarget);
}

TEST(PeriodicitySignificance, UncalibratedAmplitudeAloneWouldFail) {
  // Guards the tests above against being vacuous: with the significance test
  // disabled (alpha >= 1) the a_min = 0.3 amplitude rule fires often on noise.
  EXPECT_GT(noiseRate(8, 3, 0.5, 12, 3000, 820000u, 0.3, 1.0), 0.2);
}

TEST(PeriodicitySignificance, SquareWaveDoorsStillDetected) {
  PeriodicityModel m({8, 3});
  for (int w = 1; w <= 100; ++w) {
    m.gather(1, ((w - 1) % 8) < 4, w);   // 4 on / 4 off, period 8
    m.gather(2, ((w - 1) % 4) < 2, w);   // 2 on / 2 off, period 4 (harmonic 2)
    m.gather(3, ((w - 1) % 8) < 2, w);   // 2 on / 6 off, period 8 (never pruned here)
    if (w >= 16) {
      EXPECT_TRUE(m.isPeriodic(1, 0.3, kAlpha)) << "n=" << w;
      EXPECT_TRUE(m.isPeriodic(2, 0.3, kAlpha)) << "n=" << w;
    }
    if (w >= 32) { EXPECT_TRUE(m.isPeriodic(3, 0.3, kAlpha)) << "n=" << w; }
  }
  EXPECT_LT(m.falseAlarm(1), 1e-6);
}

TEST(PeriodicitySignificance, ConstantCellNeverPeriodicUnderAnySampling) {
  PeriodicityModel m({24, 2});
  for (int w = 1; w <= 48 * 20; ++w) {
    m.gather(1, true, w);                       // every window
    if ((w % 48) < 8) m.gather(2, true, w);     // revisit loop 8 of 48
    if ((w % 7) == 0) m.gather(3, false, w);    // always free, sparse
    for (CellId c : {1, 2, 3}) {
      if (m.has(c)) {
        EXPECT_FALSE(m.isPeriodic(c, 0.3, kAlpha)) << "cell " << c << " w=" << w;
        EXPECT_FALSE(m.isPeriodic(c, 0.0, 1.0)) << "cell " << c << " w=" << w;
        EXPECT_DOUBLE_EQ(m.falseAlarm(c), 1.0);
      }
    }
  }
}

TEST(PeriodicitySignificance, BelowPeriodGateNeverPeriodic) {
  PeriodicityModel m({8, 3});
  for (int w = 1; w < 8; ++w) {
    m.gather(1, ((w - 1) % 8) < 4, w);
    EXPECT_FALSE(m.isPeriodic(1, 0.0, 1.0)) << "n=" << w;
    EXPECT_DOUBLE_EQ(m.falseAlarm(1), 1.0);
  }
}
