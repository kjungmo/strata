// evidence_model = Sprt (strata_core 0.2.0): derivation, validation, latency, the
// error-rate bound (Ville), operating characteristic against the exact lattice
// computation and Wald's closed form, the periodicity-gate race, demotion, Intermittent,
// restart after rejection. Every random draw uses std::mt19937_64 raw output only (no
// <random> distribution), so the results do not depend on the standard library.
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include "strata_core/grid2d_backend.hpp"
#include "strata_core/voxel3d_backend.hpp"
#include "strata_core/layered_map.hpp"

using namespace strata_core;

namespace {

// Sprt parameters for tests: one tick == one window, periodicity off unless asked.
LayeredMapParams Sp(double m0 = 0.20, double m1 = 0.70, double alpha = 0.01, double beta = 0.01) {
  LayeredMapParams p;
  p.evidence_model = EvidenceModel::Sprt;
  p.layer_interval = 1;
  p.m0 = m0; p.m1 = m1; p.alpha = alpha; p.beta = beta;
  p.enable_periodicity = false;
  p.n_max_undecided = 0;
  return p;
}

struct Rng {
  std::mt19937_64 g;
  explicit Rng(std::uint64_t seed) : g(seed) {}
  double u() { return static_cast<double>(g() >> 11) * (1.0 / 9007199254740992.0); }   // [0, 1)
};

double wilsonUpper(double k, double n, double z = 1.959964) {
  const double ph = k / n, z2 = z * z;
  const double c = ph + z2 / (2 * n);
  const double h = z * std::sqrt(ph * (1 - ph) / n + z2 / (4 * n * n));
  return (c + h) / (1 + z2 / n);
}
double wilsonLower(double k, double n, double z = 1.959964) {
  const double ph = k / n, z2 = z * z;
  const double c = ph + z2 / (2 * n);
  const double h = z * std::sqrt(ph * (1 - ph) / n + z2 / (4 * n * n));
  return (c - h) / (1 + z2 / n);
}

// Exact probability that the test (graduate at the first l >= A; reject at the first
// l <= B once n >= gate touched windows; gate 1 is the pure test) graduates, for iid
// windows with hit rate m: the lattice l = h l_hit + k l_miss is walked state by state,
// no sampling.
double exactOcGraduate(double m, const SprtDerived& d, int gate = 1) {
  std::map<std::pair<int, int>, double> frontier{{{0, 0}, 1.0}};
  double grad = 0.0;
  for (int n = 1; n <= 4000 && !frontier.empty(); ++n) {
    std::map<std::pair<int, int>, double> next;
    for (const auto& kv : frontier) {
      for (int hit = 0; hit < 2; ++hit) {
        const double pr = hit ? m : 1.0 - m;
        if (pr <= 0.0) continue;
        const int h = kv.first.first + hit, k = kv.first.second + (1 - hit);
        const double l = h * d.l_hit + k * d.l_miss;
        const double q = kv.second * pr;
        if (l >= d.A) grad += q;
        else if (!(l <= d.B && n >= gate)) next[{h, k}] += q;
      }
    }
    frontier.swap(next);
  }
  return grad;
}

// Wald's closed form (no overshoot): P(graduate | m).
double waldOc(double m, double m0, double m1, double alpha, double beta) {
  const double a = std::log(m1 / m0), b = std::log((1 - m1) / (1 - m0));
  const double A = (1 - beta) / alpha, B = beta / (1 - alpha);
  const double drift = m * a + (1 - m) * b;
  auto f = [&](double h) { return m * std::pow(m1 / m0, h) + (1 - m) * std::pow((1 - m1) / (1 - m0), h) - 1; };
  double lo = drift > 0 ? -60.0 : 1e-6, hi = drift > 0 ? -1e-6 : 60.0;
  for (int i = 0; i < 300; ++i) { const double mid = 0.5 * (lo + hi); if (f(lo) * f(mid) <= 0) hi = mid; else lo = mid; }
  const double h = 0.5 * (lo + hi);
  return 1.0 - (std::pow(A, h) - 1.0) / (std::pow(A, h) - std::pow(B, h));
}

// Many independent cells fed iid Bernoulli(m) windows by one map, one decision per cell
// (a cell is no longer fed once it graduated or was erased). Returns the share that graduated.
double mcGraduateShare(const LayeredMapParams& p, double m, int cells, std::uint64_t seed, double* mean_n = nullptr) {
  LayeredMap lm(p);
  Rng rng(seed);
  std::vector<char> live(cells, 1);
  std::vector<int> decided_at(cells, 0);
  int grad = 0, active = cells;
  for (int w = 1; w <= 4000 && active > 0; ++w) {
    for (int c = 0; c < cells; ++c) {
      if (!live[c]) continue;
      if (rng.u() < m) lm.observeHit(c); else lm.observeMiss(c);
    }
    lm.tick();
    for (int c = 0; c < cells; ++c) {
      if (!live[c]) continue;
      if (lm.isStatic(c)) { live[c] = 0; --active; ++grad; decided_at[c] = w; }
      else if (lm.classify(c) == CellClass::Unknown) { live[c] = 0; --active; decided_at[c] = w; }
    }
  }
  if (mean_n) { double s = 0; for (int c = 0; c < cells; ++c) s += decided_at[c]; *mean_n = s / cells; }
  EXPECT_EQ(active, 0);
  return static_cast<double>(grad) / cells;
}

struct History { long histories{0}; long ever_static{0}; };

// Per-history error rate: a history is the life of a cell from its creation to its
// erasure (or to the end of the run). Every cell is fed one observation per window, from
// a two-state chain with stationary hit rate m and lag-1 autocorrelation rho (rho = 0:
// iid), so a rejected cell is re-created by its next observation: a new history.
History runHistories(const LayeredMapParams& p, double m, double rho, int cells, int windows,
                     std::uint64_t seed) {
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
        if (open[c]) { ++h.histories; h.ever_static += flag[c]; open[c] = 0; flag[c] = 0; }
      } else {
        open[c] = 1;
        if (lm.isStatic(c)) flag[c] = 1;
      }
    }
  }
  for (int c = 0; c < cells; ++c) if (open[c]) { ++h.histories; h.ever_static += flag[c]; }
  return h;
}

}  // namespace

