// E7 — The sequential test as shipped (strata_core 0.2.0, evidence_model = Sprt).
//
// Part 1, e7_oc.csv: operating characteristic on the real engine. For a true per-window hit
// rate m (iid windows) the share of cells that graduate, and the number of windows to a
// decision, by Monte Carlo on LayeredMap, next to (a) the EXACT value of the same rule
// (the lattice l = h l_hit + k l_miss walked state by state, no sampling) and (b) Wald's
// closed form, which ignores the overshoot of the statistic over the thresholds. Two
// rules: "pure" (period_windows = 1: reject at the first l <= B) and "shipped" (the
// periodicity gate T = 24: no lower clamp and no rejection before 24 touched windows).
//
// Part 2, e7_autocorr.csv: the guarantee holds for independent windows. Per-history rate
// of ever graduating when the windows follow a two-state chain with the stated lag-1
// autocorrelation rho at hit rate m (rho = 0 is iid), on the engine; and, for the pairs of
// the advice, the same quantity for a bare random walk that is floored at B and never
// rejected (the reading with which the "13x at exactly m0" figure was first obtained).
//
// Seeds: kSeed = 70261018 + offsets, disjoint from E0-E6 (std::mt19937_64 raw output, no
// <random> distribution, so the files do not depend on the standard library).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "common.hpp"
#include "strata_core/layered_map.hpp"

using namespace strata_core;

