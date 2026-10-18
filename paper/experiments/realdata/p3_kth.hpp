// KTH DynamicMap benchmark exporter (PREREG_P3.md section 4). Runs one arm over the frames of a sequence and writes the
// ORIGINAL points whose voxel is kept, for each export policy, as a PCD map that the benchmark's evaluation reads.
// Output points are copies of frame points, so a ground-truth point matches its own copy at distance 0; voxel centres
// would fail the benchmark's 0.05 m match. Only points with sensor distance < max_range (50 m, the rule that built the
// ground-truth cloud) are exported. Header-only; the evaluator is p3_eval.py.
#pragma once
#include <dirent.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "p3_run.hpp"

namespace realdata {
namespace p3 {

inline std::vector<std::string> listPcdFiles(const std::string& dir) {
  std::vector<std::string> out;
  DIR* d = opendir(dir.c_str());
  if (!d) fail(dir, "cannot open directory");
  while (dirent* e = readdir(d)) {
    const std::string n = e->d_name;
    if (n.size() > 4 && n.substr(n.size() - 4) == ".pcd") out.push_back(n);
  }
  closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}

struct KthExportOptions {
  double res{0.2};
  int window_steps{10};
  bool periodicity{true};
  double max_range{50.0};
  std::vector<Policy> policies{Policy::StaticOnly, Policy::NotRejected};
  std::size_t max_frames{0};          // 0 = all frames (a smaller number is for tests and reader validation)
};

struct KthExportReport {
  std::size_t frames{0}, windows{0}, hits_total{0}, points_nonfinite{0};
  std::size_t points_in_range{0}, points_out_of_range{0};
  std::size_t map_cells_end{0};
  std::size_t verdict_points[3]{0, 0, 0};            // points in range by the verdict of their voxel (Rejected, Undecided, Static)
  std::vector<std::size_t> kept;                     // per policy, same order as the options
  std::vector<std::string> out_paths;
};

// seq_dir holds pcd/NNNNNN.pcd. Output files: <out_prefix>_<policy>.pcd.
inline KthExportReport kthExport(const std::string& seq_dir, const ArmSpec& arm, const KthExportOptions& o, const std::string& out_prefix) {
  std::vector<std::string> files = listPcdFiles(seq_dir + "/pcd");
  if (files.empty()) fail(seq_dir + "/pcd", "no .pcd frames");
  if (o.max_frames > 0 && files.size() > o.max_frames) files.resize(o.max_frames);
  KthExportReport rep;
  strata_core::Voxel3DBackend b(o.res, toParams(arm, o.window_steps, o.periodicity));
  for (const std::string& f : files) {                                   // pass 1: integrate, one tick per frame
    const PcdCloud c = readPcd(seq_dir + "/pcd/" + f);
    if (!c.hdr.has_viewpoint) fail(seq_dir + "/pcd/" + f, "no VIEWPOINT (the sensor origin is needed)");
    rep.points_nonfinite += c.dropped_nonfinite;
    strata_core::Observation obs;
    obs.hits.reserve(c.xyz.size() / 3);
    for (std::size_t i = 0; i < c.xyz.size(); i += 3) obs.hits.emplace_back(c.xyz[i], c.xyz[i + 1], c.xyz[i + 2]);
    rep.hits_total += obs.hits.size();
    b.integrate(obs, c.hdr.t);
    ++rep.frames;
    if (b.tick()) ++rep.windows;
  }
  rep.map_cells_end = b.layered().cellCount();
  std::vector<std::unique_ptr<PcdStreamWriter>> w;
  for (Policy p : o.policies) {
    rep.out_paths.push_back(out_prefix + "_" + policyName(p) + ".pcd");
    w.emplace_back(new PcdStreamWriter(rep.out_paths.back()));
  }
  for (const std::string& f : files) {                                   // pass 2: export
    const PcdCloud c = readPcd(seq_dir + "/pcd/" + f);
    for (std::size_t i = 0; i < c.xyz.size(); i += 3) {
      const Eigen::Vector3d p(c.xyz[i], c.xyz[i + 1], c.xyz[i + 2]);
      if (!((p - c.hdr.t).norm() < o.max_range)) { ++rep.points_out_of_range; continue; }
      ++rep.points_in_range;
      const Verdict v = verdictOf(b.layered(), b.voxelId(p), arm.binary);
      ++rep.verdict_points[static_cast<int>(v)];
      for (std::size_t k = 0; k < o.policies.size(); ++k)
        if (keeps(o.policies[k], v)) w[k]->add(&c.xyz[i]);
    }
  }
  for (auto& x : w) { rep.kept.push_back(x->count()); x->close(); }
  return rep;
}

}  // namespace p3
}  // namespace realdata
