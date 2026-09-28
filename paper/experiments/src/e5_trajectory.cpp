// E5 — Trajectory-level false alarms of the live (pruned, re-tested) detector, the
// alpha-spending correction, and a multi-seed four-class confusion.
//
// Deployed-pipeline false-alarm events, for a cell whose ground truth is not periodic:
//   ever(N):     the cell is labelled Periodic by LayeredMap::classify at SOME window
//                1..N of the run (N = 64, 256, 1024);
//   per-window:  the fraction of (cell, window) pairs labelled Periodic over 1..1024.
// The live map is used as shipped: every window closes with endWindow() (pruning,
// re-creation and re-testing included), and the label is read after every window.
//
// Seeds (all disjoint from E0 = 20260928 + offsets < 10000, E1-E4 = 12345 + [0, 1500],
// and the unit tests = 800000-852000):
//   calibration  kCalSeed  = 30260928 + offsets < 100000  (chooses delta only);
//   evaluation   kEvalSeed = 40260928 + offsets < 100000  (never used for a choice).
//
// Pre-stated rule (fixed before the run): the shipped rule spends delta over the touch
// count (PeriodicityModel::spentLevel, schedule T/(n(n+1)), fixed a priori). Among
// kDeltas pick the LARGEST delta (fastest detection) whose calibration ever(1024)
// Wilson 95% upper limit is <= 0.01 in EVERY calibration configuration: both parameter
// sets (E2 and shipped), both touch patterns (every window; each window independently
// with probability 0.5), m = 0.1..0.9, 2000 cells each. The chosen delta is then
// evaluated on the held-out seeds together with the single-read-out rule it replaces
// (delta = 0.1, no spending, the E0 choice).
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "common.hpp"
#include "strata_core/layered_map.hpp"

using namespace strata_core;

namespace {
constexpr unsigned kCalSeed = 30260928u;
constexpr unsigned kEvalSeed = 40260928u;
constexpr double kTarget = 0.01;
constexpr int kHorizon = 1024;
const std::vector<int> kReport = {64, 256, 1024};
const std::vector<double> kDeltas = {0.01, 0.02, 0.05, 0.1, 0.2};
const std::vector<double> kOcc = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};

struct Rule { std::string name; double delta; bool spend; };

// Wilson score interval, 95%.
void wilson(int k, int n, double& lo, double& hi) {
  const double z = 1.959964, p = n ? double(k) / n : 0.0, z2 = z * z;
  const double c = (p + z2 / (2.0 * n)) / (1.0 + z2 / n);
  const double h = z * std::sqrt(p * (1.0 - p) / n + z2 / (4.0 * n * n)) / (1.0 + z2 / n);
  lo = std::max(0.0, c - h); hi = std::min(1.0, c + h);
}

LayeredMapParams e2Params() {  // identical to e2_periodicity.cpp params() except the rule
  LayeredMapParams p;
  p.layer_interval = 1; p.l_hit = 1.0; p.l_miss = -1.0; p.l_min = -5.0; p.l_max = 5.0;
  p.survival_decay = 1.0; p.graduate_prob = 0.9; p.demote_prob = 0.4;
  p.min_observations = 5; p.prune_prob = 0.05; p.enable_periodicity = true;
  p.periodic_amplitude_min = 0.3; p.periodicity.period_windows = 8;
  p.periodicity.n_harmonics = 3;
  return p;
}
LayeredMapParams shippedParams() {  // library defaults, one window per tick
  LayeredMapParams p;
  p.layer_interval = 1;
  return p;
}
LayeredMapParams withRule(LayeredMapParams p, const Rule& r) {
  p.periodic_false_alarm = r.delta; p.periodic_alpha_spending = r.spend; return p;
}

// Occupancy generators for one cell (non-periodic ground truth).
struct Gen {
  std::string kind; double m;
  bool state{false};
  bool next(std::mt19937& rng) {
    if (kind == "bernoulli") return std::bernoulli_distribution(m)(rng);
    if (kind == "constant_occupied") return true;
    if (kind == "constant_free") return false;
    // "markov": two-state chain, stationary mean m = 0.5, stay probability 0.9
    // (mean dwell 10 windows). Temporally correlated: OUTSIDE the proposition's null.
    if (std::bernoulli_distribution(0.1)(rng)) state = !state;
    return state;
  }
};

struct TrajResult {
  int cells{0};
  std::vector<int> ever;        // per kReport horizon
  long labelled{0};             // (cell, window) pairs labelled Periodic, 1..kHorizon
  double rate_sd{0};            // sd over cells of the per-cell labelled fraction
  double mean_lives{0};         // mean histories begun per cell in 1..kHorizon
  double mean_long_lives{0};    // ... of which reached n >= T touches
};

