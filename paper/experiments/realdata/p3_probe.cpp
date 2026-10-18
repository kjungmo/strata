// p3_probe: phase 3 harness (PREREG_P3.md). Single-threaded, no ROS. Subcommands:
//   arms           print the arms as engine parameters (the text of PREREG_P3.md section 2)
//   kth-validate   reader check on the first N frames of a KTH sequence directory
//   kth-export     one arm over a KTH sequence -> original points whose voxel is kept, per export policy
//   spires-validate  reader check on the first N scans of a Spires lidar-clouds.zip and its poses
//   boreas-validate  reader check on the first N lidar frames of a Boreas sequence, its pose files and the common frame
//   boreas-overlap   common frame and 10 m tile overlap of the trajectories of several Boreas sequences (poses only)
//   boreas-convert   Boreas sequence -> frame cache in the common frame (deskewed, 3 m to 50 m)
//   static-export    3D frame caches + arm -> Static and observed voxel id sets (independent or cumulative sessions)
//   lil-validate   reader check on the first N scan pairs of a LILocBench bag and its GT poses
//   ref-export     reference label file (hitrate_probe lil-reference) -> CSV of cells
//   arm-eval       frame cache + label file + arm -> final verdict of every labelled cell (2D) or counts (3D)
// The validate subcommands decode only the first N records and write what they saw to stdout; they select nothing.
#include <sys/stat.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>
#include "p3_boreas.hpp"
#include "p3_kth.hpp"

using namespace realdata;
using namespace realdata::p3;

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
struct Meta {
  std::ofstream o;
  explicit Meta(const std::string& path) : o(path) { if (!o) fail(path, "cannot write"); o.precision(10); }
  template <class T> void put(const std::string& k, const T& v) { o << k << "=" << v << "\n"; }
};
bool flag(const std::map<std::string, std::string>& a, const std::string& k, bool d) {
  auto it = a.find(k);
  if (it == a.end()) return d;
  if (it->second == "1" || it->second == "on" || it->second == "true") return true;
  if (it->second == "0" || it->second == "off" || it->second == "false") return false;
  fail("arguments", "--" + k + " must be 0 or 1");
}
std::vector<std::string> splitList(const std::string& s) {
  std::vector<std::string> v; std::stringstream ss(s); std::string t;
  while (std::getline(ss, t, ',')) if (!t.empty()) v.push_back(t);
  return v;
}
Policy parsePolicy(const std::string& s) {
  if (s == "static_only") return Policy::StaticOnly;
  if (s == "not_rejected") return Policy::NotRejected;
  if (s == "all") return Policy::All;
  fail("arguments", "unknown policy '" + s + "' (static_only, not_rejected, all)");
}
struct Verdicts { bool ok{true}; std::vector<std::string> notes; void check(bool c, const std::string& why) { if (!c) { ok = false; notes.push_back(why); } } };
int finishValidate(const Verdicts& v, const std::string& what) {
  if (v.ok) { std::cout << "VERDICT " << what << " reader OK\n"; return 0; }
  std::cout << "VERDICT " << what << " reader FAILED\n";
  for (const auto& n : v.notes) std::cout << "  - " << n << "\n";
  return 3;
}

// ------------------------------------------------------------------------------- arms ----------
int cmdArms(const std::map<std::string, std::string>&) {
  std::cout << "engine_has_sprt=" << (engineHasSprt() ? 1 : 0) << "\n";
  for (const auto& n : armNames()) std::cout << describe(armSpec(n)) << "\n";
  for (const auto& n : extraArmNames()) std::cout << describe(armSpec(n)) << "\n";
  return 0;
}

// -------------------------------------------------------------------------------- KTH ----------
int cmdKthValidate(const std::map<std::string, std::string>& a) {
  const std::string dir = need(a, "dir");
  const std::size_t n = std::stoul(opt(a, "n", "10"));
  const auto files = listPcdFiles(dir + "/pcd");
  Verdicts v;
  std::cout << "frames_in_directory=" << files.size() << " first=" << (files.empty() ? "" : files.front()) << " last=" << (files.empty() ? "" : files.back()) << "\n";
  v.check(!files.empty(), "no .pcd frames");
  for (std::size_t k = 0; k < std::min(n, files.size()); ++k) {
    const PcdCloud c = readPcd(dir + "/pcd/" + files[k], true);
    std::vector<double> r; r.reserve(c.xyz.size() / 3);
    double zmin = 1e300, zmax = -1e300, emin = 1e300, emax = -1e300;
    for (std::size_t i = 0; i < c.xyz.size(); i += 3) {
      const Eigen::Vector3d d = Eigen::Vector3d(c.xyz[i], c.xyz[i + 1], c.xyz[i + 2]) - c.hdr.t;
      const Eigen::Vector3d s = c.hdr.q.normalized().inverse() * d;          // sensor frame
      r.push_back(s.norm());
      const double el = std::atan2(s.z(), std::hypot(s.x(), s.y())) * 180.0 / M_PI;
      zmin = std::min(zmin, s.z()); zmax = std::max(zmax, s.z()); emin = std::min(emin, el); emax = std::max(emax, el);
    }
    std::sort(r.begin(), r.end());
    const double med = r.empty() ? 0 : r[r.size() / 2], mx = r.empty() ? 0 : r.back();
    std::cout << files[k] << " points=" << c.hdr.points << " finite=" << c.xyz.size() / 3 << " viewpoint=" << c.hdr.has_viewpoint
              << " q_norm=" << c.hdr.q_norm << " range_median=" << med << " range_max=" << mx << " elev_deg=[" << emin << "," << emax << "]\n";
    v.check(c.hdr.has_viewpoint, files[k] + ": no VIEWPOINT");
    v.check(std::fabs(c.hdr.q_norm - 1.0) <= 1e-3, files[k] + ": VIEWPOINT quaternion not unit");
    v.check(c.xyz.size() / 3 > 1000, files[k] + ": fewer than 1000 finite points");
    v.check(med > 0.5 && med < 60.0, files[k] + ": median sensor range outside 0.5 to 60 m (points not in the world frame, or pose convention wrong)");
    v.check(emax > emin && emax - emin > 10.0 && emax - emin < 180.0, files[k] + ": elevation span implausible");
  }
  return finishValidate(v, "kth");
}

