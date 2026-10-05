#include <gtest/gtest.h>
#include "strata_core/layered_map.hpp"
using namespace strata_core;

static LayeredMapParams P() {
  LayeredMapParams p;
  p.layer_interval = 1;          // 1 tick == 1 window
  p.l_hit = 1.0; p.l_miss = -1.0; p.l_min = -6; p.l_max = 6;
  p.survival_decay = 1.0;        // disable forgetting for deterministic threshold tests
  p.graduate_prob = 0.85; p.demote_prob = 0.4;
  p.min_observations = 3; p.prune_prob = 0.05;
  p.enable_periodicity = true; p.periodic_amplitude_min = 0.3;
  p.periodicity.period_windows = 8; p.periodicity.n_harmonics = 2;
  return p;
}

TEST(LayeredMap, GraduatesWhenProbExceedsThresholdAndObserved) {
  LayeredMap m(P());
  const CellId c = 42;
  // 3 hit-windows: log_odds = 3.0 -> sigmoid ~0.95 >= 0.85, observations=3
  for (int i = 0; i < 2; ++i) { m.observeHit(c); m.tick(); }
  EXPECT_FALSE(m.isStatic(c));                    // obs<3 and/or p below first
  m.observeHit(c); m.tick();
  EXPECT_TRUE(m.isStatic(c));
  EXPECT_EQ(m.classify(c), CellClass::Static);
  EXPECT_GT(m.occupancyProb(c), 0.85);
}

TEST(LayeredMap, MovingObstacleNeverGraduates) {
  LayeredMap m(P());
  for (CellId c = 0; c < 12; ++c) { m.observeHit(c); m.tick(); }   // each cell hit once
  for (CellId c = 0; c < 12; ++c) EXPECT_FALSE(m.isStatic(c));     // obs=1 < min_observations
}

TEST(LayeredMap, SchmittHysteresisDemotesOnlyAfterSustainedFree) {
  LayeredMap m(P());
  const CellId c = 5;
  for (int i = 0; i < 4; ++i) { m.observeHit(c); m.tick(); }       // log_odds=4 -> graduate
  ASSERT_TRUE(m.isStatic(c));
  m.observeMiss(c); m.tick();                                      // log_odds=3 -> p~0.95 > demote
  EXPECT_TRUE(m.isStatic(c));
  for (int i = 0; i < 4; ++i) { m.observeMiss(c); m.tick(); }      // drive below demote_prob
  EXPECT_FALSE(m.isStatic(c));
}

TEST(LayeredMap, PeriodicCellClassifiedPeriodicNotStatic) {
  LayeredMapParams p = P(); p.graduate_prob = 0.99;                // make Static hard so periodicity shows
  LayeredMap m(p);
  const CellId c = 7;
  for (int t = 0; t < 32; ++t) { if ((t % 8) < 4) m.observeHit(c); else m.observeMiss(c); m.tick(); }
  EXPECT_EQ(m.classify(c), CellClass::Periodic);
  EXPECT_FALSE(m.isStatic(c));
}

TEST(LayeredMap, WindowIntervalGroupsTicks) {
  LayeredMapParams p = P(); p.layer_interval = 5;
  LayeredMap m(p); const CellId c = 1;
  for (int i = 0; i < 4; ++i) { m.observeHit(c); EXPECT_FALSE(m.tick()); }
  m.observeHit(c); EXPECT_TRUE(m.tick());
  EXPECT_EQ(m.windowCount(), 1);
}

// ---- Regression: periodicity must never demote or relabel a constant wall ----

static LayeredMapParams Shipped() {   // params/grid2d.yaml + voxel3d.yaml defaults
  LayeredMapParams p;                 // struct defaults == shipped YAML (SPEC s.6)
  p.layer_interval = 1;
  return p;
}

