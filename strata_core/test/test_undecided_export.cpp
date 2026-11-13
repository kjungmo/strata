// CellClass::Undecided (alias of Transient), LayeredMap::undecidedCells(min_log_odds),
// undecidedExportLevel() and Voxel3DBackend::provisionalPoints(). The statistic of a cell with j hit
// windows and no other contact is known exactly (l is summed the way the map sums it), so the
// threshold tests use exact values, including the boundary.
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include "strata_core/layered_map.hpp"
#include "strata_core/voxel3d_backend.hpp"

using namespace strata_core;

namespace {

LayeredMapParams Sprt1() {   // one tick per window, periodicity off, no Intermittent read-out
  LayeredMapParams p;
  p.evidence_model = EvidenceModel::Sprt;
  p.layer_interval = 1;
  p.enable_periodicity = false;
  p.n_max_undecided = 0;
  return p;
}
LayeredMapParams Legacy1() {
  LayeredMapParams p;
  p.evidence_model = EvidenceModel::Legacy;
  p.layer_interval = 1;
  p.enable_periodicity = false;
  return p;
}
std::set<CellId> asSet(const std::vector<CellId>& v) { return std::set<CellId>(v.begin(), v.end()); }
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Cells 1..4 get 1..4 hit windows (4 hits graduate at the shipped pair), cell 5 one miss window.
void feedLadder(LayeredMap& m) {
  for (int w = 0; w < 4; ++w) {
    for (CellId id = 1; id <= 4; ++id) if (w < static_cast<int>(id)) m.observeHit(id);
    if (w == 0) m.observeMiss(5);
    m.endWindow();
  }
}

}  // namespace

static_assert(CellClass::Undecided == CellClass::Transient, "Undecided is an alias of Transient");
static_assert(std::is_same<decltype(CellClass::Undecided), decltype(CellClass::Transient)>::value, "same type");

TEST(UndecidedExport, AliasIsTheSameValueAndTheAccessorEqualsTransientCells) {
  EXPECT_EQ(static_cast<int>(CellClass::Undecided), static_cast<int>(CellClass::Transient));
  for (EvidenceModel model : {EvidenceModel::Sprt, EvidenceModel::Legacy}) {
    LayeredMapParams p = model == EvidenceModel::Sprt ? Sprt1() : Legacy1();
    LayeredMap m(p);
    feedLadder(m);
    EXPECT_EQ(m.undecidedCells(), m.transientCells());              // same cells, same order
    EXPECT_FALSE(m.undecidedCells().empty());
    for (CellId id = 1; id <= 5; ++id) EXPECT_EQ(m.classify(id) == CellClass::Undecided, m.classify(id) == CellClass::Transient);
  }
}

TEST(UndecidedExport, SprtThresholdAtAFractionOfAAndTheBoundaryIsInclusive) {
  const LayeredMapParams p = Sprt1();
  LayeredMap m(p);
  feedLadder(m);
  const SprtDerived d = m.sprt();
  double l1 = 0.0; l1 += d.l_hit;                          // cell 1: one hit window
  EXPECT_EQ(m.classify(4), CellClass::Static);              // four hits graduate at the shipped pair
  for (CellId id : {CellId(1), CellId(2), CellId(3), CellId(5)}) EXPECT_EQ(m.classify(id), CellClass::Transient);
  const double level = undecidedExportLevel(p, 0.5);
  EXPECT_EQ(level, 0.5 * d.A);
  // l of cells 1, 2, 3 = 1.25, 2.51, 3.76; A = 4.5951; half of A = 2.2976
  EXPECT_EQ(asSet(m.undecidedCells(level)), (std::set<CellId>{2, 3}));
  EXPECT_EQ(asSet(m.undecidedCells(undecidedExportLevel(p, 1.0))), (std::set<CellId>{}));
  EXPECT_EQ(asSet(m.undecidedCells(undecidedExportLevel(p, 0.1))), (std::set<CellId>{1, 2, 3}));
  // boundary: min equal to the statistic of cell 1 includes it, the next double above excludes it
  EXPECT_EQ(asSet(m.undecidedCells(l1)), (std::set<CellId>{1, 2, 3}));
  EXPECT_EQ(asSet(m.undecidedCells(std::nextafter(l1, kInf))), (std::set<CellId>{2, 3}));
  double l2 = l1; l2 += d.l_hit;
  EXPECT_EQ(asSet(m.undecidedCells(l2)), (std::set<CellId>{2, 3}));
  EXPECT_EQ(asSet(m.undecidedCells(std::nextafter(l2, kInf))), (std::set<CellId>{3}));
  // the whole range: the miss cell (negative statistic) comes in only at a negative level; Static never
  EXPECT_EQ(asSet(m.undecidedCells(-kInf)), (std::set<CellId>{1, 2, 3, 5}));
  EXPECT_TRUE(m.undecidedCells(kInf).empty());
  EXPECT_TRUE(m.undecidedCells(kNaN).empty());
  // order: the order of transientCells() (map iteration), filtered
  const std::vector<CellId> all = m.transientCells();
  const std::vector<CellId> got = m.undecidedCells(-kInf);
  EXPECT_EQ(got, all);
}