// ---- derivation and validation -------------------------------------------------------

TEST(Sprt, DerivedQuantitiesOfTheShippedPair) {
  const SprtDerived d = deriveSprt(Sp());
  EXPECT_NEAR(d.l_hit, std::log(3.5), 1e-12);
  EXPECT_NEAR(d.l_miss, std::log(0.3 / 0.8), 1e-12);
  EXPECT_NEAR(d.A, std::log(99.0), 1e-12);
  EXPECT_NEAR(d.B, -std::log(99.0), 1e-12);
  EXPECT_NEAR(d.ceiling, d.A + 2 * d.l_hit, 1e-12);
  EXPECT_NEAR(d.demote_level, 2 * d.l_hit, 1e-12);
  EXPECT_NEAR(d.m_star, 0.4391263739, 1e-9);
  EXPECT_EQ(d.latency, 4);
  EXPECT_EQ(d.demote_misses, 5);
}

TEST(Sprt, DerivedQuantitiesOfThePairsInTheAdvice) {
  const SprtDerived a = deriveSprt(Sp(0.15, 0.65));
  EXPECT_NEAR(a.l_hit, 1.466, 5e-4);  EXPECT_NEAR(a.l_miss, -0.887, 5e-4);
  EXPECT_NEAR(a.m_star, 0.377, 5e-4);
  EXPECT_EQ(a.latency, 4);
  EXPECT_EQ(a.demote_misses, 6);                          // "about 6 all-miss windows"
  const SprtDerived b = deriveSprt(Sp(0.15, 0.65, 0.05, 0.05));
  EXPECT_NEAR(b.A, 2.944, 5e-4);
  EXPECT_EQ(b.latency, 3);
  const SprtDerived c = deriveSprt(Sp(0.30, 0.60));      // the development placeholder
  EXPECT_EQ(c.latency, 7);
}

TEST(Sprt, ThresholdsOfUnequalErrorRatesAndACustomCeiling) {
  LayeredMapParams p = Sp(0.20, 0.70, 0.001, 0.02);        // alpha != beta: A and B differ in size
  const SprtDerived d = deriveSprt(p);
  EXPECT_NEAR(d.A, std::log(0.98 / 0.001), 1e-12);
  EXPECT_NEAR(d.B, std::log(0.02 / 0.999), 1e-12);
  EXPECT_LT(d.B, 0.0);
  EXPECT_NEAR(d.ceiling, d.A + 2 * d.l_hit, 1e-12);
  // a custom ceiling replaces the derived one; at the smallest allowed value (A) the reverse test
  // from the ceiling crosses its level at 0
  p.sprt_ceiling = d.A;
  const SprtDerived c = deriveSprt(p);
  EXPECT_EQ(c.ceiling, d.A);
  EXPECT_NEAR(c.demote_level, 0.0, 1e-12);
  EXPECT_EQ(c.demote_misses, static_cast<int>(std::ceil(d.A / -c.l_miss - 1e-12)));
}

TEST(Sprt, StructDefaultsAreTheShippedSprtPair) {
  const LayeredMapParams p;
  EXPECT_EQ(p.evidence_model, EvidenceModel::Sprt);
  EXPECT_EQ(p.m0, 0.20); EXPECT_EQ(p.m1, 0.70);
  EXPECT_EQ(p.alpha, 0.01); EXPECT_EQ(p.beta, 0.01);
  EXPECT_TRUE(std::isnan(p.sprt_ceiling));
  EXPECT_EQ(p.n_max_undecided, 80);
  EXPECT_EQ(LayeredMap(p).sprt().latency, 4);
}

