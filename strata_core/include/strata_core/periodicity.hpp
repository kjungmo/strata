#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include "strata_core/types.hpp"
namespace strata_core {
struct PeriodicityParams { int period_windows{24}; int n_harmonics{2}; };
// Per-harmonic test quantities (exposed for calibration and diagnostics).
struct HarmonicStat {
  double n{0};          // touched windows
  double amplitude{0};  // 2 |gamma_k| (centred)
  double dchi{0};       // variance explained by harmonic k: y^T M^{-1} y
  double chi0{0};       // total centred sum of squares n m (1 - m)
};
// The Fourier sums of one cell are one slot of 2 + 6 H doubles, H = max(0, n_harmonics):
//   n, s0, c[H], s[H], ec[H], es[H], ec2[H], es2[H]
// n touched windows, s0 = sum o_w; c/s = sum o_w cos/sin; ec/es = sum cos/sin over the touched
// windows (needed to mean-centre the coefficients); ec2/es2 = sum cos/sin of the doubled phase
// (needed for the phase-design matrix of the test). Entry k belongs to harmonic k + 1. A null slot
// means "no window gathered yet"; so does an all-zero one. Every read function answers both the same.

// The periodicity arithmetic, with no per-cell state: every function works on the slot the caller
// passes. LayeredMap keeps slots in a CoeffPool; PeriodicityModel keeps one vector per id.
// Documentation of the quantities is on the PeriodicityModel functions of the same name.
class PeriodicityKernel {
 public:
  explicit PeriodicityKernel(PeriodicityParams p);
  static std::size_t slotDoubles(int n_harmonics) { return 2 + 6 * static_cast<std::size_t>(n_harmonics > 0 ? n_harmonics : 0); }
  std::size_t slotDoubles() const { return slotDoubles(params_.n_harmonics); }
  // slot must hold slotDoubles() zero-initialised doubles or the sums gathered so far.
  void gather(double* slot, bool occupied, int window_index) const;
  // The phase terms of gather() depend on the window index only. phases() computes them once per
  // window, with the expressions gather(slot, occupied, window_index) uses; gather(slot, occupied, table)
  // then adds the same numbers in the same order, so both overloads give the same bits.
  struct PhaseTable { std::vector<double> ca, sa, c2, s2; };   // entry k: harmonic k + 1
  void phases(int window_index, PhaseTable& table) const;
  void gather(double* slot, bool occupied, const PhaseTable& table) const;
  double predict(const double* slot, int window_index) const;
  double amplitude(const double* slot) const;
  double falseAlarm(const double* slot) const;
  HarmonicStat harmonic(const double* slot, int k) const;
  bool isPeriodic(const double* slot, double a_min, double alpha, bool spend = false) const;
  static double tailBound(double dchi);
  static double spentLevel(double alpha, double n, int period_windows);
  const PeriodicityParams& params() const { return params_; }
 private:
  struct View;
  View view(const double* slot) const;
  void centred(const View& cell, int k, double& ak, double& bk) const;
  double harmonicAmplitude(const View& cell, int k) const;
  HarmonicStat stat(const View& cell, int k) const;
  bool gated(const View& cell) const;
  PeriodicityParams params_;
  double omega_;
  int H_;
};

// Fixed-stride slots in blocks of kBlockSlots that are never moved, so growth allocates one block
// and never copies or reallocates a slot (no 2x peak). Freed slots are reused (a free list kept
// inside the freed slots). A slot is zeroed whenever it is handed out. Moving the pool keeps every
// slot where it is. Not copyable.
class CoeffPool {
 public:
  static constexpr std::uint32_t kNone = 0x7FFFFFFFu;     // "no slot"; slot indices have 31 bits
  static constexpr std::size_t kBlockSlots = 4096;
  explicit CoeffPool(std::size_t slot_doubles);
  CoeffPool(CoeffPool&&) = default;
  CoeffPool& operator=(CoeffPool&&) = default;
  // A zeroed slot. Throws std::bad_alloc (state unchanged) or std::length_error (2^31 - 1 slots live).
  std::uint32_t acquire();
  void release(std::uint32_t idx) noexcept;
  double* at(std::uint32_t idx) { return blocks_[idx / kBlockSlots].get() + (idx % kBlockSlots) * stride_; }
  const double* at(std::uint32_t idx) const { return blocks_[idx / kBlockSlots].get() + (idx % kBlockSlots) * stride_; }
  std::size_t inUse() const { return in_use_; }
  std::size_t slotDoubles() const { return stride_; }
 private:
  std::size_t stride_;
  std::vector<std::unique_ptr<double[]>> blocks_;
  std::uint32_t next_{0};
  std::uint32_t free_head_{kNone};
  std::size_t in_use_{0};
};

// Id-keyed model with its own map (one allocation per entry), a thin wrapper over PeriodicityKernel.
// LayeredMap does not use it (it keeps the sums in a CoeffPool); it stays for standalone use.
class PeriodicityModel {
 public:
  explicit PeriodicityModel(PeriodicityParams p);
  void gather(CellId id, bool occupied, int window_index);
  double predict(CellId id, int window_index) const;
  // Dominant centred-harmonic amplitude max_k 2|gamma_k| (0 below the n >= T gate).
  double amplitude(CellId id) const;
  // Bonferroni-adjusted (over the n_harmonics) false-alarm bound of the strongest
  // harmonic under the null "occupancy is iid Bernoulli(m) over the touched
  // windows, for any unknown m": min(1, H * min_k p_k), where
  // p_k = x e^{1-x} with x = 2 dchi_k (1 if x <= 1) is a Chernoff bound on
  // P(dchi_k >= observed) that holds for every m and every non-adaptive set of
  // touched phases. Returns 1 below the n >= T gate and for a constant cell.
  double falseAlarm(CellId id) const;
  // Test quantities of harmonic k (0 <= k < n_harmonics) of a known cell.
  HarmonicStat harmonic(CellId id, int k) const;
  // p_k for a given explained variance dchi (the bound above, before Bonferroni).
  static double tailBound(double dchi) { return PeriodicityKernel::tailBound(dchi); }
  // Level spent at touch count n when alpha is spread over every read-out of one
  // uninterrupted history: alpha * T / (n (n + 1)) for n >= T, 0 below the gate.
  // The levels telescope, sum_{n >= T} = alpha, so by the union bound a cell whose
  // history is iid Bernoulli(m) over non-adaptively chosen touched windows is labelled
  // Periodic at ANY read-out with probability <= alpha, over an unbounded horizon.
  static double spentLevel(double alpha, double n, int period_windows) {
    return PeriodicityKernel::spentLevel(alpha, n, period_windows);
  }
  // Periodic iff n >= T and some harmonic k has amplitude >= a_min AND
  // H * FAP_k <= level, where level = alpha (single read-out) or
  // spentLevel(alpha, n, T) (spend = true). alpha >= 1 disables the significance
  // test (amplitude only).
  bool isPeriodic(CellId id, double a_min, double alpha, bool spend = false) const;
  bool has(CellId id) const { return cells_.count(id) > 0; }
  void erase(CellId id) { cells_.erase(id); }
  std::size_t size() const { return cells_.size(); }
 private:
  PeriodicityKernel kernel_;
  const double* slot(CellId id) const;   // null if the id has no entry
  std::unordered_map<CellId, std::vector<double>> cells_;
};
}  // namespace strata_core
