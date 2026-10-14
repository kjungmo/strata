// Core of the hit-rate probe: labels, the per-cell recorder that listens to the real
// engine's window observer, run-length coding, viewpoint diversity and the wall-thickness
// gate. Header-only. Definitions are fixed in PREREG_P1.md.
#pragma once
#include <Eigen/Core>
#include <Eigen/Eigenvalues>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <random>
#include <unordered_map>
#include <vector>
#include "realdata_io.hpp"
#include "strata_core/grid2d_backend.hpp"
#include "strata_core/layered_map.hpp"
#include "strata_core/voxel3d_backend.hpp"

namespace realdata {

using strata_core::CellId;

// ------------------------------------------------------------- cell ids ------------

// Inverse of Voxel3DBackend::voxelId (21 bits per axis, offset 2^20).
inline void decodeVoxel(CellId id, int& ix, int& iy, int& iz) {
  constexpr std::int64_t kOff = 1 << 20, kBits = 21, mask = (1LL << kBits) - 1;
  iz = static_cast<int>((id & mask) - kOff);
  iy = static_cast<int>(((id >> kBits) & mask) - kOff);
  ix = static_cast<int>(((id >> (2 * kBits)) & mask) - kOff);
}
// Inverse of gridCellId (row-major).
inline void decodeGrid(CellId id, int width, int& gx, int& gy) {
  gx = static_cast<int>(id % width);
  gy = static_cast<int>(id / width);
}

// ---------------------------------------------------------------- labels ----------

using LabelMap = std::unordered_map<CellId, std::uint8_t>;

inline void writeLabels(const std::string& path, int dim, const LabelMap& labels) {
  std::ofstream o(path, std::ios::binary);
  if (!o) fail(path, "cannot write");
  const char magic[4] = {'S', 'L', 'B', 'L'};
  const std::uint32_t ver = 1, d = static_cast<std::uint32_t>(dim);
  const std::uint64_t n = labels.size();
  o.write(magic, 4); o.write(reinterpret_cast<const char*>(&ver), 4);
  o.write(reinterpret_cast<const char*>(&d), 4); o.write(reinterpret_cast<const char*>(&n), 8);
  std::vector<std::pair<CellId, std::uint8_t>> sorted(labels.begin(), labels.end());
  std::sort(sorted.begin(), sorted.end());       // byte-stable output
  for (const auto& kv : sorted) {
    o.write(reinterpret_cast<const char*>(&kv.first), 8);
    o.write(reinterpret_cast<const char*>(&kv.second), 1);
  }
  if (!o) fail(path, "write error");
}

inline LabelMap readLabels(const std::string& path, int* dim_out = nullptr) {
  std::ifstream f(path, std::ios::binary);
  if (!f) fail(path, "cannot open");
  char magic[4]; std::uint32_t ver = 0, dim = 0; std::uint64_t n = 0;
  f.read(magic, 4); f.read(reinterpret_cast<char*>(&ver), 4);
  f.read(reinterpret_cast<char*>(&dim), 4); f.read(reinterpret_cast<char*>(&n), 8);
  if (!f || std::memcmp(magic, "SLBL", 4) != 0) fail(path, "not a label file");
  if (ver != 1) fail(path, "unsupported label version");
  if (dim_out) *dim_out = static_cast<int>(dim);
  LabelMap m; m.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(n, 1ull << 26)));
  for (std::uint64_t i = 0; i < n; ++i) {
    CellId id; std::uint8_t l;
    f.read(reinterpret_cast<char*>(&id), 8); f.read(reinterpret_cast<char*>(&l), 1);
    if (!f) fail(path, "truncated label file");
    m[id] = l;
  }
  return m;
}

// KTH label codes (PREREG_P1.md section 3).
enum KthLabel : std::uint8_t { kMoving = 1, kStaticGround = 2, kStaticStructure = 3, kEgo = 4 };

