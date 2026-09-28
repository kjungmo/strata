// E0 — Calibration of the periodic significance test at ONE read-out (run BEFORE E2;
// disjoint seeds). Chooses the level alpha of the single-read-out rule
// (periodic_alpha_spending = false). The shipped rule spends its level over the touch
// count and is calibrated at trajectory level by E5 (e5_trajectory.cpp).
// Seeds: kCalSeed = 20260928 + offsets, disjoint from every evaluation seed
// (eval::kSeed = 12345 plus offsets in [0, 1500] used by E1-E4).
//
// Pre-stated selection rule: target per-read-out false-alarm rate 0.01. Among the
// candidate nominal levels kAlphas, pick the LARGEST alpha (fastest detection)
// whose calibration false-alarm rate is <= 0.01 everywhere:
//   (i)  model level: iid Bernoulli(m) streams, every (T,H) in {(8,3),(24,2)},
//        m in 0.05..0.95, n from T upward (worst cell of the grid);
//   (ii) pipeline level: Bernoulli(m) clutter cells through the real LayeredMap
//        with the E2 parameters (pruning on), read out at every length 8..100
//        (worst length).
// The rigorous Chernoff bound guarantees <= alpha for (i) under non-adaptive
// sampling; the calibration measures how much slack it has and checks (ii),
// where pruning makes the sample adaptive and no guarantee applies.
// Also reported: rule A (the Gaussian-residual Beta approximation of the
// floating-mean periodogram at alpha=0.01) for comparison, and door detection.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "common.hpp"
#include "strata_core/layered_map.hpp"
#include "strata_core/periodicity.hpp"

using namespace strata_core;

namespace {
constexpr unsigned kCalSeed = 20260928u;
constexpr double kTarget = 0.01;
constexpr double kAmin = 0.3;
const std::vector<double> kAlphas = {0.01, 0.02, 0.05, 0.1, 0.2};

double betaFap(const HarmonicStat& h) {
  if (h.n <= 3 || h.chi0 <= 1e-12) return 1.0;
  const double P = std::min(1.0, h.dchi / h.chi0);
  if (P >= 1.0) return 0.0;
  return std::exp(0.5 * (h.n - 3) * std::log1p(-P));
}

LayeredMapParams e2Params(double alpha) {  // identical to e2_periodicity.cpp params()
  LayeredMapParams p;
  p.layer_interval = 1; p.l_hit = 1.0; p.l_miss = -1.0; p.l_min = -5.0; p.l_max = 5.0;
  p.survival_decay = 1.0; p.graduate_prob = 0.9; p.demote_prob = 0.4;
  p.min_observations = 5; p.prune_prob = 0.05; p.enable_periodicity = true;
  p.periodic_amplitude_min = kAmin; p.periodicity.period_windows = 8;
  p.periodicity.n_harmonics = 3; p.periodic_false_alarm = alpha;
  p.periodic_alpha_spending = false;   // E0 calibrates the single-read-out rule
  return p;
}
}  // namespace

