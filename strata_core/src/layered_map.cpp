#include "strata_core/layered_map.hpp"
#include <algorithm>
#include <cmath>
namespace strata_core {

LayeredMap::LayeredMap(LayeredMapParams params)
  : params_(params), periodicity_(params.periodicity) {}

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

void LayeredMap::endWindow() {
  ++window_count_;
  for (auto it = cells_.begin(); it != cells_.end();) {
    CellEvidence& e = it->second;
    const bool touched = (e.window_hits > 0 || e.window_misses > 0);
    const bool occ = e.window_hits > 0;
    const bool freed = (!occ && e.window_misses > 0);
    if (touched) {
      if (occ) e.log_odds += params_.l_hit;
      else if (freed) e.log_odds += params_.l_miss;
      e.log_odds *= params_.survival_decay;
      e.log_odds = std::min(params_.l_max, std::max(params_.l_min, e.log_odds));
      e.observations++;
      if (params_.enable_periodicity) periodicity_.gather(it->first, occ, window_count_);
    }
    const double p = sigmoid(e.log_odds);
    const double amp = params_.enable_periodicity ? periodicity_.amplitude(it->first) : 0.0;
    const bool periodic = params_.enable_periodicity && amp >= params_.periodic_amplitude_min;
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
    if (!e.graduated && p < params_.prune_prob && amp < params_.periodic_amplitude_min) {
      periodicity_.erase(it->first);
      it = cells_.erase(it);
    } else {
      ++it;
    }
  }
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
      periodicity_.amplitude(id) >= params_.periodic_amplitude_min) return CellClass::Periodic;
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
std::vector<CellId> LayeredMap::periodicCells() const {
  std::vector<CellId> o;
  for (auto& kv : cells_) if (classify(kv.first) == CellClass::Periodic) o.push_back(kv.first);
  return o;
}
}  // namespace strata_core
