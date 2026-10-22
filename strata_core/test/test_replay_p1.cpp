// Replay of the advisory numbers (P1_DECISION_ADVICE section 1, 2) through the real engine.
//
// The advisor computed the derived SPRT tables and an empirical replay with scratch
// scripts. This test makes those numbers traceable (rail R1): every derived value and
// every advisory table row is reproduced by committed code, and the empirical rows are
// produced by feeding the per-cell hit/miss run sequences of the phase 1 calibration
// split (run B) window by window through LayeredMap itself.
//
//   * Part A needs no data: derived parameters, Wald operating characteristic, expected
//     sample numbers, and the truncation table (exact lattice values against the
//     advisor's 20,000-run Monte Carlo).
//   * Part B needs the phase 1 cell tables (calibration split only, never in the
//     repository: ~/datasets/strata/p1, or $STRATA_P1_DIR). It is skipped when they are
//     absent. `paper/experiments/realdata/run_replay_p2.sh` first checks their sha256
//     and writes the table to results_p2/replay_advisory_table.txt ($STRATA_REPLAY_OUT).
//
// Each engine row is also compared, cell by cell, with a plain re-implementation of the
// rule (the advisor's replay function), so the engine and the table cannot drift apart.
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "strata_core/layered_map.hpp"

using namespace strata_core;

namespace {

LayeredMapParams Sp(double m0, double m1, double a, double b, int gate) {
  LayeredMapParams p;
  p.evidence_model = EvidenceModel::Sprt; p.layer_interval = 1;
  p.m0 = m0; p.m1 = m1; p.alpha = a; p.beta = b;
  p.enable_periodicity = false; p.n_max_undecided = 0;
  p.periodicity.period_windows = gate;
  return p;
}

// ------------------------------------------------------------------ part A: closed forms

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

double waldEN(double m, double m0, double m1, double alpha, double beta) {
  const double a = std::log(m1 / m0), b = std::log((1 - m1) / (1 - m0));
  const double A = std::log((1 - beta) / alpha), B = std::log(beta / (1 - alpha));
  const double d = m * a + (1 - m) * b;
  if (std::fabs(d) < 1e-9) return -A * B / (m * a * a + (1 - m) * b * b);
  const double P1 = waldOc(m, m0, m1, alpha, beta);
  return ((1 - P1) * B + P1 * A) / d;
}

// P(still undecided after n windows) for iid windows at hit rate m under the pure rule
// (decided at the first l >= A or l <= B): exact, from the lattice.
std::vector<double> undecidedAfter(double m, const SprtDerived& d, int nmax) {
  std::map<std::pair<int, int>, double> fr{{{0, 0}, 1.0}};
  std::vector<double> out(nmax + 1, 1.0);
  for (int n = 1; n <= nmax; ++n) {
    std::map<std::pair<int, int>, double> next;
    for (const auto& kv : fr) {
      for (int hit = 0; hit < 2; ++hit) {
        const double pr = hit ? m : 1.0 - m;
        const int h = kv.first.first + hit, k = kv.first.second + (1 - hit);
        const double l = h * d.l_hit + k * d.l_miss;
        if (l < d.A && l > d.B) next[{h, k}] += kv.second * pr;
      }
    }
    fr.swap(next);
    double s = 0; for (const auto& kv : fr) s += kv.second;
    out[n] = s;
  }
  return out;
}

// ------------------------------------------------------------------ part B: cell tables

struct Cell { int label; int N; int H; std::vector<std::pair<bool, int>> runs; };

std::vector<std::pair<bool, int>> parseRuns(const std::string& s) {
  std::vector<std::pair<bool, int>> out;
  size_t i = 0;
  while (i < s.size()) {
    const bool hit = s[i] == 'h';
    size_t j = i + 1;
    while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
    out.emplace_back(hit, std::stoi(s.substr(i + 1, j - i - 1)));
    i = j;
  }
  return out;
}

bool loadCells(const std::string& path, std::vector<Cell>& out) {
  std::ifstream f(path);
  if (!f) return false;
  std::string line;
  std::getline(f, line);                                  // header: label,ix,iy,iz,N,H,first,last,div,runs
  while (std::getline(f, line)) {
    std::vector<std::string> col;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, ',')) col.push_back(tok);
    if (col.size() < 10) continue;
    Cell c;
    c.label = std::stoi(col[0]); c.N = std::stoi(col[4]); c.H = std::stoi(col[5]);
    c.runs = parseRuns(col[9]);
    out.push_back(std::move(c));
  }
  return true;
}

struct Out { char o; int n; };