TEST(Sprt, ValidationRejectsEachBadInputOnBothSides) {
  auto bad = [](auto&& mutate) { LayeredMapParams p = Sp(); mutate(p); return p; };
  // m0 < m1, both inside (0, 1): each boundary, both sides
  EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.01; })));
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.0; })), std::invalid_argument);
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = -0.1; })), std::invalid_argument);
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.7; })), std::invalid_argument);   // m0 == m1
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.8; })), std::invalid_argument);   // m0 > m1
  EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m1 = 0.99; })));
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m1 = 1.0; })), std::invalid_argument);
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m1 = 1.5; })), std::invalid_argument);
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m1 = 0.2; })), std::invalid_argument);   // m1 == m0
  // error rates: > 0 each, alpha + beta < 1
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.alpha = 0.0; })), std::invalid_argument);
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.beta = 0.0; })), std::invalid_argument);
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.alpha = -0.01; })), std::invalid_argument);
  EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.alpha = 0.5; p.beta = 0.49; })));
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.alpha = 0.5; p.beta = 0.5; })), std::invalid_argument);
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.alpha = 0.6; p.beta = 0.5; })), std::invalid_argument);
  // not finite
  const double nan = std::nan(""), inf = INFINITY;
  EXPECT_THROW(LayeredMap(bad([&](LayeredMapParams& p) { p.m0 = nan; })), std::invalid_argument);
  EXPECT_THROW(LayeredMap(bad([&](LayeredMapParams& p) { p.m1 = inf; })), std::invalid_argument);
  EXPECT_THROW(LayeredMap(bad([&](LayeredMapParams& p) { p.alpha = nan; })), std::invalid_argument);
  EXPECT_THROW(LayeredMap(bad([&](LayeredMapParams& p) { p.beta = inf; })), std::invalid_argument);
  // m0 and m1 so close that deriving the latency would never end
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 1e-9; })), std::invalid_argument);
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = std::nextafter(0.5, 1.0); })), std::invalid_argument);
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 1e-6; })), std::invalid_argument);   // 2.3 million windows
  // below m0 = 0.5 the miss increment is the smaller one: 230,000 windows to graduate, 2.1 million to demote
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.1; p.m1 = 0.1 + 2e-6; })), std::invalid_argument);
  EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 2e-5; })));   // 115,000 windows
  EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.m0 = 0.5; p.m1 = 0.5 + 1e-4; })));   // 23,000 windows
  // n_max_undecided: 0 allowed (never), negative not
  EXPECT_NO_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.n_max_undecided = 0; })));
  EXPECT_THROW(LayeredMap(bad([](LayeredMapParams& p) { p.n_max_undecided = -1; })), std::invalid_argument);
  // ceiling: NaN derives, otherwise finite and >= A
  const double A = std::log(99.0);
  EXPECT_NO_THROW(LayeredMap(bad([&](LayeredMapParams& p) { p.sprt_ceiling = A; })));
  EXPECT_THROW(LayeredMap(bad([&](LayeredMapParams& p) { p.sprt_ceiling = A - 1e-6; })), std::invalid_argument);
  EXPECT_THROW(LayeredMap(bad([&](LayeredMapParams& p) { p.sprt_ceiling = inf; })), std::invalid_argument);
  EXPECT_NEAR(LayeredMap(bad([&](LayeredMapParams& p) { p.sprt_ceiling = 6.0; })).sprt().ceiling, 6.0, 0);
  // legacy never validates the sprt fields
  LayeredMapParams legacy = Sp(); legacy.evidence_model = EvidenceModel::Legacy; legacy.m0 = 5.0;
  EXPECT_NO_THROW(LayeredMap{legacy});
}

// ---- latency ------------------------------------------------------------------------

TEST(Sprt, AlwaysHitWallGraduatesAtTheDerivedWindowAndNeverEarlier) {
  struct Case { double m0, m1, a, b; int expect; };
  const std::vector<Case> cases = {
      {0.20, 0.70, 0.01, 0.01, 4}, {0.20, 0.70, 0.05, 0.05, 3}, {0.15, 0.65, 0.01, 0.01, 4},
      {0.30, 0.60, 0.01, 0.01, 7}, {0.05, 0.90, 0.01, 0.01, 2}, {0.10, 0.20, 0.01, 0.01, 7},
      {0.20, 0.70, 0.001, 0.01, 6}};
  for (const bool periodicity : {false, true}) {
    for (const Case& c : cases) {
      LayeredMapParams p = Sp(c.m0, c.m1, c.a, c.b);
      p.enable_periodicity = periodicity;
      LayeredMap m(p);
      const SprtDerived& d = m.sprt();
      EXPECT_EQ(d.latency, static_cast<int>(std::ceil(d.A / d.l_hit - 1e-12)))
          << c.m0 << " " << c.m1 << " " << c.a;
      EXPECT_EQ(d.latency, c.expect) << c.m0 << " " << c.m1 << " " << c.a;
      for (int w = 1; w <= d.latency + 40; ++w) {
        m.observeHit(7); m.tick();
        if (w < d.latency) {
          EXPECT_FALSE(m.isStatic(7)) << "window " << w << " case " << c.m0 << "/" << c.m1;
        } else {
          EXPECT_TRUE(m.isStatic(7)) << "window " << w << " case " << c.m0 << "/" << c.m1;
        }
        EXPECT_NE(m.classify(7), CellClass::Periodic) << "window " << w;
      }
    }
  }
}

TEST(Sprt, ThreeHitsDoNotGraduateAtTheShippedPair) {   // the v0.1 window 3 is gone
  LayeredMap m(Sp());
  for (int w = 1; w <= 3; ++w) { m.observeHit(1); m.tick(); }
  EXPECT_FALSE(m.isStatic(1));
  m.observeHit(1); m.tick();
  EXPECT_TRUE(m.isStatic(1));
  EXPECT_EQ(m.classify(1), CellClass::Static);
}

TEST(Sprt, ALongMissRunCanDelayButNotPreventGraduationBeforeTheGate) {
  // No lower clamp before n >= T = 24: 10 misses push l to -9.81, then it takes 12 hits
  // (not 8, which a clamp at B would give) to reach A. Graduation at n = 22 < 24.
  LayeredMapParams p = Sp(); p.periodicity.period_windows = 24;
  LayeredMap m(p);
  for (int w = 0; w < 10; ++w) { m.observeMiss(3); m.tick(); }
  EXPECT_FALSE(m.isStatic(3));
  EXPECT_EQ(m.observations(3), 10);
  EXPECT_LT(m.occupancyProb(3), 1e-4);                      // l = -9.81, unclamped
  for (int w = 0; w < 7; ++w) { m.observeHit(3); m.tick(); }   // n = 17: a clamp at B would have graduated at n = 18
  for (int w = 0; w < 4; ++w) { m.observeHit(3); m.tick(); EXPECT_FALSE(m.isStatic(3)) << "n = " << m.observations(3); }
  m.observeHit(3); m.tick();                                // n = 22
  EXPECT_TRUE(m.isStatic(3));
  EXPECT_EQ(m.observations(3), 22);
}