struct KthLabelParams {
  double resolution{0.2};
  double ground_height_m{0.30};   // a static voxel is ground if its lowest GT point is within this of the column minimum
  double column_m{1.0};           // side of the horizontal column used for the minimum
  // The KTH extraction marks every point within 3 m of the sensor with label 0 or 1 (the
  // ego vehicle) as dynamic. A dynamic GT point within ego_radius_m of any frame origin is
  // such a point, not a mover: a voxel that contains one and no other dynamic point gets
  // label kEgo and is left out of every statistic. 0 switches the rule off.
  double ego_radius_m{0.0};
};

struct KthLabelCounts {
  std::size_t moving{0}, ground{0}, structure{0}, ego{0};
  std::size_t gt_points{0}, gt_dynamic{0}, gt_dynamic_ego{0};
};

// Cell level labels from the GT cloud (intensity 1 = dynamic, 0 = static). A voxel with at
// least one dynamic GT point is moving. A voxel whose GT points are all static is static;
// it is ground if its lowest point lies within ground_height_m of the lowest GT point of
// its horizontal column (any label), else structure. Intensity other than 0 or 1 throws.
inline LabelMap buildKthLabels(const std::vector<float>& xyz, const std::vector<float>& intensity,
                               const KthLabelParams& prm, KthLabelCounts* counts = nullptr,
                               const std::vector<Eigen::Vector3d>* origins = nullptr) {
  if (xyz.size() != intensity.size() * 3) fail("labels", "xyz and intensity sizes differ");
  strata_core::LayeredMapParams lp;
  strata_core::Voxel3DBackend vox(prm.resolution, lp);   // only for voxelId, the engine's own indexing
  struct Agg { bool dyn{false}, ego{false}; float min_z{1e30f}; };
  if (prm.ego_radius_m > 0.0 && !origins) fail("labels", "ego_radius_m > 0 needs the frame origins");
  std::unordered_map<CellId, Agg> agg;
  std::unordered_map<std::int64_t, float> col_min;
  auto colKey = [&](double x, double y) {
    const std::int64_t cx = static_cast<std::int64_t>(std::floor(x / prm.column_m));
    const std::int64_t cy = static_cast<std::int64_t>(std::floor(y / prm.column_m));
    return (cx + (1LL << 30)) * (1LL << 31) + (cy + (1LL << 30));
  };
  KthLabelCounts c;
  for (std::size_t i = 0; i < intensity.size(); ++i) {
    const float in = intensity[i];
    if (in != 0.0f && in != 1.0f) fail("labels", "GT intensity must be 0 or 1, got " + std::to_string(in));
    const Eigen::Vector3d p(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]);
    Agg& a = agg[vox.voxelId(p)];
    if (in == 1.0f) {
      bool ego = false;
      if (prm.ego_radius_m > 0.0)
        for (const auto& o : *origins) if ((o - p).norm() <= prm.ego_radius_m) { ego = true; break; }
      if (ego) { a.ego = true; ++c.gt_dynamic_ego; } else a.dyn = true;
    }
    a.min_z = std::min(a.min_z, xyz[3 * i + 2]);
    auto it = col_min.find(colKey(p.x(), p.y()));
    if (it == col_min.end()) col_min.emplace(colKey(p.x(), p.y()), xyz[3 * i + 2]);
    else it->second = std::min(it->second, xyz[3 * i + 2]);
    ++c.gt_points; if (in == 1.0f) ++c.gt_dynamic;
  }
  // The column of a voxel is the column of its centre.
  LabelMap out; out.reserve(agg.size());
  for (const auto& kv : agg) {
    if (kv.second.dyn) { out[kv.first] = kMoving; ++c.moving; continue; }
    if (kv.second.ego) { out[kv.first] = kEgo; ++c.ego; continue; }
    const Eigen::Vector3d ctr = vox.voxelCenter(kv.first);
    auto it = col_min.find(colKey(ctr.x(), ctr.y()));
    // The voxel centre may fall in a neighbouring column of a voxel that straddles a column
    // edge; the column minimum then comes from the column of the centre. Documented in the prereg.
    const float cmin = (it == col_min.end()) ? kv.second.min_z : it->second;
    if (kv.second.min_z - cmin <= static_cast<float>(prm.ground_height_m)) { out[kv.first] = kStaticGround; ++c.ground; }
    else { out[kv.first] = kStaticStructure; ++c.structure; }
  }
  if (counts) *counts = c;
  return out;
}