// One cell through the real engine: a map holding only this cell, one window per run step.
Out engineReplay(const Cell& c, const LayeredMapParams& p) {
  LayeredMap m(p);
  int n = 0;
  for (const auto& r : c.runs) {
    for (int k = 0; k < r.second; ++k) {
      ++n;
      if (r.first) m.observeHit(0); else m.observeMiss(0);
      m.tick();
      if (m.isStatic(0)) return {'S', n};
      if (m.cellCount() == 0) return {'R', n};
    }
  }
  return {'U', n};
}

// The same cell through the real engine, fed to the END of its sequence. A rejected cell is erased
// (LayeredMap drops it), the next observation creates it again with a new history, as the deployed node
// does. Returns true if the cell was Static at any window. engineReplay above stops at the first decision
// and so reports the FIRST HISTORY of the cell; this reports the CELL.
bool everStatic(const Cell& c, const LayeredMapParams& p) {
  LayeredMap m(p);
  bool ever = false;
  for (const auto& r : c.runs) {
    for (int k = 0; k < r.second; ++k) {
      if (r.first) m.observeHit(0); else m.observeMiss(0);
      m.tick();
      if (m.isStatic(0)) ever = true;
    }
  }
  return ever;
}

// The advisor's replay function (sprt_replay2.replay_gate): clamp to [B, A + 2 l_hit] at every
// window, graduate at l >= A, reject at l <= B once n >= T. pre_floor: false = no lower clamp
// before the gate (the engine's rule), true = floor at B always.
Out refReplay(const Cell& c, const SprtDerived& d, int T, bool floor_always) {
  double l = 0.0; int n = 0;
  for (const auto& r : c.runs) {
    const double x = r.first ? d.l_hit : d.l_miss;
    for (int k = 0; k < r.second; ++k) {
      ++n; l += x;
      if (l > d.ceiling) l = d.ceiling;
      if (floor_always || n >= T) { if (l < d.B) l = d.B; }
      if (l >= d.A) return {'S', n};
      if (l <= d.B && n >= T) return {'R', n};
    }
  }
  return {'U', n};
}

double pctInterp(std::vector<int> xs, double q) {
  if (xs.empty()) return NAN;
  std::sort(xs.begin(), xs.end());
  const double k = (xs.size() - 1) * q;
  const size_t f = static_cast<size_t>(std::floor(k)), c = static_cast<size_t>(std::ceil(k));
  return f == c ? xs[f] : xs[f] + (xs[c] - xs[f]) * (k - f);
}
double pctIndex(std::vector<int> xs, double q) {
  if (xs.empty()) return NAN;
  std::sort(xs.begin(), xs.end());
  return xs[static_cast<size_t>(q * xs.size())];     // the advisor's lat[int(q * n)]
}

struct Row {
  double S{0}, R{0}, U{0};
  double lat50i{NAN}, lat90i{NAN}, lat50x{NAN}, lat90x{NAN};   // interpolated / index percentile of windows-to-Static
  long n{0}, nS{0}, nR{0};
  long n_le{0}, nS_le{0}, n_gt{0}, nS_gt{0};                   // by hit rate H/N against m0
  long mismatches{0};                                          // engine vs plain re-implementation, per cell
};

Row run(const std::vector<Cell>& cells, double m0, double m1, double a, double b, int gate, bool floor_always,
        bool check_engine) {
  const LayeredMapParams p = Sp(m0, m1, a, b, gate);
  const SprtDerived d = deriveSprt(p);
  Row r; std::vector<int> lat;
  for (const Cell& c : cells) {
    const Out o = floor_always ? refReplay(c, d, gate, true) : engineReplay(c, p);
    if (check_engine && !floor_always) {
      const Out q = refReplay(c, d, gate, false);
      if (q.o != o.o || (o.o != 'U' && q.n != o.n)) ++r.mismatches;
    }
    ++r.n;
    const bool le = static_cast<double>(c.H) / c.N <= m0;
    (le ? r.n_le : r.n_gt)++;
    if (o.o == 'S') { ++r.nS; lat.push_back(o.n); (le ? r.nS_le : r.nS_gt)++; }
    else if (o.o == 'R') ++r.nR;
  }
  r.S = static_cast<double>(r.nS) / r.n; r.R = static_cast<double>(r.nR) / r.n; r.U = 1.0 - r.S - r.R;
  r.lat50i = pctInterp(lat, 0.5); r.lat90i = pctInterp(lat, 0.9);
  r.lat50x = pctIndex(lat, 0.5);  r.lat90x = pctIndex(lat, 0.9);
  return r;
}

std::string p1Dir() {
  if (const char* e = std::getenv("STRATA_P1_DIR")) return e;
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/datasets/strata/p1";
  return "";
}

}  // namespace

// ================================================================== part A (no data)

