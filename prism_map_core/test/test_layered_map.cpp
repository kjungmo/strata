#include <gtest/gtest.h>
#include "prism_map_core/layered_map.hpp"
using namespace prism_map_core;

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
