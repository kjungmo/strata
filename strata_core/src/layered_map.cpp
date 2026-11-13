#include "strata_core/layered_map.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#ifdef STRATA_TEST_NOOP_OBSERVER
#include <cstdio>
#endif
namespace strata_core {

#ifdef STRATA_TEST_NOOP_OBSERVER
// Test-only build switch (off by default, never set by a normal build): every map starts
// with a counting observer that does nothing else. The experiment harness is rebuilt with
// it to show that its seeded CSV outputs stay byte-identical with an observer set; the
// count printed at exit shows that the observer really ran.
namespace {
struct CountingNoopObserver : WindowObserver {
  unsigned long long calls{0};
  void onTouchedCell(int, CellId, bool, bool) override { ++calls; }
  ~CountingNoopObserver() override {
    std::fprintf(stderr, "strata_core test no-op observer: %llu touched-cell reports\n", calls);
  }
};
CountingNoopObserver g_noop_observer;
}  // namespace
#endif

SprtDerived deriveSprt(const LayeredMapParams& p) {
  const auto fin = [](double v) { return std::isfinite(v); };
  if (!(fin(p.m0) && fin(p.m1) && p.m0 > 0.0 && p.m0 < p.m1 && p.m1 < 1.0))
    throw std::invalid_argument("sprt: need 0 < m0 < m1 < 1, got m0=" + std::to_string(p.m0) +
                                " m1=" + std::to_string(p.m1));
  if (!(fin(p.alpha) && fin(p.beta) && p.alpha > 0.0 && p.beta > 0.0 && p.alpha + p.beta < 1.0))
    throw std::invalid_argument("sprt: need alpha > 0, beta > 0 and alpha + beta < 1, got alpha=" +
                                std::to_string(p.alpha) + " beta=" + std::to_string(p.beta));
  if (p.n_max_undecided < 0)
    throw std::invalid_argument("sprt: n_max_undecided must be >= 0 (0 = never)");
  SprtDerived d;
  d.l_hit = std::log(p.m1 / p.m0);
  d.l_miss = std::log((1.0 - p.m1) / (1.0 - p.m0));
  d.A = std::log((1.0 - p.beta) / p.alpha);
  d.B = std::log(p.beta / (1.0 - p.alpha));
  if (std::isnan(p.sprt_ceiling)) {
    d.ceiling = d.A + 2.0 * d.l_hit;
  } else {
    if (!(fin(p.sprt_ceiling) && p.sprt_ceiling >= d.A))
      throw std::invalid_argument("sprt: sprt_ceiling must be finite and >= A=" + std::to_string(d.A));
    d.ceiling = p.sprt_ceiling;
  }
  d.demote_level = d.ceiling - d.A;
  d.m_star = -d.l_miss / (d.l_hit - d.l_miss);
  // A pair so close that a decision needs more than a million windows is a configuration error, and
  // the loops below would run for as long.
  constexpr double kMaxWindows = 1.0e6;
  if (!(d.A / d.l_hit <= kMaxWindows) || !(d.A / -d.l_miss <= kMaxWindows))
    throw std::invalid_argument("sprt: m0 and m1 are too close for these error rates (a decision would need over " +
                                std::to_string(static_cast<long>(kMaxWindows)) + " windows)");
  // Summed exactly as endWindowSprt() adds, so the numbers cannot differ by rounding.
  // (The bound on the loops is a second line of defence behind the check above.)
  const int kGuard = static_cast<int>(kMaxWindows) + 8;
  double l = 0.0;
  while (l < d.A) {
    if (d.latency > kGuard) throw std::invalid_argument("sprt: the statistic does not reach A");
    l += d.l_hit; if (l > d.ceiling) l = d.ceiling; ++d.latency;
  }
  l = d.ceiling;
  while (l > d.demote_level) {
    if (d.demote_misses > kGuard) throw std::invalid_argument("sprt: the reverse statistic does not reach its level");
    l += d.l_miss; ++d.demote_misses;
  }
  return d;
}

LayeredMap::LayeredMap(LayeredMapParams params)
  : params_(params), periodicity_(params.periodicity),
    pool_(PeriodicityKernel::slotDoubles(params.periodicity.n_harmonics)) {
  if (params_.evidence_model == EvidenceModel::Sprt) {
    sprt_ = deriveSprt(params_);
    use_sprt_ = true;
  }
#ifdef STRATA_TEST_NOOP_OBSERVER
  observer_ = &g_noop_observer;
#endif
}

double LayeredMap::sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

// First contact of a cell in the open window (both counters 0): put it on the touched list.
void LayeredMap::touch(Cell& cell) {
  if (cell.second.ev.window_hits == 0 && cell.second.ev.window_misses == 0) touched_.push_back(&cell);
}
void LayeredMap::observeHit(CellId id) {
  auto it = cells_.try_emplace(id).first;
  touch(*it);
  it->second.ev.window_hits++;
}
void LayeredMap::observeMiss(CellId id) {
  auto it = cells_.try_emplace(id).first;
  touch(*it);
  it->second.ev.window_misses++;
}

bool LayeredMap::tick() {
  ++integration_count_;
  if (params_.layer_interval <= 1 || integration_count_ % params_.layer_interval == 0) {
    endWindow();
    return true;
  }
  return false;
}

void LayeredMap::closeWindows(int k) {
  if (k <= 0) return;
  endWindow();
  window_count_ += k - 1;
}

// Only the cells the closing window touched are visited (touched_). Why the others can be skipped,
// for this rule (the Sprt rule is argued at endWindowSprt()): a cell the window did not touch
// has unchanged evidence, so the old full sweep recomputed, from the same values, what its last
// touched close had already settled:
//   - no decay (decay is inside the touched branch), so p = sigmoid(log_odds) is the same;
//   - amplitude() and isPeriodic() read only that cell's Fourier coefficients (no window index),
//     which change only in gather(), also inside the touched branch: the same amp and periodic;
//   - graduation is G' = g(G) with C = !periodic && p >= graduate_prob && observations >=
//     min_observations, G1 = G || C, G' = G1 && !(p <= demote_prob || periodic). Applying g to
//     its own output returns it: if G' is true, G1 is true and the demote test is false, so G''
//     is true; if G' is false, either the demote test is true (G'' false) or G1 is false, hence C
//     is false and G'' is false. This also holds when graduate_prob <= demote_prob;
//   - the prune test reads the same values it read at the last touched close, when it did not
//     prune, so it does not prune now.
// Every cell is created by observeHit/observeMiss, so it is touched in the window that creates it,
// and cells are erased only here, so a cell that exists and is untouched was settled by an earlier
// close. Per cell the arithmetic and its order are those of the full sweep; only the order of the
// cells differs, and cells do not read each other.
void LayeredMap::endWindow() {
  ++window_count_;
  last_window_touched_ = touched_.size();
  // cos and sin of the harmonics depend on the window index only: once per close, not once per cell.
  if (params_.enable_periodicity && !touched_.empty()) periodicity_.phases(window_count_, phase_);
  if (use_sprt_) { endWindowSprt(); return; }
  WindowObserver* const observer = observer_;
  // If the observer or an allocation throws, the cells before the failing one are done (counters reset,
  // some erased) and leave the list; the failing cell and the rest keep their counters and stay
  // listed, so the next close handles them and no entry dangles.
  std::size_t done = 0;
  try {
    for (; done < touched_.size(); ++done) {
      Cell* const cell = touched_[done];
      const CellId id = cell->first;
      CellState& st = cell->second;
      CellEvidence& e = st.ev;
      // First gather of this cell: take its slot before anything else changes, so that an allocation
      // failure leaves the cell exactly as it was (counters, evidence, no slot) and still listed.
      if (params_.enable_periodicity && st.slot == CoeffPool::kNone) st.slot = pool_.acquire();
      const bool occ = e.window_hits > 0;
      const bool freed = (!occ && e.window_misses > 0);
      if (observer) observer->onTouchedCell(window_count_, id, occ, freed);
      if (occ) e.log_odds += params_.l_hit;
      else if (freed) e.log_odds += params_.l_miss;
      e.log_odds *= params_.survival_decay;
      e.log_odds = std::min(params_.l_max, std::max(params_.l_min, e.log_odds));
      e.observations++;
      if (params_.enable_periodicity) periodicity_.gather(pool_.at(st.slot), occ, phase_);
      const double p = sigmoid(e.log_odds);
      const double amp = params_.enable_periodicity ? periodicity_.amplitude(pool_.at(st.slot)) : 0.0;
      // Periodic needs both an effect size (amp >= a_min) and noise-calibrated
      // significance (false-alarm probability <= periodic_false_alarm).
      const bool periodic = params_.enable_periodicity &&
          periodicity_.isPeriodic(pool_.at(st.slot), params_.periodic_amplitude_min,
                                  params_.periodic_false_alarm, params_.periodic_alpha_spending);
      // A strongly-periodic (semi-static) cell is predicted per phase, not baked permanently
      // Static; only non-periodic cells graduate to the persistent static map.
      if (!e.graduated && !periodic && p >= params_.graduate_prob &&
          e.observations >= params_.min_observations)
        e.graduated = true;
      // Demote on sustained free-space (Schmitt) OR once the cell reveals itself as
      // strongly periodic: a semi-static cell must not stay baked into the static map.
      if (e.graduated && (p <= params_.demote_prob || periodic))
        e.graduated = false;
      e.window_hits = 0; e.window_misses = 0;
      // The prune guard uses the lenient effect-size screen only: a candidate
      // periodic cell keeps its Fourier history (and so its growing n) until the
      // significance test resolves it, instead of restarting from n = 0.
      if (!e.graduated && p < params_.prune_prob && amp < params_.periodic_amplitude_min) {
        if (st.slot != CoeffPool::kNone) pool_.release(st.slot);
        cells_.erase(id);   // `cell`, `st` and `e` dangle from here; the loop does not use them again
      }
    }
  } catch (...) {
    touched_.erase(touched_.begin(), touched_.begin() + static_cast<std::ptrdiff_t>(done));
    throw;
  }
  touched_.clear();
}

// The sequential test. Per touched cell, in this order:
//   l += l_hit (hit window) or l_miss (miss window); no multiplier, so exp(l) is a
//   supermartingale under every hit rate <= m0 and Ville's inequality bounds the chance
//   of ever reaching A by exp(-A) = alpha/(1-beta), about alpha, per uninterrupted history (independent windows).
//   l is capped at the ceiling. Below the periodicity gate (n < T touched windows) there
//   is no lower clamp and no rejection, so a door that is closed early can still be told
//   from clutter once T windows are in; from n >= T on l is floored at B and a cell with
//   l <= B is rejected (erased, unless it is still a periodic candidate; a later hit
//   creates a new history).
//   A cell graduates at l >= A unless Periodic, and l restarts at the ceiling. A
//   graduated cell is demoted by the reverse test: l <= ceiling - A (the reverse
//   statistic from the ceiling crossed A), or Periodic. After a reverse-test demotion the
//   forward test restarts from l = 0 and from n = 0 (the count behind the periodicity gate,
//   the rejection and the n_max read-out is the length of the current test, not of the cell's life).
// Only the cells the closing window touched are visited (touched_). A cell the window did not touch
// has unchanged evidence and unchanged Fourier coefficients (both change only in the touched
// branch), so amplitude() and isPeriodic(), which read only that cell's coefficients, return what
// they returned at its last touched close. The tail of the step applied again to its own output
// is a fixed point: a graduated cell stays graduated (it was not periodic, and l > demote_level,
// or the demote branch would have fired at that close); one demoted because it is periodic stays ungraduated (still
// periodic); one demoted by the reverse test has l = 0 < A and n = 0 < gate, so it neither
// graduates nor is rejected; a cell that was not erased has the same rejected and amp values, so
// it is not erased now. Every cell is created by observeHit/observeMiss, hence touched in the
// window that creates it, and cells are erased only in a close. Per cell the arithmetic and its
// order are those of the full sweep; cells do not read each other.
void LayeredMap::endWindowSprt() {
  WindowObserver* const observer = observer_;
  const SprtDerived& d = sprt_;
  const int gate = std::max(1, params_.periodicity.period_windows);
  // If the observer or an allocation throws, the cells before the failing one are done (counters reset,
  // some erased) and leave the list; the failing cell and the rest keep their counters and stay
  // listed, so the next close handles them and no entry dangles.
  std::size_t done = 0;
  try {
    for (; done < touched_.size(); ++done) {
      Cell* const cell = touched_[done];
      const CellId id = cell->first;
      CellState& st = cell->second;
      CellEvidence& e = st.ev;
      // First gather of this cell: take its slot before anything else changes, so that an allocation
      // failure leaves the cell exactly as it was (counters, evidence, no slot) and still listed.
      if (params_.enable_periodicity && st.slot == CoeffPool::kNone) st.slot = pool_.acquire();
      const bool occ = e.window_hits > 0;
      const bool freed = (!occ && e.window_misses > 0);
      if (observer) observer->onTouchedCell(window_count_, id, occ, freed);
      e.log_odds += occ ? d.l_hit : d.l_miss;
      if (e.log_odds > d.ceiling) e.log_odds = d.ceiling;
      e.observations++;
      if (e.observations >= gate && e.log_odds < d.B) e.log_odds = d.B;
      if (params_.enable_periodicity) periodicity_.gather(pool_.at(st.slot), occ, phase_);
      const double amp = params_.enable_periodicity ? periodicity_.amplitude(pool_.at(st.slot)) : 0.0;
      const bool periodic = params_.enable_periodicity &&
          periodicity_.isPeriodic(pool_.at(st.slot), params_.periodic_amplitude_min,
                                  params_.periodic_false_alarm, params_.periodic_alpha_spending);
      if (!e.graduated && !periodic && e.log_odds >= d.A) {
        e.graduated = true;
        e.log_odds = d.ceiling;
      }
      if (e.graduated) {
        if (periodic) e.graduated = false;
        else if (e.log_odds <= d.demote_level) {
          e.graduated = false; e.log_odds = 0.0;
          e.observations = 0;                                         // the restarted test counts its own windows
        }
      }
      e.window_hits = 0; e.window_misses = 0;
      const bool rejected = !e.graduated && e.observations >= gate && e.log_odds <= d.B;
      if (rejected && amp < params_.periodic_amplitude_min) {
        if (st.slot != CoeffPool::kNone) pool_.release(st.slot);
        cells_.erase(id);   // `cell`, `st` and `e` dangle from here; the loop does not use them again
      }
    }
  } catch (...) {
    touched_.erase(touched_.begin(), touched_.begin() + static_cast<std::ptrdiff_t>(done));
    throw;
  }
  touched_.clear();
}

const double* LayeredMap::coeffOf(const CellState& st) const {
  return st.slot == CoeffPool::kNone ? nullptr : pool_.at(st.slot);
}

int LayeredMap::observations(CellId id) const {
  auto it = cells_.find(id);
  return it == cells_.end() ? 0 : it->second.ev.observations;
}

double LayeredMap::occupancyProb(CellId id) const {
  auto it = cells_.find(id);
  return it == cells_.end() ? 0.0 : sigmoid(it->second.ev.log_odds);
}

double LayeredMap::periodicProb(CellId id) const {
  auto it = cells_.find(id);
  return it == cells_.end() ? 0.5 : periodicity_.predict(coeffOf(it->second), window_count_);
}

CellClass LayeredMap::classify(CellId id) const {
  auto it = cells_.find(id);
  if (it == cells_.end()) return CellClass::Unknown;
  if (it->second.ev.graduated) return CellClass::Static;
  if (params_.enable_periodicity &&
      periodicity_.isPeriodic(coeffOf(it->second), params_.periodic_amplitude_min, params_.periodic_false_alarm,
                              params_.periodic_alpha_spending))
    return CellClass::Periodic;
  if (use_sprt_) {
    const int n = it->second.ev.observations;
    if (n >= std::max(1, params_.periodicity.period_windows) && it->second.ev.log_odds <= sprt_.B)
      return CellClass::Unknown;                                    // rejected, kept only as a periodic candidate
    if (params_.n_max_undecided > 0 && n >= params_.n_max_undecided) return CellClass::Intermittent;
    return CellClass::Transient;
  }
  if (sigmoid(it->second.ev.log_odds) >= params_.prune_prob) return CellClass::Transient;
  return CellClass::Unknown;
}

bool LayeredMap::isStatic(CellId id) const {
  auto it = cells_.find(id);
  return it != cells_.end() && it->second.ev.graduated;
}

std::vector<CellId> LayeredMap::staticCells() const {
  std::vector<CellId> o; for (auto& kv : cells_) if (kv.second.ev.graduated) o.push_back(kv.first); return o;
}
std::vector<CellId> LayeredMap::transientCells() const {
  std::vector<CellId> o;
  for (auto& kv : cells_) if (classify(kv.first) == CellClass::Transient) o.push_back(kv.first);
  return o;
}
std::vector<CellId> LayeredMap::intermittentCells() const {
  std::vector<CellId> o;
  for (auto& kv : cells_) if (classify(kv.first) == CellClass::Intermittent) o.push_back(kv.first);
  return o;
}
std::vector<CellId> LayeredMap::periodicCells() const {
  std::vector<CellId> o;
  for (auto& kv : cells_) if (classify(kv.first) == CellClass::Periodic) o.push_back(kv.first);
  return o;
}
}  // namespace strata_core
