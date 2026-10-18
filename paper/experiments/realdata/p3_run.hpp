// Phase 3 run core: drives frames through a real backend with one arm, reads the final verdict of cells,
// tallies touched windows of labelled cells, and exports kept points. Header-only, no ROS, single-threaded.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "p3_arms.hpp"
#include "probe_core.hpp"
#include "realdata_io.hpp"
#include "strata_core/grid2d_backend.hpp"
#include "strata_core/voxel3d_backend.hpp"

namespace realdata {
namespace p3 {

// What an arm says about a cell at the end of a run (PREREG_P3.md section 3.2).
//   Static     the engine's isStatic()
//   Undecided  the cell exists and is neither Static nor pruned (Transient, Periodic, Intermittent)
//   Rejected   the engine has pruned the cell or never held it; for binary arms every cell that is not Static
enum class Verdict : std::uint8_t { Rejected = 0, Undecided = 1, Static = 2 };

inline const char* verdictName(Verdict v) { return v == Verdict::Static ? "static" : v == Verdict::Undecided ? "undecided" : "rejected"; }

inline Verdict verdictOf(const strata_core::LayeredMap& m, strata_core::CellId id, bool binary) {
  if (m.isStatic(id)) return Verdict::Static;
  if (binary) return Verdict::Rejected;
  return m.classify(id) == strata_core::CellClass::Unknown ? Verdict::Rejected : Verdict::Undecided;
}

// Export policies for the KTH benchmark (PREREG_P3.md section 4): which verdicts keep a point.
enum class Policy { StaticOnly, NotRejected, All };
inline bool keeps(Policy p, Verdict v) {
  switch (p) {
    case Policy::StaticOnly: return v == Verdict::Static;
    case Policy::NotRejected: return v != Verdict::Rejected;
    default: return true;
  }
}
inline const char* policyName(Policy p) { return p == Policy::StaticOnly ? "static_only" : p == Policy::NotRejected ? "not_rejected" : "all"; }

// ---------------------------------------------------------------------------- tally ----------

struct CellTally {
  std::uint32_t n{0};            // touched windows
  std::uint32_t h{0};            // of those, windows with a hit
  std::int32_t first_static{-1}; // window index (1 = first) at whose close the cell was first Static; -1 never
  std::int32_t first_touch{-1};  // window index of the first window that touched the cell (windows to decision = first_static - first_touch + 1)
};

// Counts touched windows of the cells in `watch` (all cells if watch is null). If track_static is set,
// poll() after every closed window records the first window at which a watched cell is Static.
class TallyObserver : public strata_core::WindowObserver {
 public:
  TallyObserver(const LabelMap* watch, bool track_static) : watch_(watch), track_(track_static) {}
  void onTouchedCell(int window_index, strata_core::CellId id, bool hit, bool) override {
    if (watch_ && watch_->find(id) == watch_->end()) return;
    CellTally& c = t_[id];
    if (c.first_touch < 0) c.first_touch = window_index;
    ++c.n;
    if (hit) ++c.h;
    if (track_) touched_.push_back(id);
  }
  // Call right after the window closed (tick() returned true).
  void poll(const strata_core::LayeredMap& m) {
    for (strata_core::CellId id : touched_) {
      CellTally& c = t_[id];
      if (c.first_static < 0 && m.isStatic(id)) c.first_static = m.windowCount();
    }
    touched_.clear();
  }
  const std::unordered_map<strata_core::CellId, CellTally>& tally() const { return t_; }
 private:
  const LabelMap* watch_;
  bool track_;
  std::unordered_map<strata_core::CellId, CellTally> t_;
  std::vector<strata_core::CellId> touched_;
};

// ---------------------------------------------------------------------------- driver ----------

struct DriveReport {
  std::size_t frames{0}, steps{0}, windows{0}, hits_total{0}, hits_off_grid{0};
  std::size_t map_cells_end{0};
};

// Frames with the same group form one integration step; the backend closes a window every layer_interval steps.
// next() fills one frame and returns false at the end. A last partial window is not closed.
template <class Backend>
DriveReport drive(Backend& b, const std::function<bool(Frame&)>& next, const strata_core::GridMeta* grid, TallyObserver* tally) {
  DriveReport st;
  bool have_step = false; std::uint32_t cur_group = 0;
  auto endStep = [&]() {
    if (!have_step) return;
    have_step = false; ++st.steps;
    if (b.tick()) { ++st.windows; if (tally) tally->poll(b.layered()); }
  };
  Frame fr;
  while (next(fr)) {
    if (have_step && fr.group != cur_group) endStep();
    if (!have_step) { have_step = true; cur_group = fr.group; }
    strata_core::Observation obs;
    obs.hits.reserve(fr.size());
    for (std::size_t i = 0; i < fr.size(); ++i) {
      obs.hits.emplace_back(fr.xyz[3 * i], fr.xyz[3 * i + 1], fr.xyz[3 * i + 2]);
      if (grid) { int gx, gy; if (!strata_core::worldToGrid(*grid, fr.xyz[3 * i], fr.xyz[3 * i + 1], gx, gy)) ++st.hits_off_grid; }
    }
    st.hits_total += fr.size();
    b.integrate(obs, fr.origin);
    ++st.frames;
  }
  endStep();
  st.map_cells_end = b.layered().cellCount();
  return st;
}

// ------------------------------------------------------------------------ cell dump ----------

// One row per labelled cell: label, ix, iy, iz, n, h, verdict, first_static, first_touch. Cells never touched have n = 0
// and the verdict the engine gives to a cell it does not hold (Rejected).
inline void writeLabelledDump(const std::string& path, const LabelMap& labels, const strata_core::LayeredMap& m,
                              const TallyObserver& tally, bool binary, bool three_d, int grid_width) {
  std::vector<strata_core::CellId> ids;
  ids.reserve(labels.size());
  for (const auto& kv : labels) ids.push_back(kv.first);
  std::sort(ids.begin(), ids.end());
  std::ofstream o(path);
  if (!o) fail(path, "cannot write");
  o << "label,ix,iy,iz,n,h,verdict,first_static,first_touch\n";
  for (strata_core::CellId id : ids) {
    int ix, iy, iz = 0;
    if (three_d) decodeVoxel(id, ix, iy, iz); else decodeGrid(id, grid_width, ix, iy);
    CellTally c;
    auto it = tally.tally().find(id);
    if (it != tally.tally().end()) c = it->second;
    o << int(labels.at(id)) << "," << ix << "," << iy << "," << iz << "," << c.n << "," << c.h << ","
      << verdictName(verdictOf(m, id, binary)) << "," << c.first_static << "," << c.first_touch << "\n";
  }
}

// ------------------------------------------------------------------ streaming PCD output ----------

// Binary PCD with fields x y z (float32), written point by point; the header is rewritten at close().
class PcdStreamWriter {
 public:
  explicit PcdStreamWriter(const std::string& path) : path_(path), o_(path, std::ios::binary) {
    if (!o_) fail(path, "cannot write");
    writeHeader(0);
    header_len_ = static_cast<std::streamoff>(o_.tellp());
  }
  void add(const float* xyz) { o_.write(reinterpret_cast<const char*>(xyz), 12); ++n_; }
  std::size_t count() const { return n_; }
  void close() {
    if (closed_) return;
    closed_ = true;
    o_.seekp(0);
    writeHeader(n_);
    o_.flush();
    if (!o_) fail(path_, "write error");
    o_.close();
  }
  ~PcdStreamWriter() { try { close(); } catch (...) {} }
 private:
  // fixed width so that the header length does not change when the count is known
  void writeHeader(std::size_t n) {
    char num[32]; std::snprintf(num, sizeof num, "%020zu", n);
    o_ << "# .PCD v0.7 - Point Cloud Data file format\nVERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nWIDTH " << num
       << "\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS " << num << "\nDATA binary\n";
  }
  std::string path_;
  std::ofstream o_;
  std::streamoff header_len_{0};
  std::size_t n_{0};
  bool closed_{false};
};

}  // namespace p3
}  // namespace realdata
