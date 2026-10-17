// Core of phase 1b (PREREG_P1B.md): shape-filtered wall thickness (G1), pose consistency (G2),
// Keble 02 labels from the TLS map, the Spires time offset score, and the lean voxel probe that
// reproduces the window semantics of Voxel3DBackend for labelled voxels only (today's miss and the
// refined miss in one pass). Header-only; uses p1b_io.hpp and probe_core.hpp.
#pragma once
#include <Eigen/Eigenvalues>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>
#include "p1b_io.hpp"
#include "probe_core.hpp"

namespace realdata {

// ------------------------------------------------------------- G1: shaped thickness ----

struct ShapeParams {
  bool three_d{false};
  double radius_m{1.0};
  std::size_t min_points{50};
  double min_extent_m{1.0};
  double ratio_max{0.02};        // l_min / l_max
  double mid_min{0.5};           // 3D: (l2 - l3) / l1
  double nz_max{0.25};           // 3D: vertical patches
  std::size_t seeds{20000};
  std::uint64_t rng_seed{20261018};
};

struct ShapedResult {
  std::size_t seeds{0}, with_points{0}, accepted{0};
  std::vector<double> thickness_m;
};

inline ShapedResult shapedThickness(const std::vector<float>& xyz, const ShapeParams& prm) {
  ShapedResult res;
  const std::size_t n = xyz.size() / 3;
  if (n == 0) return res;
  const double r = prm.radius_m;
  const bool d3 = prm.three_d;
  struct Key { std::int64_t x, y, z; bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; } };
  struct KeyHash { std::size_t operator()(const Key& k) const {
    return static_cast<std::size_t>((k.x * 73856093LL) ^ (k.y * 19349663LL) ^ (k.z * 83492791LL)); } };
  std::unordered_map<Key, std::vector<std::uint32_t>, KeyHash> grid;
  auto key = [&](double x, double y, double z) {
    return Key{static_cast<std::int64_t>(std::floor(x / r)), static_cast<std::int64_t>(std::floor(y / r)),
               d3 ? static_cast<std::int64_t>(std::floor(z / r)) : 0};
  };
  for (std::size_t i = 0; i < n; ++i) grid[key(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2])].push_back(static_cast<std::uint32_t>(i));
  std::mt19937_64 rng(prm.rng_seed);
  std::uniform_int_distribution<std::size_t> pick(0, n - 1);
  std::vector<Eigen::Vector3d> nb;
  for (std::size_t s = 0; s < prm.seeds; ++s) {
    ++res.seeds;
    const std::size_t si = pick(rng);
    const Eigen::Vector3d c(xyz[3 * si], xyz[3 * si + 1], d3 ? xyz[3 * si + 2] : 0.0f);
    nb.clear();
    const Key k0 = key(c.x(), c.y(), c.z());
    for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) for (int dz = (d3 ? -1 : 0); dz <= (d3 ? 1 : 0); ++dz) {
      auto it = grid.find(Key{k0.x + dx, k0.y + dy, k0.z + dz});
      if (it == grid.end()) continue;
      for (std::uint32_t j : it->second) {
        const Eigen::Vector3d p(xyz[3 * j], xyz[3 * j + 1], d3 ? xyz[3 * j + 2] : 0.0f);
        if ((p - c).norm() <= r) nb.push_back(p);
      }
    }
    if (nb.size() < prm.min_points) continue;
    ++res.with_points;
    Eigen::Vector3d mean = Eigen::Vector3d::Zero();
    for (const auto& p : nb) mean += p;
    mean /= static_cast<double>(nb.size());
    Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
    for (const auto& p : nb) cov += (p - mean) * (p - mean).transpose();
    cov /= static_cast<double>(nb.size());
    if (d3) {
      Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(cov);
      const Eigen::Vector3d ev = es.eigenvalues();              // ascending: l3, l2, l1
      const double l3 = std::max(ev(0), 0.0), l2 = std::max(ev(1), 0.0), l1 = ev(2);
      if (!(l1 > 0.0)) continue;
      if (l3 / l1 > prm.ratio_max) continue;                    // not flat
      if ((l2 - l3) / l1 < prm.mid_min) continue;               // a line, not a plane
      if (std::fabs(es.eigenvectors().col(0).z()) > prm.nz_max) continue;   // not vertical
      if (std::sqrt(12.0 * l2) < prm.min_extent_m) continue;
      res.thickness_m.push_back(std::sqrt(12.0 * l3));
    } else {
      Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> es(cov.topLeftCorner<2, 2>());
      const Eigen::Vector2d ev = es.eigenvalues();              // ascending: l2, l1
      const double l2 = std::max(ev(0), 0.0), l1 = ev(1);
      if (!(l1 > 0.0)) continue;
      if (l2 / l1 > prm.ratio_max) continue;                    // not straight
      if (std::sqrt(12.0 * l1) < prm.min_extent_m) continue;
      res.thickness_m.push_back(std::sqrt(12.0 * l2));
    }
    ++res.accepted;
  }
  return res;
}

