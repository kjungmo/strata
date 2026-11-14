// periodic_min_periods k: the Periodic label also needs n >= k T touched windows (n is the touch
// count of the cell's Fourier history, the n of the n >= T gate). Nothing else may change: with k = 1
// every result is that of the code before the field existed (TouchedClose compares that bit for bit
// with the frozen oracle; the harness CSVs are compared byte for byte), and for a cell that is never
// labelled, any k gives the same cell state at every window.
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "strata_core/layered_map.hpp"
#include "strata_core/periodicity.hpp"
#include "test_support.hpp"

using namespace strata_core;
using namespace strata_test;

namespace {
double nOf(const PeriodicityModel& pm, CellId id) { return pm.harmonic(id, 0).n; }
}  // namespace

TEST(PeriodicMinPeriods, DefaultIsOne) {
  EXPECT_EQ(LayeredMapParams{}.periodic_min_periods, 1.0);
}

TEST(PeriodicMinPeriods, KernelLabelIsTheOldLabelAndTheCountTest) {
  struct Cfg { int T, H; double a_min, alpha; bool spend; };
  const Cfg cfgs[] = {{8, 3, 0.3, 0.1, false}, {24, 2, 0.3, 0.2, true}, {8, 2, 0.3, 1.0, false}, {12, 1, 0.2, 0.5, true}};
  long labelled_k1 = 0, withheld_at_2 = 0, withheld_at_3 = 0, checks = 0;
  for (const Cfg& c : cfgs) {
    PeriodicityParams pp; pp.period_windows = c.T; pp.n_harmonics = c.H;
    PeriodicityModel pm(pp);
    Rng rng(c.T * 100 + c.H);
    // a square-wave door (duty 0.5, 0.25), a noisy door and two noise cells
    for (int n = 1; n <= 8 * c.T; ++n) {
      for (CellId id = 0; id < 4; ++id) {
        bool occ;
        if (id == 0) occ = (n % c.T) < c.T / 2;
        else if (id == 1) occ = (n % c.T) < std::max(1, c.T / 4);
        else if (id == 2) occ = rng.u() < 0.5;
        else occ = rng.u() < (((n % c.T) < c.T / 2) ? 0.9 : 0.1);
        pm.gather(id, occ, n);
        const bool raw = pm.isPeriodic(id, c.a_min, c.alpha, c.spend);            // the existing signature
        ASSERT_EQ(raw, pm.isPeriodic(id, c.a_min, c.alpha, c.spend, 1.0));
        const double cn = nOf(pm, id);
        for (double k : {1.0, 1.5, 2.0, 3.0}) {
          const bool lab = pm.isPeriodic(id, c.a_min, c.alpha, c.spend, k);
          ASSERT_EQ(lab, raw && cn >= k * c.T) << "T=" << c.T << " id=" << id << " n=" << n << " k=" << k;
          ++checks;
        }
        if (raw) ++labelled_k1;
        if (raw && cn < 2.0 * c.T) ++withheld_at_2;
        if (raw && cn < 3.0 * c.T) ++withheld_at_3;
      }
    }
  }
  EXPECT_GT(checks, 1000);
  EXPECT_GT(labelled_k1, 100);          // not vacuous: labels occur
  EXPECT_GT(withheld_at_2, 0);          // and k = 2 and 3 withhold some of them
  EXPECT_GT(withheld_at_3, withheld_at_2);
}

TEST(PeriodicMinPeriods, KernelFunctionsOtherThanTheLabelDoNotTakeK) {
  // amplitude and falseAlarm keep the T gate: a cell between T and kT still reports both.
  PeriodicityParams pp; pp.period_windows = 8; pp.n_harmonics = 2;
  PeriodicityModel pm(pp);
  for (int n = 1; n <= 12; ++n) pm.gather(0, (n % 8) < 4, n);
  EXPECT_GT(pm.amplitude(0), 0.3);
  EXPECT_FALSE(pm.isPeriodic(0, 0.3, 1.0, false, 2.0));    // n = 12 < 16: no label ...
  EXPECT_TRUE(pm.isPeriodic(0, 0.3, 1.0, false, 1.0));     // ... though the amplitude rule alone gives one at k = 1
  EXPECT_LT(pm.falseAlarm(0), 1.0);
}