int cmdKthExport(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const ArmSpec arm = armSpec(need(a, "arm"));
  KthExportOptions o;
  o.res = std::stod(need(a, "res")); o.window_steps = std::stoi(need(a, "window")); o.periodicity = flag(a, "periodicity", true);
  o.max_range = std::stod(opt(a, "max-range", "0")); o.max_frames = std::stoul(opt(a, "max-frames", "0"));
  o.policies.clear();
  for (const auto& p : splitList(opt(a, "policies", "static_only,not_rejected"))) o.policies.push_back(parsePolicy(p));
  if (o.policies.empty()) fail("arguments", "--policies is empty");
  const KthExportReport r = kthExport(need(a, "dir"), arm, o, need(a, "out-prefix"));
  Meta m(need(a, "meta"));
  m.put("arm", arm.name); m.put("arm_params", describe(arm)); m.put("engine_has_sprt", engineHasSprt() ? 1 : 0);
  m.put("resolution_m", o.res); m.put("window_steps", o.window_steps); m.put("periodicity", o.periodicity ? 1 : 0); m.put("max_range_m", o.max_range);
  m.put("frames", r.frames); m.put("windows_closed", r.windows); m.put("hits_total", r.hits_total); m.put("points_nonfinite_dropped", r.points_nonfinite);
  m.put("points_in_range", r.points_in_range); m.put("points_out_of_range", r.points_out_of_range); m.put("map_cells_end", r.map_cells_end);
  m.put("points_voxel_rejected", r.verdict_points[0]); m.put("points_voxel_undecided", r.verdict_points[1]); m.put("points_voxel_static", r.verdict_points[2]);
  for (std::size_t k = 0; k < o.policies.size(); ++k) { m.put(std::string("kept_") + policyName(o.policies[k]), r.kept[k]); m.put(std::string("out_") + policyName(o.policies[k]), r.out_paths[k]); }
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return 0;
}

// ----------------------------------------------------------------------------- Spires ----------
std::vector<ZipArchive::Entry> pcdEntries(ZipArchive& z) {
  std::vector<ZipArchive::Entry> v;
  for (const auto& e : z.entries()) if (e.name.size() > 4 && e.name.substr(e.name.size() - 4) == ".pcd") v.push_back(e);
  std::sort(v.begin(), v.end(), [](const ZipArchive::Entry& x, const ZipArchive::Entry& y) { return x.name < y.name; });
  return v;
}