struct GateVerdictG1 { bool pass{false}; std::size_t accepted{0}; double p50{0}, p90{0}; };
inline GateVerdictG1 verdictG1(const ShapedResult& r, double t50, double t90, std::size_t min_patches = 500) {
  GateVerdictG1 v; v.accepted = r.accepted;
  v.p50 = percentile(r.thickness_m, 50); v.p90 = percentile(r.thickness_m, 90);
  v.pass = r.accepted >= min_patches && v.p50 <= t50 && v.p90 <= t90;
  return v;
}

// ------------------------------------------------------------- G2: pose consistency ----

struct ConsistencyParams {
  bool three_d{false};
  int query_every{10};         // steps
  int lag_lo{15}, lag_hi{60};  // steps (1 s to 4 s at 15 Hz; 10 and 40 at 10 Hz)
  int ref_stride{1};           // stored points per step: every ref_stride-th return
  int query_stride{1};         // query points: every query_stride-th return (a multiple of ref_stride)
  double d_cap_m{0.5};
};

// Stores the returns of a frame under its step (the frame group). Every ref_stride-th return, counted per frame.
inline void addFrameToSteps(std::vector<std::vector<float>>& steps, const Frame& fr, int ref_stride) {
  if (fr.group >= steps.size()) steps.resize(static_cast<std::size_t>(fr.group) + 1);
  auto& v = steps[fr.group];
  for (std::size_t i = 0; i < fr.size(); i += static_cast<std::size_t>(ref_stride)) v.insert(v.end(), fr.xyz.begin() + 3 * i, fr.xyz.begin() + 3 * i + 3);
}

struct ConsistencyResult {
  std::size_t queries{0}, query_points{0}, matched{0};
  std::vector<double> d_m;                 // distances of matched returns
  double matchedShare() const { return query_points ? static_cast<double>(matched) / query_points : 0.0; }
};

inline ConsistencyResult poseConsistency(const std::vector<std::vector<float>>& steps, const ConsistencyParams& prm) {
  ConsistencyResult res;
  const long n = static_cast<long>(steps.size());
  const double cell = prm.d_cap_m;
  auto keyOf = [&](double x, double y, double z) {
    const std::int64_t cx = static_cast<std::int64_t>(std::floor(x / cell)), cy = static_cast<std::int64_t>(std::floor(y / cell)),
                       cz = prm.three_d ? static_cast<std::int64_t>(std::floor(z / cell)) : 0;
    return static_cast<std::uint64_t>(((cx + (1LL << 20)) << 42) | ((cy + (1LL << 20)) << 21) | (cz + (1LL << 20)));
  };
  const int qs_ratio = std::max(1, prm.query_stride / std::max(1, prm.ref_stride));
  for (long k = 0; k < n; k += prm.query_every) {
    if (steps[k].empty()) continue;
    std::vector<float> ref;
    for (long j = std::max(0L, k - prm.lag_hi); j <= std::min(n - 1, k + prm.lag_hi); ++j) {
      const long lag = std::labs(j - k);
      if (lag < prm.lag_lo || lag > prm.lag_hi) continue;
      ref.insert(ref.end(), steps[j].begin(), steps[j].end());
    }
    if (ref.empty()) continue;
    ++res.queries;
    const std::size_t nr = ref.size() / 3;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> order(nr);
    for (std::size_t i = 0; i < nr; ++i) order[i] = {keyOf(ref[3 * i], ref[3 * i + 1], ref[3 * i + 2]), static_cast<std::uint32_t>(i)};
    std::sort(order.begin(), order.end());
    std::unordered_map<std::uint64_t, std::pair<std::uint32_t, std::uint32_t>> range;     // key -> [begin, end) in order
    for (std::size_t i = 0; i < nr;) {
      std::size_t j = i;
      while (j < nr && order[j].first == order[i].first) ++j;
      range[order[i].first] = {static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(j)};
      i = j;
    }
    const auto& q = steps[k];
    const std::size_t nq = q.size() / 3;
    for (std::size_t i = 0; i < nq; i += static_cast<std::size_t>(qs_ratio)) {
      ++res.query_points;
      const double x = q[3 * i], y = q[3 * i + 1], z = q[3 * i + 2];
      const std::int64_t cx = static_cast<std::int64_t>(std::floor(x / cell)), cy = static_cast<std::int64_t>(std::floor(y / cell)),
                         cz = prm.three_d ? static_cast<std::int64_t>(std::floor(z / cell)) : 0;
      double best = 1e300;
      for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) for (int dz = (prm.three_d ? -1 : 0); dz <= (prm.three_d ? 1 : 0); ++dz) {
        const std::uint64_t key = static_cast<std::uint64_t>(((cx + dx + (1LL << 20)) << 42) | ((cy + dy + (1LL << 20)) << 21) | (cz + dz + (1LL << 20)));
        auto it = range.find(key);
        if (it == range.end()) continue;
        for (std::uint32_t t = it->second.first; t < it->second.second; ++t) {
          const std::uint32_t r = order[t].second;
          const double ddx = ref[3 * r] - x, ddy = ref[3 * r + 1] - y, ddz = prm.three_d ? ref[3 * r + 2] - z : 0.0;
          best = std::min(best, ddx * ddx + ddy * ddy + ddz * ddz);
        }
      }
      if (best <= prm.d_cap_m * prm.d_cap_m) { ++res.matched; res.d_m.push_back(std::sqrt(best)); }
    }
  }
  return res;
}