// ---- Ville: the error rate under independent windows ---------------------------------

TEST(Sprt, EverStaticPerHistoryAtM0StaysUnderAlpha) {
  // iid Bernoulli at exactly m0 over 1024 windows: P(a history ever graduates) <= exp(-A) = alpha.
  // The statement is for the 95 % Wilson upper limit of the observed share.
  const History h = runHistories(Sp(), 0.20, 0.0, 1500, 1024, 20261018);
  ASSERT_GT(h.histories, 20000);
  const double share = static_cast<double>(h.ever_static) / h.histories;
  EXPECT_LE(wilsonUpper(h.ever_static, h.histories), 0.01) << h.ever_static << " of " << h.histories << " = " << share;
  EXPECT_GT(h.ever_static, 0);                                 // the test can see graduations at all
}

TEST(Sprt, EverStaticPerHistoryBelowM0IsSmaller) {
  const History h = runHistories(Sp(), 0.10, 0.0, 1500, 1024, 20261019);
  EXPECT_LE(wilsonUpper(h.ever_static, h.histories), 0.002) << h.ever_static << " of " << h.histories;
}

TEST(Sprt, EverStaticPerHistoryWithPeriodicityOnStaysUnderAlpha) {
  LayeredMapParams p = Sp(); p.enable_periodicity = true;       // veto only removes graduations
  const History h = runHistories(p, 0.20, 0.0, 600, 1024, 20261020);
  EXPECT_LE(wilsonUpper(h.ever_static, h.histories), 0.01) << h.ever_static << " of " << h.histories;
}

TEST(Sprt, AutocorrelatedWindowsBreakTheBoundAsStated) {
  // Negative control and the caveat of the guarantee: with lag-1 autocorrelation 0.5 at
  // exactly m0 the per-history share is far above alpha. The bound is for independent windows.
  const History h = runHistories(Sp(), 0.20, 0.50, 1500, 1024, 20261021);
  EXPECT_GT(wilsonLower(h.ever_static, h.histories), 0.01) << h.ever_static << " of " << h.histories;
}

// ---- operating characteristic ---------------------------------------------------------

TEST(Sprt, OperatingCharacteristicMatchesTheExactLatticeComputation) {
  const int N = 40000;
  int i = 0;
  for (int gate : {1, 24}) {                                       // the pure test and the shipped gate T = 24
    LayeredMapParams p = Sp(); p.periodicity.period_windows = gate;
    const SprtDerived d = LayeredMap(p).sprt();
    for (double m : {0.10, 0.20, 0.30, 0.50, 0.65, 0.70}) {
      const double exact = exactOcGraduate(m, d, gate);
      const double mc = mcGraduateShare(p, m, N, 31000 + i++);
      const double se = std::sqrt(std::max(exact * (1 - exact), 1e-6) / N);
      EXPECT_NEAR(mc, exact, 4.5 * se) << "gate " << gate << " m = " << m << " exact " << exact << " mc " << mc;
    }
  }
}

TEST(Sprt, ExactOcAgainstWaldDiffersByTheOvershootOnly) {
  const SprtDerived d = deriveSprt(Sp());
  for (double m : {0.20, 0.30, 0.50, 0.65}) {
    const double ex = exactOcGraduate(m, d), w = waldOc(m, 0.20, 0.70, 0.01, 0.01);
    EXPECT_NEAR(ex, w, 0.025) << "m = " << m << " exact " << ex << " wald " << w;
  }
  // Wald's value is the conservative one at and below m0 (overshoot only helps).
  EXPECT_LE(exactOcGraduate(0.20, d), waldOc(0.20, 0.20, 0.70, 0.01, 0.01));
  EXPECT_LE(exactOcGraduate(0.20, d), 0.01);
  EXPECT_GE(exactOcGraduate(0.70, d), 0.99);                       // power at m1
}

TEST(Sprt, ExactOcLatticeComputationSelfCheck) {
  // The reference itself: it must return the known extremes and a monotone curve.
  const SprtDerived d = deriveSprt(Sp());
  EXPECT_NEAR(exactOcGraduate(1.0, d), 1.0, 1e-12);
  EXPECT_NEAR(exactOcGraduate(0.0, d), 0.0, 1e-12);
  double prev = -1;
  for (double m = 0.05; m < 0.96; m += 0.05) { const double v = exactOcGraduate(m, d); EXPECT_GT(v, prev); prev = v; }
  EXPECT_NEAR(waldOc(0.20, 0.20, 0.70, 0.01, 0.01), 0.01, 1e-9);
  EXPECT_NEAR(waldOc(0.70, 0.20, 0.70, 0.01, 0.01), 0.99, 1e-9);
}

// ---- the periodicity gate (prune / maturity race) --------------------------------------

