// E4 — Throughput and memory.
// Per-observation integrate latency, window-close (endWindow) cell-update
// throughput, and a rough memory footprint (bytes/cell * live cells), for grid2d
// vs voxel3d at three map sizes. Deterministic hit streams from eval::kSeed.
//
// Per-cell footprint (x86-64/libstdc++, from core_math.md): CellEvidence 24 B plus
// ~32 B unordered_map node = 56 B/cell with periodicity disabled (this config).
#include <chrono>
#include <iostream>
#include <random>
#include <vector>

#include "common.hpp"
#include "strata_core/grid2d_backend.hpp"
#include "strata_core/voxel3d_backend.hpp"

using namespace strata_core;
using clk = std::chrono::steady_clock;

namespace {

constexpr int kHits = 150;      // hits per window
constexpr int kWindows = 20;    // windows per config
constexpr int kBytesPerCell = 56;  // evidence 24 + ~32 node, periodicity off

LayeredMapParams params() {
  LayeredMapParams p;
  p.layer_interval = 1;
  p.l_hit = 0.85;
  p.l_miss = -0.4;
  p.l_min = -5.0;
  p.l_max = 5.0;
  p.survival_decay = 1.0;
  p.graduate_prob = 0.8;
  p.demote_prob = 0.45;
  p.min_observations = 3;
  p.prune_prob = 0.05;
  p.enable_periodicity = false;
  return p;
}

double secs(clk::duration d) {
  return std::chrono::duration<double>(d).count();
}

}  // namespace

int main() {
  eval::Csv csv("results/e4_throughput.csv");
  csv.header(
      "backend,size_label,extent,hits_per_window,windows,us_per_integrate,"
      "hits_per_sec,us_per_endwindow,cell_updates_per_sec,final_cells,"
      "bytes_per_cell,est_bytes");
  csv.row("#seed", eval::kSeed, "", "", "", "", "", "", "", "", "", "");

  const std::vector<int> extents = {100, 250, 500};

  for (int ext : extents) {
    const std::string label = std::to_string(ext) + "x" + std::to_string(ext);

    // ---- grid2d ----
    {
      GridMeta m;
      m.width = ext;
      m.height = ext;
      m.resolution = 1.0;
      m.origin_x = 0;
      m.origin_y = 0;
      Grid2DBackend b(m, params());
      const Eigen::Vector3d sensor(ext / 2.0, ext / 2.0, 0.0);
      std::mt19937 rng(eval::kSeed + ext);
      std::uniform_real_distribution<double> u(0.0, ext);

      clk::duration tInt{0}, tTick{0};
      long long sumCells = 0;
      for (int w = 0; w < kWindows; ++w) {
        Observation obs;
        obs.hits.reserve(kHits);
        for (int i = 0; i < kHits; ++i) obs.hits.emplace_back(u(rng), u(rng), 0.0);
        auto t0 = clk::now();
        b.integrate(obs, sensor);
        auto t1 = clk::now();
        sumCells += b.layered().cellCount();
        b.tick();
        auto t2 = clk::now();
        tInt += t1 - t0;
        tTick += t2 - t1;
      }
      const double si = secs(tInt), st = secs(tTick);
      const std::size_t cells = b.layered().cellCount();
      csv.row("grid2d", label, ext, kHits, kWindows,
              si / kWindows * 1e6, (double)kHits * kWindows / si,
              st / kWindows * 1e6, sumCells / st, cells, kBytesPerCell,
              (long long)cells * kBytesPerCell);
    }

    // ---- voxel3d ----
    {
      Voxel3DBackend b(1.0, params());
      const Eigen::Vector3d sensor(ext / 2.0, ext / 2.0, ext / 4.0);
      std::mt19937 rng(eval::kSeed + 1000 + ext);
      std::uniform_real_distribution<double> u(0.0, ext);
      std::uniform_real_distribution<double> uz(0.0, ext / 2.0);

      clk::duration tInt{0}, tTick{0};
      long long sumCells = 0;
      for (int w = 0; w < kWindows; ++w) {
        Observation obs;
        obs.hits.reserve(kHits);
        for (int i = 0; i < kHits; ++i)
          obs.hits.emplace_back(u(rng), u(rng), uz(rng));
        auto t0 = clk::now();
        b.integrate(obs, sensor);
        auto t1 = clk::now();
        sumCells += b.layered().cellCount();
        b.tick();
        auto t2 = clk::now();
        tInt += t1 - t0;
        tTick += t2 - t1;
      }
      const double si = secs(tInt), st = secs(tTick);
      const std::size_t cells = b.layered().cellCount();
      csv.row("voxel3d", label, ext, kHits, kWindows,
              si / kWindows * 1e6, (double)kHits * kWindows / si,
              st / kWindows * 1e6, sumCells / st, cells, kBytesPerCell,
              (long long)cells * kBytesPerCell);
    }
  }

  std::cout << "E4 done -> results/e4_throughput.csv\n";
  return csv.ok() ? 0 : 1;
}
