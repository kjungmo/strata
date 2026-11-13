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

using namespace strata_core;

namespace {


// ------------------------------------------------------------------ frozen periodicity ----------
// Verbatim copy of PeriodicityModel as it was at 15f96e4: one std::vector<double> per coefficient and
// its own hash map. It is the oracle's periodicity model, so the oracle does not depend on the code
// under test. Comments are shortened; every expression and the order of every sum is the old one.
class FrozenPeriodicity {
 public:
  explicit FrozenPeriodicity(PeriodicityParams p) : params_(p),
    omega_(2.0 * M_PI / std::max(1, p.period_windows)) {}
  void gather(CellId id, bool occupied, int window_index);
  double predict(CellId id, int window_index) const;
  double amplitude(CellId id) const;
  double falseAlarm(CellId id) const;
  HarmonicStat harmonic(CellId id, int k) const;
  static double tailBound(double dchi);
  static double spentLevel(double alpha, double n, int period_windows);
  bool isPeriodic(CellId id, double a_min, double alpha, bool spend = false) const;
  bool has(CellId id) const { return cells_.count(id) > 0; }
  void erase(CellId id) { cells_.erase(id); }
  std::size_t size() const { return cells_.size(); }
 private:
  struct Coeff {
    double n{0}; double s0{0};
    std::vector<double> c; std::vector<double> s;
    std::vector<double> ec; std::vector<double> es;
    std::vector<double> ec2; std::vector<double> es2;
  };
  void centred(const Coeff& cell, int k, double& ak, double& bk) const;
  double harmonicAmplitude(const Coeff& cell, int k) const;
  HarmonicStat stat(const Coeff& cell, int k) const;
  bool gated(const Coeff& cell) const;
  PeriodicityParams params_;
  double omega_;
  std::unordered_map<CellId, Coeff> cells_;
};

inline void FrozenPeriodicity::gather(CellId id, bool occupied, int window_index) {
  auto& cell = cells_[id];
  if (cell.c.empty()) {
    const auto H = static_cast<std::size_t>(std::max(0, params_.n_harmonics));
    cell.c.assign(H, 0.0); cell.s.assign(H, 0.0);
    cell.ec.assign(H, 0.0); cell.es.assign(H, 0.0);
    cell.ec2.assign(H, 0.0); cell.es2.assign(H, 0.0);
  }
  const double v = occupied ? 1.0 : 0.0;
  cell.n += 1.0; cell.s0 += v;
  for (int k = 0; k < params_.n_harmonics; ++k) {
    const double a = (k + 1) * omega_ * window_index;
    const double ca = std::cos(a), sa = std::sin(a);
    cell.c[k] += v * ca;  cell.s[k] += v * sa;   // sum of o_w e^{i(k+1)wt}
    cell.ec[k] += ca;     cell.es[k] += sa;      // sum of e^{i(k+1)wt} over touched windows
    cell.ec2[k] += std::cos(2.0 * a); cell.es2[k] += std::sin(2.0 * a);   // doubled phase
  }
}

// Centred (mean-removed) FreMEn coefficient, as in Krajnik et al. (T-RO 2017):
//   gamma_k = (1/n) sum_w (o_w - mean) e^{i(k+1) omega w}
//           = (1/n) [ sum_w o_w e^{...} - mean * sum_w e^{...} ].
// Subtracting the mean times the phase sum of the *touched* windows makes the
// coefficient exactly zero for any constant cell under any sampling pattern.
inline void FrozenPeriodicity::centred(const Coeff& cell, int k, double& ak, double& bk) const {
  const double mean = cell.s0 / cell.n;
  ak = 2.0 * (cell.c[k] - mean * cell.ec[k]) / cell.n;
  bk = 2.0 * (cell.s[k] - mean * cell.es[k]) / cell.n;
}

inline bool FrozenPeriodicity::gated(const Coeff& cell) const {
  // A harmonic estimate needs at least one period's worth of touched windows.
  return cell.n <= 0.0 || cell.n < static_cast<double>(params_.period_windows);
}

inline double FrozenPeriodicity::harmonicAmplitude(const Coeff& cell, int k) const {
  double ak, bk; centred(cell, k, ak, bk);
  const double amp = std::sqrt(ak * ak + bk * bk);
  // A constant cell cancels to rounding error, not exactly zero; report 0.
  return amp < 1e-12 ? 0.0 : amp;
}

// Noise-calibrated test quantities of harmonic k.
// Least-squares fit of o_w = mu + A cos(theta_w) + B sin(theta_w) over the touched
// windows (the floating-mean periodogram of Zechmeister & Kuerster 2009). With the
// centred phase sums y = sum (o - mean) (cos, sin) and the centred phase-design
// matrix M = sum (cos - cbar, sin - sbar)(cos - cbar, sin - sbar)^T, the variance
// explained by the harmonic is dchi = y^T M^{-1} y. Because the centred phases sum
// to zero, y = sum (o_w - m) (cos - cbar, sin - sbar) for ANY constant m, so under
// an iid Bernoulli(m) null dchi is a quadratic form in the independent, 1/4-sub-
// Gaussian variables o_w - m, whatever m is and whatever the sampling pattern.
inline HarmonicStat FrozenPeriodicity::stat(const Coeff& cell, int k) const {
  HarmonicStat h;
  h.n = cell.n;
  h.amplitude = harmonicAmplitude(cell, k);
  const double n = cell.n;
  const double mean = cell.s0 / n;
  h.chi0 = n * mean * (1.0 - mean);
  if (h.chi0 <= 1e-12) return h;                       // constant cell: nothing to explain
  const double cb = cell.ec[k] / n, sb = cell.es[k] / n;
  const double scc = 0.5 * (n + cell.ec2[k]) - n * cb * cb;
  const double sss = 0.5 * (n - cell.ec2[k]) - n * sb * sb;
  const double scs = 0.5 * cell.es2[k] - n * cb * sb;
  const double yc = cell.c[k] - mean * cell.ec[k];
  const double ys = cell.s[k] - mean * cell.es[k];
  const double tr = scc + sss;
  if (tr <= 1e-9 * n) return h;                        // all touches at one phase
  const double det = scc * sss - scs * scs;
  if (det <= 1e-9 * tr * tr) {
    // Rank-1 phase design (e.g. the Nyquist harmonic, or two phases only): project
    // onto the one identifiable direction. The rank-2 bound stays valid (it is
    // larger than the rank-1 one), so this is conservative.
    h.dchi = (yc * yc + ys * ys) / tr;
  } else {
    h.dchi = (sss * yc * yc - 2.0 * scs * yc * ys + scc * ys * ys) / det;
  }
  h.dchi = std::min(h.dchi, h.chi0);                   // guard rounding
  return h;
}

// Chernoff tail bound for dchi under the iid-Bernoulli null (proof in the paper,
// Proposition "calibrated test"): whitening gives dchi = |w|^2 with w a sum of
// independent terms (o_w - m) b_w, sum b_w b_w^T = I_2, and o_w - m is 1/4-sub-
// Gaussian (Hoeffding's lemma), so E exp(lambda |w|^2) <= (1 - lambda/2)^{-1}
// (Gaussian decoupling). Optimising Chernoff over lambda gives, with x = 2 dchi,
//   P(dchi >= r) <= x e^{1 - x}   for x > 1,
// for ANY m and ANY (non-adaptive) set of touched phases.
inline double FrozenPeriodicity::tailBound(double dchi) {
  const double x = 2.0 * dchi;
  if (!(x > 1.0)) return 1.0;
  return std::min(1.0, x * std::exp(1.0 - x));
}

inline HarmonicStat FrozenPeriodicity::harmonic(CellId id, int k) const {
  auto it = cells_.find(id);
  if (it == cells_.end() || it->second.n <= 0.0 || k < 0 || k >= params_.n_harmonics)
    return HarmonicStat{};
  return stat(it->second, k);
}

inline double FrozenPeriodicity::predict(CellId id, int window_index) const {
  auto it = cells_.find(id);
  if (it == cells_.end() || it->second.n <= 0.0) return 0.5;
  const auto& cell = it->second;
  double val = cell.s0 / cell.n;
  for (int k = 0; k < params_.n_harmonics; ++k) {
    double ak, bk; centred(cell, k, ak, bk);
    const double a = (k + 1) * omega_ * window_index;
    val += ak * std::cos(a) + bk * std::sin(a);
  }
  return std::min(1.0, std::max(0.0, val));
}

inline double FrozenPeriodicity::amplitude(CellId id) const {
  auto it = cells_.find(id);
  if (it == cells_.end() || gated(it->second)) return 0.0;
  double amp = 0.0;
  for (int k = 0; k < params_.n_harmonics; ++k)
    amp = std::max(amp, harmonicAmplitude(it->second, k));
  return amp;
}

inline double FrozenPeriodicity::falseAlarm(CellId id) const {
  auto it = cells_.find(id);
  if (it == cells_.end() || gated(it->second)) return 1.0;
  double best = 1.0;
  for (int k = 0; k < params_.n_harmonics; ++k)
    best = std::min(best, tailBound(stat(it->second, k).dchi));
  return std::min(1.0, best * std::max(1, params_.n_harmonics));
}

inline double FrozenPeriodicity::spentLevel(double alpha, double n, int period_windows) {
  const double T = std::max(1, period_windows);
  if (n < T) return 0.0;
  return alpha * T / (n * (n + 1.0));
}

inline bool FrozenPeriodicity::isPeriodic(CellId id, double a_min, double alpha, bool spend) const {
  auto it = cells_.find(id);
  if (it == cells_.end() || gated(it->second)) return false;
  const double H = std::max(1, params_.n_harmonics);
  const double level = spend ? spentLevel(alpha, it->second.n, params_.period_windows) : alpha;
  for (int k = 0; k < params_.n_harmonics; ++k) {
    const double a = harmonicAmplitude(it->second, k);
    if (a <= 0.0 || a < a_min) continue;               // effect-size screen
    if (alpha >= 1.0) return true;                     // significance test disabled
    if (H * tailBound(stat(it->second, k).dchi) <= level) return true;
  }
  return false;
}

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

struct Rng {   // raw mt19937_64 output only, so the sequences do not depend on the standard library
  std::uint64_t s;
  explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
  std::uint64_t next() {   // splitmix64
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  double u() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
  int below(int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
};

using Event = std::tuple<int, CellId, bool, bool>;
struct Rec : WindowObserver {
  std::vector<Event> ev;
  void onTouchedCell(int w, CellId id, bool hit, bool miss) override { ev.emplace_back(w, id, hit, miss); }
};

bool sameBits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

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

LayeredMapParams base(EvidenceModel m, bool periodicity, int layer_interval) {
  LayeredMapParams p;
  p.evidence_model = m;
  p.enable_periodicity = periodicity;
  p.layer_interval = layer_interval;
  p.periodicity.period_windows = 8;
  return p;
}

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
  // Harmonic counts around the compile-time cap: none, one, three, the cap. -1 was accepted before
  // the cap existed and behaves as 0.
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

// ------------------------------------------------------------- per-cell memory (kernel, pool) ----------
// The periodicity arithmetic is a stateless kernel working on a slot of 2 + 6 H doubles; LayeredMap
// keeps the slots in a pool, PeriodicityModel in one vector per id. All paths (frozen old model,
// wrapper, kernel on a bare slot) must agree to the bit, for any n_harmonics that was accepted before.

TEST(CellMemory, KernelWrapperAndFrozenModelAgreeToTheBit) {
  for (int H : {-1, 0, 1, 2, 3, 4, 5, 8}) {
    for (int T : {1, 8, 24}) {
      const PeriodicityParams pp{T, H};
      FrozenPeriodicity frozen(pp);
      PeriodicityModel wrapper(pp);
      const PeriodicityKernel kernel(pp);
      std::vector<double> slot(kernel.slotDoubles(), 0.0);   // all zero: "no entry yet"
      const double* const zero = slot.data();
      std::vector<double> slot2(kernel.slotDoubles(), 0.0);   // the same history through a per-window phase table
      PeriodicityKernel::PhaseTable table;
      const CellId id = 77;
      Rng rng(1000 + H * 31 + T);
      auto agree = [&](const double* sl, int step) {
        const std::string tag = "H=" + std::to_string(H) + " T=" + std::to_string(T) + " step " + std::to_string(step);
        EXPECT_TRUE(sameBits(frozen.amplitude(id), wrapper.amplitude(id))) << tag;
        EXPECT_TRUE(sameBits(frozen.amplitude(id), kernel.amplitude(sl))) << tag;
        EXPECT_TRUE(sameBits(frozen.falseAlarm(id), wrapper.falseAlarm(id))) << tag;
        EXPECT_TRUE(sameBits(frozen.falseAlarm(id), kernel.falseAlarm(sl))) << tag;
        for (int w : {0, 1, 7, 8, 100, 12345}) {
          EXPECT_TRUE(sameBits(frozen.predict(id, w), wrapper.predict(id, w))) << tag;
          EXPECT_TRUE(sameBits(frozen.predict(id, w), kernel.predict(sl, w))) << tag;
        }
        for (int k = -1; k <= std::max(H, 0) + 1; ++k) {
          const HarmonicStat a = frozen.harmonic(id, k), b = wrapper.harmonic(id, k), c = kernel.harmonic(sl, k);
          for (const HarmonicStat* o : {&b, &c}) {
            EXPECT_TRUE(sameBits(a.n, o->n) && sameBits(a.amplitude, o->amplitude) &&
                        sameBits(a.dchi, o->dchi) && sameBits(a.chi0, o->chi0)) << tag << " k=" << k;
          }
        }
        for (double a_min : {0.0, 0.3, 0.9})
          for (double alpha : {0.01, 0.2, 1.0})
            for (bool spend : {false, true}) {
              const bool x = frozen.isPeriodic(id, a_min, alpha, spend);
              EXPECT_EQ(x, wrapper.isPeriodic(id, a_min, alpha, spend)) << tag;
              EXPECT_EQ(x, kernel.isPeriodic(sl, a_min, alpha, spend)) << tag;
            }
      };
      agree(nullptr, -2);                         // no entry: a null slot
      agree(zero, -1);                            // and an all-zero slot
      EXPECT_FALSE(wrapper.has(id));
      int w = 0;
      for (int step = 0; step < 120; ++step) {
        w += 1 + (rng.below(5) == 0 ? rng.below(6) : 0);
        const bool occ = (step % 16) < 7 ? rng.u() < 0.9 : rng.u() < 0.2;
        frozen.gather(id, occ, w);
        wrapper.gather(id, occ, w);
        kernel.gather(slot.data(), occ, w);
        kernel.phases(w, table);
        kernel.gather(slot2.data(), occ, table);
        ASSERT_EQ(std::memcmp(slot.data(), slot2.data(), slot.size() * sizeof(double)), 0)
            << "phase table path differs, H=" << H << " T=" << T << " step " << step;
        if (step < 12 || step % 7 == 0) agree(slot.data(), step);
      }
      EXPECT_TRUE(wrapper.has(id));
      EXPECT_EQ(wrapper.size(), 1u);
      wrapper.erase(id);
      EXPECT_FALSE(wrapper.has(id));
    }
  }
}

TEST(CellMemory, AMissingOrZeroSlotAnswersLikeACellWithoutAnEntry) {
  for (int H : {0, 2, 8}) {
    const PeriodicityParams pp{8, H};
    FrozenPeriodicity frozen(pp);
    const PeriodicityKernel kernel(pp);
    const std::vector<double> zero(kernel.slotDoubles(), 0.0);
    for (const double* sl : {static_cast<const double*>(nullptr), zero.data()}) {
      EXPECT_EQ(kernel.amplitude(sl), 0.0);
      EXPECT_FALSE(kernel.isPeriodic(sl, 0.0, 1.0));
      EXPECT_FALSE(kernel.isPeriodic(sl, 0.0, 0.2, true));
      EXPECT_EQ(kernel.falseAlarm(sl), 1.0);
      EXPECT_EQ(kernel.predict(sl, 5), 0.5);
      const HarmonicStat h = kernel.harmonic(sl, 0);
      EXPECT_EQ(h.n, 0.0); EXPECT_EQ(h.amplitude, 0.0); EXPECT_EQ(h.dchi, 0.0); EXPECT_EQ(h.chi0, 0.0);
    }
    EXPECT_EQ(frozen.amplitude(5), 0.0);
    EXPECT_FALSE(frozen.isPeriodic(5, 0.0, 1.0));
    EXPECT_EQ(frozen.falseAlarm(5), 1.0);
    EXPECT_EQ(frozen.predict(5, 5), 0.5);
  }
}

TEST(CellMemory, TheSlotPoolReusesZeroesAndGrowsInBlocks) {
  CoeffPool pool(14);
  EXPECT_EQ(pool.inUse(), 0u);
  std::vector<std::uint32_t> ids;
  for (int i = 0; i < 3 * static_cast<int>(CoeffPool::kBlockSlots) + 5; ++i) ids.push_back(pool.acquire());
  const double* first = pool.at(ids[0]);
  for (std::size_t i = 0; i < ids.size(); ++i) ASSERT_EQ(ids[i], i);   // fresh slots in order
  EXPECT_EQ(pool.inUse(), ids.size());
  EXPECT_EQ(pool.at(ids[0]), first) << "growth must not move an existing slot";
  for (std::uint32_t id : ids) for (std::size_t d = 0; d < 14; ++d) ASSERT_EQ(pool.at(id)[d], 0.0);
  for (std::uint32_t id : ids) std::fill(pool.at(id), pool.at(id) + 14, 7.0);   // dirty every slot
  pool.release(ids[10]); pool.release(ids[4000]);
  EXPECT_EQ(pool.inUse(), ids.size() - 2);
  const std::uint32_t a = pool.acquire(), b = pool.acquire();
  EXPECT_TRUE((a == ids[4000] && b == ids[10]));
  for (std::uint32_t id : {a, b}) for (std::size_t d = 0; d < 14; ++d) EXPECT_EQ(pool.at(id)[d], 0.0) << "a reused slot is zeroed";
  EXPECT_EQ(pool.inUse(), ids.size());
  EXPECT_EQ(pool.acquire(), ids.size()) << "after the free list is empty, the next fresh slot";
  // Moving the pool keeps the slots where they are.
  const double* p5 = pool.at(5);
  CoeffPool moved(std::move(pool));
  EXPECT_EQ(moved.at(5), p5);
}

TEST(CellMemory, TheMapHoldsASlotExactlyForTheCellsGatheredAndNotErased) {
  for (EvidenceModel m : {EvidenceModel::Sprt, EvidenceModel::Legacy}) {
    LayeredMapParams p = base(m, true, 1);
    LayeredMap map(p);
    EXPECT_EQ(map.coeffSlotsInUse(), 0u);
    map.observeHit(1); map.observeMiss(2);
    EXPECT_EQ(map.coeffSlotsInUse(), 0u) << "integrate allocates nothing but the small node";
    map.endWindow();
    EXPECT_EQ(map.coeffSlotsInUse(), 2u);
    // misses only: pruned after some closes, the slot goes with the cell
    map.observeHit(1);
    for (int w = 0; w < 200 && map.cellCount() > 1; ++w) { map.observeHit(1); map.observeMiss(2); map.endWindow(); }
    ASSERT_EQ(map.cellCount(), 1u);
    EXPECT_EQ(map.coeffSlotsInUse(), 1u) << "an erased cell releases its slot";
    map.observeMiss(2); map.endWindow();   // created again: a slot again, from zeros
    EXPECT_EQ(map.cellCount(), 2u);
    EXPECT_EQ(map.coeffSlotsInUse(), 2u);
    EXPECT_EQ(map.observations(2), 1);
    // periodicity off: never a slot
    LayeredMapParams q = base(m, false, 1);
    LayeredMap off(q);
    for (CellId c = 0; c < 100; ++c) off.observeHit(c);
    off.endWindow(); off.endWindow();
    EXPECT_EQ(off.coeffSlotsInUse(), 0u);
  }
}

TEST(CellMemory, TheNodeAndTheSlotHaveStatedSizes) {
  // Small node: key 8 + 24 = 32 bytes of payload. The 24 are the fields of CellEvidence and the
  // slot index (31 bits, sharing a word with `graduated`). The libstdc++ node adds the 8-byte link,
  // which is 40 bytes in one 48-byte malloc chunk: the same chunk as before the pool.
  // The slot of the default n_harmonics = 2 is 2 + 6 * 2 = 14 doubles = 112 bytes.
  static_assert(sizeof(double) == 8 && sizeof(CellId) == 8, "the sizes below assume LP64 doubles");
  EXPECT_EQ(sizeof(CellEvidence), 24u);
  EXPECT_EQ(LayeredMap::kCellPayloadBytes, 32u);   // pair<const CellId, CellState>; CellState is 24
  EXPECT_EQ(LayeredMap::kCellPayloadBytes - sizeof(CellId), 24u);
  EXPECT_EQ(LayeredMap(LayeredMapParams{}).coeffSlotBytes(), 112u);
  for (int h : {-3, 0, 1, 5, 8}) {
    LayeredMapParams p; p.periodicity.n_harmonics = h;
    EXPECT_EQ(LayeredMap(p).coeffSlotBytes(), (2u + 6u * static_cast<unsigned>(std::max(h, 0))) * 8u) << h;
  }
}

TEST(CellMemory, TheConstantCellShortcutGivesTheBitsOfTheFullComputation) {
  // FrozenPeriodicity has no shortcut: it is the full computation.
  for (int H : {-1, 0, 1, 2, 3, 5}) {
    for (int T : {1, 4, 8, 24}) {
      const PeriodicityParams pp{T, H};
      const PeriodicityKernel kernel(pp);
      for (int mode = 0; mode < 4; ++mode) {   // all hit, all miss, constant then one flip, mixed
        FrozenPeriodicity frozen(pp);
        std::vector<double> slot(kernel.slotDoubles(), 0.0);
        Rng rng(500 + H * 7 + T + mode);
        int w = 0;
        for (int step = 0; step < 90; ++step) {
          w += 1 + rng.below(3);
          bool occ = mode == 0 ? true : mode == 1 ? false : mode == 2 ? (step != 40 + (T % 3)) : rng.u() < 0.5;
          if (mode == 3 && step < 30) occ = true;   // constant for a while, then mixed
          frozen.gather(7, occ, w);
          kernel.gather(slot.data(), occ, w);
          const std::string tag = "H=" + std::to_string(H) + " T=" + std::to_string(T) + " mode " +
                                  std::to_string(mode) + " step " + std::to_string(step);
          EXPECT_TRUE(sameBits(frozen.amplitude(7), kernel.amplitude(slot.data()))) << tag;
          EXPECT_TRUE(sameBits(frozen.falseAlarm(7), kernel.falseAlarm(slot.data()))) << tag;
          EXPECT_TRUE(sameBits(frozen.predict(7, w + 1), kernel.predict(slot.data(), w + 1))) << tag;
          for (int k = 0; k < std::max(H, 0); ++k) {
            const HarmonicStat a = frozen.harmonic(7, k), b = kernel.harmonic(slot.data(), k);
            EXPECT_TRUE(sameBits(a.dchi, b.dchi) && sameBits(a.amplitude, b.amplitude)) << tag;
          }
          for (double a_min : {-1.0, -0.0, 0.0, 1e-300, 0.3, 2.0})
            for (double alpha : {0.0, 0.01, 0.2, 0.999, 1.0, 3.0})
              for (bool spend : {false, true})
                EXPECT_EQ(frozen.isPeriodic(7, a_min, alpha, spend), kernel.isPeriodic(slot.data(), a_min, alpha, spend)) << tag;
        }
      }
    }
  }
}
