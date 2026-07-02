// E2 — Periodicity detection.
// Doors with known periods (detectable against FreMEn base period T=8) plus a
// constant wall and several aperiodic movers as negative controls. We report:
//   (a) per-cell final classification -> Periodic TPR / FPR vs the GT periodic set;
//   (b) detected FreMEn amplitude vs observation length.
//
// Classification is the real LayeredMap::classify output. Amplitude is read from a
// PeriodicityModel fed the identical touched-window occupancy stream that LayeredMap
// feeds internally (gather(occ, window_count_) every touched window), so the two
// agree; PeriodicityModel is exercised directly because LayeredMap does not expose
// amplitude. Deterministic: aperiodic patterns precomputed from eval::kSeed.
#include <array>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "common.hpp"
#include "strata_core/layered_map.hpp"
#include "strata_core/periodicity.hpp"

using namespace strata_core;

namespace {

constexpr int kMaxLen = 64;
constexpr int kPeriod = 8;
constexpr int kHarmonics = 3;

LayeredMapParams params() {
  LayeredMapParams p;
  p.layer_interval = 1;
  p.l_hit = 1.0;
  p.l_miss = -1.0;
  p.l_min = -5.0;
  p.l_max = 5.0;
  p.survival_decay = 1.0;
  p.graduate_prob = 0.9;
  p.demote_prob = 0.4;
  p.min_observations = 5;
  p.prune_prob = 0.05;
  p.enable_periodicity = true;
  p.periodic_amplitude_min = 0.3;
  p.periodicity.period_windows = kPeriod;
  p.periodicity.n_harmonics = kHarmonics;
  return p;
}

struct Cell {
  CellId id;
  std::string name;
  bool gt_periodic;
  std::array<bool, kMaxLen> occ;  // occupancy pattern per window
};

const char* className(CellClass c) {
  switch (c) {
    case CellClass::Unknown: return "Unknown";
    case CellClass::Transient: return "Transient";
    case CellClass::Periodic: return "Periodic";
    case CellClass::Static: return "Static";
  }
  return "?";
}

std::vector<Cell> makeCells() {
  std::vector<Cell> cs;
  auto add = [&](CellId id, const std::string& n, bool gt, auto fn) {
    Cell c;
    c.id = id;
    c.name = n;
    c.gt_periodic = gt;
    for (int w = 0; w < kMaxLen; ++w) c.occ[w] = fn(w);
    cs.push_back(c);
  };
  // Periodic doors (GT positive).
  add(1, "door_p8_4on4off", true, [](int w) { return (w % 8) < 4; });
  add(2, "door_p8_2on6off", true, [](int w) { return (w % 8) < 2; });
  add(3, "door_p4_2on2off", true, [](int w) { return (w % 4) < 2; });
  // Constant wall (GT negative: high mean, ~no periodic amplitude).
  add(10, "wall_constant", false, [](int) { return true; });
  // Aperiodic movers (GT negative): deterministic pseudo-random occupancy.
  for (int a = 0; a < 4; ++a) {
    std::mt19937 rng(eval::kSeed + 500 + a);
    std::bernoulli_distribution coin(0.5);
    std::array<bool, kMaxLen> pat;
    for (int w = 0; w < kMaxLen; ++w) pat[w] = coin(rng);
    add(20 + a, "aperiodic_" + std::to_string(a), false,
        [pat](int w) { return pat[w]; });
  }
  return cs;
}

}  // namespace

int main() {
  const auto cells = makeCells();

  // (b) amplitude vs observation length, plus final classification per cell.
  eval::Csv amp("results/e2_amplitude_vs_length.csv");
  amp.header("cell,gt_periodic,obs_length,amplitude,periodic_prob");
  amp.row("#seed", eval::kSeed, "period_windows", kPeriod, "");

  const std::vector<int> lengths = {6, 8, 12, 16, 24, 32, 48, 64};
  for (const auto& c : cells) {
    for (int L : lengths) {
      PeriodicityModel pm({kPeriod, kHarmonics});
      for (int w = 0; w < L; ++w) pm.gather(c.id, c.occ[w], w + 1);
      amp.row(c.name, c.gt_periodic ? 1 : 0, L, pm.amplitude(c.id),
              pm.predict(c.id, L));
    }
  }

  // (a) final classification via the real LayeredMap pipeline, at full length.
  eval::Csv cls("results/e2_classification.csv");
  // ref_amplitude is from the non-pruning mirror model; the real pipeline may
  // prune a low-duty cell before its FreMEn evidence matures, so a cell can show
  // ref_amplitude >= a_min yet still be classified non-Periodic (see door_p8_2on6off).
  cls.header(
      "cell,gt_periodic,final_class,pred_periodic,ref_amplitude,periodic_prob");
  cls.row("#seed", eval::kSeed, "obs_length", kMaxLen, "", "");

  LayeredMap lm(params());
  PeriodicityModel mirror({kPeriod, kHarmonics});  // mirrors internal gather stream
  for (int w = 0; w < kMaxLen; ++w) {
    for (const auto& c : cells) {
      if (c.occ[w])
        lm.observeHit(c.id);
      else
        lm.observeMiss(c.id);
    }
    lm.tick();  // window_count_ becomes w+1; gather(occ, w+1) runs internally
    for (const auto& c : cells) mirror.gather(c.id, c.occ[w], w + 1);
  }

  int tp = 0, fp = 0, gt_pos = 0, gt_neg = 0;
  for (const auto& c : cells) {
    const CellClass fc = lm.classify(c.id);
    const bool pred_per = (fc == CellClass::Periodic);
    cls.row(c.name, c.gt_periodic ? 1 : 0, className(fc), pred_per ? 1 : 0,
            mirror.amplitude(c.id), lm.periodicProb(c.id));
    if (c.gt_periodic) {
      ++gt_pos;
      if (pred_per) ++tp;
    } else {
      ++gt_neg;
      if (pred_per) ++fp;
    }
  }
  const double tpr = gt_pos ? static_cast<double>(tp) / gt_pos : 0.0;
  const double fpr = gt_neg ? static_cast<double>(fp) / gt_neg : 0.0;

  eval::Csv sum("results/e2_summary.csv");
  sum.header("metric,value,note");
  sum.row("periodic_TPR", tpr, tp);
  sum.row("periodic_FPR", fpr, fp);
  sum.row("gt_periodic", gt_pos, "");
  sum.row("gt_nonperiodic", gt_neg, "");

  std::cout << "E2 done: TPR=" << tpr << " (" << tp << "/" << gt_pos
            << ") FPR=" << fpr << " (" << fp << "/" << gt_neg << ")\n";
  return (amp.ok() && cls.ok() && sum.ok()) ? 0 : 1;
}