// LILocBench reference label codes (PREREG_P1.md section 4).
enum RefLabel : std::uint8_t { kRefOccupied = 1, kRefFree = 2, kRefFreeNearOccupied = 3 };

struct RefParams {
  int min_touches{10};            // a cell needs at least this many steps that touched it
  double occupied_frac{0.5};      // hit fraction at or above this: occupied
  double free_frac{0.05};         // hit fraction at or below this: free
  int margin_cells{0};            // a free cell within this Chebyshev distance of an occupied cell is kRefFreeNearOccupied
};

// Counts, per cell, the one-step windows that touched it and the ones that hit it.
class ReferenceRecorder : public strata_core::WindowObserver {
 public:
  void onTouchedCell(int, CellId id, bool hit, bool) override {
    auto& c = counts_[id];
    ++c.first; if (hit) ++c.second;
  }
  LabelMap labels(const RefParams& p, int grid_width = 0) const {
    LabelMap out;
    for (const auto& kv : counts_) {
      if (kv.second.first < static_cast<std::uint32_t>(p.min_touches)) continue;
      const double f = static_cast<double>(kv.second.second) / kv.second.first;
      if (f >= p.occupied_frac) out[kv.first] = kRefOccupied;
      else if (f <= p.free_frac) out[kv.first] = kRefFree;
    }
    if (p.margin_cells > 0 && grid_width > 0) {
      for (auto& kv : out) {
        if (kv.second != kRefFree) continue;
        int gx, gy; decodeGrid(kv.first, grid_width, gx, gy);
        bool near = false;
        for (int dy = -p.margin_cells; dy <= p.margin_cells && !near; ++dy)
          for (int dx = -p.margin_cells; dx <= p.margin_cells && !near; ++dx) {
            if (gx + dx < 0 || gx + dx >= grid_width || gy + dy < 0) continue;
            auto it = out.find(static_cast<CellId>(gy + dy) * grid_width + (gx + dx));
            if (it != out.end() && it->second == kRefOccupied) near = true;
          }
        if (near) kv.second = kRefFreeNearOccupied;   // writes only to existing keys; the neighbour test above still reads kRefOccupied cells only
      }
    }
    return out;
  }
  std::size_t cells() const { return counts_.size(); }
 private:
  std::unordered_map<CellId, std::pair<std::uint32_t, std::uint32_t>> counts_;
};

// ------------------------------------------------------- runs and diversity -------

// Compresses a hit/miss sequence into alternating run lengths; first_hit says what run 0 is.
class RunBuilder {
 public:
  void push(bool hit) {
    if (len_ == 0) { first_hit_ = hit; cur_hit_ = hit; len_ = 1; return; }
    if (hit == cur_hit_) { if (len_ < 65535) ++len_; return; }
    runs_.push_back(static_cast<std::uint16_t>(len_));
    cur_hit_ = hit; len_ = 1;
  }
  // All runs including the open one.
  std::vector<std::uint16_t> runs() const {
    std::vector<std::uint16_t> r = runs_;
    if (len_ > 0) r.push_back(static_cast<std::uint16_t>(len_));
    return r;
  }
  bool firstIsHit() const { return first_hit_; }
  std::string str() const {          // "h3m2h1": letter is the state of the run
    std::string s; bool h = first_hit_;
    for (std::uint16_t r : runs()) { s += h ? 'h' : 'm'; s += std::to_string(r); h = !h; }
    return s;
  }
 private:
  std::vector<std::uint16_t> runs_;
  std::uint32_t len_{0};
  bool first_hit_{false}, cur_hit_{false};
};

