// E4 — Throughput and memory.
// Per-observation integrate latency, window-close (endWindow) cell-update
// throughput, and a rough memory footprint (bytes/cell * live cells), for grid2d
// vs voxel3d at three map sizes. Deterministic hit streams from eval::kSeed; each
// configuration is run once as a discarded warm-up and then kRepeats times on a
// fresh map; the median time is reported with the minimum and maximum, and every
// repeat's time is written to an optional second file.
//
// Per-cell footprint (x86-64/libstdc++): CellEvidence 24 B plus
// ~32 B unordered_map node = 56 B/cell with periodicity disabled (this config).
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "common.hpp"
#include "strata_core/grid2d_backend.hpp"
#include "strata_core/voxel3d_backend.hpp"

using namespace strata_core;
using clk = std::chrono::steady_clock;

namespace {

constexpr int kHits = 150;      // hits per window
constexpr int kWindows = 20;    // windows per config
constexpr int kRepeats = 15;    // timed runs per config (odd: the median is one of them)
constexpr int kBytesPerCell = 56;  // evidence 24 + ~32 node, periodicity off

LayeredMapParams params() {
  LayeredMapParams p; p.evidence_model = eval::armModel();   // legacy: the committed v0.1 results; sprt: the shipped test
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

// One timed run of a configuration: a fresh map fed the same seeded stream.
struct Run {
  double integrate_s{0.0};   // total time inside integrate() over the run
  double endwindow_s{0.0};   // total time inside tick() (one endWindow() each)
  long long sum_cells{0};    // live cells summed over the window closes
  std::size_t final_cells{0};
};

Run runGrid(int ext) {
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
  Run r;
  for (int w = 0; w < kWindows; ++w) {
    Observation obs;
    obs.hits.reserve(kHits);
    for (int i = 0; i < kHits; ++i) obs.hits.emplace_back(u(rng), u(rng), 0.0);
    auto t0 = clk::now();
    b.integrate(obs, sensor);
    auto t1 = clk::now();
    r.sum_cells += b.layered().cellCount();
    b.tick();
    auto t2 = clk::now();
    tInt += t1 - t0;
    tTick += t2 - t1;
  }
  r.integrate_s = secs(tInt);
  r.endwindow_s = secs(tTick);
  r.final_cells = b.layered().cellCount();
  return r;
}

Run runVoxel(int ext) {
  Voxel3DBackend b(1.0, params());
  const Eigen::Vector3d sensor(ext / 2.0, ext / 2.0, ext / 4.0);
  std::mt19937 rng(eval::kSeed + 1000 + ext);
  std::uniform_real_distribution<double> u(0.0, ext);
  std::uniform_real_distribution<double> uz(0.0, ext / 2.0);

  clk::duration tInt{0}, tTick{0};
  Run r;
  for (int w = 0; w < kWindows; ++w) {
    Observation obs;
    obs.hits.reserve(kHits);
    for (int i = 0; i < kHits; ++i)
      obs.hits.emplace_back(u(rng), u(rng), uz(rng));
    auto t0 = clk::now();
    b.integrate(obs, sensor);
    auto t1 = clk::now();
    r.sum_cells += b.layered().cellCount();
    b.tick();
    auto t2 = clk::now();
    tInt += t1 - t0;
    tTick += t2 - t1;
  }
  r.integrate_s = secs(tInt);
  r.endwindow_s = secs(tTick);
  r.final_cells = b.layered().cellCount();
  return r;
}

double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];   // kRepeats is odd
}

}  // namespace