int cmdSpiresValidate(const std::map<std::string, std::string>& a) {
  const std::size_t n = std::stoul(opt(a, "n", "10"));
  const double dt = std::stod(opt(a, "dt", "0"));
  const PoseTrack3 gt = readTumTrack3(need(a, "gt"));
  ZipArchive zip(need(a, "zip"));
  const auto entries = pcdEntries(zip);
  Verdicts v;
  std::cout << "members=" << entries.size() << " first=" << (entries.empty() ? "" : entries.front().name) << " last=" << (entries.empty() ? "" : entries.back().name)
            << " gt_poses=" << gt.size() << " gt_span=[" << gt.t0() << "," << gt.t1() << "]\n";
  v.check(!entries.empty(), "no .pcd members");
  SpiresExtrinsic ex; SpiresConvertOptions co; co.dt = dt;
  SpiresConvertReport rep;
  std::vector<std::uint8_t> buf; Frame fr;
  std::size_t scans_before_gt = 0;
  for (std::size_t k = 0; k < std::min(n, entries.size()); ++k) {
    zip.read(entries[k], buf);
    const SpiresPcd s = parsePcdBuffer(buf, entries[k].name);
    const double st = scanTimeFromName(entries[k].name);
    double tmin = 1e300, tmax = -1e300, rmax = 0;
    for (double t : s.ts) { tmin = std::min(tmin, t); tmax = std::max(tmax, t); }
    std::vector<double> rr; rr.reserve(s.xyz.size() / 3);
    for (std::size_t i = 0; i < s.xyz.size(); i += 3) { const double r = std::sqrt(double(s.xyz[i]) * s.xyz[i] + double(s.xyz[i + 1]) * s.xyz[i + 1] + double(s.xyz[i + 2]) * s.xyz[i + 2]); rr.push_back(r); rmax = std::max(rmax, r); }
    std::sort(rr.begin(), rr.end());
    const bool ok = convertSpiresScan(s, st, gt, ex, co, static_cast<std::uint32_t>(k), fr, rep);
    // a scan before the first ground-truth pose has no pose by the data, not by the reader (the conversion counts and drops it)
    const double tmid = s.ts.empty() ? st : tmin + 0.5 * (tmax - tmin);
    const bool before_gt = !ok && tmid + dt < gt.t0();
    if (before_gt) ++scans_before_gt;
    std::cout << entries[k].name << " bytes=" << buf.size() << " points=" << s.points << " finite=" << s.xyz.size() / 3 << " has_timestamp=" << (!s.ts.empty())
              << " name_time=" << st << " point_time=[" << (s.ts.empty() ? 0 : tmin) << "," << (s.ts.empty() ? 0 : tmax) << "] range_median=" << (rr.empty() ? 0 : rr[rr.size() / 2])
              << " range_max=" << rmax << " pose_ok=" << ok << (before_gt ? " (before the first GT pose)" : "") << " kept=" << (ok ? fr.size() : 0) << "\n";
    v.check(s.points > 1000, entries[k].name + ": fewer than 1000 points");
    v.check(!s.ts.empty(), entries[k].name + ": no timestamp field (the per-point deskew of calibration cannot be repeated)");
    v.check(std::isfinite(st), entries[k].name + ": the member name has no time");
    v.check(!rr.empty() && rr[rr.size() / 2] > 0.5 && rr[rr.size() / 2] < 60.0, entries[k].name + ": median range outside 0.5 to 60 m");
    v.check(ok || before_gt, entries[k].name + ": no ground-truth pose at the scan time");
    if (!s.ts.empty() && std::isfinite(st)) v.check(std::fabs(tmax - st) < 1.0 && std::fabs(tmin - st) < 1.0, entries[k].name + ": point times differ from the name time by more than 1 s");
  }
  std::cout << "scans_before_first_gt_pose=" << scans_before_gt << " scans=" << rep.scans << " dropped_no_pose=" << rep.scans_dropped_no_pose << " points_used=" << rep.points_used << " points_no_pose=" << rep.points_no_pose << "\n";
  return finishValidate(v, "spires");
}

// ----------------------------------------------------------------------------- Boreas ----------
struct BoreasSession {
  std::string dir, id;
  std::vector<std::string> frames;      // sorted .bin names
};
BoreasSession loadSession(const std::string& dir) {
  BoreasSession s; s.dir = dir;
  const std::size_t b = dir.find_last_of('/');
  s.id = dir.substr(b == std::string::npos ? 0 : b + 1);
  DIR* d = opendir((dir + "/lidar").c_str());
  if (!d) fail(dir + "/lidar", "cannot open directory");
  while (dirent* e = readdir(d)) { const std::string nme = e->d_name; if (nme.size() > 4 && nme.substr(nme.size() - 4) == ".bin") s.frames.push_back(nme); }
  closedir(d);
  std::sort(s.frames.begin(), s.frames.end());
  if (s.frames.empty()) fail(dir + "/lidar", "no .bin frames");
  return s;
}

