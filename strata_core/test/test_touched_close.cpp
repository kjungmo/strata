// The window close visits only the cells the closing window touched. Results must not change by one bit.
//
// ReferenceLayeredMap below is the frozen oracle: the full-sweep algorithm of LayeredMap as it was
// before the close was restricted to touched cells (every cell visited at every close, amplitude and
// isPeriodic recomputed for all of them). It is built on FrozenPeriodicity (below: the 15f96e4 PeriodicityModel, copied
// verbatim), deriveSprt and LayeredMapParams only. Only lines marked "instrumentation" are not part of the old algorithm; they
// count events so that the test can show it is not vacuous.
//
// The real map and the oracle get the same random contact sequences. After every close the test
// compares cell count, the four cell lists including their order, and for every id ever used
// classify, isStatic, observations, occupancyProb and periodicProb (bit equality), and the observer
// events of the window as a multiset (the call order over cells is not part of the contract).
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <stdexcept>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "strata_core/layered_map.hpp"
#include "strata_core/periodicity.hpp"

#include "test_support.hpp"

using namespace strata_core;
using namespace strata_test;

namespace {


// ---------------------------------------------------------------------------- oracle ----------

class ReferenceLayeredMap {
 public:
  explicit ReferenceLayeredMap(LayeredMapParams params)
    : params_(params), periodicity_(params.periodicity) {
    if (params_.evidence_model == EvidenceModel::Sprt) {
      sprt_ = deriveSprt(params_);
      use_sprt_ = true;
    }
  }
  void setWindowObserver(WindowObserver* observer) { observer_ = observer; }
  void observeHit(CellId id) {
    note(id);
    cells_[id].window_hits++;
  }
  void observeMiss(CellId id) {
    note(id);
    cells_[id].window_misses++;
  }
  bool tick() {
    ++integration_count_;
    if (params_.layer_interval <= 1 || integration_count_ % params_.layer_interval == 0) {
      endWindow();
      return true;
    }
    return false;
  }
  void closeWindows(int k) {
    if (k <= 0) return;
    endWindow();
    window_count_ += k - 1;
  }
  void endWindow() {
    ++window_count_;
    if (use_sprt_) { endWindowSprt(); return; }
    WindowObserver* const observer = observer_;
    for (auto it = cells_.begin(); it != cells_.end();) {
      CellEvidence& e = it->second;
      const bool touched = (e.window_hits > 0 || e.window_misses > 0);
      const bool occ = e.window_hits > 0;
      const bool freed = (!occ && e.window_misses > 0);
      if (touched) {
        if (observer) observer->onTouchedCell(window_count_, it->first, occ, freed);
        if (occ) e.log_odds += params_.l_hit;
        else if (freed) e.log_odds += params_.l_miss;
        e.log_odds *= params_.survival_decay;
        e.log_odds = std::min(params_.l_max, std::max(params_.l_min, e.log_odds));
        e.observations++;
        if (params_.enable_periodicity) periodicity_.gather(it->first, occ, window_count_);
      }
      const double p = sigmoid(e.log_odds);
      const double amp = params_.enable_periodicity ? periodicity_.amplitude(it->first) : 0.0;
      const bool periodic = params_.enable_periodicity &&
          periodicity_.isPeriodic(it->first, params_.periodic_amplitude_min,
                                  params_.periodic_false_alarm, params_.periodic_alpha_spending);
      const bool was_graduated = e.graduated;   // instrumentation
      if (!e.graduated && !periodic && p >= params_.graduate_prob &&
          e.observations >= params_.min_observations)
        e.graduated = true;
      if (e.graduated && (p <= params_.demote_prob || periodic))
        e.graduated = false;
      if (!was_graduated && e.graduated) ++graduations;   // instrumentation
      if (was_graduated && !e.graduated) ++demotions;     // instrumentation
      e.window_hits = 0; e.window_misses = 0;
      if (!e.graduated && p < params_.prune_prob && amp < params_.periodic_amplitude_min) {
        ever_erased_.insert(it->first);   // instrumentation
        ++erasures;                       // instrumentation
        periodicity_.erase(it->first);
        it = cells_.erase(it);
      } else {
        ++it;
      }
    }
  }
  int observations(CellId id) const {
    auto it = cells_.find(id);
    return it == cells_.end() ? 0 : it->second.observations;
  }
  double occupancyProb(CellId id) const {
    auto it = cells_.find(id);
    return it == cells_.end() ? 0.0 : sigmoid(it->second.log_odds);
  }
  double periodicProb(CellId id) const { return periodicity_.predict(id, window_count_); }
  CellClass classify(CellId id) const {
    auto it = cells_.find(id);
    if (it == cells_.end()) return CellClass::Unknown;
    if (it->second.graduated) return CellClass::Static;
    if (params_.enable_periodicity &&
        periodicity_.isPeriodic(id, params_.periodic_amplitude_min, params_.periodic_false_alarm,
                                params_.periodic_alpha_spending))
      return CellClass::Periodic;
    if (use_sprt_) {
      const int n = it->second.observations;
      if (n >= std::max(1, params_.periodicity.period_windows) && it->second.log_odds <= sprt_.B)
        return CellClass::Unknown;
      if (params_.n_max_undecided > 0 && n >= params_.n_max_undecided) return CellClass::Intermittent;
      return CellClass::Transient;
    }
    if (sigmoid(it->second.log_odds) >= params_.prune_prob) return CellClass::Transient;
    return CellClass::Unknown;
  }
  bool isStatic(CellId id) const {
    auto it = cells_.find(id);
    return it != cells_.end() && it->second.graduated;
  }
  std::vector<CellId> staticCells() const {
    std::vector<CellId> o; for (auto& kv : cells_) if (kv.second.graduated) o.push_back(kv.first); return o;
  }
  std::vector<CellId> transientCells() const {
    std::vector<CellId> o;
    for (auto& kv : cells_) if (classify(kv.first) == CellClass::Transient) o.push_back(kv.first);
    return o;
  }
  std::vector<CellId> intermittentCells() const {
    std::vector<CellId> o;
    for (auto& kv : cells_) if (classify(kv.first) == CellClass::Intermittent) o.push_back(kv.first);
    return o;
  }
  std::vector<CellId> periodicCells() const {
    std::vector<CellId> o;
    for (auto& kv : cells_) if (classify(kv.first) == CellClass::Periodic) o.push_back(kv.first);
    return o;
  }
  int windowCount() const { return window_count_; }
  int integrationCount() const { return integration_count_; }
  std::size_t cellCount() const { return cells_.size(); }

