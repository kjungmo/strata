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
  // Summed exactly as endWindowSprt() adds, so the numbers cannot differ by rounding.
  double l = 0.0;
  while (l < d.A) { l += d.l_hit; if (l > d.ceiling) l = d.ceiling; ++d.latency; }
  l = d.ceiling;
  while (l > d.demote_level) { l += d.l_miss; ++d.demote_misses; }
  return d;
}

LayeredMap::LayeredMap(LayeredMapParams params)
  : params_(params), periodicity_(params.periodicity) {
  if (params_.evidence_model == EvidenceModel::Sprt) {
    sprt_ = deriveSprt(params_);
    use_sprt_ = true;
  }
#ifdef STRATA_TEST_NOOP_OBSERVER
  observer_ = &g_noop_observer;
#endif
}

double LayeredMap::sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

void LayeredMap::observeHit(CellId id)  { cells_[id].window_hits++; }
void LayeredMap::observeMiss(CellId id) { cells_[id].window_misses++; }

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

void LayeredMap::endWindow() {
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
    // Periodic needs both an effect size (amp >= a_min) and noise-calibrated
    // significance (false-alarm probability <= periodic_false_alarm).
    const bool periodic = params_.enable_periodicity &&
        periodicity_.isPeriodic(it->first, params_.periodic_amplitude_min,
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
      periodicity_.erase(it->first);
      it = cells_.erase(it);
    } else {
      ++it;
    }
  }
}

// The sequential test. Per touched cell, in this order:
//   l += l_hit (hit window) or l_miss (miss window); no multiplier, so exp(l) is a
//   supermartingale under every hit rate <= m0 and Ville's inequality bounds the chance
//   of ever reaching A by exp(-A) per uninterrupted history (independent windows).
//   l is capped at the ceiling. Below the periodicity gate (n < T touched windows) there
//   is no lower clamp and no rejection, so a door that is closed early can still be told
//   from clutter once T windows are in; from n >= T on l is floored at B and a cell with
//   l <= B is rejected (erased, unless it is still a periodic candidate; a later hit
//   creates a new history).
//   A cell graduates at l >= A unless Periodic, and l restarts at the ceiling. A
//   graduated cell is demoted by the reverse test: l <= ceiling - A (the reverse
//   statistic from the ceiling crossed A), or Periodic. After a reverse-test demotion the
//   forward test restarts from l = 0.
void LayeredMap::endWindowSprt() {
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
    if (!e.graduated && !periodic && e.log_odds >= d.A) {
      e.graduated = true;
      e.log_odds = d.ceiling;
    }
    if (e.graduated) {
      if (periodic) e.graduated = false;
      else if (e.log_odds <= d.demote_level) { e.graduated = false; e.log_odds = 0.0; }
    }
    e.window_hits = 0; e.window_misses = 0;
    const bool rejected = !e.graduated && e.observations >= gate && e.log_odds <= d.B;
    if (rejected && amp < params_.periodic_amplitude_min) {
      periodicity_.erase(it->first);
      it = cells_.erase(it);
    } else {
      ++it;
    }
  }
}

int LayeredMap::observations(CellId id) const {
  auto it = cells_.find(id);
  return it == cells_.end() ? 0 : it->second.observations;
}

double LayeredMap::occupancyProb(CellId id) const {
  auto it = cells_.find(id);
  return it == cells_.end() ? 0.0 : sigmoid(it->second.log_odds);
}

double LayeredMap::periodicProb(CellId id) const {
  return periodicity_.predict(id, window_count_);
}

CellClass LayeredMap::classify(CellId id) const {
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
      return CellClass::Unknown;                                    // rejected, kept only as a periodic candidate
    if (params_.n_max_undecided > 0 && n >= params_.n_max_undecided) return CellClass::Intermittent;
    return CellClass::Transient;
  }
  if (sigmoid(it->second.log_odds) >= params_.prune_prob) return CellClass::Transient;
  return CellClass::Unknown;
}

bool LayeredMap::isStatic(CellId id) const {
  auto it = cells_.find(id);
  return it != cells_.end() && it->second.graduated;
}

std::vector<CellId> LayeredMap::staticCells() const {
  std::vector<CellId> o; for (auto& kv : cells_) if (kv.second.graduated) o.push_back(kv.first); return o;
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