int cmdBoreasValidate(const std::map<std::string, std::string>& a) {
  const BoreasSession s = loadSession(need(a, "seq"));
  const std::size_t n = std::stoul(opt(a, "n", "10"));
  Verdicts v;
  const double t_first = boreasFrameTime(s.frames.front()), t_last_wanted = boreasFrameTime(s.frames[std::min(n, s.frames.size()) - 1]);
  std::cout << "frames=" << s.frames.size() << " first=" << s.frames.front() << " last=" << s.frames.back() << "\n";
  GeoOrigin org;
  const auto gps = readBoreasPoseCsv(s.dir + "/applanix/gps_post_process.csv", 1.0, t_first - 0.5, t_last_wanted + 0.5, &org);
  const auto lp = readBoreasPoseCsv(s.dir + "/applanix/lidar_poses.csv", 1e-6, t_first - 0.01, t_last_wanted + 0.01);
  const Pose3 T = readBoreasCalib(s.dir + "/calib/T_applanix_lidar.txt");
  v.check(org.valid, "gps_post_process.csv has no latitude and longitude");
  v.check(gps.size() > 10 * n, "too few GPS rows in the window");
  v.check(lp.size() >= n, "lidar_poses.csv has fewer rows than frames in the window");
  const PoseTrack3 track = trackOf(gps);
  std::cout << "origin_lat_rad=" << org.lat << " origin_lon_rad=" << org.lon << " gps_rows_in_window=" << gps.size() << " lidar_pose_rows_in_window=" << lp.size() << "\n";
  std::cout << "T_applanix_lidar.txt t=(" << T.t.transpose() << ") yaw_deg=" << std::atan2(2 * (T.q.w() * T.q.z() + T.q.x() * T.q.y()), 1 - 2 * (T.q.y() * T.q.y() + T.q.z() * T.q.z())) * 180.0 / M_PI << "\n";
  // pose convention check: GPS (interpolated) times calibration against the lidar_poses row of the same frame
  double worst_pos = 0, worst_rot_deg = 0;
  for (std::size_t k = 0; k < std::min(n, s.frames.size()); ++k) {
    const double ft = boreasFrameTime(s.frames[k]);
    const auto row = std::find_if(lp.begin(), lp.end(), [&](const BoreasPoseRow& r) { return std::fabs(r.t - ft) < 1e-6; });
    v.check(row != lp.end(), s.frames[k] + ": no row in lidar_poses.csv");
    if (row == lp.end()) continue;
    Pose3 pa;
    v.check(track.at(ft, pa, 0.05), s.frames[k] + ": no GPS bracket at the frame time");
    if (!track.at(ft, pa, 0.05)) continue;
    const Pose3 lidar = lidarPoseFromApplanix(pa, T), ref = poseOf(*row);
    worst_pos = std::max(worst_pos, (lidar.t - ref.t).norm());
    worst_rot_deg = std::max(worst_rot_deg, lidar.q.angularDistance(ref.q) * 180.0 / M_PI);
  }
  std::cout << "gps_x_calib_vs_lidar_poses: worst_position_diff_m=" << worst_pos << " worst_rotation_diff_deg=" << worst_rot_deg << "\n";
  v.check(worst_pos < 0.10, "GPS times calibration differs from lidar_poses.csv by more than 0.10 m");
  v.check(worst_rot_deg < 0.5, "GPS times calibration differs from lidar_poses.csv by more than 0.5 degrees");
  // ENU convention check: the east and north of the files against the latitude and longitude of the origin
  std::ifstream f(s.dir + "/applanix/gps_post_process.csv");
  double worst_enu = 0; std::size_t checked = 0;
  {
    std::string line; std::getline(f, line);
    const auto hdr = csvdetail::splitCsv(line);
    const int clat = csvdetail::colOf(hdr, "latitude"), clon = csvdetail::colOf(hdr, "longitude"), ct = csvdetail::colOf(hdr, "GPSTime");
    while (std::getline(f, line) && checked < 100000) {
      const auto c = csvdetail::splitCsv(line);
      const double t = std::strtod(c[ct].c_str(), nullptr);
      if (t < t_first - 0.5) continue;
      if (t > t_last_wanted + 0.5) break;
      const double lat = std::strtod(c[clat].c_str(), nullptr), lon = std::strtod(c[clon].c_str(), nullptr);
      const Eigen::Vector3d e = enuOf(lat, lon, org.lat, org.lon);
      auto row = std::find_if(gps.begin(), gps.end(), [&](const BoreasPoseRow& r) { return std::fabs(r.t - t) < 1e-6; });
      if (row == gps.end()) continue;
      worst_enu = std::max(worst_enu, std::hypot(e.x() - row->east, e.y() - row->north));
      ++checked;
    }
  }
  std::cout << "enu_from_latlon_vs_files: rows=" << checked << " worst_horizontal_diff_m=" << worst_enu << "\n";
  v.check(checked > 0, "no GPS row compared with its latitude and longitude");
  v.check(worst_enu < 0.5, "east and north of the files differ from the geodetic ENU by more than 0.5 m");
  // lidar frames
  BoreasConvertOptions co; BoreasConvertReport rep; Frame fr;
  for (std::size_t k = 0; k < std::min(n, s.frames.size()); ++k) {
    const auto buf = readFileBytes(s.dir + "/lidar/" + s.frames[k]);
    const BoreasScan sc = parseBoreasBin(buf, s.frames[k]);
    const BoreasLayoutStats st = layoutStats(sc);
    const std::string why = layoutVerdict(st);
    std::cout << s.frames[k] << " bytes=" << buf.size() << " bytes/24=" << double(buf.size()) / 24.0 << " points=" << st.points << " range_median=" << st.range_median
              << " range_max=" << st.range_max << " z=[" << st.z_min << "," << st.z_max << "] intensity=[" << st.intensity_min << "," << st.intensity_max << "] ring=["
              << st.ring_min << "," << st.ring_max << "] ring_integer_share=" << st.ring_integer_share << " t=[" << st.t_min << "," << st.t_max << "] " << (why.empty() ? "layout_ok" : "LAYOUT: " + why) << "\n";
    v.check(why.empty(), s.frames[k] + ": " + why);
    const bool ok = convertBoreasScan(sc, boreasFrameTime(s.frames[k]), track, T, Pose3{}, co, static_cast<std::uint32_t>(k), fr, rep);
    v.check(ok, s.frames[k] + ": no pose for the conversion");
  }
  std::cout << "converted: scans=" << rep.scans << " dropped_no_pose=" << rep.scans_dropped_no_pose << " points_used=" << rep.points_used << " out_of_range=" << rep.points_out_of_range << " no_pose=" << rep.points_no_pose << "\n";
  return finishValidate(v, "boreas");
}

