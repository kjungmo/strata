// Shared helpers for the strata_core evaluation harness.
// Header-only: metric computation, CSV writing, deterministic RNG.
#pragma once
#include <cstdint>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include "strata_core/types.hpp"

namespace eval {

using strata_core::CellId;

// Fixed master seed for every stochastic experiment (recorded in CSV + summary).
constexpr unsigned kSeed = 12345u;

struct PRF {
  double precision{0.0};
  double recall{0.0};
  double f1{0.0};
  std::size_t tp{0};
  std::size_t pred{0};
  std::size_t gt{0};
};

// Set-based precision / recall / F1 of a predicted cell set vs ground-truth set.
inline PRF prf(const std::set<CellId>& predicted, const std::set<CellId>& gt) {
  PRF r;
  r.pred = predicted.size();
  r.gt = gt.size();
  std::size_t tp = 0;
  for (CellId id : predicted)
    if (gt.count(id)) ++tp;
  r.tp = tp;
  r.precision = r.pred ? static_cast<double>(tp) / r.pred : (gt.empty() ? 1.0 : 0.0);
  r.recall = r.gt ? static_cast<double>(tp) / r.gt : 1.0;
  r.f1 = (r.precision + r.recall) > 0.0
             ? 2.0 * r.precision * r.recall / (r.precision + r.recall)
             : 0.0;
  return r;
}

// Minimal CSV writer.
class Csv {
 public:
  explicit Csv(const std::string& path) : os_(path) {}
  void header(const std::string& h) { os_ << h << "\n"; }
  template <typename... Args>
  void row(Args&&... args) {
    bool first = true;
    ((emit(first, std::forward<Args>(args))), ...);
    os_ << "\n";
  }
  bool ok() const { return static_cast<bool>(os_); }

 private:
  template <typename T>
  void emit(bool& first, T&& v) {
    if (!first) os_ << ",";
    os_ << v;
    first = false;
  }
  std::ofstream os_;
};

}  // namespace eval