TEST(Sprt, AllMissCellIsKeptUntilTouchedWindowTAndErasedExactlyThen) {
  for (int T : {8, 24}) {
    LayeredMapParams p = Sp(); p.periodicity.period_windows = T;
    LayeredMap m(p);
    for (int n = 1; n < T; ++n) {
      m.observeMiss(5); m.tick();
      EXPECT_EQ(m.cellCount(), 1u) << "T=" << T << " n=" << n;                // not rejected before the gate
      EXPECT_EQ(m.classify(5), CellClass::Transient) << "T=" << T << " n=" << n;
    }
    m.observeMiss(5); m.tick();                                              // n == T
    EXPECT_EQ(m.cellCount(), 0u) << "T=" << T;
    EXPECT_EQ(m.classify(5), CellClass::Unknown);
  }
}

TEST(Sprt, DoorWith25PercentDutyIsNotRejectedBeforeTheGateAndBecomesPeriodic) {
  // 6 hit windows then 18 miss windows, period 24 = T. With periodicity on, the cell
  // must survive the gate (rate 0.25 has negative drift, l is far below B at n = 24) and
  // be read out Periodic once the significance test has two periods; it is never kept Static.
  LayeredMapParams p = Sp(); p.enable_periodicity = true;      // T = 24, H = 2, a_min = 0.3
  p.n_max_undecided = 64;
  LayeredMap m(p);
  bool periodic_seen = false;
  int candidate_unknown_checks = 0;
  for (int w = 0; w < 96; ++w) {
    if ((w % 24) < 6) { m.observeHit(9); } else { m.observeMiss(9); }
    m.tick();
    if (w + 1 <= 24) { EXPECT_EQ(m.cellCount(), 1u) << "window " << (w + 1); }   // alive through the gate
    if (m.classify(9) == CellClass::Periodic) periodic_seen = true;
    // A cell at the floor (n >= T, l <= B) that is kept only as a periodic candidate and is not yet
    // read out Periodic or Static reads Unknown: neither Transient nor Intermittent (n_max = 64 here).
    if (p.n_max_undecided > 0 && m.observations(9) >= p.periodicity.period_windows && !m.isStatic(9) &&
        m.classify(9) != CellClass::Periodic && m.occupancyProb(9) <= 1.0 / (1.0 + std::exp(-m.sprt().B)) + 1e-12) {
      ++candidate_unknown_checks;
      EXPECT_EQ(m.classify(9), CellClass::Unknown) << "window " << (w + 1);
    }
    // Before the gate a cell is never Unknown (not rejected): here Static in the first hit phase, then Transient.
    if (w + 1 < 24) { EXPECT_NE(m.classify(9), CellClass::Unknown) << "window " << (w + 1); }
    // (The door graduates in each hit phase and is demoted by the reverse test in the miss phase, which
    // restarts its count, so the floor at B from n = T is not reached at fixed window numbers here;
    // the floor itself is pinned by the gate tests above.)
  }
  EXPECT_TRUE(periodic_seen);
  EXPECT_GT(candidate_unknown_checks, 0);                      // the branch was reached, not skipped
  EXPECT_EQ(m.classify(9), CellClass::Periodic);
  EXPECT_FALSE(m.isStatic(9));
}

TEST(Sprt, RejectionAlsoDropsThePeriodicityHistoryOfTheCell) {
  // A re-created cell starts a new history in the periodicity model too: one hit after the
  // rejection predicts occupancy 1.0 (n = 1), not the 1/25 of the erased history.
  LayeredMapParams p = Sp(); p.enable_periodicity = true; p.periodicity.period_windows = 8;
  LayeredMap m(p);
  for (int n = 0; n < 8; ++n) { m.observeMiss(2); m.tick(); }
  ASSERT_EQ(m.cellCount(), 0u);
  m.observeHit(2); m.tick();
  EXPECT_GT(m.periodicProb(2), 0.99);
}

TEST(Sprt, RejectedCellIsRecreatedByTheNextHitAndStartsAFreshHistory) {
  LayeredMapParams p = Sp(); p.periodicity.period_windows = 8;
  LayeredMap m(p);
  for (int n = 0; n < 8; ++n) { m.observeMiss(2); m.tick(); }
  ASSERT_EQ(m.cellCount(), 0u);                                   // rejected and erased at n = 8
  m.observeHit(2); m.tick();
  EXPECT_EQ(m.observations(2), 1);                                // a new history
  EXPECT_NEAR(m.occupancyProb(2), 1.0 / (1.0 + std::exp(-m.sprt().l_hit)), 1e-12);
  for (int w = 2; w <= m.sprt().latency; ++w) { m.observeHit(2); m.tick(); }
  EXPECT_TRUE(m.isStatic(2));                                     // graduates after the derived latency again
  EXPECT_EQ(m.observations(2), m.sprt().latency);
}

// ---- demotion: the reverse test ---------------------------------------------------------

TEST(Sprt, GraduatedCellIsDemotedByTheReverseTestAfterTheDerivedMissCount) {
  for (const auto& pr : std::vector<std::pair<double, double>>{{0.20, 0.70}, {0.15, 0.65}, {0.30, 0.60}}) {
    LayeredMap m(Sp(pr.first, pr.second));
    const SprtDerived& d = m.sprt();
    for (int w = 0; w < d.latency + 10; ++w) { m.observeHit(4); m.tick(); }   // at the ceiling
    ASSERT_TRUE(m.isStatic(4));
    for (int k = 1; k <= d.demote_misses + 1; ++k) {
      m.observeMiss(4); m.tick();
      if (k < d.demote_misses) EXPECT_TRUE(m.isStatic(4)) << pr.first << " miss " << k;
      else                     EXPECT_FALSE(m.isStatic(4)) << pr.first << " miss " << k;
    }
    // the forward test restarted from l = 0 at the demotion; the next miss is the first of the new run
    EXPECT_GE(m.cellCount(), 1u);
  }
}