// Usage: e4_throughput [output.csv [per_repeat.csv]]   (default results/e4_throughput.csv)
//        e4_throughput sprt                            (cell counts only, results/e4_cells_sprt.csv)
// The sprt arm runs each configuration once with evidence_model = Sprt and writes only the
// deterministic columns (no timing): the timing of the sequential test is recorded later,
// on a quiet host, with measure_e4.sh, and is not part of this file.
// The wall-clock columns differ on every run and machine; the cell counts and byte
// estimates do not. run_all.sh therefore writes a local file and compares its
// deterministic columns with the committed reference (check_e4_local.py).
int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "sprt") {
    eval::sprtArm() = true;
    eval::Csv cells("results/e4_cells_sprt.csv");
    cells.header("backend,size_label,extent,hits_per_window,windows,final_cells,sum_cells,bytes_per_cell,est_bytes");
    cells.row("#seed", eval::kSeed, "", "", "", "", "", "", "");
    for (int ext : {100, 250, 500})
      for (const char* backend : {"grid2d", "voxel3d"}) {
        const Run r = std::string(backend) == "grid2d" ? runGrid(ext) : runVoxel(ext);
        cells.row(backend, std::to_string(ext) + "x" + std::to_string(ext), ext, kHits, kWindows,
                  r.final_cells, r.sum_cells, kBytesPerCell, (long long)r.final_cells * kBytesPerCell);
      }
    std::cout << "E4 (sprt) cell counts -> results/e4_cells_sprt.csv\n";
    return cells.ok() ? 0 : 1;
  }
  const std::string out = argc > 1 ? argv[1] : "results/e4_throughput.csv";
  eval::Csv csv(out);
  std::ofstream raw;
  if (argc > 2) {
    raw.open(argv[2]);
    raw << "backend,extent,repeat,us_per_integrate,us_per_endwindow,final_cells\n";
  }
  csv.header(
      "backend,size_label,extent,hits_per_window,windows,us_per_integrate,"
      "hits_per_sec,us_per_endwindow,cell_updates_per_sec,final_cells,"
      "bytes_per_cell,est_bytes,repeats,us_per_integrate_min,us_per_integrate_max,"
      "us_per_endwindow_min,us_per_endwindow_max");
  csv.row("#seed", eval::kSeed, "", "", "", "", "", "", "", "", "", "", "", "", "", "", "");

  const std::vector<int> extents = {100, 250, 500};
  bool deterministic = true;

  for (int ext : extents) {
    const std::string label = std::to_string(ext) + "x" + std::to_string(ext);
    for (const char* backend : {"grid2d", "voxel3d"}) {
      const bool grid = std::string(backend) == "grid2d";
      std::vector<double> ti, tt;
      Run first;
      (void)(grid ? runGrid(ext) : runVoxel(ext));   // warm-up, discarded
      for (int rep = 0; rep < kRepeats; ++rep) {
        const Run r = grid ? runGrid(ext) : runVoxel(ext);
        if (rep == 0) first = r;
        if (raw.is_open())
          raw << backend << "," << ext << "," << rep << "," << r.integrate_s * 1e6 / kWindows << ","
              << r.endwindow_s * 1e6 / kWindows << "," << r.final_cells << "\n";
        // The stream is seeded, so every repeat must build the same map.
        if (r.final_cells != first.final_cells || r.sum_cells != first.sum_cells) deterministic = false;
        ti.push_back(r.integrate_s);
        tt.push_back(r.endwindow_s);
      }
      // Each repeat gives a mean over its windows; the row reports the median repeat.
      const double si = median(ti), st = median(tt);
      const double us = 1e6 / kWindows;
      csv.row(backend, label, ext, kHits, kWindows,
              si * us, (double)kHits * kWindows / si,
              st * us, first.sum_cells / st, first.final_cells, kBytesPerCell,
              (long long)first.final_cells * kBytesPerCell, kRepeats,
              *std::min_element(ti.begin(), ti.end()) * us, *std::max_element(ti.begin(), ti.end()) * us,
              *std::min_element(tt.begin(), tt.end()) * us, *std::max_element(tt.begin(), tt.end()) * us);
    }
  }

  if (!deterministic) {
    std::cerr << "E4: repeats of one configuration built different maps\n";
    return 1;
  }
  std::cout << "E4 done -> " << out << "\n";
  return csv.ok() ? 0 : 1;
}