int main() {
  const int nA = static_cast<int>(kAlphas.size());
  std::vector<double> worst_null(nA, 0.0), worst_pipe(nA, 0.0), mean_pipe(nA, 0.0);
  double worst_beta = 0.0;

  // (i) model-level null grid
  eval::Csv nul("results/e0_calibration_null.csv");
  std::string hdr = "T,H,m,n,trials,amp_only,beta_0.01";
  for (double a : kAlphas) hdr += ",bound_" + std::to_string(a).substr(0, 4);
  nul.header(hdr);
  nul.row("#seed", kCalSeed, "", "", "", "", "");
  unsigned off = 0;
  for (auto [T, H] : std::vector<std::pair<int, int>>{{8, 3}, {24, 2}}) {
    const std::vector<int> ns = T == 8
        ? std::vector<int>{8, 9, 10, 12, 14, 16, 20, 24, 32, 48, 64, 100}
        : std::vector<int>{24, 26, 30, 36, 48, 72, 100, 240};
    for (double m : {0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.95}) {
      for (int n : ns) {
        std::mt19937 rng(kCalSeed + off++);
        std::bernoulli_distribution coin(m);
        const int R = 20000;
        int amp_only = 0, beta = 0;
        std::vector<int> fb(nA, 0);
        for (int r = 0; r < R; ++r) {
          PeriodicityModel pm({T, H});
          for (int w = 1; w <= n; ++w) pm.gather(0, coin(rng), w);
          double minA = 1.0, minB = 1.0;
          bool screen = false;
          for (int k = 0; k < H; ++k) {
            const HarmonicStat h = pm.harmonic(0, k);
            if (h.amplitude <= 0.0 || h.amplitude < kAmin) continue;
            screen = true;
            minA = std::min(minA, betaFap(h));
            minB = std::min(minB, PeriodicityModel::tailBound(h.dchi));
          }
          if (!screen) continue;
          ++amp_only;
          if (H * minA <= 0.01) ++beta;
          for (int i = 0; i < nA; ++i) if (H * minB <= kAlphas[i]) ++fb[i];
        }
        std::string line = std::to_string(T) + "," + std::to_string(H) + "," +
                           std::to_string(m).substr(0, 4) + "," + std::to_string(n) + "," +
                           std::to_string(R) + "," + std::to_string(double(amp_only) / R) + "," +
                           std::to_string(double(beta) / R);
        worst_beta = std::max(worst_beta, double(beta) / R);
        for (int i = 0; i < nA; ++i) {
          line += "," + std::to_string(double(fb[i]) / R);
          worst_null[i] = std::max(worst_null[i], double(fb[i]) / R);
        }
        nul.row(line);
      }
    }
  }

  // (ii) pipeline clutter, E2 parameters, read out at every length 8..100
  eval::Csv pipe("results/e0_calibration_pipeline.csv");
  pipe.header("alpha,m,obs_length,cells,periodic,rate");
  pipe.row("#seed", kCalSeed + 5000, "", "", "", "");
  const int kCells = 1000, kLen = 100;
  for (int i = 0; i < nA; ++i) {
    std::vector<double> rate_at(kLen + 1, 0.0);
    int nm = 0;
    for (double m : {0.3, 0.5, 0.7}) {
      LayeredMap lm(e2Params(kAlphas[i]));
      std::mt19937 rng(kCalSeed + 5000 + static_cast<unsigned>(m * 10));
      std::bernoulli_distribution coin(m);
      for (int w = 1; w <= kLen; ++w) {
        for (CellId c = 0; c < kCells; ++c) { if (coin(rng)) lm.observeHit(c); else lm.observeMiss(c); }
        lm.tick();
        if (w < 8) continue;
        int per = 0;
        for (CellId c = 0; c < kCells; ++c) if (lm.classify(c) == CellClass::Periodic) ++per;
        const double r = double(per) / kCells;
        pipe.row(kAlphas[i], m, w, kCells, per, r);
        worst_pipe[i] = std::max(worst_pipe[i], r);
        mean_pipe[i] += r;
      }
      ++nm;
    }
    mean_pipe[i] /= nm * (kLen - 7);
  }

  // (iii) door detection: first read-out length from which the door stays Periodic
  // through window 100 (median over 40 phase offsets / noise draws).
  eval::Csv det("results/e0_calibration_doors.csv");
  det.header("alpha,door,flip_noise,median_first_n,never");
  det.row("#seed", kCalSeed + 9000, "", "", "");
  struct Door { std::string name; int period; int on; double flip; };
  const std::vector<Door> doors = {{"p8_4on4off", 8, 4, 0.0}, {"p4_2on2off", 4, 2, 0.0},
                                   {"p8_3on5off", 8, 3, 0.0}, {"p8_4on4off_noisy", 8, 4, 0.1}};
  std::vector<double> med4(nA, 0.0);
  for (int i = 0; i < nA; ++i) {
    for (const auto& d : doors) {
      std::vector<int> firsts;
      int never = 0;
      std::mt19937 rng(kCalSeed + 9000);
      std::uniform_int_distribution<int> ph(0, d.period - 1);
      std::bernoulli_distribution flip(d.flip > 0 ? d.flip : 0.0);
      for (int rep = 0; rep < 40; ++rep) {
        const int phase = ph(rng);
        LayeredMap lm(e2Params(kAlphas[i]));
        int first = -1;
        for (int w = 1; w <= kLen; ++w) {
          bool o = ((w - 1 + phase) % d.period) < d.on;
          if (d.flip > 0 && flip(rng)) o = !o;
          if (o) lm.observeHit(1); else lm.observeMiss(1);
          lm.tick();
          const bool per = lm.classify(1) == CellClass::Periodic;
          if (per && first < 0) first = w;
          if (!per) first = -1;
        }
        if (first < 0) ++never; else firsts.push_back(first);
      }
      std::sort(firsts.begin(), firsts.end());
      const double med = firsts.empty() ? -1 : firsts[firsts.size() / 2];
      if (d.name == "p8_4on4off") med4[i] = med;
      det.row(kAlphas[i], d.name, d.flip, med, never);
    }
  }

  // selection
  eval::Csv sel("results/e0_calibration_choice.csv");
  sel.header("alpha,worst_null_rate,worst_pipeline_rate,mean_pipeline_rate,door_p8_median_first_n,meets_target,selected");
  int chosen = -1;
  for (int i = 0; i < nA; ++i)
    if (worst_null[i] <= kTarget && worst_pipe[i] <= kTarget) chosen = i;
  for (int i = 0; i < nA; ++i)
    sel.row(kAlphas[i], worst_null[i], worst_pipe[i], mean_pipe[i], med4[i],
            (worst_null[i] <= kTarget && worst_pipe[i] <= kTarget) ? 1 : 0, i == chosen ? 1 : 0);
  sel.row("#beta_rule_worst_null_rate_at_0.01", worst_beta, "", "", "", "", "");

  std::cout << "E0 calibration: selected alpha="
            << (chosen >= 0 ? kAlphas[chosen] : -1.0) << " (beta rule worst "
            << worst_beta << ")\n";
  return (nul.ok() && pipe.ok() && det.ok() && sel.ok()) ? 0 : 1;
}