TEST(Sprt, GraduationRestartsAtTheCeilingEvenWhenTheCellGraduatesBelowIt) {
  // Graduates at exactly the derived window, l = 4 l_hit = 5.01 < ceiling 7.10. The reverse test must
  // still start from the ceiling: five all-miss windows, not three.
  LayeredMap m(Sp());
  const int L = m.sprt().latency;
  for (int w = 0; w < L; ++w) { m.observeHit(4); m.tick(); }
  ASSERT_TRUE(m.isStatic(4));
  EXPECT_NEAR(m.occupancyProb(4), 1.0 / (1.0 + std::exp(-m.sprt().ceiling)), 1e-12);
  for (int k = 1; k <= m.sprt().demote_misses; ++k) {
    ASSERT_TRUE(m.isStatic(4)) << "miss " << k - 1;
    m.observeMiss(4); m.tick();
  }
  EXPECT_FALSE(m.isStatic(4));
}

TEST(Sprt, PeriodicVetoStopsGraduationAndLeavesTheStatisticAlone) {
  // 12 on / 12 off door, T = 24. Once the cell reads Periodic, its statistic follows the plain update
  // (ceiling above, floor below) and is never reset by a graduation that the veto forbids.
  LayeredMapParams p = Sp(); p.enable_periodicity = true;
  LayeredMap m(p);
  const SprtDerived& d = m.sprt();
  bool tracking = false;
  double l = 0.0;
  int checked = 0;
  for (int w = 0; w < 240; ++w) {
    const bool on = (w % 24) < 12;
    if (on) m.observeHit(6); else m.observeMiss(6);
    m.tick();
    if (tracking) {
      l = std::min(d.ceiling, l + (on ? d.l_hit : d.l_miss));
      if (m.observations(6) >= 24) l = std::max(d.B, l);                     // the floor applies from n = T of the current test
      EXPECT_NEAR(m.occupancyProb(6), 1.0 / (1.0 + std::exp(-l)), 1e-9) << "window " << (w + 1);
      EXPECT_FALSE(m.isStatic(6)) << "window " << (w + 1);
      ++checked;
    } else if (m.classify(6) == CellClass::Periodic && !m.isStatic(6)) {
      tracking = true;
      const double pr = m.occupancyProb(6);
      l = std::log(pr / (1.0 - pr));
    }
  }
  ASSERT_TRUE(tracking);
  EXPECT_GT(checked, 100);
}

TEST(Sprt, Voxel3DBackendCountsIntermittentCells) {
  LayeredMapParams p = Sp(); p.n_max_undecided = 2;
  Voxel3DBackend b(0.5, p);
  Observation o; o.hits.push_back({3.0, 0.0, 0.5});
  b.integrate(o, {0.0, 0.0, 0.5}); b.tick();
  b.integrate(o, {0.0, 0.0, 0.5}); b.tick();                      // endpoint voxel: n = 2, l = 2 l_hit = 2.5 < A
  EXPECT_GE(b.intermittentCellCount(), 1u);
  EXPECT_EQ(b.intermittentCellCount(), b.layered().intermittentCells().size());
  EXPECT_EQ(b.staticCellCount(), 0u);
}

TEST(Sprt, ShippedPairNeedsFiveAllMissWindowsToDemoteAndOneMissNever) {
  LayeredMap m(Sp());
  for (int w = 0; w < 30; ++w) { m.observeHit(4); m.tick(); }
  for (int cycle = 0; cycle < 50; ++cycle) {                        // a miss every 4th window: never demoted
    m.observeMiss(4); m.tick();
    for (int k = 0; k < 3; ++k) { m.observeHit(4); m.tick(); }
    ASSERT_TRUE(m.isStatic(4)) << "cycle " << cycle;
  }
  for (int k = 1; k <= 4; ++k) { m.observeMiss(4); m.tick(); EXPECT_TRUE(m.isStatic(4)) << "miss " << k; }
  m.observeMiss(4); m.tick();
  EXPECT_FALSE(m.isStatic(4));                                       // the fifth
  EXPECT_NEAR(m.occupancyProb(4), 0.5, 1e-12);                       // restarted at l = 0
}

TEST(Sprt, DemotedCellContinuesAsAnUndecidedCellAndCanGraduateAgain) {
  LayeredMap m(Sp());
  for (int w = 0; w < 10; ++w) { m.observeHit(4); m.tick(); }
  for (int k = 0; k < 5; ++k) { m.observeMiss(4); m.tick(); }
  ASSERT_FALSE(m.isStatic(4));
  ASSERT_EQ(m.classify(4), CellClass::Transient);
  for (int w = 1; w <= m.sprt().latency; ++w) { m.observeHit(4); m.tick(); }
  EXPECT_TRUE(m.isStatic(4));
}

