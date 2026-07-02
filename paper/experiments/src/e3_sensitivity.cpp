// E3 — Parameter sensitivity: hysteresis band and decay.
// Ground-truth static walls observed under occlusion noise (each window a wall
// cell is seen occupied with prob p_occ, else observed free) plus fresh transient
// movers. We sweep graduate_prob x demote_prob (including the degenerate
// graduate==demote, i.e. no hysteresis) and survival_decay, reporting the final
// Static-layer F1 and the number of "flicker" transitions (per-window toggles of a
// wall cell's isStatic state). Wide hysteresis + low forgetting suppress flicker.
//
// Deterministic: the identical noise sequence is replayed for every config
// (rng reseeded with eval::kSeed each config), so differences are purely parametric.
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <vector>

#include "common.hpp"
#include "strata_core/layered_map.hpp"

using namespace strata_core;

namespace {

constexpr int kWalls = 50;
constexpr int kWindows = 80;
constexpr int kMoversPerWindow = 10;
constexpr double kOcc = 0.6;  // per-window occupied-observation prob for a wall cell
constexpr CellId kMoverBase = 100000;

LayeredMapParams base() {
  LayeredMapParams p;
  p.layer_interval = 1;
  p.l_hit = 0.85;
  p.l_miss = -0.4;
  p.l_min = -5.0;
  p.l_max = 5.0;
  p.min_observations = 3;
  p.prune_prob = 0.01;  // low: keep noisy walls alive, isolate graduate flicker
  p.enable_periodicity = false;
  return p;
}

struct Result {
  double f1, precision, recall;
  std::size_t pred;
  long flicker;
};

Result run(double graduate_prob, double demote_prob, double decay) {
  LayeredMapParams p = base();
  p.graduate_prob = graduate_prob;
  p.demote_prob = demote_prob;
  p.survival_decay = decay;
  LayeredMap lm(p);

  std::set<CellId> gt;
  for (int i = 1; i <= kWalls; ++i) gt.insert(i);

  std::mt19937 rng(eval::kSeed);  // identical sequence every config
  std::bernoulli_distribution seen(kOcc);

  std::map<CellId, bool> prevStatic;
  for (int i = 1; i <= kWalls; ++i) prevStatic[i] = false;
  long flicker = 0;
  CellId mover = kMoverBase;

  for (int w = 0; w < kWindows; ++w) {
    for (int i = 1; i <= kWalls; ++i) {
      if (seen(rng))
        lm.observeHit(i);
      else
        lm.observeMiss(i);
    }
    for (int m = 0; m < kMoversPerWindow; ++m) lm.observeHit(mover++);
    lm.tick();
    for (int i = 1; i <= kWalls; ++i) {
      const bool s = lm.isStatic(i);
      if (s != prevStatic[i]) ++flicker;
      prevStatic[i] = s;
    }
  }

  std::set<CellId> pred;
  for (CellId id : lm.staticCells()) pred.insert(id);
  auto m = eval::prf(pred, gt);
  return {m.f1, m.precision, m.recall, m.pred, flicker};
}

}  // namespace

int main() {
  eval::Csv csv("results/e3_sensitivity.csv");
  csv.header(
      "graduate_prob,demote_prob,survival_decay,hysteresis_band,degenerate,"
      "final_f1,final_precision,final_recall,pred_static,flicker_transitions");
  csv.row("#seed", eval::kSeed, "windows", kWindows, "walls", kWalls, "occ",
          kOcc, "", "");

  const std::vector<double> grads = {0.6, 0.7, 0.8, 0.9};
  const std::vector<double> decays = {0.90, 0.97, 1.0};

  for (double g : grads) {
    // demote choices: a low floor, a mid floor, and the degenerate no-hysteresis
    // case (demote == graduate). Keep demote <= graduate.
    std::vector<double> demotes = {0.3, 0.5, g};
    for (double d : decays) {
      for (double dem : demotes) {
        if (dem > g + 1e-9) continue;
        const bool degenerate = (std::abs(dem - g) < 1e-9);
        auto r = run(g, dem, d);
        csv.row(g, dem, d, g - dem, degenerate ? 1 : 0, r.f1, r.precision,
                r.recall, r.pred, r.flicker);
      }
    }
  }

  std::cout << "E3 done -> results/e3_sensitivity.csv\n";
  return csv.ok() ? 0 : 1;
}