int cmdBoreasOverlap(const std::map<std::string, std::string>& a) {
  const auto dirs = splitList(need(a, "seqs"));
  const double tile = std::stod(opt(a, "tile", "10")), need_share = std::stod(opt(a, "min-share", "0.8"));
  if (dirs.size() < 2) fail("arguments", "--seqs needs at least two sequence directories (comma separated; the first is the reference)");
  std::vector<BoreasSession> ss; std::vector<GeoOrigin> org(dirs.size()); std::vector<std::vector<Eigen::Vector2d>> common(dirs.size()), raw(dirs.size());
  for (std::size_t i = 0; i < dirs.size(); ++i) {
    ss.push_back(loadSession(dirs[i]));
    const double t0 = boreasFrameTime(ss[i].frames.front()), t1 = boreasFrameTime(ss[i].frames.back());
    const auto rows = readBoreasPoseCsv(dirs[i] + "/applanix/lidar_poses.csv", 1e-6, t0 - 1e-3, t1 + 1e-3);
    readBoreasPoseCsv(dirs[i] + "/applanix/gps_post_process.csv", 1.0, 1e300, -1e300, &org[i]);     // origin only: stops after the first row
    std::set<double> have; for (const auto& r : rows) have.insert(r.t);
    std::size_t missing = 0;
    for (const auto& f : ss[i].frames) if (!have.count(boreasFrameTime(f))) ++missing;
    std::cout << ss[i].id << " frames=" << ss[i].frames.size() << " pose_rows_in_slice=" << rows.size() << " frames_without_pose_row=" << missing << " origin_lat_deg=" << org[i].lat * 180 / M_PI << " origin_lon_deg=" << org[i].lon * 180 / M_PI << "\n";
    if (!org[i].valid) fail(dirs[i], "no origin latitude and longitude");
    const Pose3 to_ref = sessionToReference(org[0], org[i]);
    for (const auto& r : rows) {
      const Eigen::Vector3d p = to_ref.q * Eigen::Vector3d(r.east, r.north, r.up) + to_ref.t;
      common[i].emplace_back(p.x(), p.y());
      raw[i].emplace_back(r.east, r.north);
    }
  }
  bool all_pass = true;
  std::cout << "tile_m=" << tile << " rule: pass if every ordered pair has share_near >= " << need_share << "\n";
  std::cout << "frame,a,b,tiles_a,tiles_b,share_a_in_b,share_b_in_a,share_a_near_b,share_b_near_a\n";
  for (int variant = 0; variant < 2; ++variant) {
    for (std::size_t i = 0; i < dirs.size(); ++i) for (std::size_t j = i + 1; j < dirs.size(); ++j) {
      const TileOverlap o = tileOverlap(tileHistogram(variant == 0 ? common[i] : raw[i], tile), tileHistogram(variant == 0 ? common[j] : raw[j], tile));
      std::cout << (variant == 0 ? "common" : "raw_local") << "," << ss[i].id << "," << ss[j].id << "," << o.tiles_a << "," << o.tiles_b << "," << o.share_a_in_b << "," << o.share_b_in_a << "," << o.share_a_near_b << "," << o.share_b_near_a << "\n";
      if (variant == 0 && (o.share_a_near_b < need_share || o.share_b_near_a < need_share)) all_pass = false;
    }
  }
  std::cout << "VERDICT boreas tile overlap " << (all_pass ? "PASS" : "FAIL") << " (common frame; the raw_local rows show what the check would give without the common frame)\n";
  return all_pass ? 0 : 3;
}


int cmdBoreasConvert(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const BoreasSession s = loadSession(need(a, "seq"));
  const BoreasSession ref = loadSession(need(a, "ref"));
  const std::size_t maxf = std::stoul(opt(a, "max-frames", "0"));
  const std::size_t nf = maxf > 0 ? std::min(maxf, s.frames.size()) : s.frames.size();
  BoreasConvertOptions co;
  co.r_min = std::stod(opt(a, "r-min", "3")); co.r_max = std::stod(opt(a, "r-max", "50")); co.dt = std::stod(opt(a, "dt", "0"));
  const double zoff = std::stod(opt(a, "z-offset", "0"));
  const double tf = boreasFrameTime(s.frames.front()), tl = boreasFrameTime(s.frames[nf - 1]);
  GeoOrigin org, refo;
  const auto gps = readBoreasPoseCsv(s.dir + "/applanix/gps_post_process.csv", 1.0, tf - 0.5, tl + 0.5, &org);
  readBoreasPoseCsv(ref.dir + "/applanix/gps_post_process.csv", 1.0, 1e300, -1e300, &refo);
  const PoseTrack3 track = trackOf(gps);
  const Pose3 T = readBoreasCalib(s.dir + "/calib/T_applanix_lidar.txt");
  const Pose3 to_common = sessionToReference(refo, org, zoff);
  CacheWriter w(need(a, "out"), 3);
  BoreasConvertReport rep; Frame fr;
  for (std::size_t k = 0; k < nf; ++k) {
    const BoreasScan sc = parseBoreasBin(readFileBytes(s.dir + "/lidar/" + s.frames[k]), s.frames[k]);
    if (convertBoreasScan(sc, boreasFrameTime(s.frames[k]), track, T, to_common, co, static_cast<std::uint32_t>(k), fr, rep)) w.add(fr);
  }
  w.finish();
  Meta m(need(a, "meta"));
  m.put("sequence", s.id); m.put("reference", ref.id); m.put("r_min_m", co.r_min); m.put("r_max_m", co.r_max); m.put("dt_s", co.dt); m.put("z_offset_m", zoff);
  m.put("frames_in_directory", s.frames.size()); m.put("frames_written", w.frames()); m.put("scans_dropped_no_pose", rep.scans_dropped_no_pose);
  m.put("points_in_files", rep.points_in); m.put("points_nonfinite", rep.points_nonfinite); m.put("points_out_of_range", rep.points_out_of_range);
  m.put("points_no_pose", rep.points_no_pose); m.put("points_used", rep.points_used);
  m.put("origin_lat_rad", org.lat); m.put("origin_lon_rad", org.lon); m.put("reference_lat_rad", refo.lat); m.put("reference_lon_rad", refo.lon);
  m.put("to_common_t_x", to_common.t.x()); m.put("to_common_t_y", to_common.t.y()); m.put("to_common_t_z", to_common.t.z());
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return 0;
}