TEST(LayeredMap, AlwaysHitWallNeverPeriodicT8) {
  LayeredMapParams p = P();           // T=8, H=2
  LayeredMap m(p);
  const CellId c = 100;
  for (int w = 1; w <= 200; ++w) {
    m.observeHit(c); m.tick();
    EXPECT_NE(m.classify(c), CellClass::Periodic) << "window " << w;
    if (w >= 3) { EXPECT_TRUE(m.isStatic(c)) << "window " << w; }
  }
}

TEST(LayeredMap, AlwaysHitWallNeverPeriodicShippedDefaultsT24) {
  LayeredMap m(Shipped());            // T=24, H=2, a_min=0.3
  const CellId c = 101;
  for (int w = 1; w <= 400; ++w) {
    m.observeHit(c); m.tick();
    EXPECT_NE(m.classify(c), CellClass::Periodic) << "window " << w;   // was P at 29..40
    if (w >= 3) { EXPECT_TRUE(m.isStatic(c)) << "window " << w; }
  }
}

TEST(LayeredMap, RevisitLoopWall8of48StaysStatic) {
  LayeredMap m(Shipped());
  const CellId c = 102;
  bool graduated_once = false;
  for (int w = 1; w <= 48 * 20; ++w) {
    if ((w % 48) < 8) m.observeHit(c);   // seen 8 of every 48 windows, untouched otherwise
    m.tick();
    EXPECT_NE(m.classify(c), CellClass::Periodic) << "window " << w;   // uncentred a=1.66
    if (m.isStatic(c)) graduated_once = true;
    if (graduated_once) { EXPECT_TRUE(m.isStatic(c)) << "window " << w; }
  }
  EXPECT_TRUE(graduated_once);
  EXPECT_EQ(m.classify(c), CellClass::Static);
}

TEST(LayeredMap, GenuineDoorStillPeriodicE2Params) {
  // E2 door: 4 on / 4 off, T=8, H=3, read out at lengths that are not multiples of T.
  // The calibrated significance test needs about two periods of evidence, so the
  // door is required to be Periodic from window 16 on (it was window 8 before).
  LayeredMapParams p = P();
  p.graduate_prob = 0.9; p.demote_prob = 0.4; p.min_observations = 5;
  p.l_min = -5; p.l_max = 5; p.periodicity.n_harmonics = 3;
  p.periodic_alpha_spending = false; p.periodic_false_alarm = 0.1;   // single-read-out rule
  LayeredMap m(p);
  const CellId c = 103;
  for (int w = 0; w < 64; ++w) {
    if ((w % 8) < 4) m.observeHit(c); else m.observeMiss(c);
    m.tick();
    if (w + 1 >= 16) { EXPECT_EQ(m.classify(c), CellClass::Periodic) << "window " << (w + 1); }
  }
}

// SPEC s.3.4 puts the hysteresis band on occupancy only; SPEC s.1/s.3.5 (prose) say a
// periodic cell is "predicted per phase, rather than frozen into the static layer",
// so the periodic predicate may demote without the band. What the band must still
// guarantee is that demotion needs contradicting (free) evidence. With a centred
// amplitude a <= 4 m (1 - m) for touched-window occupancy mean m, so a_min = 0.3
// requires at least ~8.1% free windows: a wall with rarer free windows is never
// demoted by the periodic path.
TEST(LayeredMap, PeriodicDemotionNeedsContradictingFreeEvidence) {
  LayeredMap m(Shipped());             // T=24, a_min=0.3
  const CellId c = 104;
  for (int w = 1; w <= 24 * 20; ++w) {
    if ((w % 24) == 0) m.observeMiss(c); else m.observeHit(c);   // free 1 of 24 (4.2%)
    m.tick();
    EXPECT_NE(m.classify(c), CellClass::Periodic) << "window " << w;
    if (w >= 3) { EXPECT_TRUE(m.isStatic(c)) << "window " << w; }
  }
}

