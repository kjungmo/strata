// p1b_probe: phase 1b tools (PREREG_P1B.md). Single-threaded, no ROS.
//   gate2d        G1 and G2 for a LILocBench frame cache
//   gate-kth      G1 and G2 for KTH KITTI 00 (frames and GT cloud)
//   gate-spires   G1 and G2 for a Spires frame cache
//   spires-tls    TLS map -> voxel tables, only inside the Keble 02 area (section 4.2)
//   spires-offset scan-to-GT time offset search and alignment check (sections 4.4, 4.5)
//   spires-convert  Spires zip + GT poses -> frame cache in the TLS frame (section 4.3)
//   spires-labels   TLS tables + frame cache -> cell labels (section 4.6)
//   spires-measure  frame cache + labels -> per-cell records, both miss definitions (sections 4.7, 5)
//   kth-measure3    KTH frames + labels -> per-cell records through the lean probe (equivalence and secondary check)
#include <dirent.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "p1b_core.hpp"

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
double nowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
long peakRssKb() {
  std::ifstream f("/proc/self/status"); std::string line;
  while (std::getline(f, line)) if (line.rfind("VmHWM:", 0) == 0) return std::atol(line.c_str() + 6);
  return -1;
}
std::string wallClock() {
  const std::time_t t = std::time(nullptr);
  char buf[64];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S%z", std::localtime(&t));
  return buf;
}
void log(const std::string& s) { std::cerr << wallClock() << " " << s << std::endl; }

struct Meta {
  std::ofstream o;
  explicit Meta(const std::string& path) : o(path) { if (!o) fail(path, "cannot write"); o.precision(10); }
  template <class T> void put(const std::string& k, const T& v) { o << k << "=" << v << "\n"; }
};

std::vector<int> parseIntList(const std::string& s) {
  std::vector<int> v; std::stringstream ss(s); std::string t;
  while (std::getline(ss, t, ',')) v.push_back(std::stoi(t));
  if (v.empty()) fail("arguments", "empty list");
  return v;
}

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

// -------------------------------------------------------------------- gates ----------

// Thresholds of PREREG_P1B.md section 3.
struct GateThr { double t50, t90, d50, d90; };
const GateThr kThr2d{0.10, 0.20, 0.05, 0.10};
const GateThr kThr3d{0.20, 0.40, 0.10, 0.20};

void putGate(Meta& m, const ShapedResult& g1, const GateVerdictG1& v1, const ConsistencyResult& g2, const GateVerdictG2& v2, const GateThr& thr,
             const ShapeParams& sp, const ConsistencyParams& cp) {
  m.put("g1_radius_m", sp.radius_m); m.put("g1_seeds", g1.seeds); m.put("g1_with_points", g1.with_points); m.put("g1_accepted", g1.accepted);
  m.put("g1_thickness_m_p50", v1.p50); m.put("g1_thickness_m_p90", v1.p90);
  m.put("g1_thickness_m_p10", percentile(g1.thickness_m, 10)); m.put("g1_thickness_m_p25", percentile(g1.thickness_m, 25));
  m.put("g1_thickness_m_p75", percentile(g1.thickness_m, 75));
  m.put("g1_limit_p50_m", thr.t50); m.put("g1_limit_p90_m", thr.t90); m.put("g1_pass", v1.pass ? 1 : 0);
  m.put("g2_query_every", cp.query_every); m.put("g2_lag_lo", cp.lag_lo); m.put("g2_lag_hi", cp.lag_hi);
  m.put("g2_ref_stride", cp.ref_stride); m.put("g2_query_stride", cp.query_stride);
  m.put("g2_queries", g2.queries); m.put("g2_query_points", g2.query_points); m.put("g2_matched", g2.matched);
  m.put("g2_matched_share", v2.matched_share); m.put("g2_d_m_p50", v2.p50); m.put("g2_d_m_p90", v2.p90);
  m.put("g2_d_m_p25", percentile(g2.d_m, 25)); m.put("g2_d_m_p75", percentile(g2.d_m, 75));
  m.put("g2_limit_p50_m", thr.d50); m.put("g2_limit_p90_m", thr.d90); m.put("g2_pass", v2.pass ? 1 : 0);
  m.put("gates_pass", (v1.pass && v2.pass) ? 1 : 0);
}

