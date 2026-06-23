#pragma once
#include <cstddef>
#include <unordered_map>
#include <vector>
#include "prism_map_core/types.hpp"
namespace prism_map_core {
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
  struct Coeff { double n{0}; double s0{0}; std::vector<double> c; std::vector<double> s; };
  PeriodicityParams params_;
  double omega_;
  std::unordered_map<CellId, Coeff> cells_;
};
}  // namespace prism_map_core
