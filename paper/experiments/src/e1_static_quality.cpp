// E1 — Static-map quality vs time.
// A world with ground-truth static walls plus transient movers at three clutter
// densities. Per epoch (window) we measure the Static-layer precision/recall/F1
// against the GT wall set, for both grid2d and voxel3d backends.
//
// Deterministic: movers drawn from std::mt19937 seeded by eval::kSeed + density id.
#include <iomanip>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "common.hpp"
#include "strata_core/grid2d_backend.hpp"
#include "strata_core/voxel3d_backend.hpp"

using namespace strata_core;

namespace {

constexpr int kWidth = 120;
constexpr int kHeight = 120;
constexpr double kRes = 1.0;
constexpr int kWindows = 40;
constexpr double kVoxel = 1.0;

LayeredMapParams params() {
  LayeredMapParams p;
  p.layer_interval = 1;
  p.l_hit = 0.85;
  p.l_miss = -0.4;
  p.l_min = -5.0;
  p.l_max = 5.0;
  p.survival_decay = 0.97;
  p.graduate_prob = 0.8;
  p.demote_prob = 0.45;
  p.min_observations = 3;
  p.prune_prob = 0.05;
  p.enable_periodicity = false;  // isolate static-layer quality
  return p;
}

// Ground-truth static walls: a horizontal segment y=60, x in [20,100] and a
// vertical segment x=60, y in [20,100] (an L/cross of ~161 cells).
struct Wall {
  int gx, gy;
};
std::vector<Wall> wallCells() {
  std::vector<Wall> w;
  for (int x = 20; x <= 100; ++x) w.push_back({x, 60});
  for (int y = 20; y <= 100; ++y) w.push_back({60, y});
  return w;
}

const char* densName(int d) { return d == 0 ? "low" : (d == 1 ? "med" : "high"); }
int densMovers(int d) { return d == 0 ? 5 : (d == 1 ? 25 : 100); }

}  // namespace

int main() {
  eval::Csv csv("results/e1_static_quality.csv");
  csv.header(
      "backend,density,n_movers,window,gt_static,pred_static,tp,precision,recall,"
      "f1");
  csv.row("#seed", eval::kSeed, "", "", "", "", "", "", "", "");

  const auto walls = wallCells();
  const Eigen::Vector3d sensor2d(5.5, 5.5, 0.0);
  const Eigen::Vector3d sensor3d(5.5, 5.5, 0.5);

  GridMeta meta;
  meta.width = kWidth;
  meta.height = kHeight;
  meta.resolution = kRes;
  meta.origin_x = 0;
  meta.origin_y = 0;

  for (int d = 0; d < 3; ++d) {
    const int nMovers = densMovers(d);

    // ---- grid2d ----
    {
      Grid2DBackend b(meta, params());
      std::set<CellId> gt;
      for (const auto& w : walls) gt.insert(gridCellId(meta, w.gx, w.gy));
      std::mt19937 rng(eval::kSeed + d);
      std::uniform_int_distribution<int> ux(0, kWidth - 1), uy(0, kHeight - 1);
      for (int win = 0; win < kWindows; ++win) {
        Observation obs;
        for (const auto& w : walls)
          obs.hits.emplace_back(w.gx + 0.5, w.gy + 0.5, 0.0);
        for (int i = 0; i < nMovers; ++i)
          obs.hits.emplace_back(ux(rng) + 0.5, uy(rng) + 0.5, 0.0);
        b.integrate(obs, sensor2d);
        b.tick();
        std::set<CellId> pred;
        for (CellId id : b.layered().staticCells()) pred.insert(id);
        auto m = eval::prf(pred, gt);
        csv.row("grid2d", densName(d), nMovers, win, m.gt, m.pred, m.tp,
                m.precision, m.recall, m.f1);
      }
    }

    // ---- voxel3d ----
    {
      Voxel3DBackend b(kVoxel, params());
      std::set<CellId> gt;
      for (const auto& w : walls)
        gt.insert(b.voxelId(Eigen::Vector3d(w.gx + 0.5, w.gy + 0.5, 0.5)));
      std::mt19937 rng(eval::kSeed + 100 + d);
      std::uniform_real_distribution<double> ux(0.0, kWidth), uy(0.0, kHeight);
      for (int win = 0; win < kWindows; ++win) {
        Observation obs;
        for (const auto& w : walls)
          obs.hits.emplace_back(w.gx + 0.5, w.gy + 0.5, 0.5);
        for (int i = 0; i < nMovers; ++i)
          obs.hits.emplace_back(ux(rng), uy(rng), 0.5);
        b.integrate(obs, sensor3d);
        b.tick();
        std::set<CellId> pred;
        for (CellId id : b.layered().staticCells()) pred.insert(id);
        auto m = eval::prf(pred, gt);
        csv.row("voxel3d", densName(d), nMovers, win, m.gt, m.pred, m.tp,
                m.precision, m.recall, m.f1);
      }
    }
  }

  std::cout << "E1 done -> results/e1_static_quality.csv\n";
  return csv.ok() ? 0 : 1;
}