TEST(ReplayP1, DerivedTableOfTheAdvice) {
  struct R { double m0, m1, a; double lh, lm, A, mstar; int lat; double en_star, en_m1, en_m0; };
  const std::vector<R> rows = {
      {0.15, 0.65, 0.01, 1.466, -0.887, 4.595, 0.377, 4, 16.2, 7.0, 8.4},
      {0.15, 0.65, 0.05, 1.466, -0.887, 2.944, 0.377, 3, 6.7, 4.1, 5.0},
      {0.10, 0.55, 0.01, 1.705, -0.693, 4.595, 0.289, 3, 17.9, 7.2, 9.9},
      {0.10, 0.55, 0.05, 1.705, -0.693, 2.944, 0.289, 2, 7.3, 4.2, 5.8},
      {0.25, 0.70, 0.01, 1.030, -0.916, 4.595, 0.471, 5, 22.4, 10.1, 10.5}};
  for (const R& r : rows) {
    const SprtDerived d = deriveSprt(Sp(r.m0, r.m1, r.a, r.a, 24));
    EXPECT_NEAR(d.l_hit, r.lh, 6e-4); EXPECT_NEAR(d.l_miss, r.lm, 6e-4); EXPECT_NEAR(d.A, r.A, 6e-4);
    EXPECT_NEAR(d.B, -r.A, 6e-4); EXPECT_NEAR(d.m_star, r.mstar, 6e-4);
    EXPECT_EQ(d.latency, r.lat) << r.m0 << " " << r.m1 << " " << r.a;
    EXPECT_NEAR(waldEN(d.m_star, r.m0, r.m1, r.a, r.a), r.en_star, 0.06);
    EXPECT_NEAR(waldEN(r.m1, r.m0, r.m1, r.a, r.a), r.en_m1, 0.06);
    EXPECT_NEAR(waldEN(r.m0, r.m0, r.m1, r.a, r.a), r.en_m0, 0.06);
  }
  // the anti-conservative rounding and the development placeholder
  EXPECT_EQ(deriveSprt(Sp(0.10, 0.70, 0.01, 0.01, 24)).latency, 3);
  EXPECT_NEAR(deriveSprt(Sp(0.10, 0.70, 0.01, 0.01, 24)).m_star, 0.361, 6e-4);
  EXPECT_EQ(deriveSprt(Sp(0.30, 0.60, 0.01, 0.01, 24)).latency, 7);
  EXPECT_NEAR(waldEN(deriveSprt(Sp(0.30, 0.60, 0.01, 0.01, 24)).m_star, 0.30, 0.60, 0.01, 0.01), 54.4, 0.06);
}

TEST(ReplayP1, WaldOperatingCharacteristicOfThe2DPairInTheAdvice) {
  EXPECT_NEAR(waldOc(0.15, 0.15, 0.65, 0.01, 0.01), 0.010, 6e-4);
  EXPECT_NEAR(waldOc(0.30, 0.15, 0.65, 0.01, 0.01), 0.21, 6e-3);
  EXPECT_NEAR(waldOc(0.50, 0.15, 0.65, 0.01, 0.01), 0.88, 6e-3);
  EXPECT_NEAR(waldOc(0.65, 0.15, 0.65, 0.01, 0.01), 0.990, 6e-4);
}

TEST(ReplayP1, TruncationTableAgainstTheAdvisorsMonteCarloAtM) {
  // The advisor's table (20,000 runs, P(undecided after n windows) at m*, pure rule). The exact lattice
  // value must lie within four standard errors of each Monte Carlo figure.
  struct R { double m0, m1, a; std::map<int, double> mc; };
  const std::vector<R> rows = {
      {0.15, 0.65, 0.01, {{8, 0.702}, {16, 0.423}, {24, 0.273}, {32, 0.171}, {48, 0.065}, {64, 0.024}, {96, 0.003}, {128, 0.001}}},
      {0.15, 0.65, 0.05, {{8, 0.428}, {16, 0.138}, {24, 0.045}, {32, 0.014}, {48, 0.002}, {64, 0.001}}},
      {0.10, 0.55, 0.01, {{8, 0.791}, {16, 0.518}, {24, 0.295}, {32, 0.199}, {48, 0.084}, {64, 0.033}, {96, 0.005}, {128, 0.001}}}};
  for (const R& r : rows) {
    const SprtDerived d = deriveSprt(Sp(r.m0, r.m1, r.a, r.a, 1));
    const std::vector<double> u = undecidedAfter(d.m_star, d, 128);
    for (const auto& kv : r.mc) {
      const double se = std::sqrt(std::max(kv.second * (1 - kv.second), 1e-4) / 20000.0);
      EXPECT_NEAR(u[kv.first], kv.second, std::max(4 * se, 0.0007)) << r.m0 << " n=" << kv.first;
    }
  }
}