TEST(Sprt, DemotionRestartsTheWindowCountSoALongHistoryDoesNotReadIntermittentAtOnce) {
  // 100 hit windows (far more than n_max = 64), then 5 misses: the reverse test demotes the cell.
  // The restarted test has seen 0 windows, so it is not Intermittent, and it gets the same
  // no-clamp grace before T as a new cell.
  LayeredMapParams p = Sp(); p.n_max_undecided = 64;
  LayeredMap m(p);
  for (int w = 0; w < 100; ++w) { m.observeHit(4); m.tick(); }
  ASSERT_TRUE(m.isStatic(4));
  for (int k = 0; k < 5; ++k) { m.observeMiss(4); m.tick(); }
  ASSERT_FALSE(m.isStatic(4));
  EXPECT_EQ(m.observations(4), 0);
  EXPECT_EQ(m.classify(4), CellClass::Transient);
  m.observeHit(4); m.tick();
  EXPECT_EQ(m.observations(4), 1);
  EXPECT_EQ(m.classify(4), CellClass::Transient);
  // grace: more misses than the floor would allow before T, and the cell is neither floored nor erased
  const int T = std::max(1, p.periodicity.period_windows);
  for (int k = 0; k < 10; ++k) { m.observeMiss(4); m.tick(); }
  ASSERT_LT(m.observations(4), T);
  EXPECT_LT(m.occupancyProb(4), 1.0 / (1.0 + std::exp(-m.sprt().B)));   // below sigma(B): not floored
  EXPECT_NE(m.classify(4), CellClass::Intermittent);
}

TEST(Sprt, DoorGraduatesEarlyAndPeriodicityThenDemotesIt) {
  // 4 on / 4 off, T = 8, H = 3: graduates at window 4 (n < T), the reverse test never
  // fires (four misses from the ceiling stay above the demotion level), only the periodic
  // predicate removes it.
  LayeredMapParams p = Sp(); p.enable_periodicity = true;
  p.periodicity.period_windows = 8; p.periodicity.n_harmonics = 3;
  p.periodic_alpha_spending = false; p.periodic_false_alarm = 0.1;
  LayeredMap m(p);
  bool was_static = false;
  for (int w = 0; w < 64; ++w) {
    if ((w % 8) < 4) m.observeHit(6); else m.observeMiss(6);
    m.tick();
    if (m.isStatic(6)) was_static = true;
  }
  EXPECT_TRUE(was_static);
  EXPECT_EQ(m.classify(6), CellClass::Periodic);
  EXPECT_FALSE(m.isStatic(6));
}

// ---- Intermittent -----------------------------------------------------------------------

namespace {
// Keeps l near 0 for a long time: hit when sigma(l) <= 0.5, miss otherwise.
void feedZeroDrift(LayeredMap& m, CellId c) {
  if (m.occupancyProb(c) > 0.5) m.observeMiss(c); else m.observeHit(c);
  m.tick();
}
}  // namespace

TEST(Sprt, UndecidedCellIsIntermittentFromNMaxOnAndStillCanGraduate) {
  LayeredMapParams p = Sp(); p.n_max_undecided = 80;
  LayeredMap m(p);
  for (int n = 1; n < 80; ++n) {
    feedZeroDrift(m, 8);
    EXPECT_EQ(m.classify(8), CellClass::Transient) << "n = " << n;
  }
  feedZeroDrift(m, 8);                                   // n == 80
  EXPECT_EQ(m.classify(8), CellClass::Intermittent);
  EXPECT_EQ(m.intermittentCells().size(), 1u);
  EXPECT_TRUE(m.transientCells().empty());
  EXPECT_FALSE(m.isStatic(8));
  for (int w = 0; w < 6; ++w) { m.observeHit(8); m.tick(); }
  EXPECT_TRUE(m.isStatic(8));
  EXPECT_EQ(m.classify(8), CellClass::Static);           // Static wins over Intermittent
}

TEST(Sprt, NMaxZeroNeverReadsOutIntermittent) {
  LayeredMapParams p = Sp(); p.n_max_undecided = 0;
  LayeredMap m(p);
  for (int n = 1; n <= 400; ++n) feedZeroDrift(m, 8);
  EXPECT_EQ(m.classify(8), CellClass::Transient);
  EXPECT_TRUE(m.intermittentCells().empty());
}

TEST(Sprt, PeriodicWinsOverIntermittent) {
  LayeredMapParams p = Sp(); p.enable_periodicity = true; p.n_max_undecided = 48;
  p.periodicity.period_windows = 8; p.periodicity.n_harmonics = 3;
  p.periodic_alpha_spending = false; p.periodic_false_alarm = 0.1;
  LayeredMap m(p);
  for (int w = 0; w < 96; ++w) { if ((w % 8) < 4) m.observeHit(6); else m.observeMiss(6); m.tick(); }
  ASSERT_GE(m.observations(6), 48);
  EXPECT_EQ(m.classify(6), CellClass::Periodic);
  EXPECT_TRUE(m.intermittentCells().empty());
}

TEST(Sprt, IntermittentCellIsRenderedLikeTransientInTheGridAndCounted) {
  GridMeta gm; gm.width = 20; gm.height = 20; gm.resolution = 1.0; gm.origin_x = 0; gm.origin_y = 0;
  LayeredMapParams p = Sp(); p.n_max_undecided = 3; p.enable_periodicity = false;
  Grid2DBackend b(gm, p);
  const Eigen::Vector3d sensor(0.5, 0.5, 0.0);
  Observation hit; hit.hits.push_back(Eigen::Vector3d(5.5, 0.5, 0.0));          // endpoint (5, 0)
  Observation far; far.hits.push_back(Eigen::Vector3d(15.5, 0.5, 0.0));         // ray passes (5, 0): a miss there
  b.integrate(hit, sensor); b.tick();
  b.integrate(hit, sensor); b.tick();
  b.integrate(far, sensor); b.tick();                                            // hit, hit, miss: l = 1.52, n = 3
  const CellId c = gridCellId(gm, 5, 0);
  ASSERT_EQ(b.layered().classify(c), CellClass::Intermittent);
  // Cells (0..4, 0) lie on the ray in all three windows (3 misses, n = 3): also Intermittent,
  // leaning free. Cells (6..14, 0) lie on the ray of the last window only, and its endpoint (15, 0) was hit once (n = 1): Transient.
  EXPECT_EQ(b.intermittentCellCount(), 6u);
  EXPECT_EQ(b.transientCellCount(), 10u);
  EXPECT_EQ(b.intermittentCellCount(), b.layered().intermittentCells().size());
  EXPECT_GT(b.layered().occupancyProb(c), 0.5);
  // The cell leans occupied: drawn as obstacle (50), as a Transient one would be. A cell
  // that leans free is not drawn as an obstacle.
  GridMap g = b.toOccupancyGrid();
  EXPECT_EQ(g.data[c], 50);
  ASSERT_LT(b.layered().occupancyProb(gridCellId(gm, 3, 0)), 0.5);
  ASSERT_EQ(b.layered().classify(gridCellId(gm, 3, 0)), CellClass::Intermittent);
  EXPECT_EQ(g.data[gridCellId(gm, 3, 0)], 0);
}

