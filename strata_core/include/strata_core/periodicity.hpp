#pragma once
#include <cstddef>
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
  static double tailBound(double dchi);
  // Level spent at touch count n when alpha is spread over every read-out of one
  // uninterrupted history: alpha * T / (n (n + 1)) for n >= T, 0 below the gate.
  // The levels telescope, sum_{n >= T} = alpha, so by the union bound a cell whose
  // history is iid Bernoulli(m) over non-adaptively chosen touched windows is labelled
  // Periodic at ANY read-out with probability <= alpha, over an unbounded horizon.
  static double spentLevel(double alpha, double n, int period_windows);
  // Periodic iff n >= T and some harmonic k has amplitude >= a_min AND
  // H * FAP_k <= level, where level = alpha (single read-out) or
  // spentLevel(alpha, n, T) (spend = true). alpha >= 1 disables the significance
  // test (amplitude only).
  bool isPeriodic(CellId id, double a_min, double alpha, bool spend = false) const;
  bool has(CellId id) const { return cells_.count(id) > 0; }
  void erase(CellId id) { cells_.erase(id); }
  std::size_t size() const { return cells_.size(); }
 private:
  // n touched windows, s0 = sum o_w; c/s = sum o_w cos/sin; ec/es = sum cos/sin over
  // the touched windows (needed to mean-centre the coefficients); ec2/es2 = sum
  // cos/sin of the doubled phase (needed for the phase-design matrix of the test).
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
}  // namespace strata_core