  // instrumentation
  long erasures{0}, recreations{0}, graduations{0}, demotions{0};

 private:
  static double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }
  void note(CellId id) {   // instrumentation: a cell created again after it was erased
    if (cells_.find(id) == cells_.end() && ever_erased_.count(id)) { ++recreations; ever_erased_.erase(id); }
  }
  void endWindowSprt() {
    WindowObserver* const observer = observer_;
    const SprtDerived& d = sprt_;
    const int gate = std::max(1, params_.periodicity.period_windows);
    for (auto it = cells_.begin(); it != cells_.end();) {
      CellEvidence& e = it->second;
      const bool touched = (e.window_hits > 0 || e.window_misses > 0);
      const bool occ = e.window_hits > 0;
      const bool freed = (!occ && e.window_misses > 0);
      if (touched) {
        if (observer) observer->onTouchedCell(window_count_, it->first, occ, freed);
        e.log_odds += occ ? d.l_hit : d.l_miss;
        if (e.log_odds > d.ceiling) e.log_odds = d.ceiling;
        e.observations++;
        if (e.observations >= gate && e.log_odds < d.B) e.log_odds = d.B;
        if (params_.enable_periodicity) periodicity_.gather(it->first, occ, window_count_);
      }
      const double amp = params_.enable_periodicity ? periodicity_.amplitude(it->first) : 0.0;
      const bool periodic = params_.enable_periodicity &&
          periodicity_.isPeriodic(it->first, params_.periodic_amplitude_min,
                                  params_.periodic_false_alarm, params_.periodic_alpha_spending);
      const bool was_graduated = e.graduated;   // instrumentation
      if (!e.graduated && !periodic && e.log_odds >= d.A) {
        e.graduated = true;
        e.log_odds = d.ceiling;
      }
      if (e.graduated) {
        if (periodic) e.graduated = false;
        else if (e.log_odds <= d.demote_level) {
          e.graduated = false; e.log_odds = 0.0;
          e.observations = 0;
        }
      }
      if (!was_graduated && e.graduated) ++graduations;   // instrumentation
      if (was_graduated && !e.graduated) ++demotions;     // instrumentation
      e.window_hits = 0; e.window_misses = 0;
      const bool rejected = !e.graduated && e.observations >= gate && e.log_odds <= d.B;
      if (rejected && amp < params_.periodic_amplitude_min) {
        ever_erased_.insert(it->first);   // instrumentation
        ++erasures;                       // instrumentation
        periodicity_.erase(it->first);
        it = cells_.erase(it);
      } else {
        ++it;
      }
    }
  }
  LayeredMapParams params_;
  SprtDerived sprt_{};
  bool use_sprt_{false};
  std::unordered_map<CellId, CellEvidence> cells_;
  FrozenPeriodicity periodicity_;
  WindowObserver* observer_{nullptr};
  int integration_count_{0};
  int window_count_{0};
  std::set<CellId> ever_erased_;   // instrumentation
};

