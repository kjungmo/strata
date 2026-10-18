// E6 — False Static labels on aperiodic clutter: the four-class confusion of E5 under
// the SHIPPED defaults, and the share of Bernoulli(m) cells labelled Static as a
// function of m, for four parameter sets.
//
// Fixed before the run:
//  (1) Confusion. The protocol of E5's confusion (e5_trajectory.cpp, part 3): 20 scenes,
//      seeds kEvalSeed + 97000 + s, per scene 50 walls, 20 doors of each of three types
//      at random phase, 100 aperiodic Bernoulli(0.5) cells, 50 dynamic cells (mover with
//      probability 0.1 per window); touched every window; read out at 64 and 256 windows
//      with the shipped rule (delta 0.2 spent). Parameter sets:
//        e2            E2 parameters; the same code path and seeds as E5, so its rows
//                      must equal E5's "spending" rows (number_guard.py checks it);
//        e2_decay097   E2 parameters with survival_decay 0.97;
//        shipped       library defaults (layer_interval 1).
//      Doors: E2 sets use E5's doors (8: 4 on / 4 off, 8: 2 on / 6 off, 4: 2 on / 2 off).
//      The shipped set tests T = 24, H = 2, which cannot see those periods, so it gets
//      the same duty cycles at three times the period (24: 12/12, 24: 6/18, 12: 6/6).
//  (2) Sweep. Bernoulli(m) cells, m = 0.05, 0.10, ..., 0.95, 2000 cells per point
//      (10 seeds of 200, seeds kSweepSeed + 100 * point + s), touched every window, 256
//      windows; counted: Static at 64 and at 256, ever Static within 256. Parameter
//      sets: the three above and
//        shipped_sym   library defaults with l_miss = -l_hit = -0.85.
//      The mean-field threshold m_dagger written beside each row is
//        (logit(graduate_prob) * (1 - lambda) / lambda - l_miss) / (l_hit - l_miss),
//      the occupancy rate above which the stationary mean of the unclamped log-odds
//      recursion l <- lambda * (l + x) lies above the graduation threshold.
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "common.hpp"
#include "strata_core/layered_map.hpp"

using namespace strata_core;

namespace {
constexpr unsigned kEvalSeed = 40260928u;    // E5's evaluation base (confusion: + 97000)
constexpr unsigned kSweepSeed = 50260928u;   // disjoint from E0-E5 and the unit tests

LayeredMapParams e2Params() {  // identical to e5_trajectory.cpp e2Params()
  LayeredMapParams p; p.evidence_model = EvidenceModel::Legacy;   // the committed results are the v0.1 arithmetic
  p.layer_interval = 1; p.l_hit = 1.0; p.l_miss = -1.0; p.l_min = -5.0; p.l_max = 5.0;
  p.survival_decay = 1.0; p.graduate_prob = 0.9; p.demote_prob = 0.4;
  p.min_observations = 5; p.prune_prob = 0.05; p.enable_periodicity = true;
  p.periodic_amplitude_min = 0.3; p.periodicity.period_windows = 8;
  p.periodicity.n_harmonics = 3;
  return p;
}
LayeredMapParams shippedParams() {
  LayeredMapParams p; p.evidence_model = EvidenceModel::Legacy;   // the committed results are the v0.1 arithmetic
  p.layer_interval = 1;
  return p;
}
struct Set { std::string name; LayeredMapParams p; bool e2_doors; };

double meanFieldThreshold(const LayeredMapParams& p) {
  const double lg = std::log(p.graduate_prob / (1.0 - p.graduate_prob));
  return (lg * (1.0 - p.survival_decay) / p.survival_decay - p.l_miss) / (p.l_hit - p.l_miss);
}
}  // namespace

