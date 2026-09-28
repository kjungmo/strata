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
  LayeredMapParams p = P();
  p.graduate_prob = 0.9; p.demote_prob = 0.4; p.min_observations = 5;
  p.l_min = -5; p.l_max = 5; p.periodicity.n_harmonics = 3;
  LayeredMap m(p);
  const CellId c = 103;
  for (int w = 0; w < 64; ++w) {
    if ((w % 8) < 4) m.observeHit(c); else m.observeMiss(c);
    m.tick();
    if (w + 1 >= 8) { EXPECT_EQ(m.classify(c), CellClass::Periodic) << "window " << (w + 1); }
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
