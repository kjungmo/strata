#pragma once
#include <cstddef>
#include <unordered_map>
#include <vector>
#include "prism_map_core/types.hpp"
#include "prism_map_core/periodicity.hpp"
namespace prism_map_core {
enum class CellClass { Unknown, Transient, Periodic, Static };
struct LayeredMapParams {
  int layer_interval{10};
  double l_hit{0.85}; double l_miss{-0.4}; double l_min{-5.0}; double l_max{5.0};
  double survival_decay{0.97};
  double graduate_prob{0.8}; double demote_prob{0.45};
  int min_observations{3}; double prune_prob{0.05};
  bool enable_periodicity{true}; double periodic_amplitude_min{0.3};
  PeriodicityParams periodicity{};
};
struct CellEvidence {
  double log_odds{0.0};
  int observations{0};
  int window_hits{0};
  int window_misses{0};
  bool graduated{false};
};
class LayeredMap {
 public:
  explicit LayeredMap(LayeredMapParams params);
  void observeHit(CellId id);
  void observeMiss(CellId id);
  bool tick();
  void endWindow();
  CellClass classify(CellId id) const;
  bool isStatic(CellId id) const;
  double occupancyProb(CellId id) const;
  double periodicProb(CellId id) const;
  std::vector<CellId> staticCells() const;
  std::vector<CellId> transientCells() const;
  std::vector<CellId> periodicCells() const;
  int windowCount() const { return window_count_; }
  int integrationCount() const { return integration_count_; }
  std::size_t cellCount() const { return cells_.size(); }
  const LayeredMapParams& params() const { return params_; }
 private:
  static double sigmoid(double x);
  LayeredMapParams params_;
  std::unordered_map<CellId, CellEvidence> cells_;
  PeriodicityModel periodicity_;
  int integration_count_{0};
  int window_count_{0};
};
}  // namespace prism_map_core