TEST(UndecidedExport, LegacyThresholdAtAFractionOfLogitGraduateProb) {
  const LayeredMapParams p = Legacy1();
  LayeredMap m(p);
  feedLadder(m);
  // legacy: (l + l_hit) * decay, clamped; graduate at p >= 0.8 and 3 observations
  auto step = [&](double l) { return std::min(p.l_max, std::max(p.l_min, (l + p.l_hit) * p.survival_decay)); };
  const double l1 = step(0.0), l2 = step(l1);
  EXPECT_EQ(m.classify(3), CellClass::Static);
  EXPECT_EQ(m.classify(4), CellClass::Static);
  EXPECT_EQ(m.classify(2), CellClass::Transient);           // p >= 0.8 but only two observations
  EXPECT_EQ(m.classify(1), CellClass::Transient);
  EXPECT_EQ(m.classify(5), CellClass::Transient);           // one miss: p = 0.40 >= prune_prob
  const double logit = std::log(p.graduate_prob / (1.0 - p.graduate_prob));
  EXPECT_EQ(undecidedExportLevel(p, 0.5), 0.5 * logit);
  EXPECT_EQ(undecidedExportLevel(p, 1.0), logit);
  EXPECT_EQ(asSet(m.undecidedCells(undecidedExportLevel(p, 0.5))), (std::set<CellId>{1, 2}));   // 0.82, 1.62 >= 0.69
  EXPECT_EQ(asSet(m.undecidedCells(undecidedExportLevel(p, 1.0))), (std::set<CellId>{2}));       // 1.62 >= 1.39 > 0.82
  EXPECT_EQ(asSet(m.undecidedCells(l1)), (std::set<CellId>{1, 2}));
  EXPECT_EQ(asSet(m.undecidedCells(std::nextafter(l1, kInf))), (std::set<CellId>{2}));
  EXPECT_EQ(asSet(m.undecidedCells(l2)), (std::set<CellId>{2}));
  EXPECT_EQ(asSet(m.undecidedCells(std::nextafter(l2, kInf))), (std::set<CellId>{}));
  EXPECT_EQ(asSet(m.undecidedCells(-kInf)), (std::set<CellId>{1, 2, 5}));
}

TEST(UndecidedExport, IntermittentCellsAreUndecidedAndExportedAboveTheLevel) {
  LayeredMapParams p = Sprt1(); p.n_max_undecided = 6;
  LayeredMap m(p);
  // cell 10: H H M H M H (statistic 3.05 after 6 windows, never at A); cell 11: M H M H M H (0.81)
  const bool a[6] = {true, true, false, true, false, true};
  const bool b[6] = {false, true, false, true, false, true};
  for (int w = 0; w < 6; ++w) {
    if (a[w]) m.observeHit(10); else m.observeMiss(10);
    if (b[w]) m.observeHit(11); else m.observeMiss(11);
    m.endWindow();
  }
  ASSERT_EQ(m.classify(10), CellClass::Intermittent);
  ASSERT_EQ(m.classify(11), CellClass::Intermittent);
  EXPECT_TRUE(m.transientCells().empty());                  // Intermittent is not in transientCells()/undecidedCells()
  EXPECT_TRUE(m.undecidedCells().empty());
  EXPECT_EQ(asSet(m.undecidedCells(undecidedExportLevel(p, 0.5))), (std::set<CellId>{10}));
  EXPECT_EQ(asSet(m.undecidedCells(0.0)), (std::set<CellId>{10, 11}));
  EXPECT_EQ(asSet(m.undecidedCells(-kInf)), (std::set<CellId>{10, 11}));
}

TEST(UndecidedExport, StaticPeriodicAndRejectedButKeptCellsAreNeverExported) {
  LayeredMapParams p = Sprt1(); p.enable_periodicity = true; p.n_max_undecided = 64;   // as the 25 % door test
  LayeredMap m(p);
  int unknown_alive = 0, periodic_seen = 0, static_seen = 0;
  for (int w = 0; w < 96; ++w) {
    if ((w % 24) < 6) m.observeHit(9); else m.observeMiss(9);
    m.observeHit(20);                                         // a wall: graduates and stays Static
    m.tick();
    const std::set<CellId> got = asSet(m.undecidedCells(-kInf));
    const CellClass c = m.classify(9);
    if (c == CellClass::Unknown && m.cellCount() >= 1) { ++unknown_alive; EXPECT_EQ(got.count(9), 0u) << "window " << (w + 1); }
    if (c == CellClass::Periodic) { ++periodic_seen; EXPECT_EQ(got.count(9), 0u) << "window " << (w + 1); }
    if (c == CellClass::Static) { ++static_seen; EXPECT_EQ(got.count(9), 0u) << "window " << (w + 1); }
    if (c == CellClass::Transient || c == CellClass::Intermittent) EXPECT_EQ(got.count(9), 1u) << "window " << (w + 1);
    if (w >= 4) { ASSERT_EQ(m.classify(20), CellClass::Static); EXPECT_EQ(got.count(20), 0u); }
  }
  EXPECT_GT(unknown_alive, 0);          // the rejected-but-kept branch was reached
  EXPECT_GT(periodic_seen, 0);
  EXPECT_GT(static_seen, 0);
}