// ------------------------------------------------------------------------------ helpers ----------

using Event = std::tuple<int, CellId, bool, bool>;
struct Rec : WindowObserver {
  std::vector<Event> ev;
  void onTouchedCell(int w, CellId id, bool hit, bool miss) override { ev.emplace_back(w, id, hit, miss); }
};


template <class A, class B>
std::string compareMaps(const A& real, const B& ref, const std::vector<CellId>& ids) {
  std::ostringstream o;
  if (real.cellCount() != ref.cellCount()) o << "cellCount " << real.cellCount() << " vs " << ref.cellCount() << "; ";
  if (real.windowCount() != ref.windowCount()) o << "windowCount; ";
  if (real.integrationCount() != ref.integrationCount()) o << "integrationCount; ";
  if (real.staticCells() != ref.staticCells()) o << "staticCells (content or order); ";
  if (real.transientCells() != ref.transientCells()) o << "transientCells (content or order); ";
  if (real.periodicCells() != ref.periodicCells()) o << "periodicCells (content or order); ";
  if (real.intermittentCells() != ref.intermittentCells()) o << "intermittentCells (content or order); ";
  for (CellId id : ids) {
    if (real.classify(id) != ref.classify(id)) o << "classify(" << id << "); ";
    if (real.isStatic(id) != ref.isStatic(id)) o << "isStatic(" << id << "); ";
    if (real.observations(id) != ref.observations(id)) o << "observations(" << id << "); ";
    if (!sameBits(real.occupancyProb(id), ref.occupancyProb(id))) o << "occupancyProb(" << id << "); ";
    if (!sameBits(real.periodicProb(id), ref.periodicProb(id))) o << "periodicProb(" << id << "); ";
  }
  return o.str();
}

// ------------------------------------------------------------------------------ scenario ----------

enum class Kind { Wall, Periodic, Clutter, Burst, Both, Free, Flicker, Solid, Flip };

struct CellPlan {
  CellId id;
  Kind kind;
  int start, stop;   // active windows [start, stop)
  int phase;
};

struct Counters {
  long erasures{0}, recreations{0}, graduations{0}, demotions{0}, periodic_seen{0}, intermittent_seen{0};
  long closes{0}, empty_closes{0}, multi_closes{0};
};

struct Config {
  std::string name;
  LayeredMapParams p;
};

std::vector<Config> configs() {
  std::vector<Config> c;
  c.push_back({"sprt periodicity on, 1 tick per window", base(EvidenceModel::Sprt, true, 1)});
  c.push_back({"sprt periodicity off", base(EvidenceModel::Sprt, false, 1)});
  c.push_back({"sprt periodicity on, 3 ticks per window", base(EvidenceModel::Sprt, true, 3)});
  {
    LayeredMapParams p = base(EvidenceModel::Sprt, true, 1);
    p.n_max_undecided = 20; p.sprt_ceiling = 9.0;
    c.push_back({"sprt n_max 20, own ceiling", p});
  }
  {
    LayeredMapParams p = base(EvidenceModel::Sprt, true, 1);
    p.periodic_alpha_spending = false; p.periodic_false_alarm = 1.0;   // amplitude rule only
    c.push_back({"sprt amplitude rule only", p});
  }
  c.push_back({"legacy periodicity on, 1 tick per window", base(EvidenceModel::Legacy, true, 1)});
  c.push_back({"legacy periodicity off", base(EvidenceModel::Legacy, false, 1)});
  c.push_back({"legacy periodicity on, 4 ticks per window", base(EvidenceModel::Legacy, true, 4)});
  {
    LayeredMapParams p = base(EvidenceModel::Legacy, true, 1);
    p.graduate_prob = 0.4; p.demote_prob = 0.5; p.min_observations = 2;   // graduate_prob <= demote_prob
    c.push_back({"legacy graduate_prob <= demote_prob", p});
  }
  {
    LayeredMapParams p = base(EvidenceModel::Legacy, false, 1);
    p.graduate_prob = 0.45; p.demote_prob = 0.45;
    c.push_back({"legacy graduate_prob == demote_prob", p});
  }
  // Harmonic counts: negative (behaves as none), none, one, three, four, and above four (5, 8).
  for (int h : {-1, 0, 1, 3, 4, 5, 8}) {
    for (EvidenceModel m : {EvidenceModel::Sprt, EvidenceModel::Legacy}) {
      LayeredMapParams p = base(m, true, 1);
      p.periodicity.n_harmonics = h;
      c.push_back({std::string(m == EvidenceModel::Sprt ? "sprt" : "legacy") + " n_harmonics " + std::to_string(h), p});
    }
  }
  return c;
}