// ---- engine plumbing --------------------------------------------------------------------

TEST(Sprt, OccupancyProbIsTheLogisticOfTheStatistic) {
  LayeredMap m(Sp());
  const SprtDerived& d = m.sprt();
  EXPECT_EQ(m.occupancyProb(1), 0.0);                                    // unknown cell
  m.observeHit(1); m.tick();
  EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-d.l_hit)), 1e-12);
  m.observeMiss(1); m.tick();
  EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-(d.l_hit + d.l_miss))), 1e-12);
  for (int w = 0; w < 6; ++w) { m.observeHit(1); m.tick(); }
  ASSERT_TRUE(m.isStatic(1));
  EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-d.ceiling)), 1e-12);   // restarted at the ceiling
}

TEST(Sprt, HitWinsOverMissInTheSameWindow) {
  LayeredMap m(Sp());
  m.observeMiss(1); m.observeHit(1); m.observeMiss(1); m.tick();
  EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-m.sprt().l_hit)), 1e-12);
}

TEST(Sprt, CeilingCapsTheStatisticOfAnUngraduatedPeriodicCandidate) {
  // A cell that is hit forever stays at the ceiling, however long: no growth beyond A + 2 l_hit.
  LayeredMap m(Sp());
  for (int w = 0; w < 500; ++w) { m.observeHit(1); m.tick(); }
  EXPECT_NEAR(m.occupancyProb(1), 1.0 / (1.0 + std::exp(-m.sprt().ceiling)), 1e-12);
}

TEST(Sprt, CloseWindowsEqualsRepeatedEndWindow) {
  LayeredMapParams p = Sp(); p.layer_interval = 1000;
  LayeredMap a(p), b(p);
  for (int i = 0; i < 3; ++i) { a.observeHit(1); a.observeMiss(2); a.closeWindows(5); }
  for (int i = 0; i < 3; ++i) { b.observeHit(1); b.observeMiss(2); for (int k = 0; k < 5; ++k) b.endWindow(); }
  EXPECT_EQ(a.windowCount(), b.windowCount());
  for (CellId c : {1, 2}) {
    EXPECT_EQ(a.observations(c), b.observations(c));
    EXPECT_EQ(a.isStatic(c), b.isStatic(c));
    EXPECT_EQ(a.occupancyProb(c), b.occupancyProb(c));
  }
}

namespace {
struct Rec : WindowObserver {
  std::vector<std::vector<int>> rows;
  void onTouchedCell(int w, CellId id, bool hit, bool miss) override {
    rows.push_back({w, static_cast<int>(id), hit ? 1 : 0, miss ? 1 : 0});
  }
};
}  // namespace

TEST(Sprt, WindowObserverSeesTheSameTouchesAsUnderLegacy) {
  LayeredMapParams ps = Sp(); LayeredMapParams pl = Sp(); pl.evidence_model = EvidenceModel::Legacy;
  LayeredMap s(ps), l(pl);
  Rec rs, rl; s.setWindowObserver(&rs); l.setWindowObserver(&rl);
  Rng rng(5);
  for (int w = 0; w < 200; ++w) {
    for (int c = 0; c < 20; ++c) {
      const double u = rng.u();
      if (u < 0.3) { s.observeHit(c); l.observeHit(c); }
      else if (u < 0.6) { s.observeMiss(c); l.observeMiss(c); }
    }
    s.tick(); l.tick();
  }
  ASSERT_FALSE(rs.rows.empty());
  std::sort(rs.rows.begin(), rs.rows.end()); std::sort(rl.rows.begin(), rl.rows.end());
  // the same cells are touched in the same windows with the same outcome: the cell set
  // each model keeps differs (rejected cells are erased), but a touched cell is reported either way
  EXPECT_EQ(rs.rows, rl.rows);
}

TEST(Sprt, CellStaysTwentyFourBytes) {   // the sequential test adds no per-cell state
  EXPECT_EQ(sizeof(CellEvidence), 24u);
}

TEST(Sprt, LegacyIsStillAvailableByName) {
  LayeredMapParams p; p.evidence_model = EvidenceModel::Legacy; p.layer_interval = 1;
  p.enable_periodicity = false;
  LayeredMap m(p);
  for (int w = 1; w <= 3; ++w) { m.observeHit(1); m.tick(); }
  EXPECT_TRUE(m.isStatic(1));                                            // v0.1: window 3
  EXPECT_EQ(m.sprt().latency, 0);
}