int cmdGate2d(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  CacheReader cache(need(a, "cache"));
  std::vector<float> pts; std::size_t k = 0;
  const int stride = std::stoi(opt(a, "stride", "5"));
  std::vector<std::vector<float>> steps;
  ConsistencyParams cp; cp.three_d = false; cp.query_every = 10; cp.lag_lo = 15; cp.lag_hi = 60; cp.ref_stride = 1; cp.query_stride = 1;
  Frame fr;
  while (cache.next(fr)) {
    for (std::size_t i = 0; i < fr.size(); ++i)
      if (k++ % stride == 0) pts.insert(pts.end(), fr.xyz.begin() + 3 * i, fr.xyz.begin() + 3 * i + 3);
    addFrameToSteps(steps, fr, cp.ref_stride);
  }
  ShapeParams sp; sp.three_d = false; sp.radius_m = 1.0;
  const ShapedResult g1 = shapedThickness(pts, sp);
  const GateVerdictG1 v1 = verdictG1(g1, kThr2d.t50, kThr2d.t90);
  const ConsistencyResult g2 = poseConsistency(steps, cp);
  const GateVerdictG2 v2 = verdictG2(g2, kThr2d.d50, kThr2d.d90);
  Meta m(need(a, "meta"));
  m.put("kind", "gate2d"); m.put("hits_total", k); m.put("g1_points_used", pts.size() / 3); m.put("steps", steps.size());
  putGate(m, g1, v1, g2, v2, kThr2d, sp, cp);
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return 0;
}

int cmdGateKth(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const std::string dir = need(a, "dir");
  ConsistencyParams cp; cp.three_d = true; cp.query_every = 10; cp.lag_lo = 10; cp.lag_hi = 40; cp.ref_stride = 4; cp.query_stride = 8;
  std::vector<std::vector<float>> steps;
  const std::vector<std::string> names = listPcd(dir + "/pcd");
  for (std::size_t i = 0; i < names.size(); ++i) {
    PcdCloud c = readPcd(dir + "/pcd/" + names[i]);
    Frame fr; fr.group = static_cast<std::uint32_t>(i); fr.xyz = std::move(c.xyz);
    addFrameToSteps(steps, fr, cp.ref_stride);
  }
  PcdCloud gt = readPcd(need(a, "gt"), true);
  std::vector<float> stat; std::size_t k = 0;
  for (std::size_t i = 0; i < gt.intensity.size(); ++i)
    if (gt.intensity[i] == 0.0f && (k++ % 4 == 0)) stat.insert(stat.end(), gt.xyz.begin() + 3 * i, gt.xyz.begin() + 3 * i + 3);
  ShapeParams sp; sp.three_d = true; sp.radius_m = 2.0;
  const ShapedResult g1 = shapedThickness(stat, sp);
  const GateVerdictG1 v1 = verdictG1(g1, kThr3d.t50, kThr3d.t90);
  const ConsistencyResult g2 = poseConsistency(steps, cp);
  const GateVerdictG2 v2 = verdictG2(g2, kThr3d.d50, kThr3d.d90);
  Meta m(need(a, "meta"));
  m.put("kind", "gate-kth"); m.put("frames", names.size()); m.put("g1_points_used", stat.size() / 3);
  putGate(m, g1, v1, g2, v2, kThr3d, sp, cp);
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return 0;
}