TEST(LayeredMap, GraduatedDoorIsDemotedOncePeriodic) {
  // SPEC prose: a periodic cell must not stay frozen in the static layer. The E2
  // door graduates at window 5 (n < T, amplitude gated) and p never reaches
  // demote_prob, so only the periodic predicate can remove it.
  LayeredMapParams p = P();
  p.graduate_prob = 0.9; p.demote_prob = 0.4; p.min_observations = 5;
  p.l_min = -5; p.l_max = 5; p.periodicity.n_harmonics = 3;
  p.periodic_alpha_spending = false; p.periodic_false_alarm = 0.1;   // single-read-out rule
  LayeredMap m(p);
  const CellId c = 105;
  bool was_static = false;
  for (int w = 0; w < 16; ++w) {
    if ((w % 8) < 4) m.observeHit(c); else m.observeMiss(c);
    m.tick();
    was_static = was_static || m.isStatic(c);
  }
  EXPECT_TRUE(was_static);
  EXPECT_FALSE(m.isStatic(c));
  EXPECT_EQ(m.classify(c), CellClass::Periodic);
}

// ---- Noise-calibrated periodic predicate in the live pipeline ----
// Bernoulli(0.5) clutter cells go through pruning and re-creation (which resets
// their Fourier history); the calibrated test must still keep the per-read-out
// Periodic rate at or below alpha. Seeds 830000+ are test-only.
#include <algorithm>
#include <random>
#include <vector>

TEST(LayeredMap, BernoulliClutterRarelyPeriodicWithPruning) {
  LayeredMapParams p = P();
  p.graduate_prob = 0.9; p.demote_prob = 0.4; p.min_observations = 5;
  p.l_min = -5; p.l_max = 5; p.periodicity.n_harmonics = 3;
  // the single-read-out rule at 0.1 meets the 0.01 target per read-out
  p.periodic_alpha_spending = false; p.periodic_false_alarm = 0.1;
  const int kCells = 400;
  int periodic = 0, reads = 0;
  for (int L : {8, 13, 17, 24, 41, 64, 100}) {
    LayeredMap m(p);
    std::mt19937 rng(830000u + L);
    std::bernoulli_distribution coin(0.5);
    for (int w = 0; w < L; ++w) {
      for (CellId c = 0; c < kCells; ++c) { if (coin(rng)) m.observeHit(c); else m.observeMiss(c); }
      m.tick();
    }
    for (CellId c = 0; c < kCells; ++c) { ++reads; if (m.classify(c) == CellClass::Periodic) ++periodic; }
  }
  EXPECT_LE(static_cast<double>(periodic) / reads, 0.01);
}

TEST(LayeredMap, CalibrationCanBeDisabledForLegacyAmplitudeRule) {
  // alpha >= 1 switches the significance test off (amplitude-only rule, v0.1.0 semantics).
  LayeredMapParams p = P(); p.graduate_prob = 0.99; p.periodic_false_alarm = 1.0;
  LayeredMap m(p);
  for (int t = 0; t < 8; ++t) { if ((t % 8) < 4) m.observeHit(7); else m.observeMiss(7); m.tick(); }
  EXPECT_EQ(m.classify(7), CellClass::Periodic);   // detected at n = T, no significance delay
}

// ---- Trajectory level: alpha spending in the live pipeline (seeds 850000+) ----
TEST(LayeredMap, AlphaSpendingIsTheShippedRule) {
  EXPECT_TRUE(LayeredMapParams{}.periodic_alpha_spending);
}

TEST(LayeredMap, GraduatedDoorIsDemotedOncePeriodicWithSpending) {
  LayeredMapParams p = P();
  p.graduate_prob = 0.9; p.demote_prob = 0.4; p.min_observations = 5;
  p.l_min = -5; p.l_max = 5; p.periodicity.n_harmonics = 3;
  p.periodic_alpha_spending = true;
  LayeredMap m(p);
  const CellId c = 106;
  bool was_static = false;
  for (int w = 0; w < 64; ++w) {
    if ((w % 8) < 4) m.observeHit(c); else m.observeMiss(c);
    m.tick();
    was_static = was_static || m.isStatic(c);
  }
  EXPECT_TRUE(was_static);
  EXPECT_FALSE(m.isStatic(c));
  EXPECT_EQ(m.classify(c), CellClass::Periodic);
}