TEST(ReplayP1, NMaxOfTheShippedPairIsTheSmallestMultipleOf16UnderThreePercent) {
  // PREREG.md: n_max = 80 for (0.20, 0.70, 0.01, 0.01) at the zero-drift rate, under the shipped gate rule.
  const SprtDerived d = deriveSprt(Sp(0.20, 0.70, 0.01, 0.01, 24));
  std::map<std::pair<int, int>, double> fr{{{0, 0}, 1.0}};
  std::vector<double> u(129, 1.0);
  for (int n = 1; n <= 128; ++n) {
    std::map<std::pair<int, int>, double> next;
    for (const auto& kv : fr) {
      for (int hit = 0; hit < 2; ++hit) {
        const double pr = hit ? d.m_star : 1.0 - d.m_star;
        const int h = kv.first.first + hit, k = kv.first.second + (1 - hit);
        const double l = h * d.l_hit + k * d.l_miss;
        if (l >= d.A) continue;
        if (l <= d.B && n >= 24) continue;
        next[{h, k}] += kv.second * pr;
      }
    }
    fr.swap(next);
    double s = 0; for (const auto& kv : fr) s += kv.second;
    u[n] = s;
  }
  int first = 0;
  for (int n = 16; n <= 128; n += 16) if (u[n] <= 0.03) { first = n; break; }
  EXPECT_EQ(first, 80);
  EXPECT_NEAR(u[80], 0.014975807756304834, 1e-9);        // PREREG.json: undecided_at_n_max
  EXPECT_GT(u[64], 0.03);
}

// ================================================================== part B (needs the cell tables)

