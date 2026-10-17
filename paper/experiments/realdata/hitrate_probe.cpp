// hitrate_probe: feeds real LiDAR frames through the real strata_core backends and records,
// per labelled cell, what the state machine sees in every window it touches (hit or miss,
// "hit wins" applied), through the read-only window observer. Single-threaded, no ROS.
// Subcommands (see PREREG_P1.md for the definitions and REPRODUCE below for the order):
//   kth-labels   GT cloud -> cell labels (moving / static ground / static structure)
//   kth-gate     pose sanity and wall thickness of a KTH sequence
//   kth-measure  frames + labels -> per-cell records
//   lil-offset   LILocBench bag + GT poses -> scan-to-GT time offset search (PREREG_P1.md section 11.1)
//   lil-convert  LILocBench bag + GT poses -> frame cache
//   lil-gate     wall thickness of a frame cache
//   lil-reference  frame cache -> reference occupancy labels
//   lil-measure  frame cache + labels -> per-cell records
#include <sys/stat.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <dirent.h>
#include <iostream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "probe_core.hpp"
#include "realdata_io.hpp"

using namespace realdata;

namespace {

std::map<std::string, std::string> parseArgs(int argc, char** argv, int from) {
  std::map<std::string, std::string> a;
  for (int i = from; i < argc; ++i) {
    std::string k = argv[i];
    if (k.rfind("--", 0) != 0 || i + 1 >= argc) fail("arguments", "expected --key value pairs, got '" + k + "'");
    a[k.substr(2)] = argv[++i];
  }
  return a;
}
std::string need(const std::map<std::string, std::string>& a, const std::string& k) {
  auto it = a.find(k);
  if (it == a.end()) fail("arguments", "missing --" + k);
  return it->second;
}
std::string opt(const std::map<std::string, std::string>& a, const std::string& k, const std::string& d) {
  auto it = a.find(k); return it == a.end() ? d : it->second;
}

double nowSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
long peakRssKb() {
  std::ifstream f("/proc/self/status"); std::string line;
  while (std::getline(f, line)) if (line.rfind("VmHWM:", 0) == 0) return std::atol(line.c_str() + 6);
  return -1;
}

struct Meta {
  std::ofstream o;
  explicit Meta(const std::string& path) : o(path) { if (!o) fail(path, "cannot write"); o.precision(10); }
  template <class T> void put(const std::string& k, const T& v) { o << k << "=" << v << "\n"; }
};

std::vector<std::string> listPcd(const std::string& dir) {
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

struct DriveStats {
  std::size_t frames{0}, steps{0}, windows{0}, hits_total{0}, hits_off_grid{0};
  std::size_t map_cells_end{0};
};

// Drives frames through a backend. A step is a run of frames with equal group; a window
// closes after W steps (layer_interval = W, one tick per step). Registers the origin of the
// first frame of each window with the recorder (viewpoint diversity).
template <class Backend>
DriveStats drive(Backend& b, const std::function<bool(Frame&)>& next, int W, CellRecorder* rec,
                 const strata_core::GridMeta* grid) {
  DriveStats st;
  bool have_step = false; std::uint32_t cur_group = 0; int steps_in_window = 0;
  auto endStep = [&]() {
    if (!have_step) return;
    have_step = false; ++st.steps; ++steps_in_window;
    if (b.tick()) { steps_in_window = 0; ++st.windows; }
  };
  Frame fr;
  while (next(fr)) {
    if (have_step && fr.group != cur_group) endStep();
    if (!have_step) {
      have_step = true; cur_group = fr.group;
      if (steps_in_window == 0 && rec) rec->setWindowOrigin(b.layered().windowCount() + 1, fr.origin);
    }
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

strata_core::LayeredMapParams engineParams(int W) {
  strata_core::LayeredMapParams p;     // shipped evidence parameters, untouched
  p.layer_interval = W;                // one window per W steps
  p.enable_periodicity = false;        // the observer's reports do not depend on it (tested); saves memory
  return p;
}

void writeCells(const std::string& path, const CellRecorder& rec, bool three_d, int grid_width) {
  std::vector<CellId> ids;
  ids.reserve(rec.stats().size());
  for (const auto& kv : rec.stats()) ids.push_back(kv.first);
  std::sort(ids.begin(), ids.end());
  std::ofstream o(path);
  if (!o) fail(path, "cannot write");
  o << "label,ix,iy,iz,N,H,first,last,div,runs\n";
  for (CellId id : ids) {
    const CellStats& s = rec.stats().at(id);
    int ix, iy, iz = 0;
    if (three_d) decodeVoxel(id, ix, iy, iz); else decodeGrid(id, grid_width, ix, iy);
    o << int(rec.labels().at(id)) << "," << ix << "," << iy << "," << iz << "," << s.n << "," << s.h << ","
      << s.first << "," << s.last << "," << s.centre_windows.size() << "," << s.runs.str() << "\n";
  }
}

void writeDriveMeta(Meta& m, const DriveStats& st, const CellRecorder& rec, double secs, int W, double res) {
  m.put("window_steps", W); m.put("resolution_m", res);
  m.put("frames", st.frames); m.put("steps", st.steps); m.put("windows_closed", st.windows);
  m.put("hits_total", st.hits_total); m.put("hits_off_grid", st.hits_off_grid);
  m.put("engine_cells_end", st.map_cells_end);
  m.put("observer_reports", rec.reports()); m.put("observer_reports_labelled", rec.labelledReports());
  m.put("observer_inconsistent", rec.inconsistent()); m.put("window_origin_missing", rec.missingOrigin());
  m.put("labelled_cells_in_file", rec.labels().size()); m.put("labelled_cells_touched", rec.stats().size());
  m.put("seconds", secs); m.put("peak_rss_kb", peakRssKb());
}

void writeThicknessMeta(Meta& m, const ThicknessResult& r, double res, const std::string& prefix) {
  m.put(prefix + "seeds", r.seeds); m.put(prefix + "used", r.used);
  for (double q : {10.0, 25.0, 50.0, 75.0, 90.0}) {
    m.put(prefix + "thickness_m_p" + std::to_string(int(q)), percentile(r.thickness_m, q));
    m.put(prefix + "thickness_cells_p" + std::to_string(int(q)), percentile(r.thickness_m, q) / res);
  }
}

// ----------------------------------------------------------------------- KTH ------

struct ClassCheck { std::size_t gt_dynamic_not_moving_class{0}, moving_class_not_gt_dynamic{0}, frames{0}; };

// Semantic class of every GT point, read from the intensity of the frame PCDs (PREREG_P1.md
// section 11.2). The frames must concatenate to the GT cloud point for point: any xyz that differs,
// or a different total, is an error.
std::vector<std::uint16_t> readFrameClasses(const std::string& dir, const PcdCloud& gt, ClassCheck& chk) {
  std::vector<std::uint16_t> cls(gt.intensity.size(), 0);
  std::size_t idx = 0;
  for (const std::string& n : listPcd(dir + "/pcd")) {
    const PcdCloud c = readPcd(dir + "/pcd/" + n, true);
    ++chk.frames;
    if (c.intensity.size() * 3 != c.xyz.size()) fail(n, "no intensity");
    for (std::size_t i = 0; i < c.intensity.size(); ++i, ++idx) {
      if (idx >= cls.size()) fail(n, "frames hold more points than the GT cloud");
      if (std::memcmp(&c.xyz[3 * i], &gt.xyz[3 * idx], 12) != 0) fail(n, "point " + std::to_string(i) + " differs from GT point " + std::to_string(idx));
      const int k = classOfIntensity(c.intensity[i]);
      cls[idx] = static_cast<std::uint16_t>(k);
      const bool moving_class = k >= 252 && k <= 259, gt_dyn = gt.intensity[idx] == 1.0f;
      if (gt_dyn && !moving_class) ++chk.gt_dynamic_not_moving_class;
      if (!gt_dyn && moving_class) ++chk.moving_class_not_gt_dynamic;
    }
  }
  if (idx != cls.size()) fail(dir, "frames hold " + std::to_string(idx) + " points, the GT cloud " + std::to_string(cls.size()));
  return cls;
}

int cmdKthLabels(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  PcdCloud gt = readPcd(need(a, "gt"), true);
  KthLabelParams p; p.resolution = std::stod(need(a, "res"));
  p.ground_height_m = std::stod(opt(a, "ground-h", "0.30")); p.column_m = std::stod(opt(a, "column", "1.0"));
  p.ego_radius_m = std::stod(opt(a, "ego-radius", "0"));
  std::vector<Eigen::Vector3d> origins;
  if (p.ego_radius_m > 0.0) {
    const std::string dir = need(a, "dir");
    for (const std::string& n : listPcd(dir + "/pcd")) {
      std::ifstream f(dir + "/pcd/" + n, std::ios::binary);
      const PcdHeader h = readPcdHeader(f, n);
      if (!h.has_viewpoint) fail(n, "no VIEWPOINT");
      origins.push_back(h.t);
    }
  }
  KthLabelCounts c;
  const bool with_classes = std::stoi(opt(a, "classes", "0")) != 0;
  ClassCheck chk;
  std::vector<std::uint16_t> classes;
  if (with_classes) classes = readFrameClasses(need(a, "dir"), gt, chk);
  const LabelMap labels = buildKthLabels(gt.xyz, gt.intensity, p, &c, &origins, with_classes ? &classes : nullptr);
  writeLabels(need(a, "out"), 3, labels);
  Meta m(need(a, "meta"));
  if (with_classes) {
    m.put("class_groups", 1); m.put("class_frames", chk.frames);
    m.put("class_gt_dynamic_not_moving_class", chk.gt_dynamic_not_moving_class);
    m.put("class_moving_class_not_gt_dynamic", chk.moving_class_not_gt_dynamic);
    m.put("cells_structure_solid", c.structure_solid); m.put("cells_structure_vehicle", c.structure_vehicle);
    m.put("cells_structure_vegetation", c.structure_vegetation); m.put("cells_structure_other", c.structure_other);
  }
  m.put("resolution_m", p.resolution); m.put("ground_height_m", p.ground_height_m); m.put("column_m", p.column_m);
  m.put("gt_points", c.gt_points); m.put("gt_dynamic_points", c.gt_dynamic);
  m.put("gt_nonfinite_dropped", gt.dropped_nonfinite);
  m.put("ego_radius_m", p.ego_radius_m); m.put("frame_origins", origins.size());
  m.put("gt_dynamic_points_ego", c.gt_dynamic_ego); m.put("cells_ego_excluded", c.ego);
  m.put("cells_moving", c.moving); m.put("cells_static_ground", c.ground); m.put("cells_static_structure", c.structure);
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return 0;
}

int cmdKthGate(const std::map<std::string, std::string>& a) {
  const std::string dir = need(a, "dir");
  std::vector<PcdHeader> hdrs;
  for (const std::string& n : listPcd(dir + "/pcd")) {
    std::ifstream f(dir + "/pcd/" + n, std::ios::binary);
    hdrs.push_back(readPcdHeader(f, n));
  }
  const PoseSanity ps = checkPoses(hdrs);
  PcdCloud gt = readPcd(need(a, "gt"), true);
  std::vector<float> stat;
  std::size_t k = 0;
  for (std::size_t i = 0; i < gt.intensity.size(); ++i)
    if (gt.intensity[i] == 0.0f && (k++ % 4 == 0)) stat.insert(stat.end(), gt.xyz.begin() + 3 * i, gt.xyz.begin() + 3 * i + 3);
  ThicknessParams tp; tp.radius_m = 1.0; tp.min_points = 50; tp.min_extent_m = 0.6; tp.nz_max = 0.25;
  const ThicknessResult tr = wallThickness(stat, true, tp);
  Meta m(need(a, "meta"));
  m.put("frames", ps.frames); m.put("pose_bad_quaternion", ps.bad_quaternion);
  m.put("pose_big_translation_step", ps.big_translation_step); m.put("pose_big_rotation_step", ps.big_rotation_step);
  m.put("pose_max_step_m", ps.max_step_m); m.put("pose_max_rot_deg", ps.max_rot_deg); m.put("pose_ok", ps.ok() ? 1 : 0);
  m.put("thickness_points_used", stat.size() / 3);
  writeThicknessMeta(m, tr, std::stod(need(a, "res")), "");
  return 0;
}

int cmdKthMeasure(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const std::string dir = need(a, "dir");
  const double res = std::stod(need(a, "res"));
  const int W = std::stoi(need(a, "window"));
  if (W < 1) fail("arguments", "--window must be at least 1");
  const LabelMap labels = readLabels(need(a, "labels"));
  std::vector<std::string> names = listPcd(dir + "/pcd");
  const std::size_t first = std::stoul(opt(a, "first-frame", "0"));
  const std::size_t last = std::min<std::size_t>(names.size(), std::stoul(opt(a, "last-frame", std::to_string(names.size()))));
  if (first >= last) fail("arguments", "empty frame range");
  CellRecorder rec(labels, 1.0);
  strata_core::Voxel3DBackend b(res, engineParams(W));
  b.setWindowObserver(&rec);
  std::size_t i = first; std::size_t dropped = 0;
  auto next = [&](Frame& fr) {
    if (i >= last) return false;
    PcdCloud c = readPcd(dir + "/pcd/" + names[i]);
    if (!c.hdr.has_viewpoint) fail(names[i], "no VIEWPOINT");
    fr.group = static_cast<std::uint32_t>(i); fr.origin = c.hdr.t; fr.xyz = std::move(c.xyz);
    dropped += c.dropped_nonfinite;
    ++i; return true;
  };
  const DriveStats st = drive(b, next, W, &rec, nullptr);
  writeCells(need(a, "out"), rec, true, 0);
  Meta m(need(a, "meta"));
  writeDriveMeta(m, st, rec, nowSeconds() - t0, W, res);
  m.put("first_frame_index", first); m.put("last_frame_index_exclusive", last);
  m.put("frame_first_name", names[first]); m.put("frame_last_name", names[last - 1]);
  m.put("nonfinite_points_dropped", dropped);
  return 0;
}

// ------------------------------------------------------------------ LILocBench ----

std::string stripSlash(const std::string& s) { return (!s.empty() && s[0] == '/') ? s.substr(1) : s; }

Anchor parseAnchor(const std::string& s) {
  if (s == "start") return Anchor::Start;
  if (s == "end") return Anchor::End;
  if (s == "single") return Anchor::Single;
  fail("arguments", "--anchor must be start, end or single, got '" + s + "'");
}
const char* anchorName(Anchor a) { return a == Anchor::Start ? "start" : a == Anchor::End ? "end" : "single"; }

struct LilBag {
  std::vector<ScanRecord> front, rear;
  std::vector<Se2> front_ext, rear_ext;     // every /tf sample, to check that the extrinsics are constant
};

LilBag loadLilBag(const std::map<std::string, std::string>& a) {
  const std::string front_topic = opt(a, "front", "/laser_scan_front/scan"), rear_topic = opt(a, "rear", "/laser_scan_rear/scan");
  LilBag b;
  readBag(need(a, "bag"), [&](const BagMessage& m) {
    if (m.topic == front_topic || m.topic == rear_topic) {
      ScanRecord r; r.msg = decodeLaserScan(m.data, m.size); r.stamp = r.msg.stamp();
      (m.topic == front_topic ? b.front : b.rear).push_back(std::move(r));
    } else if (m.topic == "/tf") {
      for (const TfEdge& e : decodeTfMessage(m.data, m.size)) {
        if (stripSlash(e.parent) == "base_link" && stripSlash(e.child) == "scan_front_link") b.front_ext.push_back(se2FromTf(e));
        if (stripSlash(e.parent) == "base_link" && stripSlash(e.child) == "scan_rear_link") b.rear_ext.push_back(se2FromTf(e));
      }
    }
  });
  if (b.front.empty() || b.rear.empty()) fail(need(a, "bag"), "no scans on the front or rear topic");
  if (b.front_ext.empty() || b.rear_ext.empty()) fail(need(a, "bag"), "no /tf base_link -> scan_front_link / scan_rear_link");
  return b;
}

// Scan-to-GT time offset search (PREREG_P1.md section 11.1) on one sequence: writes the score of every
// candidate to a CSV and the selection to a meta file. Exit code 3 if the procedure fails its validity rule.
int cmdLilOffset(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const PoseTrack gt = readTumTrack(need(a, "gt"));
  LilBag bag = loadLilBag(a);
  const std::size_t stride = std::stoul(opt(a, "stride", "10"));
  if (stride < 1) fail("arguments", "--stride must be at least 1");
  const double half = std::stod(opt(a, "half-range", "0.30")), step = std::stod(opt(a, "step", "0.01"));
  const int nk = static_cast<int>(std::lround(half / step));
  std::vector<OffsetScore> all;
  std::ofstream csv(need(a, "out"));
  if (!csv) fail(need(a, "out"), "cannot write");
  csv.precision(10);
  csv << "anchor,dt_s,c_front,c_rear,score,dropped_share,hits\n";
  for (Anchor an : {Anchor::Start, Anchor::End, Anchor::Single})
    for (int k = -nk; k <= nk; ++k) {
      const double dt = k * step;
      const OffsetScore s = scoreOffset(gt, bag.front_ext[0], bag.rear_ext[0], bag.front, bag.rear, an, dt, stride);
      all.push_back(s);
      csv << anchorName(an) << "," << dt << "," << s.c_front << "," << s.c_rear << "," << s.score() << "," << s.dropped_share << "," << s.hits << "\n";
    }
  const OffsetChoice ch = selectOffset(all, half);
  Meta m(need(a, "meta"));
  m.put("stride", stride); m.put("candidates", all.size()); m.put("lowest_score", ch.lowest_score);
  m.put("selected_anchor", anchorName(ch.best.anchor)); m.put("selected_dt_s", ch.best.dt);
  m.put("selected_score", ch.best.score()); m.put("selected_dropped_share", ch.best.dropped_share);
  {
    const double ti_s = bag.front[0].msg.time_increment; const std::size_t n_b = bag.front[0].msg.ranges.size();
    m.put("selected_beam0_offset_s", ch.best.anchor == Anchor::End ? ch.best.dt - (n_b - 1) * ti_s : ch.best.dt);
  }
  m.put("valid", ch.ok ? 1 : 0); m.put("reason", ch.ok ? "ok" : ch.reason);
  // Per-laser optimum for the selected anchor (diagnostic only).
  double bf = 1e30, br = 1e30, dtf = 0, dtr = 0;
  for (const auto& s : all) {
    if (s.anchor != ch.best.anchor) continue;
    if (s.c_front < bf || (s.c_front == bf && std::fabs(s.dt) < std::fabs(dtf))) { bf = s.c_front; dtf = s.dt; }
    if (s.c_rear < br || (s.c_rear == br && std::fabs(s.dt) < std::fabs(dtr))) { br = s.c_rear; dtr = s.dt; }
  }
  m.put("front_only_best_dt_s", dtf); m.put("rear_only_best_dt_s", dtr);
  m.put("front_rear_optima_agree_within_0.02s", std::fabs(dtf - dtr) <= 0.02 + 1e-9 ? 1 : 0);
  m.put("scan_time_s", bag.front[0].msg.scan_time); m.put("time_increment_s", bag.front[0].msg.time_increment);
  m.put("beams_per_scan", bag.front[0].msg.ranges.size());
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return ch.ok ? 0 : 3;
}

int cmdLilConvert(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const PoseTrack gt = readTumTrack(need(a, "gt"));
  LilBag bag = loadLilBag(a);
  std::vector<ScanRecord>& front = bag.front; std::vector<ScanRecord>& rear = bag.rear;
  std::vector<Se2>& front_ext = bag.front_ext; std::vector<Se2>& rear_ext = bag.rear_ext;
  auto spread = [](const std::vector<Se2>& v) {
    double d = 0;
    for (const Se2& e : v) d = std::max({d, std::hypot(e.x - v[0].x, e.y - v[0].y), std::fabs(Se2::wrap(e.yaw - v[0].yaw))});
    return d;
  };
  ConvertReport rep;
  rep.counts_match = front.size() == rear.size();
  ConvertOptions co; co.deskew = std::stoi(need(a, "deskew")) != 0;
  co.anchor = parseAnchor(opt(a, "anchor", "start"));
  co.time_offset_s = std::stod(opt(a, "time-offset", "0"));
  const std::size_t n = std::min(front.size(), rear.size());
  std::vector<Frame> frames; frames.reserve(2 * n);
  double prev = 0, min_x = 1e30, max_x = -1e30, min_y = 1e30, max_y = -1e30;
  for (std::size_t k = 0; k < n; ++k) {
    const double skew = std::fabs(front[k].stamp - rear[k].stamp);
    rep.max_pair_skew_s = std::max(rep.max_pair_skew_s, skew);
    if (skew > co.max_pair_skew_s) rep.pair_skew_ok = false;
    if (skew > co.max_pair_skew_alt_s) rep.pair_skew_ok_alt = false;
    if (k > 0) rep.max_scan_gap_s = std::max(rep.max_scan_gap_s, front[k].stamp - prev);
    prev = front[k].stamp;
    const std::size_t before = frames.size();
    convertScanPair(gt, front_ext[0], rear_ext[0], front[k], rear[k], static_cast<std::uint32_t>(k), co, frames, rep);
    ++rep.steps;
    for (std::size_t f = before; f < frames.size(); ++f)
      for (std::size_t i = 0; i < frames[f].size(); ++i) {
        min_x = std::min<double>(min_x, frames[f].xyz[3 * i]); max_x = std::max<double>(max_x, frames[f].xyz[3 * i]);
        min_y = std::min<double>(min_y, frames[f].xyz[3 * i + 1]); max_y = std::max<double>(max_y, frames[f].xyz[3 * i + 1]);
      }
  }
  writeCache(need(a, "out"), 2, frames);
  Meta m(need(a, "meta"));
  m.put("deskew", co.deskew ? 1 : 0);
  m.put("anchor", anchorName(co.anchor)); m.put("time_offset_s", co.time_offset_s);
  m.put("scans_front", front.size()); m.put("scans_rear", rear.size()); m.put("counts_match", rep.counts_match ? 1 : 0);
  m.put("steps", rep.steps); m.put("frames_written", frames.size());
  m.put("scans_dropped_no_pose", rep.scans_dropped_no_pose);
  m.put("beams_total", rep.beams_total); m.put("beams_used", rep.beams_used);
  m.put("beams_no_return_or_out_of_range", rep.beams_invalid); m.put("beams_dropped_no_pose", rep.beams_no_pose);
  m.put("max_front_rear_stamp_skew_s", rep.max_pair_skew_s); m.put("pair_skew_ok", rep.pair_skew_ok ? 1 : 0);
  m.put("pair_skew_bound_s", co.max_pair_skew_s); m.put("pair_skew_ok_at_0.03", rep.pair_skew_ok_alt ? 1 : 0);
  m.put("max_front_scan_gap_s", rep.max_scan_gap_s);
  m.put("tf_samples_front", front_ext.size()); m.put("tf_samples_rear", rear_ext.size());
  m.put("tf_front_spread", spread(front_ext)); m.put("tf_rear_spread", spread(rear_ext));
  m.put("tf_front_x", front_ext[0].x); m.put("tf_front_y", front_ext[0].y); m.put("tf_front_yaw", front_ext[0].yaw);
  m.put("tf_rear_x", rear_ext[0].x); m.put("tf_rear_y", rear_ext[0].y); m.put("tf_rear_yaw", rear_ext[0].yaw);
  m.put("gt_poses", gt.size()); m.put("gt_t0", gt.t0()); m.put("gt_t1", gt.t1());
  double max_gt_gap = 0, max_step = 0, max_turn = 0;
  const auto& s = gt.samples();
  for (std::size_t i = 1; i < s.size(); ++i) {
    max_gt_gap = std::max(max_gt_gap, s[i].t - s[i - 1].t);
    max_step = std::max(max_step, std::hypot(s[i].pose.x - s[i - 1].pose.x, s[i].pose.y - s[i - 1].pose.y));
    max_turn = std::max(max_turn, std::fabs(Se2::wrap(s[i].pose.yaw - s[i - 1].pose.yaw)));
  }
  m.put("gt_max_gap_s", max_gt_gap); m.put("gt_max_step_m", max_step); m.put("gt_max_turn_rad", max_turn);
  m.put("scan_first_stamp", front.front().stamp); m.put("scan_last_stamp", front.back().stamp);
  m.put("hits_bbox_x_min", min_x); m.put("hits_bbox_x_max", max_x); m.put("hits_bbox_y_min", min_y); m.put("hits_bbox_y_max", max_y);
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return 0;
}

std::function<bool(Frame&)> cacheProvider(std::vector<Frame>& frames) {
  auto idx = std::make_shared<std::size_t>(0);
  return [&frames, idx](Frame& fr) {
    if (*idx >= frames.size()) return false;
    fr = std::move(frames[(*idx)++]);
    return true;
  };
}

strata_core::GridMeta gridFromArgs(const std::map<std::string, std::string>& a, double res) {
  strata_core::GridMeta g;
  const double o = std::stod(opt(a, "grid-origin", "-30")), size = std::stod(opt(a, "grid-size", "60"));
  g.resolution = res; g.origin_x = o; g.origin_y = o;
  g.width = g.height = static_cast<int>(std::ceil(size / res));
  return g;
}

int cmdLilGate(const std::map<std::string, std::string>& a) {
  std::vector<Frame> frames = readCache(need(a, "cache"));
  std::vector<float> pts; std::size_t k = 0;
  const int stride = std::stoi(opt(a, "stride", "5"));
  for (const Frame& f : frames)
    for (std::size_t i = 0; i < f.size(); ++i)
      if (k++ % stride == 0) pts.insert(pts.end(), f.xyz.begin() + 3 * i, f.xyz.begin() + 3 * i + 3);
  ThicknessParams tp; tp.radius_m = 0.5; tp.min_points = 20; tp.min_extent_m = 0.6;
  const ThicknessResult tr = wallThickness(pts, false, tp);
  Meta m(need(a, "meta"));
  m.put("hits_total", k); m.put("stride", stride); m.put("thickness_points_used", pts.size() / 3);
  writeThicknessMeta(m, tr, std::stod(need(a, "res")), "");
  return 0;
}

int cmdLilReference(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const double res = std::stod(need(a, "res"));
  std::vector<Frame> frames = readCache(need(a, "cache"));
  const strata_core::GridMeta grid = gridFromArgs(a, res);
  ReferenceRecorder ref;
  strata_core::Grid2DBackend b(grid, engineParams(1));
  b.setWindowObserver(&ref);
  const DriveStats st = drive(b, cacheProvider(frames), 1, nullptr, &grid);
  RefParams rp; rp.min_touches = std::stoi(opt(a, "min-touches", "10"));
  rp.occupied_frac = std::stod(opt(a, "occ", "0.5")); rp.free_frac = std::stod(opt(a, "free", "0.05"));
  rp.margin_cells = std::stoi(opt(a, "margin", "0"));
  const LabelMap labels = ref.labels(rp, grid.width);
  std::size_t occ = 0, fre = 0, near = 0;
  for (const auto& kv : labels) (kv.second == kRefOccupied ? occ : kv.second == kRefFree ? fre : near)++;
  writeLabels(need(a, "out"), 2, labels);
  Meta m(need(a, "meta"));
  m.put("resolution_m", res); m.put("steps", st.steps); m.put("windows_closed", st.windows);
  m.put("hits_total", st.hits_total); m.put("hits_off_grid", st.hits_off_grid);
  m.put("min_touches", rp.min_touches); m.put("occupied_frac", rp.occupied_frac); m.put("free_frac", rp.free_frac);
  m.put("cells_touched", ref.cells()); m.put("margin_cells", rp.margin_cells);
  m.put("cells_ref_occupied", occ); m.put("cells_ref_free", fre); m.put("cells_ref_free_near_occupied", near);
  m.put("grid_width", grid.width); m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return 0;
}

int cmdLilMeasure(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const double res = std::stod(need(a, "res"));
  const int W = std::stoi(need(a, "window"));
  if (W < 1) fail("arguments", "--window must be at least 1");
  std::vector<Frame> frames = readCache(need(a, "cache"));
  const LabelMap labels = readLabels(need(a, "labels"));
  const strata_core::GridMeta grid = gridFromArgs(a, res);
  CellRecorder rec(labels, 1.0);
  strata_core::Grid2DBackend b(grid, engineParams(W));
  b.setWindowObserver(&rec);
  const DriveStats st = drive(b, cacheProvider(frames), W, &rec, &grid);
  writeCells(need(a, "out"), rec, false, grid.width);
  Meta m(need(a, "meta"));
  writeDriveMeta(m, st, rec, nowSeconds() - t0, W, res);
  m.put("grid_width", grid.width);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "usage: hitrate_probe <kth-labels|kth-gate|kth-measure|lil-offset|lil-convert|lil-gate|lil-reference|lil-measure> --key value ...\n"; return 2; }
  try {
    const std::string cmd = argv[1];
    const auto a = parseArgs(argc, argv, 2);
    if (cmd == "kth-labels") return cmdKthLabels(a);
    if (cmd == "kth-gate") return cmdKthGate(a);
    if (cmd == "kth-measure") return cmdKthMeasure(a);
    if (cmd == "lil-convert") return cmdLilConvert(a);
    if (cmd == "lil-gate") return cmdLilGate(a);
    if (cmd == "lil-offset") return cmdLilOffset(a);
    if (cmd == "lil-reference") return cmdLilReference(a);
    if (cmd == "lil-measure") return cmdLilMeasure(a);
    std::cerr << "unknown subcommand " << cmd << "\n"; return 2;
  } catch (const std::exception& e) {
    std::cerr << "hitrate_probe: " << e.what() << "\n"; return 1;
  }
}
