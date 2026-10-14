// Tests of the read-only window observer (LayeredMap::setWindowObserver).
#include <gtest/gtest.h>
#include <cstring>
#include <map>
#include <random>
#include <tuple>
#include <vector>
#include "strata_core/grid2d_backend.hpp"
#include "strata_core/layered_map.hpp"
#include "strata_core/voxel3d_backend.hpp"

using namespace strata_core;

namespace {

struct Report { int window; CellId id; bool hit; bool miss; double prob_at_report; };

// Records every report and the occupancy probability the map shows at that moment.
struct Recorder : WindowObserver {
  const LayeredMap* map{nullptr};
  std::vector<Report> reports;
  void onTouchedCell(int w, CellId id, bool hit, bool miss) override {
    reports.push_back({w, id, hit, miss, map ? map->occupancyProb(id) : -1.0});
  }
};

struct Noop : WindowObserver {
  unsigned long long calls{0};
  void onTouchedCell(int, CellId, bool, bool) override { ++calls; }
};

LayeredMapParams oneScanWindows() {
  LayeredMapParams p;
  p.layer_interval = 1;
  return p;
}

}  // namespace

TEST(WindowObserver, ReportsTouchedCellsWithHitWinsRule) {
  LayeredMap lm(oneScanWindows());
  Recorder rec; rec.map = &lm;
  lm.setWindowObserver(&rec);
  lm.observeHit(10);                       // hit only
  lm.observeMiss(11); lm.observeMiss(11);  // misses only
  lm.observeMiss(12); lm.observeHit(12); lm.observeMiss(12);  // both: the hit wins
  lm.endWindow();
  ASSERT_EQ(rec.reports.size(), 3u);
  std::map<CellId, Report> by;
  for (const auto& r : rec.reports) by[r.id] = r;
  EXPECT_EQ(by[10].window, 1);
  EXPECT_TRUE(by[10].hit);  EXPECT_FALSE(by[10].miss);
  EXPECT_FALSE(by[11].hit); EXPECT_TRUE(by[11].miss);
  EXPECT_TRUE(by[12].hit);  EXPECT_FALSE(by[12].miss);
}

TEST(WindowObserver, UntouchedCellsAreNotReportedAndWindowIndexAdvances) {
  LayeredMap lm(oneScanWindows());
  Recorder rec; rec.map = &lm;
  lm.setWindowObserver(&rec);
  lm.observeHit(5);
  lm.endWindow();                 // window 1: cell 5 touched
  lm.endWindow();                 // window 2: nothing touched, cell 5 still alive
  lm.observeMiss(5);
  lm.endWindow();                 // window 3
  ASSERT_EQ(rec.reports.size(), 2u);
  EXPECT_EQ(rec.reports[0].window, 1);
  EXPECT_EQ(rec.reports[1].window, 3);
  EXPECT_TRUE(rec.reports[0].hit);
  EXPECT_TRUE(rec.reports[1].miss);
}

TEST(WindowObserver, CalledBeforeTheEvidenceUpdate) {
  LayeredMap lm(oneScanWindows());
  Recorder rec; rec.map = &lm;
  lm.setWindowObserver(&rec);
  lm.observeHit(7);
  const double p0 = lm.occupancyProb(7);   // a fresh cell: log-odds 0, probability 0.5
  EXPECT_DOUBLE_EQ(p0, 0.5);
  lm.endWindow();
  ASSERT_EQ(rec.reports.size(), 1u);
  EXPECT_DOUBLE_EQ(rec.reports[0].prob_at_report, 0.5);   // not yet updated at the report
  const double after_first = lm.occupancyProb(7);
  EXPECT_GT(after_first, 0.5);                            // updated after the window
  lm.observeHit(7);
  lm.endWindow();
  EXPECT_DOUBLE_EQ(rec.reports[1].prob_at_report, after_first);   // the second report sees window 1's update only
  EXPECT_GT(lm.occupancyProb(7), after_first);
}

TEST(WindowObserver, CloseWindowsReportsOnlyTheFirstWindow) {
  LayeredMap lm(oneScanWindows());
  Recorder rec; rec.map = &lm;
  lm.setWindowObserver(&rec);
  lm.observeHit(1);
  lm.closeWindows(4);
  ASSERT_EQ(rec.reports.size(), 1u);
  EXPECT_EQ(rec.reports[0].window, 1);
  EXPECT_EQ(lm.windowCount(), 4);
  lm.observeHit(1);
  lm.closeWindows(1);
  ASSERT_EQ(rec.reports.size(), 2u);
  EXPECT_EQ(rec.reports[1].window, 5);
}

TEST(WindowObserver, TickReportsOnlyAtWindowClose) {
  LayeredMapParams p; p.layer_interval = 3;
  LayeredMap lm(p);
  Recorder rec; rec.map = &lm;
  lm.setWindowObserver(&rec);
  lm.observeHit(2); EXPECT_FALSE(lm.tick());
  lm.observeHit(2); EXPECT_FALSE(lm.tick());
  EXPECT_TRUE(rec.reports.empty());
  lm.observeMiss(2); EXPECT_TRUE(lm.tick());
  ASSERT_EQ(rec.reports.size(), 1u);
  EXPECT_TRUE(rec.reports[0].hit);   // two hits and a miss in the window: hit wins
}

TEST(WindowObserver, NullRemovesTheObserver) {
  LayeredMap lm(oneScanWindows());
  Noop n;
  lm.setWindowObserver(&n);
  lm.observeHit(1); lm.endWindow();
  EXPECT_EQ(n.calls, 1u);
  lm.setWindowObserver(nullptr);
  lm.observeHit(1); lm.endWindow();
  EXPECT_EQ(n.calls, 1u);
}