TEST(ReplayP1, AdvisoryTableThroughLayeredMapOnTheCalibrationSequences) {
  const std::string dir = p1Dir();
  std::vector<Cell> st10, cl10, st2, cl2, nwd10, nws10, frs10;
  {
    std::vector<Cell> all;
    if (dir.empty() || !loadCells(dir + "/lil/cells_static_0_r0.05_W10.csv", all))
      GTEST_SKIP() << "phase 1 cell tables not present under " << dir << " (set STRATA_P1_DIR)";
    for (auto& c : all) {
      if (c.label == 1) st10.push_back(c);
      else if (c.label == 2 && c.H >= 1) frs10.push_back(c);
      else if (c.label == 3 && c.H >= 1) nws10.push_back(c);
    }
    all.clear();
    ASSERT_TRUE(loadCells(dir + "/lil/cells_dynamics_0_r0.05_W10.csv", all));
    for (auto& c : all) {
      if (c.label == 2 && c.H >= 1) cl10.push_back(c);
      else if (c.label == 3 && c.H >= 1) nwd10.push_back(c);
    }
    all.clear();
    ASSERT_TRUE(loadCells(dir + "/lil/cells_static_0_r0.05_W2.csv", all));
    for (auto& c : all) if (c.label == 1) st2.push_back(c);
    all.clear();
    ASSERT_TRUE(loadCells(dir + "/lil/cells_dynamics_0_r0.05_W2.csv", all));
    for (auto& c : all) if (c.label == 2 && c.H >= 1) cl2.push_back(c);
  }
  ASSERT_EQ(st10.size(), 11027u);
  ASSERT_EQ(cl10.size(), 24099u);          // the n of the advisory table
  ASSERT_EQ(st2.size(), 11027u);
  ASSERT_EQ(cl2.size(), 24119u);

  std::ostringstream rep;
  rep << std::fixed << std::setprecision(4);
  rep << "Replay of P1_DECISION_ADVICE section 1 through LayeredMap (evidence_model sprt, periodicity off, one window per step).\n"
      << "Structure: static_0 cells labelled structure (all N >= 1). Clutter: dynamics_0 cells labelled free with H >= 1.\n"
      << "S, R, U: share graduated, rejected, undecided. lat: windows to Static, p50/p90 (interpolated | by index).\n"
      << "FS: clutter false-Static; FS<=m0 and FS>m0 split the clutter at hit rate H/N = m0.\n\n";
  auto emit = [&](const char* name, double m0, double m1, double a, int gate, bool floor_always,
                  const std::vector<Cell>& st, const std::vector<Cell>& cl, const Row& rs, const Row& rc) {
    rep << name << " (m0 " << m0 << " m1 " << m1 << " alpha=beta " << a << ", T " << gate << (floor_always ? ", floor at B" : "")
        << ")\n  structure n=" << rs.n << "  S " << rs.S << " R " << rs.R << " U " << rs.U
        << "  lat " << rs.lat50i << "/" << rs.lat90i << " | " << rs.lat50x << "/" << rs.lat90x << "\n"
        << "  clutter   n=" << rc.n << "  FS " << rc.S << "  FS<=m0 " << (rc.n_le ? double(rc.nS_le) / rc.n_le : NAN) << " (n=" << rc.n_le
        << ")  FS>m0 " << (rc.n_gt ? double(rc.nS_gt) / rc.n_gt : NAN) << " (n=" << rc.n_gt << ")\n";
    (void)st; (void)cl;
  };

  // Row 1: (0.15, 0.65, 0.01), pure rule (T = 1)
  const Row s1 = run(st10, 0.15, 0.65, 0.01, 0.01, 1, false, true), c1 = run(cl10, 0.15, 0.65, 0.01, 0.01, 1, false, true);
  emit("row 1 pure", 0.15, 0.65, 0.01, 1, false, st10, cl10, s1, c1);
  EXPECT_EQ(s1.mismatches + c1.mismatches, 0);
  EXPECT_NEAR(s1.S, 0.868, 0.002); EXPECT_NEAR(s1.R, 0.066, 0.002); EXPECT_NEAR(s1.U, 0.066, 0.002);
  EXPECT_EQ(s1.lat50i, 4); EXPECT_EQ(s1.lat90i, 7);
  EXPECT_NEAR(c1.S, 0.025, 0.002);
  EXPECT_EQ(c1.n_le, 21849); EXPECT_EQ(c1.n_gt, 2250);
  EXPECT_NEAR(double(c1.nS_le) / c1.n_le, 0.0065, 0.002);
  EXPECT_NEAR(double(c1.nS_gt) / c1.n_gt, 0.208, 0.002);
  // text of (c): 337 of 732 rejected structure cells sit below m1
  {
    long rej = 0, below = 0;
    const SprtDerived d = deriveSprt(Sp(0.15, 0.65, 0.01, 0.01, 1));
    for (const Cell& c : st10) {
      if (refReplay(c, d, 1, false).o == 'R' && c.N >= 5) { ++rej; if (double(c.H) / c.N < 0.65) ++below; }
    }
    rep << "  structure rejected with N >= 5: " << rej << ", of which hit rate < m1: " << below << "\n";
    EXPECT_EQ(rej, 732); EXPECT_EQ(below, 337);
  }
  // text of (d): controls at T = 1
  {
    const Row nd = run(nwd10, 0.15, 0.65, 0.01, 0.01, 1, false, false), ns = run(nws10, 0.15, 0.65, 0.01, 0.01, 1, false, false),
              fr = run(frs10, 0.15, 0.65, 0.01, 0.01, 1, false, false);
    rep << "  controls (graduated share): near-wall free dynamics_0 " << nd.S << " (n=" << nd.n << "), near-wall free static_0 "
        << ns.S << " (n=" << ns.n << "), reference-free hit in static_0 " << fr.S << " (n=" << fr.n << ")\n";
    EXPECT_NEAR(nd.S, 0.0965, 0.002); EXPECT_NEAR(ns.S, 0.0645, 0.002); EXPECT_NEAR(fr.S, 0.0071, 0.002);
  }

  // Row 2: gate T = 24 with a floor at B at every window (the rejected variant; plain re-implementation only)
  const Row s2 = run(st10, 0.15, 0.65, 0.01, 0.01, 24, true, false), c2 = run(cl10, 0.15, 0.65, 0.01, 0.01, 24, true, false);
  emit("row 2 gate 24, floor at B (not the engine's rule)", 0.15, 0.65, 0.01, 24, true, st10, cl10, s2, c2);
  EXPECT_NEAR(s2.S, 0.915, 0.002); EXPECT_NEAR(s2.R, 0.013, 0.002); EXPECT_NEAR(s2.U, 0.072, 0.002);
  EXPECT_EQ(s2.lat50x, 4); EXPECT_EQ(s2.lat90x, 10);
  EXPECT_NEAR(c2.S, 0.035, 0.002);
  EXPECT_NEAR(double(c2.nS_le) / c2.n_le, 0.0078, 0.002);
  EXPECT_NEAR(double(c2.nS_gt) / c2.n_gt, 0.295, 0.002);

  // Row 3: gate T = 24, no lower clamp before the gate: the engine's rule
  const Row s3 = run(st10, 0.15, 0.65, 0.01, 0.01, 24, false, true), c3 = run(cl10, 0.15, 0.65, 0.01, 0.01, 24, false, true);
  emit("row 3 gate 24, no lower clamp before the gate (the engine)", 0.15, 0.65, 0.01, 24, false, st10, cl10, s3, c3);
  EXPECT_EQ(s3.mismatches + c3.mismatches, 0);
  EXPECT_NEAR(s3.S, 0.904, 0.002); EXPECT_NEAR(s3.R, 0.024, 0.002); EXPECT_NEAR(s3.U, 0.071, 0.002);
  EXPECT_EQ(s3.lat50x, 4); EXPECT_EQ(s3.lat90x, 8);
  EXPECT_NEAR(c3.S, 0.028, 0.002);
  // the engine's rule beats the floor-at-B variant on false-Static, and rejects more structure
  EXPECT_LT(c3.S, c2.S); EXPECT_GT(s3.R, s2.R);

  // Row 4: (0.15, 0.65, 0.05), pure
  const Row s4 = run(st10, 0.15, 0.65, 0.05, 0.05, 1, false, true), c4 = run(cl10, 0.15, 0.65, 0.05, 0.05, 1, false, true);
  emit("row 4 pure", 0.15, 0.65, 0.05, 1, false, st10, cl10, s4, c4);
  EXPECT_EQ(s4.mismatches + c4.mismatches, 0);
  EXPECT_NEAR(s4.S, 0.865, 0.002); EXPECT_NEAR(s4.R, 0.092, 0.002); EXPECT_NEAR(s4.U, 0.043, 0.002);
  EXPECT_EQ(s4.lat50i, 3); EXPECT_EQ(s4.lat90i, 4);
  EXPECT_NEAR(c4.S, 0.032, 0.002);
  EXPECT_NEAR(double(c4.nS_le) / c4.n_le, 0.0105, 0.002);
  EXPECT_NEAR(double(c4.nS_gt) / c4.n_gt, 0.235, 0.002);

  // Row 5: the placeholder (0.30, 0.60, 0.01), pure
  const Row s5 = run(st10, 0.30, 0.60, 0.01, 0.01, 1, false, true), c5 = run(cl10, 0.30, 0.60, 0.01, 0.01, 1, false, true);
  emit("row 5 pure", 0.30, 0.60, 0.01, 1, false, st10, cl10, s5, c5);
  EXPECT_EQ(s5.mismatches + c5.mismatches, 0);
  EXPECT_NEAR(s5.S, 0.830, 0.002); EXPECT_NEAR(s5.R, 0.042, 0.002); EXPECT_NEAR(s5.U, 0.128, 0.002);
  EXPECT_EQ(s5.lat50i, 7); EXPECT_EQ(s5.lat90i, 14);
  EXPECT_NEAR(c5.S, 0.011, 0.002);

  // Row 6: W = 2, (0.10, 0.55, 0.01), pure
  const Row s6 = run(st2, 0.10, 0.55, 0.01, 0.01, 1, false, true), c6 = run(cl2, 0.10, 0.55, 0.01, 0.01, 1, false, true);
  emit("row 6 (W = 2) pure", 0.10, 0.55, 0.01, 1, false, st2, cl2, s6, c6);
  EXPECT_EQ(s6.mismatches + c6.mismatches, 0);
  EXPECT_NEAR(s6.S, 0.834, 0.002); EXPECT_NEAR(s6.R, 0.135, 0.002); EXPECT_NEAR(s6.U, 0.031, 0.002);
  EXPECT_EQ(s6.lat50i, 3); EXPECT_EQ(s6.lat90i, 7);
  EXPECT_NEAR(c6.S, 0.026, 0.002);
  EXPECT_NEAR(double(c6.nS_le) / c6.n_le, 0.0104, 0.002);
  EXPECT_NEAR(double(c6.nS_gt) / c6.n_gt, 0.251, 0.002);

  if (const char* out = std::getenv("STRATA_REPLAY_OUT")) {
    std::ofstream o(out);
    o << rep.str();
  }
}

