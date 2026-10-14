#pragma once
#include <cstddef>
#include <unordered_map>
#include <vector>
#include "strata_core/types.hpp"
#include "strata_core/periodicity.hpp"
namespace strata_core {
enum class CellClass { Unknown, Transient, Periodic, Static };
struct LayeredMapParams {
  int layer_interval{10};
  double l_hit{0.85}; double l_miss{-0.4}; double l_min{-5.0}; double l_max{5.0};
  double survival_decay{0.97};
  double graduate_prob{0.8}; double demote_prob{0.45};
  int min_observations{3}; double prune_prob{0.05};
  bool enable_periodicity{true}; double periodic_amplitude_min{0.3};
  // Nominal level alpha of the periodic significance test: a Chernoff bound
  // (Bonferroni over n_harmonics) bounds the false-alarm probability of one read-out
  // of an iid-Bernoulli cell by alpha. The map re-tests every window, so the shipped
  // rule spends alpha over the touch count (periodic_alpha_spending): the probability
  // that such a cell is EVER labelled Periodic during one uninterrupted history is
  // <= alpha, for any horizon; a pruned and re-created cell starts a new history.
  // 0.2 is the largest candidate level whose trajectory-level false-alarm rate
  // (ever Periodic within 1024 windows, Wilson 95% upper limit) stayed <= 0.01 on a
  // held-out calibration set (paper E5). >= 1 disables the test (amplitude rule only).
  double periodic_false_alarm{0.2};
  // Spend periodic_false_alarm over the touch count (PeriodicityModel::spentLevel)
  // instead of using it at every read-out; false restores the single-read-out rule.
  bool periodic_alpha_spending{true};
  PeriodicityParams periodicity{};
};
struct CellEvidence {
  double log_odds{0.0};
  int observations{0};
  int window_hits{0};
  int window_misses{0};
  bool graduated{false};
};
// Read-only observer of window closes. At every endWindow(), before any evidence update,
// the map calls onTouchedCell once for every cell that the closing window touched, with
// the outcome exactly as the state machine scores it: hit is true if the window saw at
// least one hit on the cell; miss is true if it saw misses and no hit (a hit wins over
// a miss in the same window), so exactly one of the two is true. Untouched cells are not
// reported. window_index is windowCount() of the closing window (1 for the first).
// closeWindows(k) reports its first window only; the k - 1 empty windows touch nothing.
// The call order over cells is the iteration order of the internal hash map and carries
// no meaning. The observer must not call mutating functions of the map and must outlive
// its registration. It has no effect on the map: with no observer set the only cost is
// one pointer read per endWindow().
class WindowObserver {
 public:
  virtual ~WindowObserver() = default;
  virtual void onTouchedCell(int window_index, CellId id, bool hit, bool miss) = 0;
};
class LayeredMap {
 public:
  explicit LayeredMap(LayeredMapParams params);
  // Non-owning; nullptr removes the observer. See WindowObserver.
  void setWindowObserver(WindowObserver* observer) { observer_ = observer; }
  void observeHit(CellId id);
  void observeMiss(CellId id);
  bool tick();
  void endWindow();
  // Close k >= 1 windows at once: the open window, then k - 1 windows that observed
  // nothing. A window with no observation changes no cell (decay, graduation,
  // demotion and pruning read only touched evidence or values the first close
  // already settled), so the empty ones only advance the window index. Exactly
  // equivalent to k endWindow() calls; O(cells) instead of O(k * cells).
  void closeWindows(int k);
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
  WindowObserver* observer_{nullptr};
  int integration_count_{0};
  int window_count_{0};   // 2^31 windows is 68 years at 1 s windows, 13.6 years at 0.2 s
};
}  // namespace strata_core