int cmdGateSpires(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  CacheReader cache(need(a, "cache"));
  ConsistencyParams cp; cp.three_d = true; cp.query_every = 10; cp.lag_lo = 10; cp.lag_hi = 40; cp.ref_stride = 4; cp.query_stride = 8;
  std::vector<std::vector<float>> steps;
  std::vector<float> pts; std::size_t k = 0;
  const int stride = std::stoi(opt(a, "stride", "50"));
  Frame fr;
  while (cache.next(fr)) {
    for (std::size_t i = 0; i < fr.size(); ++i)
      if (k++ % stride == 0) pts.insert(pts.end(), fr.xyz.begin() + 3 * i, fr.xyz.begin() + 3 * i + 3);
    addFrameToSteps(steps, fr, cp.ref_stride);
  }
  ShapeParams sp; sp.three_d = true; sp.radius_m = 2.0;
  const ShapedResult g1 = shapedThickness(pts, sp);
  const GateVerdictG1 v1 = verdictG1(g1, kThr3d.t50, kThr3d.t90);
  const ConsistencyResult g2 = poseConsistency(steps, cp);
  const GateVerdictG2 v2 = verdictG2(g2, kThr3d.d50, kThr3d.d90);
  Meta m(need(a, "meta"));
  m.put("kind", "gate-spires"); m.put("returns_total", k); m.put("g1_points_used", pts.size() / 3); m.put("steps", steps.size());
  putGate(m, g1, v1, g2, v2, kThr3d, sp, cp);
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return 0;
}

// -------------------------------------------------------------------- Spires ---------

std::vector<ZipArchive::Entry> pcdEntries(ZipArchive& z) {
  std::vector<ZipArchive::Entry> v;
  for (const auto& e : z.entries()) if (e.name.size() > 4 && e.name.substr(e.name.size() - 4) == ".pcd") v.push_back(e);
  std::sort(v.begin(), v.end(), [](const ZipArchive::Entry& x, const ZipArchive::Entry& y) { return x.name < y.name; });
  return v;
}

int cmdSpiresTls(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const PoseTrack3 gt = readTumTrack3(need(a, "gt"));
  std::vector<Eigen::Vector2d> pos;
  for (const auto& s : gt.samples()) pos.emplace_back(s.pose.t.x(), s.pose.t.y());
  const double radius = std::stod(opt(a, "radius", "30"));
  DiscUnion area(pos, radius);
  log("reading the TLS map, keeping points within " + std::to_string(radius) + " m of the Keble 02 track");
  const std::vector<double> res = {0.2, 0.1, 0.05};
  TlsTables t = readTlsArea(need(a, "pcd"), area, res);
  const std::string dir = need(a, "out-dir");
  for (std::size_t i = 0; i < res.size(); ++i) {
    char name[64]; std::snprintf(name, sizeof name, "/tls_r%.2f.tbl", res[i]);
    saveVoxTable(dir + name, t.vox[i], res[i]);
  }
  saveVoxTable(dir + "/tls_columns.tbl", t.columns, 1.0);
  Meta m(need(a, "meta"));
  m.put("area_radius_m", radius); m.put("track_positions", pos.size());
  m.put("tls_points_read", t.points_read); m.put("tls_points_kept", t.points_kept);
  m.put("tls_points_discarded_outside_area", t.points_discarded); m.put("tls_points_nonfinite", t.points_nonfinite);
  for (std::size_t i = 0; i < res.size(); ++i) m.put("tls_voxels_r" + std::to_string(res[i]), t.vox[i].size());
  m.put("tls_columns", t.columns.size());
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return 0;
}