TEST(ReplayP1, InSampleKittiReplayOfTheAdvice) {
  const std::string dir = p1Dir();
  std::vector<Cell> all;
  if (dir.empty() || !loadCells(dir + "/kth00/cells_r0.2_W10.csv", all))
    GTEST_SKIP() << "KITTI 00 cell table not present under " << dir;
  std::vector<Cell> st, mv;
  for (auto& c : all) {
    if ((c.label & 0x0F) == 3) st.push_back(c);
    else if (c.label == 1) mv.push_back(c);
  }
  all.clear();
  ASSERT_EQ(st.size(), 283511u);
  ASSERT_EQ(mv.size(), 5901u);
  std::ostringstream rep;
  rep << std::fixed << std::setprecision(4);
  rep << "In-sample replay on KITTI 00 (KTH benchmark, 0.2 m voxels, W = 10, single pass), pair (0.15, 0.65, 0.01), pure rule.\n"
      << "Structure: static_structure voxels (n=" << st.size() << "); moving voxels (n=" << mv.size() << ").\n";
  const Row rs = run(st, 0.15, 0.65, 0.01, 0.01, 1, false, true);
  rep << "  structure  S " << rs.S << "  R " << rs.R << "  U " << rs.U << "  windows to Static p50 " << rs.lat50x << "\n";
  EXPECT_EQ(rs.mismatches, 0);
  EXPECT_NEAR(rs.S, 0.1963, 0.002); EXPECT_NEAR(rs.R, 0.0759, 0.002); EXPECT_NEAR(rs.U, 0.7277, 0.002);
  EXPECT_EQ(rs.lat50x, 4);
  // cells with N >= 4: share graduated and undecided
  {
    const LayeredMapParams p = Sp(0.15, 0.65, 0.01, 0.01, 1);
    long n4 = 0, s4 = 0, u4 = 0;
    for (const Cell& c : st) if (c.N >= 4) { ++n4; const Out o = engineReplay(c, p); s4 += o.o == 'S'; u4 += o.o == 'U'; }
    EXPECT_EQ(n4, 145520);
    EXPECT_NEAR(double(s4) / n4, 0.3825, 0.002); EXPECT_NEAR(double(u4) / n4, 0.4696, 0.002);
    rep << "  structure voxels touched in >= 4 windows: n=" << n4 << "  S " << double(s4) / n4 << "  U " << double(u4) / n4 << "\n";
  }
  // moving voxels by hit-rate band
  {
    const LayeredMapParams p = Sp(0.15, 0.65, 0.01, 0.01, 1);
    long n[3] = {0, 0, 0}, g[3] = {0, 0, 0};
    for (const Cell& c : mv) {
      const double r = double(c.H) / c.N;
      const int b = r <= 0.15 ? 0 : (r >= 0.65 ? 2 : 1);
      ++n[b]; if (engineReplay(c, p).o == 'S') ++g[b];
    }
    EXPECT_EQ(n[0], 5025); EXPECT_EQ(g[0], 0);
    EXPECT_EQ(n[1], 694);  EXPECT_NEAR(double(g[1]) / n[1], 0.174, 0.002);
    EXPECT_EQ(n[2], 182);  EXPECT_NEAR(double(g[2]) / n[2], 0.984, 0.002);
    rep << "  moving voxels graduated, by hit-rate band: <= 0.15: " << g[0] << " of " << n[0] << "; (0.15, 0.65): " << g[1]
        << " of " << n[1] << "; >= 0.65: " << g[2] << " of " << n[2] << "\n";
  }
  if (const char* out = std::getenv("STRATA_REPLAY_KITTI_OUT")) {
    std::ofstream o(out);
    o << rep.str();
  }
}