TEST(PeriodicMinPeriods, MapLabelFollowsTheCountAndAGraduatedDoorIsDemotedWhenTheLabelArrives) {
  // 4 on / 4 off, T = 8, H = 3 (as the Sprt door test). An independent model gives the expected label.
  long static_between = 0;
  for (double k : {1.0, 2.0, 3.0}) {
    LayeredMapParams p = base(EvidenceModel::Sprt, true, 1);
    p.periodicity.n_harmonics = 3; p.periodic_alpha_spending = false; p.periodic_false_alarm = 0.1;
    p.n_max_undecided = 0; p.periodic_min_periods = k;
    LayeredMap m(p);
    PeriodicityModel pm(p.periodicity);
    long first_label = -1, withheld = 0, price = 0;
    for (int w = 1; w <= 64; ++w) {
      const bool hit = ((w - 1) % 8) < 4;
      if (hit) m.observeHit(6); else m.observeMiss(6);
      m.tick();
      pm.gather(6, hit, w);
      const bool raw = pm.isPeriodic(6, p.periodic_amplitude_min, p.periodic_false_alarm, p.periodic_alpha_spending);
      const bool want = raw && nOf(pm, 6) >= k * p.periodicity.period_windows;
      ASSERT_EQ(m.classify(6) == CellClass::Periodic, want) << "k=" << k << " window " << w;
      if (want && first_label < 0) first_label = w;
      if (raw && !want) { ++withheld; if (m.isStatic(6)) { ++price; } }
      if (want) { EXPECT_FALSE(m.isStatic(6)); }            // the label demotes and blocks graduation
    }
    ASSERT_GT(first_label, 0) << "k=" << k;
    EXPECT_GE(first_label, static_cast<long>(k * p.periodicity.period_windows));
    if (k == 1.0) { EXPECT_EQ(withheld, 0); }
    if (k > 1.0) { EXPECT_GT(withheld, 0); static_between += price; }
  }
  EXPECT_GT(static_between, 0);   // with k > 1 the door sits in the static map before the label: the price of the option
}

TEST(PeriodicMinPeriods, CellsThatAreNeverLabelledAreIdenticalForEveryK) {
  for (EvidenceModel model : {EvidenceModel::Sprt, EvidenceModel::Legacy}) {
    LayeredMapParams p1 = base(model, true, 1);
    p1.periodicity.period_windows = 12;
    LayeredMapParams p3 = p1; p3.periodic_min_periods = 3.0;
    LayeredMapParams p1x = p1; p1x.periodic_min_periods = 1.0;   // explicit 1.0 is the default
    LayeredMap a(p1), b(p3), c(p1x);
    Rng rng(model == EvidenceModel::Sprt ? 11 : 12);
    const int kIds = 300, kWindows = 400;
    std::vector<double> rate(kIds); std::vector<int> door(kIds);
    for (int i = 0; i < kIds; ++i) { rate[i] = 0.02 + 0.96 * rng.u(); door[i] = (i % 25 == 0) ? 12 : 0; }
    std::unordered_map<CellId, bool> ever;
    long compared = 0, labelled_ids = 0, skipped_after = 0;
    for (int w = 0; w < kWindows; ++w) {
      for (int i = 0; i < kIds; ++i) {
        if (rng.u() > 0.6) continue;                           // touched in about 60 % of the windows
        bool hit = door[i] ? ((w % door[i]) < door[i] / 2) : (rng.u() < rate[i]);
        if (hit) { a.observeHit(i); b.observeHit(i); c.observeHit(i); }
        else { a.observeMiss(i); b.observeMiss(i); c.observeMiss(i); }
      }
      a.tick(); b.tick(); c.tick();
      for (int i = 0; i < kIds; ++i) {
        if (a.classify(i) == CellClass::Periodic && !ever[i]) { ever[i] = true; ++labelled_ids; }
        // k = 1 and an explicit 1.0 are always the same map
        ASSERT_EQ(a.classify(i), c.classify(i));
        ASSERT_TRUE(sameBits(a.occupancyProb(i), c.occupancyProb(i)));
        if (ever[i]) { ++skipped_after; continue; }            // labelled under k = 1: k = 3 may differ from here on
        ++compared;
        ASSERT_EQ(a.classify(i), b.classify(i)) << "id " << i << " window " << w;
        ASSERT_EQ(a.isStatic(i), b.isStatic(i));
        ASSERT_EQ(a.observations(i), b.observations(i));
        ASSERT_TRUE(sameBits(a.occupancyProb(i), b.occupancyProb(i)));
        ASSERT_TRUE(sameBits(a.periodicProb(i), b.periodicProb(i)));
      }
    }
    EXPECT_GT(compared, 20000);
    EXPECT_GT(labelled_ids, 0);        // the labelled cells exist, so the comparison skips something
    EXPECT_GT(skipped_after, 0);
    EXPECT_EQ(a.cellCount() > 0, true);
  }
}