int cmdSpiresOffset(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const PoseTrack3 gt = readTumTrack3(need(a, "gt"));
  const std::string dir = need(a, "tls-dir");
  const VoxTable t005 = loadVoxTable(dir + "/tls_r0.05.tbl");
  const VoxTable t01 = loadVoxTable(dir + "/tls_r0.10.tbl");
  ZipArchive zip(need(a, "zip"));
  const auto entries = pcdEntries(zip);
  if (entries.empty()) fail(need(a, "zip"), "no .pcd members");
  const std::size_t stride = std::stoul(opt(a, "stride", "30"));
  std::vector<SpiresPcd> scans; std::vector<double> times;
  std::vector<std::uint8_t> buf;
  for (std::size_t k = 0; k < entries.size(); k += stride) {
    zip.read(entries[k], buf);
    scans.push_back(parsePcdBuffer(buf, entries[k].name));
    times.push_back(scanTimeFromName(entries[k].name));
  }
  log("loaded " + std::to_string(scans.size()) + " sample scans of " + std::to_string(entries.size()));
  SpiresExtrinsic ex; SpiresConvertOptions co;
  const double half = std::stod(opt(a, "half-range", "0.15")), step = std::stod(opt(a, "step", "0.01"));
  const int nk = static_cast<int>(std::lround(half / step));
  std::vector<OffsetScore3> all;
  std::ofstream csv(need(a, "csv"));
  if (!csv) fail(need(a, "csv"), "cannot write");
  csv.precision(10);
  csv << "dt_s,score,returns_used,points_no_pose\n";
  for (int i = -nk; i <= nk; ++i) {
    const OffsetScore3 s = scoreSpiresOffset(scans, times, gt, ex, co, i * step, t005);
    all.push_back(s);
    csv << s.dt << "," << s.score << "," << s.used << "," << s.no_pose_points << "\n";
  }
  const int pick = selectSpiresOffset(all);
  OffsetScore3 zero = all[nk];
  const OffsetScore3& best = all[pick];
  const double align = alignmentShare(scans, times, gt, ex, co, best.dt, t01);
  // share of points without a pose at the selected dt: all kept points of the sample vs the used ones
  SpiresConvertOptions c2 = co; c2.dt = best.dt;
  SpiresConvertReport rep; Frame fr;
  for (std::size_t k = 0; k < scans.size(); ++k) convertSpiresScan(scans[k], times[k], gt, ex, c2, 0, fr, rep);
  const double kept = static_cast<double>(rep.points_used + rep.points_no_pose);
  const double no_pose_share = kept > 0 ? rep.points_no_pose / kept : 1.0;
  const bool edge = std::fabs(best.dt) >= half - 1e-9;
  const bool valid = !edge && no_pose_share <= 0.01;
  const bool align_ok = align >= 0.5;
  Meta m(need(a, "meta"));
  m.put("sample_scans", scans.size()); m.put("scan_stride", stride); m.put("scans_in_zip", entries.size());
  m.put("candidates", all.size()); m.put("selected_dt_s", best.dt); m.put("selected_score", best.score);
  m.put("score_at_dt0", zero.score); m.put("highest_score", [&] { double b = 0; for (auto& s : all) b = std::max(b, s.score); return b; }());
  m.put("selected_no_pose_share", no_pose_share); m.put("selected_on_grid_edge", edge ? 1 : 0);
  m.put("scans_scan_time_fallback", rep.scans_scan_time_fallback); m.put("scans_no_timestamp_field", rep.scans_no_timestamp_field);
  m.put("alignment_share", align); m.put("alignment_ok", align_ok ? 1 : 0);
  m.put("valid", valid ? 1 : 0);
  m.put("reason", valid ? (align_ok ? "ok" : "alignment check failed") : (edge ? "the selected offset lies on the edge of the grid" : "more than 1 % of the points lack a pose"));
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return (valid && align_ok) ? 0 : 3;
}

int cmdSpiresConvert(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const PoseTrack3 gt = readTumTrack3(need(a, "gt"));
  ZipArchive zip(need(a, "zip"));
  const auto entries = pcdEntries(zip);
  if (entries.empty()) fail(need(a, "zip"), "no .pcd members");
  SpiresExtrinsic ex; SpiresConvertOptions co;
  co.dt = std::stod(need(a, "dt"));
  co.r_min = std::stod(opt(a, "r-min", "1.5")); co.r_max = std::stod(opt(a, "r-max", "20"));
  CacheWriter w(need(a, "out"), 3);
  SpiresConvertReport rep;
  std::vector<std::uint8_t> buf; Frame fr;
  double first_t = 0, last_t = 0; std::size_t max_pts = 0;
  for (std::size_t k = 0; k < entries.size(); ++k) {
    zip.read(entries[k], buf);
    const SpiresPcd s = parsePcdBuffer(buf, entries[k].name);
    const double st = scanTimeFromName(entries[k].name);
    if (k == 0) first_t = st; last_t = st;
    if (convertSpiresScan(s, st, gt, ex, co, static_cast<std::uint32_t>(k), fr, rep)) { w.add(fr); max_pts = std::max(max_pts, fr.size()); }
    if ((k + 1) % 500 == 0) log("converted " + std::to_string(k + 1) + " of " + std::to_string(entries.size()) + " scans");
  }
  w.finish();
  Meta m(need(a, "meta"));
  m.put("dt_s", co.dt); m.put("r_min_m", co.r_min); m.put("r_max_m", co.r_max);
  m.put("scans_in_zip", entries.size()); m.put("scans_written", w.frames()); m.put("scans_dropped_no_pose", rep.scans_dropped_no_pose);
  m.put("scans_scan_time_fallback", rep.scans_scan_time_fallback); m.put("scans_no_timestamp_field", rep.scans_no_timestamp_field);
  m.put("points_in_header", rep.points_in); m.put("points_nonfinite", rep.points_nonfinite);
  m.put("points_out_of_range", rep.points_out_of_range); m.put("points_no_pose", rep.points_no_pose); m.put("points_used", rep.points_used);
  m.put("max_points_in_frame", max_pts);
  m.put("first_scan_time", first_t); m.put("last_scan_time", last_t);
  m.put("gt_poses", gt.size()); m.put("gt_t0", gt.t0()); m.put("gt_t1", gt.t1());
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return 0;
}