std::vector<CellPlan> makePlans(Rng& rng, int windows) {
  std::vector<CellPlan> plans;
  const int n = 160;
  for (int i = 0; i < n; ++i) {
    CellPlan c;
    c.id = static_cast<CellId>(i) * 104729 + ((i % 5 == 0) ? (1LL << 40) : 0) + 3;
    c.kind = static_cast<Kind>(rng.below(9));
    c.start = rng.below(windows / 2);
    c.stop = c.start + 20 + rng.below(windows);
    c.phase = rng.below(8);
    plans.push_back(c);
  }
  return plans;
}

void contacts(Rng& rng, const CellPlan& c, int w, LayeredMap& real, ReferenceLayeredMap& ref) {
  auto hit = [&] { real.observeHit(c.id); ref.observeHit(c.id); };
  auto miss = [&] { real.observeMiss(c.id); ref.observeMiss(c.id); };
  const bool active = w >= c.start && w < c.stop;
  switch (c.kind) {
    case Kind::Wall:
      if (active) { if (rng.u() < 0.95) hit(); else miss(); }
      else if (w >= c.stop && w < c.stop + 30) miss();   // the wall is gone, rays pass through
      break;
    case Kind::Periodic:
      if (active) { if (((w + c.phase) % 8) < 4) hit(); else miss(); }
      break;
    case Kind::Clutter:
      if (rng.u() < 0.3) { if (rng.u() < 0.25) hit(); else miss(); }
      break;
    case Kind::Burst:
      if (active && rng.u() < 0.7) { if (rng.u() < 0.5) hit(); else miss(); }
      break;
    case Kind::Both:
      if (rng.u() < 0.3) {   // hit and miss on the same cell in one window, any order, any repeat
        const int reps = 1 + rng.below(3);
        for (int r = 0; r < reps; ++r) { if (rng.below(2)) { hit(); miss(); } else { miss(); hit(); } }
        if (rng.below(2)) miss();
      }
      break;
    case Kind::Free:
      if (rng.u() < 0.8) miss();
      break;
    case Kind::Flicker:
      if (rng.u() < 0.5) { if (rng.u() < 0.5) hit(); else miss(); }
      break;
    case Kind::Solid:   // hit in every touched window, with gaps: a constant cell for the whole run
      if (active && rng.u() < 0.8) hit();
      break;
    case Kind::Flip:    // constant (all hit or all miss) for 30 windows, then mixed
      if (active && rng.u() < 0.9) {
        if (w < c.start + 30) { if (c.phase % 2) hit(); else miss(); }
        else { if (rng.u() < 0.5) hit(); else miss(); }
      }
      break;
  }
}