int main() {
  LayeredMapParams e2d = e2Params(); e2d.survival_decay = 0.97;
  LayeredMapParams sym = shippedParams(); sym.l_miss = -sym.l_hit;
  const std::vector<Set> conf_sets = {{"e2", e2Params(), true}, {"e2_decay097", e2d, true},
                                      {"shipped", shippedParams(), false}};
  const std::vector<Set> sweep_sets = {{"e2", e2Params(), true}, {"e2_decay097", e2d, true},
                                       {"shipped", shippedParams(), false}, {"shipped_sym", sym, false}};

  // ---------------------------------------------------------------- (1) confusion
  eval::Csv cf("results/e6_confusion.csv");
  cf.header("params,readout,gt,pred_S,pred_P,pred_T,pred_U");
  cf.row("#seed", kEvalSeed + 97000, "scenes", 20, "", "", "");
  const std::vector<std::string> gts = {"wall", "door", "aperiodic", "dynamic"};
  const std::vector<int> readouts = {64, 256};
  for (const auto& set : conf_sets) {
    std::vector<std::vector<std::vector<int>>> cnt(
        2, std::vector<std::vector<int>>(4, std::vector<int>(4, 0)));
    for (int s = 0; s < 20; ++s) {
      std::mt19937 rng(kEvalSeed + 97000 + unsigned(s));
      struct C { int gt; int period, on, phase; };
      std::vector<C> cells;
      for (int i = 0; i < 50; ++i) cells.push_back({0, 0, 0, 0});
      const int k = set.e2_doors ? 1 : 3;
      const int dp[3][2] = {{8 * k, 4 * k}, {8 * k, 2 * k}, {4 * k, 2 * k}};
      for (int t = 0; t < 3; ++t)
        for (int i = 0; i < 20; ++i)
          cells.push_back({1, dp[t][0], dp[t][1], std::uniform_int_distribution<int>(0, dp[t][0] - 1)(rng)});
      for (int i = 0; i < 100; ++i) cells.push_back({2, 0, 0, 0});
      for (int i = 0; i < 50; ++i) cells.push_back({3, 0, 0, 0});
      LayeredMap lm(set.p);
      std::bernoulli_distribution coin(0.5), mover(0.1);
      for (int w = 1; w <= readouts.back(); ++w) {
        for (std::size_t c = 0; c < cells.size(); ++c) {
          const auto& x = cells[c];
          bool o = false;
          if (x.gt == 0) o = true;
          else if (x.gt == 1) o = ((w - 1 + x.phase) % x.period) < x.on;
          else if (x.gt == 2) o = coin(rng);
          else o = mover(rng);
          if (o) lm.observeHit(c); else lm.observeMiss(c);
        }
        lm.tick();
        for (int q = 0; q < 2; ++q) {
          if (w != readouts[q]) continue;
          for (std::size_t c = 0; c < cells.size(); ++c) {
            const CellClass kc = lm.classify(c);
            const int col = kc == CellClass::Static ? 0 : kc == CellClass::Periodic ? 1
                          : kc == CellClass::Transient ? 2 : 3;
            ++cnt[q][cells[c].gt][col];
          }
        }
      }
    }
    for (int q = 0; q < 2; ++q)
      for (int g = 0; g < 4; ++g)
        cf.row(set.name, readouts[q], gts[g], cnt[q][g][0], cnt[q][g][1], cnt[q][g][2], cnt[q][g][3]);
  }

  // ---------------------------------------------------------------- (2) sweep over m
  eval::Csv sw("results/e6_static_vs_m.csv");
  sw.header("params,m,cells,static_64,static_256,ever_static_256,periodic_256,mean_field_threshold");
  sw.row("#seed", kSweepSeed, "", "", "", "", "", "");
  for (const auto& set : sweep_sets) {
    for (int j = 1; j <= 19; ++j) {
      const double m = 0.05 * j;
      int s64 = 0, s256 = 0, ever = 0, per = 0, cells = 0;
      for (int s = 0; s < 10; ++s) {
        std::mt19937 rng(kSweepSeed + 100u * unsigned(j) + unsigned(s));
        std::bernoulli_distribution coin(m);
        LayeredMap lm(set.p);
        std::vector<char> ev(200, 0);
        for (int w = 1; w <= 256; ++w) {
          for (int c = 0; c < 200; ++c) {
            if (coin(rng)) lm.observeHit(c); else lm.observeMiss(c);
          }
          lm.tick();
          for (int c = 0; c < 200; ++c) {
            const bool st = lm.isStatic(c);
            if (st) ev[c] = 1;
            if (w == 64 && st) ++s64;
            if (w == 256 && st) ++s256;
            if (w == 256 && lm.classify(c) == CellClass::Periodic) ++per;
          }
        }
        for (int c = 0; c < 200; ++c) ever += ev[c];
        cells += 200;
      }
      sw.row(set.name, 0.05 * j, cells, s64, s256, ever, per, meanFieldThreshold(set.p));
    }
  }
  std::cout << "E6 done -> results/e6_confusion.csv, results/e6_static_vs_m.csv\n";
  return (cf.ok() && sw.ok()) ? 0 : 1;
}