int cmdSpiresLabels(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const double res = std::stod(need(a, "res"));
  const std::string dir = need(a, "tls-dir");
  char name[64]; std::snprintf(name, sizeof name, "/tls_r%.2f.tbl", res);
  const VoxTable tls = loadVoxTable(dir + name);
  const VoxTable columns = loadVoxTable(dir + "/tls_columns.tbl");
  CacheReader cache(need(a, "cache"));
  log("collecting hit voxels at " + std::to_string(res) + " m");
  const VoxTable hits = collectHitVoxels(cache, res);
  SpiresLabelParams lp; lp.res = res; lp.quota = std::stoul(opt(a, "quota", "2000000"));
  SpiresLabelCounts c;
  const LabelMap labels = buildSpiresLabels(tls, columns, hits, lp, &c);
  writeLabels(need(a, "out"), 3, labels);
  Meta m(need(a, "meta"));
  m.put("resolution_m", res); m.put("quota", lp.quota);
  m.put("tls_voxels", c.tls_voxels); m.put("tls_voxels_occupied", c.tls_occupied);
  m.put("structure_all", c.structure_all); m.put("ground_all", c.ground_all); m.put("clutter_all", c.clutter_all);
  m.put("structure_kept", c.structure); m.put("ground_kept", c.ground); m.put("clutter_kept", c.clutter);
  m.put("keep_fraction_structure", c.keep_structure); m.put("keep_fraction_ground", c.keep_ground); m.put("keep_fraction_clutter", c.keep_clutter);
  m.put("hit_voxels", c.hit_voxels); m.put("hit_voxels_tls_present", c.hit_voxels_tls_present);
  m.put("hit_voxels_buffer", c.hit_voxels_buffer); m.put("hit_voxels_unsupported_column", c.hit_voxels_unsupported);
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return 0;
}

// ------------------------------------------------------------------ lean measurement --

void writeCellsLean(const std::string& path, const CellRecorder& rec) {
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
    decodeVoxel(id, ix, iy, iz);
    o << int(rec.labels().at(id)) << "," << ix << "," << iy << "," << iz << "," << s.n << "," << s.h << ","
      << s.first << "," << s.last << "," << s.centre_windows.size() << "," << s.runs.str() << "\n";
  }
}