// Runs one seeded history on the real map and the oracle; returns "" if they agreed after every close.
std::string runHistory(const Config& cfg, std::uint64_t seed, int windows, Counters& tot) {
  Rng rng(seed);
  LayeredMap real(cfg.p);
  ReferenceLayeredMap ref(cfg.p);
  Rec rr, rf;
  real.setWindowObserver(&rr);
  ref.setWindowObserver(&rf);
  const std::vector<CellPlan> plans = makePlans(rng, windows);
  std::vector<CellId> ids;
  for (const auto& c : plans) ids.push_back(c.id);
  ids.push_back(-12345);   // never used: must read the same (unknown) in both
  auto check = [&](const char* what, int w) -> std::string {
    std::sort(rr.ev.begin(), rr.ev.end());
    std::sort(rf.ev.begin(), rf.ev.end());
    std::string bad = compareMaps(real, ref, ids);
    if (rr.ev != rf.ev) bad += "observer events; ";
    rr.ev.clear(); rf.ev.clear();
    if (!bad.empty()) {
      std::ostringstream o; o << cfg.name << ", seed " << seed << ", window step " << w << " after " << what << ": " << bad;
      return o.str();
    }
    return {};
  };
  for (int w = 0; w < windows; ++w) {
    const bool silent = rng.u() < 0.05;   // a close with nothing touched
    if (!silent) {
      // several rounds per window (each tick of a multi-tick window) in a random cell order
      const int rounds = std::max(1, cfg.p.layer_interval);
      for (int r = 0; r < rounds; ++r) {
        for (const auto& c : plans) contacts(rng, c, w, real, ref);
        if (r + 1 < rounds) {
          const bool a = real.tick(), b = ref.tick();
          if (a != b) return cfg.name + ": tick() result differs";
          if (a) { ++tot.closes; }
          if (auto bad = check("tick", w); !bad.empty()) return bad;
        }
      }
    }
    const double u = rng.u();
    if (u < 0.65) {
      const bool a = real.tick(), b = ref.tick();
      if (a != b) return cfg.name + ": tick() result differs";
      if (a) { ++tot.closes; if (silent) ++tot.empty_closes; }
      if (auto bad = check("tick", w); !bad.empty()) return bad;
    } else if (u < 0.85) {
      real.endWindow(); ref.endWindow(); ++tot.closes; if (silent) ++tot.empty_closes;
      if (auto bad = check("endWindow", w); !bad.empty()) return bad;
    } else if (u < 0.95) {
      const int k = 2 + rng.below(4);
      real.closeWindows(k);
      for (int i = 0; i < k; ++i) ref.endWindow();   // the contract: k endWindow() calls
      ++tot.closes; ++tot.multi_closes;
      if (auto bad = check("closeWindows(k)", w); !bad.empty()) return bad;
    } else {
      real.endWindow(); ref.endWindow();             // a second close at once: nothing touched
      real.endWindow(); ref.endWindow();
      tot.closes += 2; ++tot.empty_closes;
      if (auto bad = check("two endWindow()", w); !bad.empty()) return bad;
    }
    if (!ref.periodicCells().empty()) ++tot.periodic_seen;
    if (!ref.intermittentCells().empty()) ++tot.intermittent_seen;
  }
  tot.erasures += ref.erasures; tot.recreations += ref.recreations;
  tot.graduations += ref.graduations; tot.demotions += ref.demotions;
  return {};
}

}  // namespace

// ------------------------------------------------------------------------------------ tests ----------

TEST(TouchedClose, EveryCloseMatchesTheFullSweepOracleBitForBit) {
  const std::vector<Config> cs = configs();
  ASSERT_GE(cs.size(), 6u);
  constexpr int kSeeds = 10, kWindows = 400;
  std::vector<Counters> per(cs.size());
  for (std::size_t i = 0; i < cs.size(); ++i) {
    for (int s = 1; s <= kSeeds; ++s) {
      const std::string bad = runHistory(cs[i], static_cast<std::uint64_t>(s) * 7919 + i, kWindows, per[i]);
      ASSERT_TRUE(bad.empty()) << bad;
    }
  }
  // Not vacuous: every kind of event happened in the runs of each model, so the comparison above
  // covered erasure, re-creation, graduation, demotion and the Periodic / Intermittent labels.
  Counters sprt, legacy, sprt_per, legacy_per;
  for (std::size_t i = 0; i < cs.size(); ++i) {
    Counters& t = cs[i].p.evidence_model == EvidenceModel::Sprt ? sprt : legacy;
    t.erasures += per[i].erasures; t.recreations += per[i].recreations;
    t.graduations += per[i].graduations; t.demotions += per[i].demotions;
    t.periodic_seen += per[i].periodic_seen; t.intermittent_seen += per[i].intermittent_seen;
    t.empty_closes += per[i].empty_closes; t.multi_closes += per[i].multi_closes;
    if (cs[i].p.enable_periodicity) {
      Counters& u = cs[i].p.evidence_model == EvidenceModel::Sprt ? sprt_per : legacy_per;
      u.periodic_seen += per[i].periodic_seen;
    }
  }
  for (const Counters* t : {&sprt, &legacy}) {
    EXPECT_GT(t->erasures, 0) << (t == &sprt ? "sprt" : "legacy");
    EXPECT_GT(t->recreations, 0) << (t == &sprt ? "sprt" : "legacy");
    EXPECT_GT(t->graduations, 0) << (t == &sprt ? "sprt" : "legacy");
    EXPECT_GT(t->demotions, 0) << (t == &sprt ? "sprt" : "legacy");
    EXPECT_GT(t->empty_closes, 0);
    EXPECT_GT(t->multi_closes, 0);
  }
  EXPECT_GT(sprt_per.periodic_seen, 0);
  EXPECT_GT(legacy_per.periodic_seen, 0);
  EXPECT_GT(sprt.intermittent_seen, 0);
}