namespace {

// A reproducible mix of always-hit, bursty, flicker and one-off cells over many windows.
// Returns a fingerprint of everything the map exposes after every window.
std::vector<std::uint64_t> fingerprint(WindowObserver* obs, bool periodicity, int windows = 300) {
  LayeredMapParams p = oneScanWindows();
  p.enable_periodicity = periodicity;
  LayeredMap lm(p);
  lm.setWindowObserver(obs);
  std::mt19937 rng(777);
  std::uniform_real_distribution<double> u(0.0, 1.0);
  std::vector<std::uint64_t> fp;
  auto push = [&](double d) { std::uint64_t b; std::memcpy(&b, &d, sizeof b); fp.push_back(b); };
  for (int w = 0; w < windows; ++w) {
    for (CellId id = 0; id < 120; ++id) {
      const double m = (id % 6 == 0) ? 1.0 : (id % 6 == 1) ? 0.6 : (id % 6 == 2) ? 0.3
                     : (id % 6 == 3) ? ((w / 8) % 2 ? 0.9 : 0.05) : 0.0;
      const double r = u(rng);
      if (id % 6 == 5) { if (r < 0.1) lm.observeMiss(id); continue; }   // misses only
      if (r < 0.9) { if (u(rng) < m) lm.observeHit(id); else lm.observeMiss(id); }
    }
    lm.endWindow();
    push(static_cast<double>(lm.cellCount()));
    push(static_cast<double>(lm.staticCells().size()));
    push(static_cast<double>(lm.transientCells().size()));
    push(static_cast<double>(lm.periodicCells().size()));
    for (CellId id = 0; id < 120; ++id) {
      push(lm.occupancyProb(id));
      push(lm.periodicProb(id));
      push(static_cast<double>(static_cast<int>(lm.classify(id))));
    }
  }
  return fp;
}

}  // namespace

TEST(WindowObserver, NoBehaviourChangeWithNoopOrRecordingObserver) {
  for (bool periodicity : {false, true}) {
    const auto base = fingerprint(nullptr, periodicity);
    ASSERT_GT(base.size(), 1000u);
    Noop n;
    const auto with_noop = fingerprint(&n, periodicity);
    EXPECT_GT(n.calls, 1000u) << "the no-op observer was not called, the comparison would be vacuous";
    EXPECT_EQ(base, with_noop) << "periodicity=" << periodicity;
    Recorder rec;                        // a recording observer must not change the map either
    const auto with_rec = fingerprint(&rec, periodicity);
    EXPECT_EQ(base, with_rec) << "periodicity=" << periodicity;
    EXPECT_EQ(rec.reports.size(), n.calls);
  }
}

TEST(WindowObserver, ReportsDoNotDependOnPeriodicityOrPruning) {
  // What the observer sees is the touch record of each window. It must be the same with
  // the periodic model on or off.
  Recorder a, b;
  fingerprint(&a, false);
  fingerprint(&b, true);
  ASSERT_EQ(a.reports.size(), b.reports.size());
  auto key = [](const Recorder& r) {
    std::map<std::pair<int, CellId>, std::pair<bool, bool>> m;
    for (const auto& x : r.reports) m[{x.window, x.id}] = {x.hit, x.miss};
    return m;
  };
  EXPECT_EQ(key(a), key(b));
}

TEST(WindowObserver, Voxel3DBackendForwardsAndSeesTheEndpointVoxelAsHit) {
  LayeredMapParams p; p.layer_interval = 1;
  Voxel3DBackend b(0.2, p);
  Recorder rec;
  b.setWindowObserver(&rec);
  Observation obs; obs.hits.push_back(Eigen::Vector3d(2.0, 0.0, 0.0));
  b.integrate(obs, Eigen::Vector3d(0.0, 0.0, 0.0));
  EXPECT_TRUE(b.tick());
  const CellId end_id = b.voxelId(Eigen::Vector3d(2.0, 0.0, 0.0));
  int hits = 0, misses = 0; bool end_hit = false;
  for (const auto& r : rec.reports) {
    EXPECT_NE(r.hit, r.miss);
    hits += r.hit; misses += r.miss;
    if (r.id == end_id) end_hit = r.hit;
  }
  EXPECT_EQ(hits, 1);
  EXPECT_TRUE(end_hit);
  EXPECT_EQ(misses, 10);   // samples every 0.1 m from 0.1 to 1.9 m fall in voxels 0..9; the endpoint is voxel 10
}

TEST(WindowObserver, Grid2DBackendForwardsAndRayCellsAreMisses) {
  GridMeta meta; meta.width = 40; meta.height = 40; meta.resolution = 0.1; meta.origin_x = -2.0; meta.origin_y = -2.0;
  LayeredMapParams p; p.layer_interval = 1;
  Grid2DBackend b(meta, p);
  Recorder rec;
  b.setWindowObserver(&rec);
  Observation obs; obs.hits.push_back(Eigen::Vector3d(1.0, 0.0, 0.0));
  b.integrate(obs, Eigen::Vector3d(0.0, 0.0, 0.0));
  EXPECT_TRUE(b.tick());
  int hits = 0, misses = 0;
  for (const auto& r : rec.reports) { hits += r.hit; misses += r.miss; EXPECT_NE(r.hit, r.miss); }
  EXPECT_EQ(hits, 1);
  EXPECT_EQ(misses, 10);   // cells from the sensor cell up to, excluding, the hit cell
}