struct GateVerdictG2 { bool pass{false}; double matched_share{0}, p50{0}, p90{0}; std::size_t queries{0}, query_points{0}; };
inline GateVerdictG2 verdictG2(const ConsistencyResult& r, double d50, double d90, double min_share = 0.5) {
  GateVerdictG2 v; v.queries = r.queries; v.query_points = r.query_points; v.matched_share = r.matchedShare();
  v.p50 = percentile(r.d_m, 50); v.p90 = percentile(r.d_m, 90);
  v.pass = r.queries > 0 && v.matched_share >= min_share && v.p50 <= d50 && v.p90 <= d90;
  return v;
}

// ------------------------------------------------------------- Keble 02 labels ---------

// Label codes are those of the KTH labels (probe_core.hpp): 1 moving = clutter here, 2 static ground, 3 static structure.
struct SpiresLabelParams {
  double res{0.2};
  double ground_height_m{0.30};
  std::uint32_t min_tls_points{3};
  std::size_t quota{2000000};
};

struct SpiresLabelCounts {
  std::size_t tls_voxels{0}, tls_occupied{0}, structure_all{0}, ground_all{0}, clutter_all{0};
  std::size_t structure{0}, ground{0}, clutter{0};             // after thinning
  std::size_t hit_voxels{0}, hit_voxels_tls_present{0}, hit_voxels_buffer{0}, hit_voxels_unsupported{0};
  double keep_structure{1}, keep_ground{1}, keep_clutter{1};
};

// All distinct voxels that hold at least one return endpoint of the cache.
inline VoxTable collectHitVoxels(CacheReader& cache, double res) {
  VoxIndex vi; vi.res = res;
  VoxTable seen(1 << 20);
  Frame fr;
  while (cache.next(fr))
    for (std::size_t i = 0; i < fr.size(); ++i) {
      const std::uint64_t id = vi.id(Eigen::Vector3d(fr.xyz[3 * i], fr.xyz[3 * i + 1], fr.xyz[3 * i + 2]));
      auto& s = seen.upsert(id);
      if (s.count < 0xFFFFFFFFu) ++s.count;
    }
  return seen;
}

inline bool thinKeep(std::uint64_t id, double p) {
  if (p >= 1.0) return true;
  return splitmix64(id) < static_cast<std::uint64_t>(p * 18446744073709551616.0);
}