TEST(TouchedClose, ClosesVisitOnlyTheCellsOfTheClosingWindow) {
  for (EvidenceModel m : {EvidenceModel::Sprt, EvidenceModel::Legacy}) {
    LayeredMapParams p = base(m, true, 1);
    LayeredMap map(p);
    Rec rec; map.setWindowObserver(&rec);
    EXPECT_EQ(map.lastWindowTouched(), 0u);
    constexpr CellId N = 3000;
    for (CellId c = 0; c < N; ++c) map.observeHit(c);
    EXPECT_EQ(map.lastWindowTouched(), 0u) << "the open window is not closed yet";
    map.endWindow();
    EXPECT_EQ(map.lastWindowTouched(), static_cast<std::size_t>(N));
    EXPECT_EQ(rec.ev.size(), static_cast<std::size_t>(N));
    ASSERT_EQ(map.cellCount(), static_cast<std::size_t>(N)) << "one hit keeps a cell in both models";
    for (int w = 0; w < 50; ++w) {
      rec.ev.clear();
      const CellId c = (w * 37) % N;
      if (w % 2) map.observeHit(c); else map.observeMiss(c);
      map.observeHit(c);   // a second contact of the same cell does not count it twice
      map.endWindow();
      EXPECT_EQ(map.lastWindowTouched(), 1u);
      ASSERT_EQ(rec.ev.size(), 1u);
      EXPECT_EQ(std::get<1>(rec.ev[0]), c);
      EXPECT_EQ(map.cellCount(), static_cast<std::size_t>(N));
    }
    rec.ev.clear();
    map.endWindow();   // nothing touched
    EXPECT_EQ(map.lastWindowTouched(), 0u);
    EXPECT_TRUE(rec.ev.empty());
    map.observeMiss(5); map.observeMiss(6);
    map.closeWindows(3);   // the first window visits both cells; the two empty ones visit none
    EXPECT_EQ(rec.ev.size(), 2u);
    EXPECT_EQ(map.lastWindowTouched(), 2u) << "the cells the close call visited, not the last empty window";
    map.observeMiss(5);
    map.tick();
    EXPECT_EQ(map.lastWindowTouched(), 1u);
  }
}

TEST(TouchedClose, ACellErasedAtACloseIsCreatedAgainByTheNextContact) {
  for (EvidenceModel m : {EvidenceModel::Sprt, EvidenceModel::Legacy}) {
    LayeredMap map(base(m, false, 1));
    const CellId c = 42, other = 43;
    int erased_at = -1;
    map.observeHit(other);   // keeps the map non-empty
    for (int w = 1; w <= 200 && erased_at < 0; ++w) {
      map.observeMiss(c);
      map.endWindow();
      if (map.classify(c) == CellClass::Unknown) erased_at = w;
    }
    ASSERT_GT(erased_at, 0) << "a cell with only misses is pruned (rejected)";
    EXPECT_EQ(map.observations(c), 0);
    EXPECT_EQ(map.cellCount(), 1u);
    map.observeHit(c);   // re-created: touched in this window, a new history
    map.endWindow();
    EXPECT_EQ(map.lastWindowTouched(), 1u);
    EXPECT_EQ(map.observations(c), 1);
    EXPECT_EQ(map.cellCount(), 2u);
    EXPECT_GT(map.occupancyProb(c), 0.5);
  }
}