// frame source: returns false at the end. steps = frames here (one frame is one integration step).
int runLean(const std::map<std::string, std::string>& a, const std::function<bool(Frame&)>& next, const std::string& tag) {
  const double t0 = nowSeconds();
  const double res = std::stod(need(a, "res"));
  const std::vector<int> wins = parseIntList(need(a, "windows"));
  const LabelMap labels = readLabels(need(a, "labels"));
  std::vector<LeanVoxelProbe::Spec> specs;
  for (int w : wins) { specs.push_back({w, false}); specs.push_back({w, true}); }
  LeanVoxelProbe probe(res, labels, specs);
  Frame fr; std::size_t frames = 0, hits = 0;
  while (next(fr)) {
    probe.beginFrame(fr.origin);
    probe.integrate(fr.xyz, fr.origin);
    probe.endFrame();
    ++frames; hits += fr.size();
    if (frames % 250 == 0) log(tag + ": " + std::to_string(frames) + " frames, " + std::to_string(hits) + " returns");
  }
  const double secs = nowSeconds() - t0;
  const std::string prefix = need(a, "out-prefix");
  const auto& cn = probe.counters();
  for (std::size_t i = 0; i < probe.channels(); ++i) {
    const auto& c = probe.channel(i);
    char stem[256];
    std::snprintf(stem, sizeof stem, "%s_W%d_%s", prefix.c_str(), c.spec.W, c.spec.refined ? "refined" : "today");
    writeCellsLean(std::string(stem) + ".csv", *c.rec);
    Meta m(std::string(stem) + ".meta");
    m.put("window_steps", c.spec.W); m.put("resolution_m", res); m.put("miss_definition", c.spec.refined ? "refined" : "today");
    m.put("frames", frames); m.put("steps", frames); m.put("windows_closed", c.windows);
    m.put("hits_total", hits);
    m.put("observer_reports_labelled", c.rec->labelledReports());
    m.put("observer_inconsistent", c.rec->inconsistent()); m.put("window_origin_missing", c.rec->missingOrigin());
    m.put("labelled_cells_in_file", labels.size()); m.put("labelled_cells_touched", c.rec->stats().size());
    m.put("rays", cn.rays); m.put("march_samples", cn.samples); m.put("labelled_samples", cn.labelled_samples);
    m.put("grazing_structure_miss_events", cn.structure_miss_events); m.put("grazing_structure_miss_events_with_endpoint_within_1_voxel", cn.structure_miss_events_graze);
    if (!c.spec.refined) {
      m.put("grazing_structure_miss_windows", c.structure_miss_windows);
      m.put("grazing_structure_miss_windows_with_graze_event", c.structure_miss_windows_with_graze);
    }
    m.put("seconds", secs); m.put("peak_rss_kb", peakRssKb());
  }
  return 0;
}

int cmdSpiresMeasure(const std::map<std::string, std::string>& a) {
  CacheReader cache(need(a, "cache"));
  return runLean(a, [&](Frame& fr) { return cache.next(fr); }, "spires-measure");
}

int cmdKthMeasure3(const std::map<std::string, std::string>& a) {
  const std::string dir = need(a, "dir");
  const std::vector<std::string> names = listPcd(dir + "/pcd");
  std::size_t i = 0;
  auto next = [&](Frame& fr) {
    if (i >= names.size()) return false;
    PcdCloud c = readPcd(dir + "/pcd/" + names[i]);
    if (!c.hdr.has_viewpoint) fail(names[i], "no VIEWPOINT");
    fr.group = static_cast<std::uint32_t>(i); fr.origin = c.hdr.t; fr.xyz = std::move(c.xyz);
    ++i; return true;
  };
  return runLean(a, next, "kth-measure3");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "usage: p1b_probe <gate2d|gate-kth|gate-spires|spires-tls|spires-offset|spires-convert|spires-labels|spires-measure|kth-measure3> --key value ...\n"; return 2; }
  try {
    const std::string cmd = argv[1];
    const auto a = parseArgs(argc, argv, 2);
    if (cmd == "gate2d") return cmdGate2d(a);
    if (cmd == "gate-kth") return cmdGateKth(a);
    if (cmd == "gate-spires") return cmdGateSpires(a);
    if (cmd == "spires-tls") return cmdSpiresTls(a);
    if (cmd == "spires-offset") return cmdSpiresOffset(a);
    if (cmd == "spires-convert") return cmdSpiresConvert(a);
    if (cmd == "spires-labels") return cmdSpiresLabels(a);
    if (cmd == "spires-measure") return cmdSpiresMeasure(a);
    if (cmd == "kth-measure3") return cmdKthMeasure3(a);
    std::cerr << "unknown subcommand " << cmd << "\n"; return 2;
  } catch (const std::exception& e) {
    std::cerr << "p1b_probe: " << e.what() << "\n"; return 1;
  }
}