TEST(UndecidedExport, ErasedCellsAreNotExportedAndTheCallChangesNothing) {
  LayeredMapParams p = Sprt1();
  LayeredMap m(p);
  for (int w = 0; w < 24; ++w) { m.observeMiss(7); if (w % 2 == 0) m.observeHit(8); else m.observeMiss(8); m.tick(); }   // 7 is rejected at n = T; 8 stays undecided
  ASSERT_EQ(m.classify(7), CellClass::Unknown);
  const std::size_t cells = m.cellCount();
  const std::size_t touched = m.lastWindowTouched();
  const double prob = m.occupancyProb(8);
  const std::vector<CellId> a = m.undecidedCells(-kInf);
  const std::vector<CellId> b = m.undecidedCells(-kInf);
  EXPECT_EQ(a, b);
  EXPECT_EQ(a.size(), 1u);
  EXPECT_EQ(m.cellCount(), cells);
  EXPECT_EQ(m.lastWindowTouched(), touched);
  EXPECT_EQ(m.occupancyProb(8), prob);
  const LayeredMap& cm = m;                                  // the accessors are const
  EXPECT_EQ(cm.undecidedCells(0.0).size(), cm.undecidedCells(0.0).size());
}

TEST(UndecidedExport, ExportLevelValidatesTheFractionAndLegacyGraduateProb) {
  const LayeredMapParams p = Sprt1();
  for (double f : {0.0, -0.1, 1.0000001, 2.0, kNaN, kInf, -kInf}) {
    try { (void)undecidedExportLevel(p, f); FAIL() << "fraction " << f; }
    catch (const std::invalid_argument& e) { EXPECT_NE(std::string(e.what()).find("undecided_min_evidence"), std::string::npos) << e.what(); }
  }
  EXPECT_NO_THROW(undecidedExportLevel(p, 1.0));
  EXPECT_NO_THROW(undecidedExportLevel(p, 1e-9));
  EXPECT_EQ(undecidedExportLevel(p, 1.0), deriveSprt(p).A);
  LayeredMapParams sp = Sprt1(); sp.graduate_prob = 7.0;      // ignored by Sprt
  EXPECT_NO_THROW(undecidedExportLevel(sp, 0.5));
  LayeredMapParams lg = Legacy1();
  for (double g : {0.0, 1.0, -0.5, 1.5, kNaN, kInf}) {
    lg.graduate_prob = g;
    try { (void)undecidedExportLevel(lg, 0.5); FAIL() << "graduate_prob " << g; }
    catch (const std::invalid_argument& e) { EXPECT_NE(std::string(e.what()).find("graduate_prob"), std::string::npos) << e.what(); }
  }
  lg.graduate_prob = 0.8;
  EXPECT_NO_THROW(undecidedExportLevel(lg, 0.5));
  LayeredMapParams bad = Sprt1(); bad.m0 = 0.9;               // an unusable Sprt pair is reported by deriveSprt
  EXPECT_THROW(undecidedExportLevel(bad, 0.5), std::invalid_argument);
}

TEST(UndecidedExport, VoxelBackendProvisionalPointsAreTheCentresOfTheUndecidedVoxels) {
  LayeredMapParams p = Sprt1();
  Voxel3DBackend b(0.2, p);
  const Eigen::Vector3d origin(0.05, 0.05, 0.05), hit(1.01, 0.05, 0.05);
  Observation obs; obs.hits.push_back(hit);
  const CellId id = b.voxelId(hit);
  const double level = undecidedExportLevel(p, 0.5);         // 2.30: two hit windows (2.51) reach it
  b.integrate(obs, origin); b.tick();
  EXPECT_TRUE(b.provisionalPoints(level).empty());           // one window: 1.25 < 2.30
  EXPECT_GT(b.layered().undecidedCells(-kInf).size(), 1u);   // the ray voxels are undecided but below
  b.integrate(obs, origin); b.tick();
  std::vector<Eigen::Vector3d> pts = b.provisionalPoints(level);
  ASSERT_EQ(pts.size(), 1u);
  EXPECT_TRUE(pts[0].isApprox(b.voxelCenter(id)));
  EXPECT_TRUE(b.staticPoints().empty());
  b.integrate(obs, origin); b.tick();
  EXPECT_EQ(b.provisionalPoints(level).size(), 1u);          // three windows: still undecided
  b.integrate(obs, origin); b.tick();                         // the fourth graduates it
  EXPECT_TRUE(b.provisionalPoints(level).empty());
  ASSERT_EQ(b.staticPoints().size(), 1u);
  EXPECT_TRUE(b.staticPoints()[0].isApprox(b.voxelCenter(id)));
}