// Runs `seeds` independent maps of `per_seed` cells each through kHorizon windows.
TrajResult trajectory(const LayeredMapParams& p, const std::string& kind, double m,
                      bool half_touch, unsigned seed0, int seeds, int per_seed) {
  TrajResult R;
  R.ever.assign(kReport.size(), 0);
  std::vector<double> frac;
  double lives = 0, long_lives = 0;
  for (int s = 0; s < seeds; ++s) {
    LayeredMap lm(p);
    std::mt19937 rng(seed0 + static_cast<unsigned>(s));
    std::bernoulli_distribution touch(0.5);
    std::vector<Gen> gen(per_seed, Gen{kind, m});
    for (auto& g : gen) g.state = std::bernoulli_distribution(0.5)(rng);
    std::vector<int> first(per_seed, -1), lab(per_seed, 0), nlife(per_seed, 0),
        nlong(per_seed, 0), touches(per_seed, 0);
    std::vector<char> present(per_seed, 0), counted(per_seed, 0);
    const int T = p.periodicity.period_windows;
    for (int w = 1; w <= kHorizon; ++w) {
      std::vector<char> t(per_seed, 0);
      for (int c = 0; c < per_seed; ++c) {
        const bool o = gen[c].next(rng);
        const bool tt = !half_touch || touch(rng);
        if (!tt) continue;
        t[c] = 1;
        if (o) lm.observeHit(c); else lm.observeMiss(c);
      }
      lm.tick();
      for (int c = 0; c < per_seed; ++c) {
        const bool pr = lm.occupancyProb(c) > 0.0;
        if (pr && !present[c]) { ++nlife[c]; touches[c] = 0; counted[c] = 0; }
        if (pr && t[c]) ++touches[c];
        if (pr && touches[c] >= T && !counted[c]) { ++nlong[c]; counted[c] = 1; }
        present[c] = pr;
        if (lm.classify(c) == CellClass::Periodic) { ++lab[c]; if (first[c] < 0) first[c] = w; }
      }
    }
    for (int c = 0; c < per_seed; ++c) {
      for (std::size_t h = 0; h < kReport.size(); ++h)
        if (first[c] > 0 && first[c] <= kReport[h]) ++R.ever[h];
      R.labelled += lab[c];
      frac.push_back(double(lab[c]) / kHorizon);
      lives += nlife[c]; long_lives += nlong[c];
    }
    R.cells += per_seed;
  }
  double mu = 0; for (double f : frac) mu += f; mu /= frac.size();
  double v = 0; for (double f : frac) v += (f - mu) * (f - mu); v /= (frac.size() - 1);
  R.rate_sd = std::sqrt(v);
  R.mean_lives = lives / R.cells; R.mean_long_lives = long_lives / R.cells;
  return R;
}

// Median first window from which a door stays Periodic through kHorizon/2 (runs at
// phases 0..period-1, cycling), and how many runs never get there.
void doorDelay(const LayeredMapParams& p, int period, int on, double flip, unsigned seed,
               int runs, double& median, int& never) {
  std::mt19937 rng(seed);
  std::bernoulli_distribution fl(flip > 0 ? flip : 0.0);
  std::vector<int> firsts; never = 0;
  for (int r = 0; r < runs; ++r) {
    const int phase = r % period;
    LayeredMap lm(p);
    int first = -1;
    for (int w = 1; w <= kHorizon / 2; ++w) {
      bool o = ((w - 1 + phase) % period) < on;
      if (flip > 0 && fl(rng)) o = !o;
      if (o) lm.observeHit(1); else lm.observeMiss(1);
      lm.tick();
      const bool per = lm.classify(1) == CellClass::Periodic;
      if (per && first < 0) first = w;
      if (!per) first = -1;
    }
    if (first < 0) ++never; else firsts.push_back(first);
  }
  std::sort(firsts.begin(), firsts.end());
  median = firsts.empty() ? -1 : firsts[firsts.size() / 2];
}

const char* cname(CellClass c) {
  switch (c) {
    case CellClass::Unknown: return "U";
    case CellClass::Transient: return "T";
    case CellClass::Periodic: return "P";
    case CellClass::Static: return "S";
  }
  return "?";
}
}  // namespace