namespace {
constexpr std::uint64_t kSeed = 70261018ull;

struct Rng {
  std::mt19937_64 g;
  explicit Rng(std::uint64_t s) : g(s) {}
  double u() { return static_cast<double>(g() >> 11) * (1.0 / 9007199254740992.0); }
};

void wilson(double k, double n, double& lo, double& hi) {
  const double z = 1.959964, z2 = z * z, ph = k / n;
  const double c = ph + z2 / (2 * n), h = z * std::sqrt(ph * (1 - ph) / n + z2 / (4 * n * n));
  lo = (c - h) / (1 + z2 / n); hi = (c + h) / (1 + z2 / n);
}

LayeredMapParams sp(double m0, double m1, double alpha, double beta, int gate) {
  LayeredMapParams p;
  p.evidence_model = EvidenceModel::Sprt; p.layer_interval = 1;
  p.m0 = m0; p.m1 = m1; p.alpha = alpha; p.beta = beta;
  p.enable_periodicity = false; p.n_max_undecided = 0;
  p.periodicity.period_windows = gate;
  return p;
}

// Exact P(graduate) and E[windows to decision] of the rule with periodicity gate `gate`
// (reject at the first l <= B with n >= gate, graduate at the first l >= A).
struct Exact { double grad; double mean_n; double undecided; };
Exact exactOc(double m, const SprtDerived& d, int gate) {
  std::map<std::pair<int, int>, double> frontier{{{0, 0}, 1.0}};
  double grad = 0.0, rej = 0.0, en = 0.0;
  for (int n = 1; n <= 6000 && !frontier.empty(); ++n) {
    std::map<std::pair<int, int>, double> next;
    for (const auto& kv : frontier) {
      for (int hit = 0; hit < 2; ++hit) {
        const double pr = hit ? m : 1.0 - m;
        if (pr <= 0.0) continue;
        const int h = kv.first.first + hit, k = kv.first.second + (1 - hit);
        const double l = h * d.l_hit + k * d.l_miss, q = kv.second * pr;
        if (l >= d.A) { grad += q; en += q * n; }
        else if (l <= d.B && n >= gate) { rej += q; en += q * n; }
        else next[{h, k}] += q;
      }
    }
    frontier.swap(next);
  }
  const double decided = grad + rej;
  return {grad, decided > 0 ? en / decided : 0.0, std::max(0.0, 1.0 - decided)};
}

double waldOc(double m, double m0, double m1, double alpha, double beta) {
  const double a = std::log(m1 / m0), b = std::log((1 - m1) / (1 - m0));
  const double A = (1 - beta) / alpha, B = beta / (1 - alpha);
  const double drift = m * a + (1 - m) * b;
  if (std::fabs(drift) < 1e-9) return std::log(1.0 / B) / (std::log(A) - std::log(B));
  auto f = [&](double h) { return m * std::pow(m1 / m0, h) + (1 - m) * std::pow((1 - m1) / (1 - m0), h) - 1; };
  double lo = drift > 0 ? -60.0 : 1e-6, hi = drift > 0 ? -1e-6 : 60.0;
  for (int i = 0; i < 300; ++i) { const double mid = 0.5 * (lo + hi); if (f(lo) * f(mid) <= 0) hi = mid; else lo = mid; }
  const double h = 0.5 * (lo + hi);
  return 1.0 - (std::pow(A, h) - 1.0) / (std::pow(A, h) - std::pow(B, h));
}

struct Mc { double grad; double undecided; double mean_n; };
Mc mcOc(const LayeredMapParams& p, double m, int cells, std::uint64_t seed) {
  LayeredMap lm(p);
  Rng rng(seed);
  std::vector<char> live(cells, 1);
  std::vector<int> at(cells, 0);
  int grad = 0, rej = 0, active = cells, w = 0;
  for (; w < 6000 && active > 0; ++w) {
    for (int c = 0; c < cells; ++c) {
      if (!live[c]) continue;
      if (rng.u() < m) lm.observeHit(c); else lm.observeMiss(c);
    }
    lm.tick();
    for (int c = 0; c < cells; ++c) {
      if (!live[c]) continue;
      if (lm.isStatic(c)) { live[c] = 0; --active; ++grad; at[c] = w + 1; }
      else if (lm.classify(c) == CellClass::Unknown) { live[c] = 0; --active; ++rej; at[c] = w + 1; }
    }
  }
  double s = 0; for (int c = 0; c < cells; ++c) s += at[c];
  const int decided = grad + rej;
  return {static_cast<double>(grad) / cells, static_cast<double>(cells - decided) / cells,
          decided > 0 ? s / decided : 0.0};
}

struct History { long histories{0}; long ever{0}; };
History histories(const LayeredMapParams& p, double m, double rho, int cells, int windows, std::uint64_t seed) {
  LayeredMap lm(p);
  Rng rng(seed);
  std::vector<char> x(cells), open(cells, 0), flag(cells, 0);
  for (int c = 0; c < cells; ++c) x[c] = rng.u() < m;
  const double p11 = m + rho * (1 - m), p10 = m * (1 - rho);
  History h;
  for (int w = 0; w < windows; ++w) {
    for (int c = 0; c < cells; ++c) {
      if (x[c]) lm.observeHit(c); else lm.observeMiss(c);
      x[c] = rng.u() < (x[c] ? p11 : p10);
    }
    lm.tick();
    for (int c = 0; c < cells; ++c) {
      if (lm.observations(c) == 0) {
        if (open[c]) { ++h.histories; h.ever += flag[c]; open[c] = 0; flag[c] = 0; }
      } else {
        open[c] = 1;
        if (lm.isStatic(c)) flag[c] = 1;
      }
    }
  }
  for (int c = 0; c < cells; ++c) if (open[c]) { ++h.histories; h.ever += flag[c]; }
  return h;
}

// Bare random walk, floored at B, never rejected, `horizon` windows: P(ever l >= A).
double referenceWalk(double m0, double m1, double alpha, double beta, double m, double rho, int runs, int horizon,
                     std::uint64_t seed) {
  const double lh = std::log(m1 / m0), lm_ = std::log((1 - m1) / (1 - m0));
  const double A = std::log((1 - beta) / alpha), B = std::log(beta / (1 - alpha));
  Rng rng(seed);
  const double p11 = m + rho * (1 - m), p10 = m * (1 - rho);
  long ever = 0;
  for (int r = 0; r < runs; ++r) {
    double l = 0.0; int x = rng.u() < m ? 1 : 0;
    for (int n = 0; n < horizon; ++n) {
      l += x ? lh : lm_;
      if (l < B) l = B;
      if (l >= A) { ++ever; break; }
      x = rng.u() < (x ? p11 : p10) ? 1 : 0;
    }
  }
  return static_cast<double>(ever) / runs;
}
}  // namespace