void writeIds(const std::string& path, std::vector<CellId> ids) {
  std::sort(ids.begin(), ids.end());
  std::ofstream o(path, std::ios::binary);
  if (!o) fail(path, "cannot write");
  for (CellId id : ids) { const std::int64_t v = id; o.write(reinterpret_cast<const char*>(&v), 8); }
}

// 3D caches -> Static voxel sets. independent: a fresh map per cache; cumulative: one map, a snapshot after each cache.
int cmdStaticExport(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const ArmSpec arm = armSpec(need(a, "arm"));
  const double res = std::stod(need(a, "res"));
  const int W = std::stoi(need(a, "window"));
  const bool per = flag(a, "periodicity", true);
  const std::string mode = opt(a, "mode", "independent");
  if (mode != "independent" && mode != "cumulative") fail("arguments", "--mode must be independent or cumulative");
  const auto caches = splitList(need(a, "caches"));
  Meta m(need(a, "meta"));
  m.put("arm", arm.name); m.put("arm_params", describe(arm)); m.put("engine_has_sprt", engineHasSprt() ? 1 : 0);
  m.put("mode", mode); m.put("resolution_m", res); m.put("window_steps", W); m.put("periodicity", per ? 1 : 0); m.put("sessions", caches.size());
  std::unique_ptr<strata_core::Voxel3DBackend> b;
  for (std::size_t i = 0; i < caches.size(); ++i) {
    if (!b || mode == "independent") b.reset(new strata_core::Voxel3DBackend(res, toParams(arm, W, per)));
    CacheReader cache(caches[i]);
    if (cache.dim() != 3) fail(caches[i], "not a 3D cache");
    const DriveReport st = drive(*b, [&](Frame& f) { return cache.next(f); }, nullptr, nullptr);
    std::vector<CellId> stat = b->layered().staticCells(), obs = stat;
    for (CellId id : b->layered().transientCells()) obs.push_back(id);
    for (CellId id : b->layered().periodicCells()) obs.push_back(id);
    for (CellId id : b->layered().intermittentCells()) obs.push_back(id);
    const std::string pre = need(a, "out-prefix") + "_s" + std::to_string(i);
    writeIds(pre + "_static.ids", stat);
    writeIds(pre + "_observed.ids", obs);
    const std::string k = "session" + std::to_string(i) + "_";
    m.put(k + "cache", caches[i]); m.put(k + "frames", st.frames); m.put(k + "windows_closed", st.windows); m.put(k + "hits_total", st.hits_total);
    m.put(k + "map_cells_end", st.map_cells_end); m.put(k + "static_cells", stat.size()); m.put(k + "observed_cells", obs.size());
  }
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
  return 0;
}

// -------------------------------------------------------------------------- LILocBench ----------
struct StopReading {};

std::string stripSlash(const std::string& s) { return (!s.empty() && s[0] == '/') ? s.substr(1) : s; }

