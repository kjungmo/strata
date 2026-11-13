#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "strata_core/types.hpp"
#include "strata_core/periodicity.hpp"
namespace strata_core {
// Intermittent (sprt only): undecided after n_max_undecided touched windows and not
// Periodic. It is a read-out; the test keeps running and the cell can still graduate.
enum class CellClass { Unknown, Transient, Periodic, Static, Intermittent };
// How a cell's evidence is accumulated and when it graduates.
//   Legacy: the v0.1 rule l <- decay * (l + x), graduate at graduate_prob after
//           min_observations windows. Reproduced bit for bit; opt in by name.
//   Sprt:   Wald's sequential probability ratio test on the per-window hit rate.
//           The user states what to tell apart (m0 < m1) and the two error rates
//           (alpha, beta); increments and thresholds are derived (SprtDerived).
enum class EvidenceModel { Legacy, Sprt };
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

  // ---- evidence_model = Sprt (version 0.2.0) -------------------------------------
  // Hypotheses on the per-window hit rate of one cell: H0 "clutter", rate <= m0;
  // H1 "structure", rate >= m1. alpha = P(clutter at m0 graduates), beta = P(structure
  // at m1 is rejected); both hold for independent windows only (SPEC 3.2a).
  // Defaults are the measured pair of the phase 1b calibration (LILocBench, 0.05 m,
  // 1.0 s windows = 15 scans at 15 Hz; paper/experiments/realdata/PREREG.md). They are
  // PROVISIONAL while PREREG.md reads "NOT FROZEN": test_prereg_defaults ties them to
  // PREREG.json, so a changed freeze fails the build until they are updated. The same
  // pair is the shipped 3D default; it is NOT calibrated for 3D (SPEC 3.2a).
  EvidenceModel evidence_model{EvidenceModel::Sprt};
  double m0{0.20};
  double m1{0.70};
  double alpha{0.01};
  double beta{0.01};
  // Cap on the statistic. NaN: derived, A + 2 l_hit (two hit windows of headroom, so a
  // graduated wall needs a stated number of misses before demotion is considered).
  double sprt_ceiling{std::numeric_limits<double>::quiet_NaN()};
  // A cell that is still undecided after this many touched windows and is not Periodic
  // is read out as Intermittent. 0: never. 80 = the smallest multiple of 16 whose
  // probability of staying undecided at the zero-drift rate m* is <= 0.03 (PREREG.md).
  int n_max_undecided{80};
};
// Quantities derived once from (m0, m1, alpha, beta) for evidence_model = Sprt.
struct SprtDerived {
  double l_hit{0.0};       // ln(m1 / m0), added in a hit window
  double l_miss{0.0};      // ln((1 - m1) / (1 - m0)), added in a miss window (negative)
  double A{0.0};           // graduate at l >= A = ln((1 - beta) / alpha)
  double B{0.0};           // reject at l <= B = ln(beta / (1 - alpha)), once n >= T
  double ceiling{0.0};     // upper clamp: A + 2 l_hit unless sprt_ceiling is set
  double demote_level{0.0};  // ceiling - A: the reverse test from the ceiling at level A
  double m_star{0.0};      // zero-drift hit rate -l_miss / (l_hit - l_miss)
  int latency{0};          // windows an always-hit cell needs to graduate (summed as the engine sums)
  int demote_misses{0};    // all-miss windows a graduated cell at the ceiling needs to be demoted
};
// Throws std::invalid_argument unless 0 < m0 < m1 < 1, 0 < alpha, beta and
// alpha + beta < 1 (all finite), and sprt_ceiling is NaN or finite and >= A.
SprtDerived deriveSprt(const LayeredMapParams& p);
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
// The call order over cells is the order in which the window first touched them and carries
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
  // Not copyable: touched_ holds pointers into this object's own cells_. Moving is fine
  // (a moved unordered_map keeps its elements where they are).
  LayeredMap(const LayeredMap&) = delete;
  LayeredMap& operator=(const LayeredMap&) = delete;
  LayeredMap(LayeredMap&&) = default;
  LayeredMap& operator=(LayeredMap&&) = default;
  // Non-owning; nullptr removes the observer. See WindowObserver.
  void setWindowObserver(WindowObserver* observer) { observer_ = observer; }
  void observeHit(CellId id);
  void observeMiss(CellId id);
  bool tick();
  // Visits only the cells the closing window touched; the cost does not grow with the
  // number of cells the map holds. Every other cell is left exactly as it was: an
  // untouched cell's evidence and Fourier coefficients change only at a touched close, so
  // its decay, amplitude, Periodic test, graduation, demotion and pruning outcome would
  // be recomputed from the same values to the same result (see endWindow() in the source).
  void endWindow();
  // Close k >= 1 windows at once: the open window, then k - 1 windows that observed
  // nothing. A window with no observation changes no cell (decay, graduation,
  // demotion and pruning read only touched evidence or values the first close
  // already settled), so the empty ones only advance the window index. Exactly
  // equivalent to k endWindow() calls; O(cells touched by the open window) instead of
  // O(k * cells).
  void closeWindows(int k);
  CellClass classify(CellId id) const;
  bool isStatic(CellId id) const;
  double occupancyProb(CellId id) const;
  double periodicProb(CellId id) const;
  std::vector<CellId> staticCells() const;
  std::vector<CellId> transientCells() const;
  std::vector<CellId> periodicCells() const;
  std::vector<CellId> intermittentCells() const;
  // Number of touched windows of a known cell (0 if unknown); read-out for tests and tools.
  int observations(CellId id) const;
  // Valid for evidence_model = Sprt; zero-initialised for Legacy.
  const SprtDerived& sprt() const { return sprt_; }
  int windowCount() const { return window_count_; }
  int integrationCount() const { return integration_count_; }
  std::size_t cellCount() const { return cells_.size(); }
  // Number of cells the last close call visited: the cells the closing window touched (0
  // before the first close). closeWindows(k) counts its first window, the only one that
  // touches cells. Read-out for diagnostics.
  std::size_t lastWindowTouched() const { return last_window_touched_; }
  const LayeredMapParams& params() const { return params_; }
 private:
  // The map element of one cell: the evidence and the index of the cell's Fourier sums in pool_
  // (CoeffPool::kNone until the cell is first gathered, which happens only with enable_periodicity).
  // observeHit/observeMiss touch only this small node.
  struct CellState {
    CellEvidence ev;
    std::uint32_t slot{CoeffPool::kNone};
  };
  using Cell = std::pair<const CellId, CellState>;
 public:
  // Bytes of one map element (key, evidence, slot index), without the hash map's node link and the
  // allocator's own overhead. Pinned by a test so that growth is noticed; NOT the memory per cell.
  static constexpr std::size_t kCellPayloadBytes = sizeof(Cell);
  // Bytes of the Fourier sums of one cell that has them, for this map's n_harmonics.
  std::size_t coeffSlotBytes() const { return pool_.slotDoubles() * sizeof(double); }
  // Cells that hold Fourier sums now: the cells gathered at least once and not erased since.
  // Always 0 with enable_periodicity off. Read-out for tests and tools.
  std::size_t coeffSlotsInUse() const { return pool_.inUse(); }
 private:
  static double sigmoid(double x);
  void endWindowSprt();
  LayeredMapParams params_;
  SprtDerived sprt_{};
  bool use_sprt_{false};
  void touch(Cell& cell);
  const double* coeffOf(const CellState& st) const;   // null: the cell has no sums
  std::unordered_map<CellId, CellState> cells_;
  // Cells that the open window touched, in order of first contact. Pointers to the elements
  // of a std::unordered_map stay valid across rehash and across erase of other elements, so
  // the close reaches a touched cell and, through its slot index, its Fourier sums without a hash
  // lookup (it still looks up by id to erase a cell). A cell is erased only inside a close, and the close
  // removes every cell it finishes from this list (all of them, or on an exception the
  // finished prefix), so no entry dangles. Not copyable (above).
  std::vector<Cell*> touched_;
  std::size_t last_window_touched_{0};
  PeriodicityKernel periodicity_;   // parameters only; the sums live in pool_, one slot per gathered cell
  CoeffPool pool_;
  PeriodicityKernel::PhaseTable phase_;   // the open close's phase terms, filled at the start of a close
  WindowObserver* observer_{nullptr};
  int integration_count_{0};
  int window_count_{0};   // 2^31 windows is 68 years at 1 s windows, 13.6 years at 0.2 s
};
}  // namespace strata_core