inline LabelMap buildSpiresLabels(const VoxTable& tls, const VoxTable& columns, const VoxTable& hits, const SpiresLabelParams& prm,
                                  SpiresLabelCounts* counts = nullptr) {
  VoxIndex vi; vi.res = prm.res;
  SpiresLabelCounts c;
  std::vector<std::uint64_t> structure, ground, clutter;
  for (const auto& s : tls.slots()) {
    if (s.key == VoxTable::kEmpty) continue;
    ++c.tls_voxels;
    if (s.count < prm.min_tls_points) continue;
    ++c.tls_occupied;
    const Eigen::Vector3d ctr = vi.center(s.key);
    const VoxTable::Slot* col = columns.find(columnKey(ctr.x(), ctr.y()));
    const float cmin = col ? col->minz : s.minz;
    if (s.minz - cmin <= static_cast<float>(prm.ground_height_m)) ground.push_back(s.key); else structure.push_back(s.key);
  }
  std::sort(structure.begin(), structure.end());
  std::sort(ground.begin(), ground.end());
  for (const auto& s : hits.slots()) {
    if (s.key == VoxTable::kEmpty) continue;
    ++c.hit_voxels;
    if (tls.countOf(s.key) > 0) { ++c.hit_voxels_tls_present; continue; }
    std::int64_t x, y, z; VoxIndex::unpack(s.key, x, y, z);
    bool near = false;
    for (int dx = -1; dx <= 1 && !near; ++dx) for (int dy = -1; dy <= 1 && !near; ++dy) for (int dz = -1; dz <= 1 && !near; ++dz)
      if ((dx || dy || dz) && tls.countOf(vi.pack(x + dx, y + dy, z + dz)) > 0) near = true;
    if (near) { ++c.hit_voxels_buffer; continue; }
    const Eigen::Vector3d ctr = vi.center(s.key);
    if (!columns.find(columnKey(ctr.x(), ctr.y()))) { ++c.hit_voxels_unsupported; continue; }
    clutter.push_back(s.key);
  }
  std::sort(clutter.begin(), clutter.end());
  c.structure_all = structure.size(); c.ground_all = ground.size(); c.clutter_all = clutter.size();
  const double q = static_cast<double>(prm.quota);
  auto keepFrac = [&](std::size_t n) { return n > prm.quota ? q / static_cast<double>(n) : 1.0; };
  c.keep_structure = keepFrac(structure.size()); c.keep_ground = keepFrac(ground.size()); c.keep_clutter = keepFrac(clutter.size());
  LabelMap out;
  auto add = [&](const std::vector<std::uint64_t>& v, double p, std::uint8_t code, std::size_t& cnt) {
    for (std::uint64_t id : v) if (thinKeep(id, p)) { out[id] = code; ++cnt; }
  };
  add(structure, c.keep_structure, kStaticStructure, c.structure);
  add(ground, c.keep_ground, kStaticGround, c.ground);
  add(clutter, c.keep_clutter, kMoving, c.clutter);
  if (counts) *counts = c;
  return out;
}

// ------------------------------------------------------------- time offset score -------

struct OffsetScore3 { double dt{0}; double score{0}; std::size_t used{0}, no_pose_points{0}; };

// Share of sampled returns whose 0.05 m voxel, or one of its six face neighbours, holds >= min_points TLS points.
inline OffsetScore3 scoreSpiresOffset(const std::vector<SpiresPcd>& scans, const std::vector<double>& scan_times, const PoseTrack3& gt,
                                      const SpiresExtrinsic& ex, SpiresConvertOptions opt, double dt, const VoxTable& tls005,
                                      std::uint32_t min_points = 2, std::size_t point_stride = 5) {
  opt.dt = dt;
  VoxIndex vi; vi.res = 0.05;
  OffsetScore3 s; s.dt = dt;
  std::size_t good = 0;
  SpiresConvertReport rep;
  Frame fr;
  for (std::size_t k = 0; k < scans.size(); ++k) {
    if (!convertSpiresScan(scans[k], scan_times[k], gt, ex, opt, 0, fr, rep)) continue;
    for (std::size_t i = 0; i < fr.size(); i += point_stride) {
      const Eigen::Vector3d p(fr.xyz[3 * i], fr.xyz[3 * i + 1], fr.xyz[3 * i + 2]);
      std::int64_t x, y, z; vi.coords(p, x, y, z);
      bool hit = tls005.countOf(vi.pack(x, y, z)) >= min_points;
      static const int off[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
      for (int f = 0; f < 6 && !hit; ++f) hit = tls005.countOf(vi.pack(x + off[f][0], y + off[f][1], z + off[f][2])) >= min_points;
      ++s.used; if (hit) ++good;
    }
  }
  s.no_pose_points = rep.points_no_pose;
  s.score = s.used ? static_cast<double>(good) / s.used : 0.0;
  return s;
}

// PREREG_P1B.md 4.5: the highest score; among scores of at least 0.995 times the highest, the smallest |dt|.
inline int selectSpiresOffset(const std::vector<OffsetScore3>& all, double tie = 0.995) {
  if (all.empty()) return -1;
  double best = 0;
  for (const auto& s : all) best = std::max(best, s.score);
  int pick = -1;
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (all[i].score < tie * best) continue;
    if (pick < 0 || std::fabs(all[i].dt) < std::fabs(all[pick].dt) - 1e-12) pick = static_cast<int>(i);
  }
  return pick;
}