int main() {
  const std::vector<std::pair<std::string, LayeredMapParams>> psets = {
      {"e2", e2Params()}, {"shipped", shippedParams()}};

  // ---------------------------------------------------------------- (1) calibration
  eval::Csv cal("results/e5_calibration.csv");
  cal.header("delta,params,touch,m,cells,ever_1024,wilson_hi_1024,mean_lives");
  cal.row("#seed", kCalSeed, "", "", "", "", "", "");
  std::vector<double> worst_hi(kDeltas.size(), 0.0);
  std::vector<int> worst_k(kDeltas.size(), 0);
  unsigned off = 0;
  for (std::size_t i = 0; i < kDeltas.size(); ++i) {
    const Rule r{"spending", kDeltas[i], true};
    off = 0;   // every candidate sees the same calibration streams
    for (const auto& ps : psets)
      for (bool half : {false, true})
        for (double m : kOcc) {
          const TrajResult R = trajectory(withRule(ps.second, r), "bernoulli", m, half,
                                          kCalSeed + 100 * off++, 4, 500);
          double lo, hi; wilson(R.ever[2], R.cells, lo, hi);
          if (hi > worst_hi[i]) { worst_hi[i] = hi; worst_k[i] = R.ever[2]; }
          cal.row(kDeltas[i], ps.first, half ? "half" : "every", m, R.cells, R.ever[2], hi,
                  R.mean_lives);
        }
  }
  // detection cost on calibration seeds (reported, not part of the rule)
  eval::Csv calc("results/e5_calibration_choice.csv");
  calc.header("delta,worst_wilson_hi_1024,worst_ever_count,door_e2_p8_median,door_shipped_p24_median,meets_target,selected");
  int chosen = -1;
  for (std::size_t i = 0; i < kDeltas.size(); ++i) if (worst_hi[i] <= kTarget) chosen = int(i);
  for (std::size_t i = 0; i < kDeltas.size(); ++i) {
    const Rule r{"spending", kDeltas[i], true};
    double m8, m24; int nv;
    doorDelay(withRule(e2Params(), r), 8, 4, 0.0, kCalSeed + 90000, 40, m8, nv);
    doorDelay(withRule(shippedParams(), r), 24, 12, 0.0, kCalSeed + 90001, 48, m24, nv);
    calc.row(kDeltas[i], worst_hi[i], worst_k[i], m8, m24, worst_hi[i] <= kTarget ? 1 : 0,
             int(i) == chosen ? 1 : 0);
  }
  if (chosen < 0) { std::cerr << "no candidate meets the target\n"; return 2; }
  const double kChosen = kDeltas[chosen];
  const std::vector<Rule> rules = {{"single_readout", 0.1, false}, {"spending", kChosen, true}};

  // ---------------------------------------------------------------- (2) held-out evaluation
  eval::Csv ev("results/e5_trajectory.csv");
  ev.header("rule,delta,params,touch,kind,m,cells,ever_64,ever_256,ever_1024,"
            "wilson_lo_1024,wilson_hi_1024,labelled,cell_windows,per_window_rate,"
            "per_window_ci_hi,mean_lives,mean_lives_reaching_T,bound_delta_x_lives");
  ev.row("#seed", kEvalSeed, "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "");
  off = 0;
  struct K { std::string kind; double m; };
  std::vector<K> kinds;
  for (double m : kOcc) kinds.push_back({"bernoulli", m});
  kinds.push_back({"constant_occupied", 1.0});
  kinds.push_back({"constant_free", 0.0});
  kinds.push_back({"markov", 0.5});
  for (const auto& r : rules) {
    off = 0;   // both rules see the same held-out streams
    for (const auto& ps : psets)
      for (bool half : {false, true})
        for (const auto& k : kinds) {
          const TrajResult R = trajectory(withRule(ps.second, r), k.kind, k.m, half,
                                          kEvalSeed + 100 * off++, 10, 200);
          double lo, hi; wilson(R.ever[2], R.cells, lo, hi);
          const double cw = double(R.cells) * kHorizon;
          const double rate = R.labelled / cw;
          const double ci_hi = rate + 1.959964 * R.rate_sd / std::sqrt(double(R.cells));
          const double bound = r.spend ? std::min(1.0, r.delta * R.mean_lives) : -1.0;
          ev.row(r.name, r.delta, ps.first, half ? "half" : "every", k.kind, k.m, R.cells,
                 R.ever[0], R.ever[1], R.ever[2], lo, hi, R.labelled, cw, rate, ci_hi,
                 R.mean_lives, R.mean_long_lives, bound);
        }
  }

  // detection delay on held-out seeds, both rules
  eval::Csv dd("results/e5_doors.csv");
  dd.header("rule,delta,params,door,period,on,flip,runs,median_first_n,never");
  dd.row("#seed", kEvalSeed + 95000, "", "", "", "", "", "", "", "");
  struct D { std::string ps; std::string name; int period, on; double flip; };
  const std::vector<D> doors = {{"e2", "p8_4on4off", 8, 4, 0.0}, {"e2", "p4_2on2off", 4, 2, 0.0},
                                {"e2", "p8_3on5off", 8, 3, 0.0}, {"e2", "p8_4on4off_noisy", 8, 4, 0.1},
                                {"shipped", "p24_12on12off", 24, 12, 0.0},
                                {"shipped", "p24_12on12off_noisy", 24, 12, 0.1}};
  for (const auto& r : rules)
    for (std::size_t j = 0; j < doors.size(); ++j) {
      const auto& d = doors[j];
      double med; int nv;
      doorDelay(withRule(d.ps == "e2" ? e2Params() : shippedParams(), r), d.period, d.on, d.flip,
                kEvalSeed + 95000 + unsigned(j), 48, med, nv);
      dd.row(r.name, r.delta, d.ps, d.name, d.period, d.on, d.flip, 48, med, nv);
    }

  // ---------------------------------------------------------------- (3) four-class confusion
  // E2-style scenes (E2 parameters, touched every window), 20 held-out seeds. Per scene:
  // 50 walls (always occupied), 20 doors of each E2 type at random phase, 100 aperiodic
  // Bernoulli(0.5) cells and 50 dynamic cells (a mover crosses the cell in a window
  // with probability 0.1). Correct labels: wall -> Static, door -> Periodic,
  // aperiodic and dynamic -> Transient or Unknown (not persistent, not periodic).
  eval::Csv cf("results/e5_confusion.csv");
  cf.header("rule,delta,readout,gt,pred_S,pred_P,pred_T,pred_U,ever_periodic");
  cf.row("#seed", kEvalSeed + 97000, "scenes", 20, "", "", "", "", "");
  const std::vector<std::string> gts = {"wall", "door", "aperiodic", "dynamic"};
  for (const auto& r : rules) {
    std::vector<std::vector<std::vector<int>>> cnt(
        2, std::vector<std::vector<int>>(4, std::vector<int>(4, 0)));
    std::vector<std::vector<int>> ever(2, std::vector<int>(4, 0));
    const std::vector<int> readouts = {64, 256};
    for (int s = 0; s < 20; ++s) {
      std::mt19937 rng(kEvalSeed + 97000 + unsigned(s));
      struct C { int gt; int period, on, phase; };
      std::vector<C> cells;
      for (int i = 0; i < 50; ++i) cells.push_back({0, 0, 0, 0});
      const int dp[3][2] = {{8, 4}, {8, 2}, {4, 2}};
      for (int t = 0; t < 3; ++t)
        for (int i = 0; i < 20; ++i)
          cells.push_back({1, dp[t][0], dp[t][1], std::uniform_int_distribution<int>(0, dp[t][0] - 1)(rng)});
      for (int i = 0; i < 100; ++i) cells.push_back({2, 0, 0, 0});
      for (int i = 0; i < 50; ++i) cells.push_back({3, 0, 0, 0});
      LayeredMap lm(withRule(e2Params(), r));
      std::bernoulli_distribution coin(0.5), mover(0.1);
      std::vector<std::vector<char>> evp(2, std::vector<char>(cells.size(), 0));
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
        for (std::size_t c = 0; c < cells.size(); ++c) {
          const bool per = lm.classify(c) == CellClass::Periodic;
          for (int q = 0; q < 2; ++q) if (per && w <= readouts[q]) evp[q][c] = 1;
        }
        for (int q = 0; q < 2; ++q) {
          if (w != readouts[q]) continue;
          for (std::size_t c = 0; c < cells.size(); ++c) {
            const CellClass k = lm.classify(c);
            const int col = k == CellClass::Static ? 0 : k == CellClass::Periodic ? 1
                          : k == CellClass::Transient ? 2 : 3;
            ++cnt[q][cells[c].gt][col];
            ever[q][cells[c].gt] += evp[q][c];
          }
        }
      }
    }
    for (int q = 0; q < 2; ++q)
      for (int g = 0; g < 4; ++g)
        cf.row(r.name, r.delta, q == 0 ? 64 : 256, gts[g], cnt[q][g][0], cnt[q][g][1],
               cnt[q][g][2], cnt[q][g][3], ever[q][g]);
  }

  std::cout << "E5: selected spending delta=" << kChosen << "\n";
  return (cal.ok() && calc.ok() && ev.ok() && dd.ok() && cf.ok()) ? 0 : 1;
}