TEST(LayeredMap, NoisyWallRarelyEverDemotedByPeriodicPathWithSpending) {
  // A graduated wall is never pruned, so its Fourier history is uninterrupted and
  // the spent test bounds P(ever Periodic over the whole run) by alpha.
  LayeredMapParams p;   // shipped defaults, one window per tick
  p.layer_interval = 1;
  const int kWalls = 300, kWin = 1000;
  for (bool spend : {false, true}) {
    p.periodic_alpha_spending = spend;
    LayeredMap m(p);
    std::mt19937 rng(850000u);
    std::bernoulli_distribution coin(0.6);
    std::vector<bool> ever(kWalls, false);
    for (int w = 0; w < kWin; ++w) {
      for (CellId c = 0; c < kWalls; ++c) { if (coin(rng)) m.observeHit(c); else m.observeMiss(c); }
      m.tick();
      for (CellId c = 0; c < kWalls; ++c) if (m.classify(c) == CellClass::Periodic) ever[c] = true;
    }
    const double r = static_cast<double>(std::count(ever.begin(), ever.end(), true)) / kWalls;
    if (spend) { EXPECT_LE(r, p.periodic_false_alarm); EXPECT_LE(r, 0.01); }
    else { EXPECT_GT(r, 0.01); }   // non-vacuous: the single-read-out rule accumulates
  }
}

TEST(LayeredMap, PrunedClutterRarelyEverPeriodicWithSpending) {
  LayeredMapParams p = P();
  p.graduate_prob = 0.9; p.demote_prob = 0.4; p.min_observations = 5;
  p.l_min = -5; p.l_max = 5; p.periodicity.n_harmonics = 3;
  p.periodic_alpha_spending = true;
  p.periodic_false_alarm = LayeredMapParams{}.periodic_false_alarm;   // shipped level
  const int kCells = 500, kWin = 1024;
  for (double occ : {0.3, 0.4, 0.5}) {
    LayeredMap m(p);
    std::mt19937 rng(851000u + unsigned(occ * 10));
    std::bernoulli_distribution coin(occ);
    std::vector<bool> ever(kCells, false);
    for (int w = 0; w < kWin; ++w) {
      for (CellId c = 0; c < kCells; ++c) { if (coin(rng)) m.observeHit(c); else m.observeMiss(c); }
      m.tick();
      for (CellId c = 0; c < kCells; ++c) if (m.classify(c) == CellClass::Periodic) ever[c] = true;
    }
    EXPECT_LE(static_cast<double>(std::count(ever.begin(), ever.end(), true)) / kCells, 0.02)
        << "m=" << occ;
  }
}
TEST(LayeredMap, CloseWindowsEqualsRepeatedEmptyWindows) {
  // A door-like cell, a wall and a cell that is pruned on the way; then a silent gap.
  LayeredMap a(P()), b(P());
  for (int w = 0; w < 20; ++w) {
    for (LayeredMap* m : {&a, &b}) {
      m->observeHit(1);                                   // wall
      if (w % 8 < 4) m->observeHit(2); else m->observeMiss(2);   // door, T = 8
      if (w < 3) m->observeHit(3); else m->observeMiss(3);       // object that leaves
    }
    a.endWindow(); b.endWindow();
  }
  a.observeHit(1); b.observeHit(1);                       // open window has evidence
  a.closeWindows(13);
  for (int i = 0; i < 13; ++i) b.endWindow();
  EXPECT_EQ(a.windowCount(), b.windowCount());
  EXPECT_EQ(a.cellCount(), b.cellCount());
  for (CellId c : {1, 2, 3}) {
    EXPECT_EQ(a.classify(c), b.classify(c));
    EXPECT_DOUBLE_EQ(a.occupancyProb(c), b.occupancyProb(c));
    EXPECT_DOUBLE_EQ(a.periodicProb(c), b.periodicProb(c));
  }
  a.closeWindows(0);                                      // no-op
  EXPECT_EQ(a.windowCount(), b.windowCount());
}