// Share of sampled returns whose 0.1 m voxel has a TLS point in its 3 x 3 x 3 neighbourhood (PREREG_P1B.md 4.4).
inline double alignmentShare(const std::vector<SpiresPcd>& scans, const std::vector<double>& scan_times, const PoseTrack3& gt,
                             const SpiresExtrinsic& ex, SpiresConvertOptions opt, double dt, const VoxTable& tls01, std::size_t point_stride = 5) {
  opt.dt = dt;
  VoxIndex vi; vi.res = 0.1;
  std::size_t used = 0, good = 0;
  SpiresConvertReport rep; Frame fr;
  for (std::size_t k = 0; k < scans.size(); ++k) {
    if (!convertSpiresScan(scans[k], scan_times[k], gt, ex, opt, 0, fr, rep)) continue;
    for (std::size_t i = 0; i < fr.size(); i += point_stride) {
      std::int64_t x, y, z; vi.coords(Eigen::Vector3d(fr.xyz[3 * i], fr.xyz[3 * i + 1], fr.xyz[3 * i + 2]), x, y, z);
      bool near = false;
      for (int dx = -1; dx <= 1 && !near; ++dx) for (int dy = -1; dy <= 1 && !near; ++dy) for (int dz = -1; dz <= 1 && !near; ++dz)
        if (tls01.countOf(vi.pack(x + dx, y + dy, z + dz)) > 0) near = true;
      ++used; if (near) ++good;
    }
  }
  return used ? static_cast<double>(good) / used : 0.0;
}

// ------------------------------------------------------------- the lean voxel probe -----

// Reproduces, for labelled voxels only, what Voxel3DBackend + LayeredMap feed to a WindowObserver: the same
// half-voxel ray march (same floating point expressions), a tick per frame, a window per `W` ticks, a hit wins over a miss.
// Each channel is one (window length, miss definition) pair. Refined miss: a march sample counts only if its voxel is
// farther than one voxel (Chebyshev) from the endpoint voxel. Today's miss counts every sample (PREREG_P1B.md section 5).
class LeanVoxelProbe {
 public:
  struct Spec { int W{10}; bool refined{false}; };
  struct Channel {
    Spec spec;
    std::unique_ptr<CellRecorder> rec;
    std::vector<std::uint8_t> flags;           // bit 0 hit, bit 1 miss; per label index
    std::vector<std::uint8_t> graze;           // today's channels: a miss event with the endpoint within one voxel
    std::vector<std::uint32_t> touched;
    int steps_in_window{0}, windows{0};
    std::uint64_t structure_miss_windows{0}, structure_miss_windows_with_graze{0};
  };
  struct Counters {
    std::uint64_t rays{0}, samples{0}, labelled_samples{0}, structure_miss_events{0}, structure_miss_events_graze{0};
  };

  LeanVoxelProbe(double res, const LabelMap& labels, const std::vector<Spec>& specs)
      : res_(res), labels_(labels), table_(labels.size() * 2 + 16), bloom_(std::size_t(1) << 21, 0) {
    vi_.res = res;
    keys_.reserve(labels.size()); code_.reserve(labels.size());
    std::vector<std::pair<std::uint64_t, std::uint8_t>> sorted(labels.begin(), labels.end());
    std::sort(sorted.begin(), sorted.end());
    for (const auto& kv : sorted) {
      table_.upsert(kv.first).count = static_cast<std::uint32_t>(keys_.size()) + 1;
      keys_.push_back(kv.first); code_.push_back(labelBase(kv.second));
      const std::uint64_t h = splitmix64(kv.first) >> 37;
      bloom_[h >> 6] |= (1ULL << (h & 63));
    }
    for (const Spec& s : specs) {
      Channel c; c.spec = s;
      c.rec.reset(new CellRecorder(labels_, 1.0));
      c.flags.assign(keys_.size(), 0);
      if (!s.refined) c.graze.assign(keys_.size(), 0);
      ch_.push_back(std::move(c));
    }
  }
  std::size_t channels() const { return ch_.size(); }
  const Channel& channel(std::size_t i) const { return ch_[i]; }
  const Counters& counters() const { return cnt_; }