// Greedy, order dependent: a point opens a new centre if it is at least min_dist from every
// existing centre. The count is the viewpoint diversity of a cell (PREREG_P1.md section 5).
inline bool opensNewCentre(const std::vector<Eigen::Vector3d>& centres, const Eigen::Vector3d& p, double min_dist) {
  for (const auto& c : centres) if ((c - p).norm() < min_dist) return false;
  return true;
}

struct CellStats {
  std::uint32_t n{0}, h{0};
  std::int32_t first{0}, last{0};
  RunBuilder runs;
  std::vector<std::uint32_t> centre_windows;   // window indices whose origins are the greedy centres
};

// Listens to the engine's window observer for the labelled cells only. The probe must call
// setWindowOrigin(w, origin) for window w (1-based) before that window closes.
class CellRecorder : public strata_core::WindowObserver {
 public:
  CellRecorder(const LabelMap& labels, double diversity_dist_m)
      : labels_(labels), dist_(diversity_dist_m) { origins_.push_back(Eigen::Vector3d::Zero()); }
  void setWindowOrigin(int window, const Eigen::Vector3d& o) {
    if (window < 1) fail("recorder", "window index starts at 1");
    if (static_cast<std::size_t>(window) >= origins_.size()) origins_.resize(window + 1, Eigen::Vector3d::Zero());
    origins_[window] = o;
    have_ = std::max(have_, window);
  }
  void onTouchedCell(int window, CellId id, bool hit, bool miss) override {
    ++reports_;
    if (hit == miss) ++inconsistent_;                       // must never happen: exactly one is true
    auto lab = labels_.find(id);
    if (lab == labels_.end()) return;
    ++labelled_reports_;
    CellStats& s = stats_[id];
    if (s.n == 0) s.first = window;
    s.last = window;
    ++s.n; if (hit) ++s.h;
    s.runs.push(hit);
    if (window > have_) { ++missing_origin_; return; }
    bool opens = true;                       // same rule as opensNewCentre(), without copying the centres
    for (std::uint32_t cw : s.centre_windows)
      if ((origins_[cw] - origins_[window]).norm() < dist_) { opens = false; break; }
    if (opens) s.centre_windows.push_back(static_cast<std::uint32_t>(window));
  }
  const LabelMap& labels() const { return labels_; }
  const std::unordered_map<CellId, CellStats>& stats() const { return stats_; }
  std::size_t reports() const { return reports_; }
  std::size_t labelledReports() const { return labelled_reports_; }
  std::size_t inconsistent() const { return inconsistent_; }
  std::size_t missingOrigin() const { return missing_origin_; }
 private:
  const LabelMap& labels_;
  double dist_;
  std::unordered_map<CellId, CellStats> stats_;
  std::vector<Eigen::Vector3d> origins_;
  int have_{0};
  std::size_t reports_{0}, labelled_reports_{0}, inconsistent_{0}, missing_origin_{0};
};

// ------------------------------------------------------------ wall thickness ------

struct ThicknessResult {
  std::size_t seeds{0}, used{0};
  std::vector<double> thickness_m;     // one per accepted seed, uniform-width equivalent sqrt(12 * variance)
};

// Local plane/line fits around random seeds. 2D: a straight piece must extend at least
// min_extent_m along its major axis. 3D: the local plane must be vertical (|n_z| <= nz_max)
// and extend at least min_extent_m along its second axis. Acceptance does not look at the
// thickness itself, so a smeared wall cannot select itself out of the gate.
struct ThicknessParams {
  double radius_m{0.5};
  std::size_t min_points{20};
  double min_extent_m{0.6};
  double nz_max{0.25};
  std::size_t seeds{20000};
  std::uint64_t rng_seed{20261012};
};

