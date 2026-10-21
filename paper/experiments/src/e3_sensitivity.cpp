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
#include <string>
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
  LayeredMapParams p; p.evidence_model = eval::armModel();   // legacy: the committed v0.1 results; sprt: the shipped test
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

Result run(double graduate_prob, double demote_prob, double decay, bool periodicity) {
  LayeredMapParams p = base();
  p.enable_periodicity = periodicity;  // defaults T=24, H=2, a_min=0.3, delta=0.2 spent (E5) when on
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


int sweep(bool periodicity, const char* path) {
  eval::Csv csv(path);
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
        auto r = run(g, dem, d, periodicity);
        csv.row(g, dem, d, g - dem, degenerate ? 1 : 0, r.f1, r.precision,
                r.recall, r.pred, r.flicker);
      }
    }
  }

  std::cout << "E3 done -> " << path << "\n";
  return csv.ok() ? 0 : 1;
}

// ---- sprt arm -----------------------------------------------------------------------
// The v0.1 sweep (graduate_prob x demote_prob x survival_decay) has no counterpart under
// the sequential test: thresholds are derived from (m0, m1, alpha, beta) and there is no
// decay. The hysteresis band is fixed by the error rate: a graduated cell restarts at the
// ceiling and is demoted when the reverse statistic from there crosses A, so the number of
// all-miss windows to a demotion (demote_misses) follows from (l_miss, A) alone. The
// distance between the ceiling and A does not enter the dynamics of a cell that is not
// held back by the periodicity test, so it is not swept. The sprt arm sweeps four (m0, m1)
// pairs x three error rates (alpha = beta) on the identical noise sequence. Nothing is
// chosen from it; the file is a sensitivity table.
Result runSprt(double m0, double m1, double ab, bool periodicity, SprtDerived& d) {
  LayeredMapParams p = base();
  p.enable_periodicity = periodicity;
  p.m0 = m0; p.m1 = m1; p.alpha = ab; p.beta = ab;
  LayeredMap lm(p);
  d = lm.sprt();

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
      if (seen(rng)) lm.observeHit(i); else lm.observeMiss(i);
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

int sweepSprt(bool periodicity, const std::string& path) {
  eval::Csv csv(path);
  csv.header(
      "m0,m1,alpha_beta,A,l_hit,l_miss,m_star,latency,demote_misses,"
      "final_f1,final_precision,final_recall,pred_static,flicker_transitions");
  csv.row("#seed", eval::kSeed, "windows", kWindows, "walls", kWalls, "occ", kOcc, "", "", "", "", "", "");
  const std::vector<std::pair<double, double>> pairs = {{0.20, 0.70}, {0.20, 0.60}, {0.30, 0.70}, {0.10, 0.50}};
  for (const auto& pr : pairs)
    for (double ab : {0.05, 0.01, 0.001}) {
      SprtDerived d;
      const auto r = runSprt(pr.first, pr.second, ab, periodicity, d);
      csv.row(pr.first, pr.second, ab, d.A, d.l_hit, d.l_miss, d.m_star, d.latency, d.demote_misses,
              r.f1, r.precision, r.recall, r.pred, r.flicker);
    }
  std::cout << "E3 (sprt) done -> " << path << "\n";
  return csv.ok() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (!eval::parseArm(argc, argv)) return 2;
  if (eval::sprtArm()) {
    int rc = sweepSprt(false, eval::armPath("results/e3_sensitivity.csv"));
    rc |= sweepSprt(true, eval::armPath("results/e3_sensitivity_periodic.csv"));
    return rc;
  }
  // Main sweep isolates the hysteresis band (periodicity off); the second sweep
  // repeats it with periodicity on at the shipped defaults, where the periodic
  // predicate can also move a wall cell in and out of the static layer.
  int rc = sweep(false, "results/e3_sensitivity.csv");
  rc |= sweep(true, "results/e3_sensitivity_periodic.csv");
  return rc;
}