  void beginFrame(const Eigen::Vector3d& origin) {
    for (auto& c : ch_) if (c.steps_in_window == 0) c.rec->setWindowOrigin(c.windows + 1, origin);
  }
  void integrate(const std::vector<float>& xyz, const Eigen::Vector3d& origin) {
    const std::size_t n = xyz.size() / 3;
    for (std::size_t i = 0; i < n; ++i) ray(origin, Eigen::Vector3d(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]));
  }
  void endFrame() {
    for (auto& c : ch_) {
      if (++c.steps_in_window < c.spec.W) continue;
      close(c);
    }
  }

 private:
  int lookup(std::uint64_t key) const {
    const std::uint64_t h = splitmix64(key) >> 37;
    if (!(bloom_[h >> 6] & (1ULL << (h & 63)))) return -1;
    const VoxTable::Slot* s = table_.find(key);
    return s ? static_cast<int>(s->count) - 1 : -1;
  }
  void touch(Channel& c, int idx, std::uint8_t bit) {
    if (c.flags[idx] == 0) c.touched.push_back(static_cast<std::uint32_t>(idx));
    c.flags[idx] |= bit;
  }
  void ray(const Eigen::Vector3d& o, const Eigen::Vector3d& h) {
    ++cnt_.rays;
    std::int64_t ex, ey, ez; vi_.coords(h, ex, ey, ez);
    const Eigen::Vector3d d = h - o;
    const double len = d.norm();
    if (len > 1e-6) {
      const int steps = static_cast<int>(len / (res_ * 0.5));
      const Eigen::Vector3d st = d / std::max(steps, 1);
      std::int64_t lx = 0, ly = 0, lz = 0; bool have = false;
      for (int i = 1; i < steps; ++i) {
        const Eigen::Vector3d p = o + st * i;
        std::int64_t x, y, z; vi_.coords(p, x, y, z);
        if (have && x == lx && y == ly && z == lz) continue;
        lx = x; ly = y; lz = z; have = true;
        ++cnt_.samples;
        const int idx = lookup(vi_.pack(x, y, z));
        if (idx < 0) continue;
        ++cnt_.labelled_samples;
        const std::int64_t cheb = std::max({std::llabs(x - ex), std::llabs(y - ey), std::llabs(z - ez)});
        const bool structure = code_[idx] == kStaticStructure;
        if (structure) { ++cnt_.structure_miss_events; if (cheb <= 1) ++cnt_.structure_miss_events_graze; }
        for (auto& c : ch_) {
          if (c.spec.refined) { if (cheb > 1) touch(c, idx, 2); }
          else { touch(c, idx, 2); if (structure && cheb <= 1) c.graze[idx] = 1; }
        }
      }
    }
    const int idx = lookup(vi_.pack(ex, ey, ez));
    if (idx >= 0) for (auto& c : ch_) touch(c, idx, 1);
  }
  void close(Channel& c) {
    ++c.windows;
    for (std::uint32_t idx : c.touched) {
      const std::uint8_t f = c.flags[idx];
      const bool hit = (f & 1) != 0, miss = !hit && (f & 2) != 0;
      c.rec->onTouchedCell(c.windows, keys_[idx], hit, miss);
      if (!c.spec.refined && code_[idx] == kStaticStructure && miss) {
        ++c.structure_miss_windows;
        if (c.graze[idx]) ++c.structure_miss_windows_with_graze;
      }
      c.flags[idx] = 0;
      if (!c.spec.refined) c.graze[idx] = 0;
    }
    c.touched.clear();
    c.steps_in_window = 0;
  }

  double res_;
  VoxIndex vi_;
  const LabelMap& labels_;
  VoxTable table_;
  std::vector<std::uint64_t> bloom_;
  std::vector<std::uint64_t> keys_;
  std::vector<std::uint8_t> code_;
  std::vector<Channel> ch_;
  Counters cnt_;
};

}  // namespace realdata