int cmdLilValidate(const std::map<std::string, std::string>& a) {
  const std::size_t n = std::stoul(opt(a, "n", "10"));
  const double dt = std::stod(opt(a, "time-offset", "-0.13"));
  const PoseTrack gt = readTumTrack(need(a, "gt"));
  std::vector<ScanRecord> front, rear; std::vector<Se2> fext, rext;
  const std::string ft = opt(a, "front", "/laser_scan_front/scan"), rt = opt(a, "rear", "/laser_scan_rear/scan");
  std::size_t messages = 0;
  try {
    readBag(need(a, "bag"), [&](const BagMessage& m) {
      ++messages;
      if (m.topic == ft || m.topic == rt) {
        ScanRecord r; r.msg = decodeLaserScan(m.data, m.size); r.stamp = r.msg.stamp();
        (m.topic == ft ? front : rear).push_back(std::move(r));
      } else if (m.topic == "/tf") {
        for (const TfEdge& e : decodeTfMessage(m.data, m.size)) {
          if (stripSlash(e.parent) == "base_link" && stripSlash(e.child) == "scan_front_link") fext.push_back(se2FromTf(e));
          if (stripSlash(e.parent) == "base_link" && stripSlash(e.child) == "scan_rear_link") rext.push_back(se2FromTf(e));
        }
      }
      if (front.size() >= n && rear.size() >= n && !fext.empty() && !rext.empty()) throw StopReading{};
    });
  } catch (const StopReading&) {}
  Verdicts v;
  v.check(front.size() >= n && rear.size() >= n, "fewer than N scans on the front or rear topic before the bag ended");
  v.check(!fext.empty() && !rext.empty(), "no /tf base_link -> scan_front_link / scan_rear_link before the scans");
  std::cout << "gt_poses=" << gt.size() << " gt_span=[" << gt.t0() << "," << gt.t1() << "] messages_read=" << messages << "\n";
  if (!v.ok) return finishValidate(v, "lilocbench");
  ConvertOptions co; co.deskew = true; co.anchor = Anchor::Start; co.time_offset_s = dt;
  ConvertReport rep; std::vector<Frame> frames; std::size_t pairs_before_gt = 0;
  for (std::size_t k = 0; k < n; ++k) {
    const ScanRecord& f = front[k]; const ScanRecord& r = rear[k];
    const std::size_t before = frames.size();
    convertScanPair(gt, fext[0], rext[0], f, r, static_cast<std::uint32_t>(k), co, frames, rep);
    ++rep.steps;
    std::cout << "pair " << k << " front_stamp=" << f.stamp << " rear_stamp=" << r.stamp << " skew_s=" << std::fabs(f.stamp - r.stamp) << " beams=" << f.msg.ranges.size() << "/" << r.msg.ranges.size()
              << " angle_min=" << f.msg.angle_min << " time_increment=" << f.msg.time_increment << " frames_made=" << frames.size() - before << "\n";
    v.check(f.msg.ranges.size() == 811 && r.msg.ranges.size() == 811, "pair " + std::to_string(k) + ": not 811 beams (the sensor of the calibration sequences)");
    v.check(std::fabs(f.stamp - r.stamp) <= 0.05, "pair " + std::to_string(k) + ": front/rear stamp skew above 0.05 s");
    // a scan before the first ground-truth pose has no pose by the data, not by the reader (the conversion drops and counts it)
    const std::size_t expected = (f.stamp + dt >= gt.t0() ? 1u : 0u) + (r.stamp + dt >= gt.t0() ? 1u : 0u);
    if (expected < 2) ++pairs_before_gt;
    v.check(frames.size() - before == expected, "pair " + std::to_string(k) + ": a scan was dropped although the ground truth covers it");
  }
  std::cout << "pairs_with_a_scan_before_the_first_gt_pose=" << pairs_before_gt << "\n";
  std::cout << "extrinsics front=(" << fext[0].x << "," << fext[0].y << "," << fext[0].yaw << ") rear=(" << rext[0].x << "," << rext[0].y << "," << rext[0].yaw << ") beams_used=" << rep.beams_used << " invalid=" << rep.beams_invalid << " no_pose=" << rep.beams_no_pose << "\n";
  v.check(std::fabs(fext[0].x - 0.3046) < 1e-3 && std::fabs(rext[0].x + 0.3027) < 1e-3, "extrinsics differ from those of the calibration sequences");
  return finishValidate(v, "lilocbench");
}

int cmdRefExport(const std::map<std::string, std::string>& a) {
  int dim = 0;
  const LabelMap labels = readLabels(need(a, "labels"), &dim);
  if (dim != 2) fail(need(a, "labels"), "not a 2D label file");
  const double res = std::stod(need(a, "res"));
  const double o = std::stod(opt(a, "grid-origin", "-30")), size = std::stod(opt(a, "grid-size", "60"));
  const int width = static_cast<int>(std::ceil(size / res));
  std::vector<CellId> ids; for (const auto& kv : labels) ids.push_back(kv.first);
  std::sort(ids.begin(), ids.end());
  std::ofstream out(need(a, "out"));
  if (!out) fail(need(a, "out"), "cannot write");
  out << "label,ix,iy\n";
  for (CellId id : ids) { int gx, gy; decodeGrid(id, width, gx, gy); out << int(labels.at(id)) << "," << gx << "," << gy << "\n"; }
  std::cerr << "cells=" << ids.size() << " grid_width=" << width << "\n";
  return 0;
}