TEST(PeriodicMinPeriods, ALargerKNeverAddsALabelOnTheSameHistory) {
  // Same occupancy history, no pruning in the way: the labels at k = 2, 3 are subsets of the labels at k = 1.
  PeriodicityParams pp; pp.period_windows = 8; pp.n_harmonics = 2;
  Rng rng(5);
  long extra = 0, checks = 0, removed = 0;
  for (int cell = 0; cell < 200; ++cell) {
    PeriodicityModel pm(pp);
    const double m = 0.05 + 0.9 * rng.u();
    for (int n = 1; n <= 80; ++n) {
      const bool door = (cell % 2 == 0) ? (((n % 8) < 4) != (rng.u() < 0.1)) : (rng.u() < m);   // half the cells are noisy doors
      pm.gather(0, door, n);
      const bool l1 = pm.isPeriodic(0, 0.3, 0.2, true, 1.0), l2 = pm.isPeriodic(0, 0.3, 0.2, true, 2.0),
                 l3 = pm.isPeriodic(0, 0.3, 0.2, true, 3.0);
      if ((l2 && !l1) || (l3 && !l2)) ++extra;
      if (l1 && !l2) ++removed;
      const bool f1 = pm.isPeriodic(0, 0.3, 1.0, false, 1.0), f2 = pm.isPeriodic(0, 0.3, 1.0, false, 2.0);   // amplitude-only
      if (f2 && !f1) ++extra;
      if (f1 && !f2) ++removed;
      ++checks;
    }
  }
  EXPECT_EQ(extra, 0);
  EXPECT_EQ(checks, 200 * 80);
  EXPECT_GT(removed, 0);   // not vacuous: k = 2 removes labels that k = 1 gives
}

TEST(PeriodicMinPeriods, ValidationAcceptsOneOrMoreAndNamesTheParameterOtherwise) {
  for (EvidenceModel model : {EvidenceModel::Sprt, EvidenceModel::Legacy}) {
    for (bool periodicity : {true, false}) {
      LayeredMapParams p = base(model, periodicity, 1);
      for (double ok : {1.0, 1.0000001, 1.5, 2.0, 3.0, 1e6}) { p.periodic_min_periods = ok; EXPECT_NO_THROW(LayeredMap{p}) << ok; }
      for (double bad : {0.999999, 0.5, 0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        p.periodic_min_periods = bad;
        try { LayeredMap m(p); FAIL() << "accepted " << bad; }
        catch (const std::invalid_argument& e) { EXPECT_NE(std::string(e.what()).find("periodic_min_periods"), std::string::npos) << e.what(); }
      }
    }
  }
}

TEST(PeriodicMinPeriods, ProductsThatRoundUpNeedExactlyTheirCount) {
  // 2.2 * 25 = 55.00000000000001 and 1.1 * 10 = 11.000000000000002 in doubles: the bound is the ceiling with 1e-9 of
  // slack, so these need 55 and 11 windows, not 56 and 12.
  struct Case { int T; double k; int need; };
  for (const Case& c : {Case{25, 2.2, 55}, Case{10, 1.1, 11}, Case{8, 2.0, 16}, Case{24, 1.5, 36}}) {
    PeriodicityParams pp; pp.period_windows = c.T; pp.n_harmonics = 2;
    PeriodicityModel pm(pp);
    int first = -1;
    for (int n = 1; n <= 4 * c.T && first < 0; ++n) {
      pm.gather(0, (n % c.T) < c.T / 2, n);                  // a square wave of period T: amplitude-only label at n = T
      if (pm.isPeriodic(0, 0.3, 1.0, false, c.k)) first = n;
    }
    EXPECT_EQ(first, c.need) << "T=" << c.T << " k=" << c.k;
  }
}

TEST(PeriodicMinPeriods, ANonPositivePeriodKeepsTodaysBehaviourAtEveryK) {
  // period_windows <= 0 is not validated; the label must be the old one there, whatever k is.
  for (int T : {0, -3}) {
    PeriodicityParams pp; pp.period_windows = T; pp.n_harmonics = 2;
    PeriodicityModel pm(pp);
    Rng rng(77);
    for (int n = 1; n <= 40; ++n) {
      pm.gather(0, rng.u() < 0.5, n);
      for (bool spend : {false, true}) {
        const bool raw = pm.isPeriodic(0, 0.3, 0.2, spend);
        for (double k : {1.0, 2.0, 3.5}) EXPECT_EQ(pm.isPeriodic(0, 0.3, 0.2, spend, k), raw) << "T=" << T << " n=" << n;
      }
    }
  }
}
