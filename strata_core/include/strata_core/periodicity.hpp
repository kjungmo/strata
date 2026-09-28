#pragma once
#include <cstddef>
#include <unordered_map>
#include <vector>
#include "strata_core/types.hpp"
namespace strata_core {
struct PeriodicityParams { int period_windows{24}; int n_harmonics{2}; };
class PeriodicityModel {
 public:
  explicit PeriodicityModel(PeriodicityParams p);
  void gather(CellId id, bool occupied, int window_index);
  double predict(CellId id, int window_index) const;
  double amplitude(CellId id) const;
  bool has(CellId id) const { return cells_.count(id) > 0; }
  void erase(CellId id) { cells_.erase(id); }
  std::size_t size() const { return cells_.size(); }
 private:
  // n touched windows, s0 = sum o_w; c/s = sum o_w cos/sin; ec/es = sum cos/sin over
  // the touched windows (needed to mean-centre the coefficients).
  struct Coeff {
    double n{0}; double s0{0};
    std::vector<double> c; std::vector<double> s;
    std::vector<double> ec; std::vector<double> es;
  };
  void centred(const Coeff& cell, int k, double& ak, double& bk) const;
  PeriodicityParams params_;
  double omega_;
  std::unordered_map<CellId, Coeff> cells_;
};
}  // namespace strata_core