// ----------------------------------------------------------------------------- arm-eval ----------
template <class Backend>
void evalCommon(Backend& b, CacheReader& cache, const LabelMap& labels, const ArmSpec& arm, const std::map<std::string, std::string>& a,
                const strata_core::GridMeta* grid, bool three_d, int width, double t0) {
  const bool track = flag(a, "latency", true);
  TallyObserver tally(&labels, track);
  b.setWindowObserver(&tally);
  const DriveReport st = drive(b, [&](Frame& f) { return cache.next(f); }, grid, &tally);
  const std::string format = opt(a, "format", three_d ? "counts" : "cells");
  const int nmin = std::stoi(opt(a, "nmin", "5"));
  if (format == "cells") {
    writeLabelledDump(need(a, "out"), labels, b.layered(), tally, arm.binary, three_d, width);
  } else if (format == "counts") {
    std::map<std::tuple<int, int, int>, std::size_t> cnt;      // label, eligible, verdict
    std::map<std::pair<int, int>, std::size_t> lat;            // label, first_static window
    for (const auto& kv : labels) {
      CellTally c; auto it = tally.tally().find(kv.first);
      if (it != tally.tally().end()) c = it->second;
      const Verdict v = verdictOf(b.layered(), kv.first, arm.binary);
      ++cnt[{int(kv.second), c.n >= static_cast<std::uint32_t>(nmin) ? 1 : 0, int(v)}];
      if (c.first_static >= 0) ++lat[{int(kv.second), c.first_static}];
    }
    std::ofstream o(need(a, "out"));
    if (!o) fail(need(a, "out"), "cannot write");
    o << "label,eligible,verdict,count\n";
    for (const auto& kv : cnt) o << std::get<0>(kv.first) << "," << std::get<1>(kv.first) << "," << verdictName(static_cast<Verdict>(std::get<2>(kv.first))) << "," << kv.second << "\n";
    std::ofstream l(need(a, "out") + ".latency");
    l << "label,first_static_window,count\n";
    for (const auto& kv : lat) l << kv.first.first << "," << kv.first.second << "," << kv.second << "\n";
  } else {
    fail("arguments", "--format must be cells or counts");
  }
  Meta m(need(a, "meta"));
  m.put("arm", arm.name); m.put("arm_params", describe(arm)); m.put("engine_has_sprt", engineHasSprt() ? 1 : 0);
  m.put("dim", three_d ? 3 : 2); m.put("resolution_m", opt(a, "res", "")); m.put("window_steps", opt(a, "window", "")); m.put("periodicity", flag(a, "periodicity", true) ? 1 : 0);
  m.put("nmin", nmin); m.put("format", format); m.put("frames", st.frames); m.put("steps", st.steps); m.put("windows_closed", st.windows);
  m.put("hits_total", st.hits_total); m.put("hits_off_grid", st.hits_off_grid); m.put("map_cells_end", st.map_cells_end); m.put("labelled_cells", labels.size());
  m.put("seconds", nowSeconds() - t0); m.put("peak_rss_kb", peakRssKb());
}

int cmdArmEval(const std::map<std::string, std::string>& a) {
  const double t0 = nowSeconds();
  const ArmSpec arm = armSpec(need(a, "arm"));
  const double res = std::stod(need(a, "res"));
  const int W = std::stoi(need(a, "window"));
  const bool per = flag(a, "periodicity", true);
  int ldim = 0;
  const LabelMap labels = readLabels(need(a, "labels"), &ldim);
  CacheReader cache(need(a, "cache"));
  if (cache.dim() != ldim) fail("arguments", "cache and label file have different dimensions");
  if (cache.dim() == 2) {
    strata_core::GridMeta g;
    const double o = std::stod(opt(a, "grid-origin", "-30")), size = std::stod(opt(a, "grid-size", "60"));
    g.resolution = res; g.origin_x = o; g.origin_y = o; g.width = g.height = static_cast<int>(std::ceil(size / res));
    strata_core::Grid2DBackend b(g, toParams(arm, W, per));
    evalCommon(b, cache, labels, arm, a, &g, false, g.width, t0);
  } else {
    strata_core::Voxel3DBackend b(res, toParams(arm, W, per));
    evalCommon(b, cache, labels, arm, a, nullptr, true, 0, t0);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "usage: p3_probe <arms|kth-validate|kth-export|spires-validate|boreas-validate|boreas-overlap|lil-validate|ref-export|arm-eval> --key value ...\n"; return 2; }
  try {
    const std::string cmd = argv[1];
    const auto a = parseArgs(argc, argv, 2);
    if (cmd == "arms") return cmdArms(a);
    if (cmd == "kth-validate") return cmdKthValidate(a);
    if (cmd == "kth-export") return cmdKthExport(a);
    if (cmd == "spires-validate") return cmdSpiresValidate(a);
    if (cmd == "boreas-validate") return cmdBoreasValidate(a);
    if (cmd == "boreas-overlap") return cmdBoreasOverlap(a);
    if (cmd == "boreas-convert") return cmdBoreasConvert(a);
    if (cmd == "static-export") return cmdStaticExport(a);
    if (cmd == "lil-validate") return cmdLilValidate(a);
    if (cmd == "ref-export") return cmdRefExport(a);
    if (cmd == "arm-eval") return cmdArmEval(a);
    std::cerr << "unknown subcommand " << cmd << "\n"; return 2;
  } catch (const std::exception& e) {
    std::cerr << "p3_probe: " << e.what() << "\n"; return 1;
  }
}