inline ThicknessResult wallThickness(const std::vector<float>& xyz, bool three_d, const ThicknessParams& prm) {
  ThicknessResult res;
  const std::size_t n = xyz.size() / 3;
  if (n == 0) return res;
  const double r = prm.radius_m;
  struct Key { std::int64_t x, y, z; bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; } };
  struct KeyHash { std::size_t operator()(const Key& k) const {
    return static_cast<std::size_t>((k.x * 73856093LL) ^ (k.y * 19349663LL) ^ (k.z * 83492791LL)); } };
  std::unordered_map<Key, std::vector<std::uint32_t>, KeyHash> grid;
  auto key = [&](double x, double y, double z) {
    return Key{static_cast<std::int64_t>(std::floor(x / r)), static_cast<std::int64_t>(std::floor(y / r)),
               three_d ? static_cast<std::int64_t>(std::floor(z / r)) : 0};
  };
  for (std::size_t i = 0; i < n; ++i) grid[key(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2])].push_back(static_cast<std::uint32_t>(i));
  std::mt19937_64 rng(prm.rng_seed);
  std::uniform_int_distribution<std::size_t> pick(0, n - 1);
  std::vector<Eigen::Vector3d> nb;
  for (std::size_t s = 0; s < prm.seeds; ++s) {
    ++res.seeds;
    const std::size_t si = pick(rng);
    const Eigen::Vector3d c(xyz[3 * si], xyz[3 * si + 1], xyz[3 * si + 2]);
    nb.clear();
    const Key k0 = key(c.x(), c.y(), c.z());
    for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) for (int dz = (three_d ? -1 : 0); dz <= (three_d ? 1 : 0); ++dz) {
      auto it = grid.find(Key{k0.x + dx, k0.y + dy, k0.z + dz});
      if (it == grid.end()) continue;
      for (std::uint32_t j : it->second) {
        const Eigen::Vector3d p(xyz[3 * j], xyz[3 * j + 1], three_d ? xyz[3 * j + 2] : 0.0f);
        const Eigen::Vector3d cc(c.x(), c.y(), three_d ? c.z() : 0.0);
        if ((p - cc).norm() <= r) nb.push_back(p);
      }
    }
    if (nb.size() < prm.min_points) continue;
    Eigen::Vector3d mean = Eigen::Vector3d::Zero();
    for (const auto& p : nb) mean += p;
    mean /= static_cast<double>(nb.size());
    Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
    for (const auto& p : nb) cov += (p - mean) * (p - mean).transpose();
    cov /= static_cast<double>(nb.size());
    if (three_d) {
      Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(cov);
      const Eigen::Vector3d ev = es.eigenvalues();           // ascending
      const Eigen::Vector3d normal = es.eigenvectors().col(0);
      if (std::fabs(normal.z()) > prm.nz_max) continue;
      if (std::sqrt(12.0 * std::max(ev(1), 0.0)) < prm.min_extent_m) continue;
      res.thickness_m.push_back(std::sqrt(12.0 * std::max(ev(0), 0.0)));
    } else {
      Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> es(cov.topLeftCorner<2, 2>());
      const Eigen::Vector2d ev = es.eigenvalues();
      if (std::sqrt(12.0 * std::max(ev(1), 0.0)) < prm.min_extent_m) continue;
      res.thickness_m.push_back(std::sqrt(12.0 * std::max(ev(0), 0.0)));
    }
    ++res.used;
  }
  return res;
}

// Linear-interpolation percentile, q in [0, 100] (numpy's default method).
inline double percentile(std::vector<double> v, double q) {
  if (v.empty()) return std::nan("");
  std::sort(v.begin(), v.end());
  const double pos = (q / 100.0) * (v.size() - 1);
  const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
  const std::size_t hi = std::min(lo + 1, v.size() - 1);
  return v[lo] + (pos - lo) * (v[hi] - v[lo]);
}

}  // namespace realdata