// ================================================================== part C: the shipped pair on the 1.0 s calibration sequences

namespace {
std::string p1bDir() {
  if (const char* e = std::getenv("STRATA_P1B_DIR")) return e;
  return "/mnt/d/wsl/strata/p1b";
}
}  // namespace

// Not a reproduction of an advisory number: the realised rates of the SHIPPED pair (0.20, 0.70, 0.01, 0.01)
// on the W = 15 (1.0 s at 15 Hz) calibration sequences, the numbers that belong beside the bound alpha = 0.01.
TEST(ReplayP1, ShippedPairOnTheOneSecondCalibrationSequences) {
  const std::string dir = p1bDir();
  std::vector<Cell> st, cl, all;
  if (!loadCells(dir + "/lil_cells_static_0_r0.05_W15.csv", all))
    GTEST_SKIP() << "W = 15 cell tables not present under " << dir << " (set STRATA_P1B_DIR)";
  for (auto& c : all) if (c.label == 1) st.push_back(c);
  all.clear();
  ASSERT_TRUE(loadCells(dir + "/lil_cells_dynamics_0_r0.05_W15.csv", all));
  for (auto& c : all) if (c.label == 2 && c.H >= 1) cl.push_back(c);
  ASSERT_GT(st.size(), 5000u);
  ASSERT_GT(cl.size(), 10000u);

  std::ostringstream rep;
  rep << std::fixed << std::setprecision(4);
  rep << "Realised rates of the shipped pair (m0 0.20, m1 0.70, alpha = beta = 0.01) on the W = 15 calibration sequences\n"
      << "(LILocBench, 0.05 m, 1.0 s windows). Structure: static_0, label structure, all N >= 1 (n=" << st.size()
      << "). Clutter: dynamics_0, label free with H >= 1 (n=" << cl.size() << ").\n"
      << "Bound under independent windows: P(clutter at m0 graduates) <= exp(-A) = alpha/(1-beta) = 0.0101 (about alpha) per history.\n"
      << "UNIT of the rows 'pure rule' and 'shipped rule': the FIRST HISTORY / FIRST DECISION of each cell (the replay stops at the first S or R).\n"
      << "UNIT of the rows under 'whole sequence': the CELL (the replay runs to the end of the sequence; a rejected cell is erased and the next\n"
      << "observation starts a new history); ever-Static = the cell was Static at any window. The units differ: read the second for per-cell rates.\n\n";
  for (int gate : {1, 24}) {
    const Row s = run(st, 0.20, 0.70, 0.01, 0.01, gate, false, true), c = run(cl, 0.20, 0.70, 0.01, 0.01, gate, false, true);
    EXPECT_EQ(s.mismatches + c.mismatches, 0);
    rep << (gate == 1 ? "pure rule (T = 1)" : "shipped rule (gate T = 24, no lower clamp before the gate)") << "\n"
        << "  structure  S " << s.S << "  R " << s.R << "  U " << s.U << "  windows to Static p50/p90 " << s.lat50x << "/" << s.lat90x << "\n"
        << "  clutter    FS " << c.S << " (n=" << c.n << ")  FS at H/N <= m0 " << double(c.nS_le) / c.n_le << " (n=" << c.n_le
        << ")  FS above m0 " << double(c.nS_gt) / c.n_gt << " (n=" << c.n_gt << ")\n";
    EXPECT_NEAR(s.S + s.R + s.U, 1.0, 1e-12);
    // the population the bound is stated for: cells whose observed rate is at or below m0
    EXPECT_LE(double(c.nS_le) / c.n_le, 0.01) << "gate " << gate;
    EXPECT_GT(c.nS_gt, 0);
  }
  // Whole sequence, per cell (review finding 1). Three engine configurations, one cell per map:
  //   A  periodicity off, gate T = 24, n_max_undecided 0   (the 'shipped rule' engine above, run to the end)
  //   B  periodicity on, n_max_undecided 0                 (the node's periodicity, no Intermittent cut)
  //   C  the library defaults as the node ships them       (periodicity on, n_max_undecided 80)
  struct Cfg { const char* name; LayeredMapParams p; };
  LayeredMapParams pa = Sp(0.20, 0.70, 0.01, 0.01, 24);
  LayeredMapParams pb; pb.layer_interval = 1; pb.n_max_undecided = 0; pb.enable_periodicity = true;
  LayeredMapParams pc; pc.layer_interval = 1;
  ASSERT_TRUE(pc.enable_periodicity);
  ASSERT_EQ(pc.n_max_undecided, 80);
  rep << "\nwhole sequence, per cell (unit: the cell)\n";
  const Cfg cfgs[] = {{"A periodicity off, T = 24, n_max_undecided 0", pa}, {"B periodicity on, n_max_undecided 0", pb},
                      {"C library defaults (periodicity on, n_max_undecided 80)", pc}};
  double ever_le[3] = {0, 0, 0};
  int gi = 0;
  for (const Cfg& g : cfgs) {
    long n = 0, ev = 0, nle = 0, evle = 0, ngt = 0, evgt = 0, sn = 0, sev = 0;
    for (const Cell& c : cl) {
      const bool le = static_cast<double>(c.H) / c.N <= 0.20, e = everStatic(c, g.p);
      ++n; ev += e; (le ? nle : ngt)++; (le ? evle : evgt) += e;
    }
    for (const Cell& c : st) { ++sn; sev += everStatic(c, g.p); }
    rep << "  " << g.name << "\n"
        << "    clutter ever-Static " << double(ev) / n << " (n=" << n << ")  at H/N <= m0 " << double(evle) / nle << " (n=" << nle
        << ")  above m0 " << double(evgt) / ngt << " (n=" << ngt << ")\n"
        << "    structure ever-Static " << double(sev) / sn << " (n=" << sn << ")\n";
    EXPECT_EQ(nle, 22273) << g.name;   // the populations of the first-decision rows above
    EXPECT_EQ(ngt, 1823) << g.name;
    EXPECT_EQ(sn, 11027) << g.name;
    ever_le[gi++] = double(evle) / nle;
  }
  EXPECT_LE(ever_le[1], ever_le[0]);     // periodicity can only remove Static cells here
  EXPECT_EQ(ever_le[1], ever_le[2]);     // n_max_undecided 80 does not change the ever-Static count on these sequences
  // The per-cell rate of the population the bound is stated for is NOT below alpha, although the
  // first-history rate is (0.0040): the SPEC quotes both. Pinned so a change of engine shows here.
  EXPECT_GT(ever_le[0], 0.01) << "per-cell ever-Static at H/N <= m0, periodicity off";
  EXPECT_GT(ever_le[1], 0.01) << "per-cell ever-Static at H/N <= m0, periodicity on";
  EXPECT_LT(ever_le[0], 0.03);
  EXPECT_LT(ever_le[1], 0.03);
  if (const char* out = std::getenv("STRATA_REPLAY_W15_OUT")) {
    std::ofstream o(out);
    o << rep.str();
  }
}