int main() {
  const double m0 = 0.20, m1 = 0.70, alpha = 0.01, beta = 0.01;

  // ------------------------------------------------------------------ part 1: operating characteristic
  eval::Csv oc("results/e7_oc.csv");
  oc.header("rule,m,cells,p_graduate_mc,mc_lo,mc_hi,p_graduate_exact,p_graduate_wald,undecided_mc,undecided_exact,mean_windows_mc,mean_windows_exact");
  oc.row("#seed", kSeed, "m0", m0, "m1", m1, "alpha", alpha, "beta", beta);
  const int N = 40000;
  std::vector<double> grid;
  for (int i = 1; i <= 19; ++i) grid.push_back(0.05 * i);
  const double mstar = deriveSprt(sp(m0, m1, alpha, beta, 24)).m_star;
  grid.push_back(mstar);
  std::sort(grid.begin(), grid.end());
  int idx = 0;
  for (const auto& rule : std::vector<std::pair<std::string, int>>{{"pure", 1}, {"shipped", 24}}) {
    const LayeredMapParams p = sp(m0, m1, alpha, beta, rule.second);
    const SprtDerived d = deriveSprt(p);
    for (double m : grid) {
      const Mc mc = mcOc(p, m, N, kSeed + 1000 + idx++);
      const Exact ex = exactOc(m, d, rule.second);
      double lo, hi; wilson(mc.grad * N, N, lo, hi);
      oc.row(rule.first, m, N, mc.grad, lo, hi, ex.grad, waldOc(m, m0, m1, alpha, beta), mc.undecided, ex.undecided,
             mc.mean_n, ex.mean_n);
    }
  }

  // ------------------------------------------------------------------ part 2: autocorrelation
  eval::Csv ac("results/e7_autocorr.csv");
  ac.header("reading,m0,m1,m,rho,units,horizon_windows,graduated,share,wilson_lo,wilson_hi,ratio_to_alpha");
  ac.row("#seed", kSeed + 5000, "alpha", alpha, "beta", beta, "", "", "", "", "", "", "");
  const int cells = 1500, windows = 1024;
  int j = 0;
  for (double m : {0.20, 0.10}) {
    for (double rho : {0.0, 0.10, 0.23, 0.35, 0.50}) {
      const History h = histories(sp(m0, m1, alpha, beta, 24), m, rho, cells, windows, kSeed + 6000 + j++);
      double lo, hi; wilson(h.ever, h.histories, lo, hi);
      const double share = static_cast<double>(h.ever) / h.histories;
      ac.row("engine_per_history", m0, m1, m, rho, h.histories, windows, h.ever, share, lo, hi, share / alpha);
    }
  }
  for (const auto& pr : std::vector<std::pair<double, double>>{{0.15, 0.65}, {0.20, 0.70}}) {
    for (double rho : {0.0, 0.10, 0.23, 0.35, 0.50}) {
      const int runs = 100000, horizon = 200;
      const double share = referenceWalk(pr.first, pr.second, alpha, beta, pr.first, rho, runs, horizon, kSeed + 7000 + j++);
      double lo, hi; wilson(share * runs, runs, lo, hi);
      ac.row("reference_walk_floor_B", pr.first, pr.second, pr.first, rho, runs, horizon, static_cast<long>(share * runs + 0.5),
             share, lo, hi, share / alpha);
    }
  }
  std::cout << "E7 done: results/e7_oc.csv, results/e7_autocorr.csv\n";
  return 0;
}