TEST(TouchedClose, TheMapCannotBeCopiedAndAMovedMapKeepsItsOpenWindow) {
  static_assert(!std::is_copy_constructible<LayeredMap>::value, "a copy would alias the touched list");
  static_assert(!std::is_copy_assignable<LayeredMap>::value, "a copy would alias the touched list");
  LayeredMapParams p = base(EvidenceModel::Sprt, true, 1);
  LayeredMap a(p);
  ReferenceLayeredMap ref(p);
  for (CellId c = 0; c < 500; ++c) { a.observeHit(c); ref.observeHit(c); }
  LayeredMap b(std::move(a));   // moved with a window open
  for (CellId c = 250; c < 750; ++c) { b.observeMiss(c); ref.observeMiss(c); }
  b.endWindow(); ref.endWindow();
  EXPECT_EQ(b.lastWindowTouched(), 750u);
  std::vector<CellId> ids;
  for (CellId c = 0; c < 750; ++c) ids.push_back(c);
  EXPECT_EQ(compareMaps(b, ref, ids), "");
  LayeredMap d(p);
  d = std::move(b);             // and by assignment
  for (CellId c = 0; c < 100; ++c) { d.observeHit(c); ref.observeHit(c); }
  d.endWindow(); ref.endWindow();
  EXPECT_EQ(d.lastWindowTouched(), 100u);
  EXPECT_EQ(compareMaps(d, ref, ids), "");
}

namespace {
struct ThrowAt : WindowObserver {
  int throw_on{-1}, calls{0};
  std::vector<CellId> seen;
  void onTouchedCell(int, CellId id, bool, bool) override {
    if (++calls == throw_on) throw std::runtime_error("observer failed");
    seen.push_back(id);
  }
};
}  // namespace

TEST(TouchedClose, AnObserverThatThrowsMidCloseLeavesAListThatTheNextCloseCanUse) {
  for (EvidenceModel m : {EvidenceModel::Sprt, EvidenceModel::Legacy}) {
    const LayeredMapParams p = base(m, false, 1);
    auto window = [](LayeredMap& map) {
      for (CellId c = 0; c < 5; ++c) map.observeMiss(c);   // only misses: pruned after some closes
      map.observeHit(8);                                    // stays
      map.endWindow();
    };
    int prune_window = 0;   // the close at which cells 0..4 are pruned, found on a probe map
    {
      LayeredMap probe(p);
      for (int w = 1; w <= 200 && prune_window == 0; ++w) {
        window(probe);
        if (probe.cellCount() == 1u) prune_window = w;
      }
    }
    ASSERT_GT(prune_window, 1);
    LayeredMap map(p);
    ThrowAt obs; map.setWindowObserver(&obs);
    for (int w = 1; w < prune_window; ++w) window(map);
    ASSERT_EQ(map.cellCount(), 6u);
    // The pruning window. First contacts are 0..4, 8, then 5, 6, 7, 9 (calls 1..10). The observer
    // throws on call 8 (cell 6): cells 0..4 (erased), 8 and 5 are finished, 6, 7 and 9 are not.
    for (CellId c = 0; c < 5; ++c) map.observeMiss(c);
    map.observeHit(8);
    for (CellId c : {5, 6, 7, 9}) map.observeHit(c);
    obs.calls = 0; obs.throw_on = 8;
    EXPECT_THROW(map.endWindow(), std::runtime_error);
    obs.throw_on = -1;
    EXPECT_EQ(map.cellCount(), 5u) << "cells 0..4 are erased; 8, 5, 6, 7, 9 are held";
    EXPECT_EQ(map.observations(5), 1);
    EXPECT_EQ(map.observations(6), 0) << "the failing cell was not processed";
    // The next close must visit exactly the unfinished cells and read no erased cell.
    obs.seen.clear();
    map.endWindow();
    EXPECT_EQ(map.lastWindowTouched(), 3u);
    std::sort(obs.seen.begin(), obs.seen.end());
    EXPECT_EQ(obs.seen, (std::vector<CellId>{6, 7, 9}));
    for (CellId c = 0; c < 5; ++c) EXPECT_EQ(map.classify(c), CellClass::Unknown);
    for (CellId c : {5, 6, 7, 9}) EXPECT_EQ(map.observations(c), 1) << c;
    EXPECT_EQ(map.cellCount(), 5u);
    // The map keeps working, and a cell erased in the failed close is created again by a contact.
    map.observeMiss(7); map.observeHit(0);
    map.endWindow();
    EXPECT_EQ(map.lastWindowTouched(), 2u);
    EXPECT_EQ(map.observations(7), 2);
    EXPECT_EQ(map.observations(0), 1);
  }
}
