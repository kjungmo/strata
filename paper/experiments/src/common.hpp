// Shared helpers for the strata_core evaluation harness.
// Header-only: metric computation, CSV writing, deterministic RNG.
#pragma once
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include "strata_core/layered_map.hpp"
#include "strata_core/types.hpp"

namespace eval {

using strata_core::CellId;

// Evidence-model arm of a harness executable. With no argument an executable runs the
// v0.1 arithmetic ("legacy") and writes the committed results/*.csv byte for byte. With the
// argument "sprt" it runs the same scenes with evidence_model = Sprt (the shipped defaults
// m0, m1, alpha, beta of LayeredMapParams unless the experiment sweeps them) and writes
// results/<name>_sprt.csv, so no legacy file is ever overwritten.
inline bool& sprtArm() { static bool v = false; return v; }
// Returns false (after printing a usage line) when an argument is neither absent nor "sprt".
inline bool parseArm(int argc, char** argv) {
  if (argc == 1) return true;
  if (argc == 2 && std::string(argv[1]) == "sprt") { sprtArm() = true; return true; }
  std::fprintf(stderr, "usage: %s [sprt]\n", argv[0]);
  return false;
}
// "results/e1_x.csv" -> "results/e1_x_sprt.csv" in the sprt arm, unchanged otherwise.
inline std::string armPath(const std::string& legacy_path) {
  if (!sprtArm()) return legacy_path;
  const std::string ext = ".csv";
  return legacy_path.substr(0, legacy_path.size() - ext.size()) + "_sprt" + ext;
}
// Evidence model of the arm; the v0.1 fields a caller sets afterwards are ignored by Sprt.
inline strata_core::EvidenceModel armModel() {
  return sprtArm() ? strata_core::EvidenceModel::Sprt : strata_core::EvidenceModel::Legacy;
}

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
