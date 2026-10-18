// Boreas readers and the common frame (PREREG_P3.md section 5). Header-only, no ROS.
//   * lidar .bin: 6 x float32 per point (x y z intensity ring t), t = seconds relative to the frame time in the file name
//   * applanix/gps_post_process.csv (200 Hz, seconds, with latitude and longitude in radians) and lidar_poses.csv
//     (one row per lidar frame, microseconds): east, north, up in the ENU frame whose origin is the first GPS row of the session
//   * calib/T_applanix_lidar.txt: a point in the lidar frame maps to the applanix frame
//   * the common frame: the ENU frame of a reference session, built from the latitude and longitude of each session's origin
// Conventions were read from the first rows of a real session (results_p3/access, boreas_peek*): heading is the yaw of the
// applanix frame about up, counter-clockwise from east; the lidar pose is the applanix pose times T_applanix_lidar.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "p1b_io.hpp"
#include "realdata_io.hpp"

namespace realdata {
namespace p3 {

// ------------------------------------------------------------------------- lidar .bin ----------

struct BoreasScan {
  std::vector<float> xyz, intensity, ring, t;
  std::size_t points{0};            // records in the file
  std::size_t dropped_nonfinite{0};
  std::size_t size() const { return xyz.size() / 3; }
};

constexpr std::size_t kBoreasRecordBytes = 24;

inline BoreasScan parseBoreasBin(const std::vector<std::uint8_t>& buf, const std::string& name) {
  if (buf.empty() || buf.size() % kBoreasRecordBytes != 0)
    fail(name, "size " + std::to_string(buf.size()) + " is not a multiple of 24 bytes (6 x float32 per point expected)");
  BoreasScan s;
  s.points = buf.size() / kBoreasRecordBytes;
  s.xyz.reserve(s.points * 3); s.intensity.reserve(s.points); s.ring.reserve(s.points); s.t.reserve(s.points);
  for (std::size_t i = 0; i < s.points; ++i) {
    float v[6];
    std::memcpy(v, buf.data() + i * kBoreasRecordBytes, kBoreasRecordBytes);
    bool finite = true;
    for (float x : v) finite = finite && std::isfinite(x);
    if (!finite) { ++s.dropped_nonfinite; continue; }
    s.xyz.insert(s.xyz.end(), v, v + 3);
    s.intensity.push_back(v[3]); s.ring.push_back(v[4]); s.t.push_back(v[5]);
  }
  return s;
}

inline std::vector<std::uint8_t> readFileBytes(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) fail(path, "cannot open");
  f.seekg(0, std::ios::end);
  const std::streamsize n = f.tellg();
  f.seekg(0);
  std::vector<std::uint8_t> b(static_cast<std::size_t>(n));
  f.read(reinterpret_cast<char*>(b.data()), n);
  if (!f) fail(path, "read error");
  return b;
}

struct BoreasLayoutStats {
  std::size_t points{0}, dropped_nonfinite{0};
  double x_min{0}, x_max{0}, y_min{0}, y_max{0}, z_min{0}, z_max{0}, range_max{0}, range_median{0};
  double intensity_min{0}, intensity_max{0}, ring_min{0}, ring_max{0}, t_min{0}, t_max{0};
  double ring_integer_share{0};
};

inline BoreasLayoutStats layoutStats(const BoreasScan& s) {
  BoreasLayoutStats o;
  o.points = s.points; o.dropped_nonfinite = s.dropped_nonfinite;
  const std::size_t n = s.size();
  if (n == 0) return o;
  o.x_min = o.y_min = o.z_min = o.intensity_min = o.ring_min = o.t_min = 1e300;
  o.x_max = o.y_max = o.z_max = o.intensity_max = o.ring_max = o.t_max = -1e300;
  std::vector<double> r; r.reserve(n);
  std::size_t integer = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double x = s.xyz[3 * i], y = s.xyz[3 * i + 1], z = s.xyz[3 * i + 2];
    o.x_min = std::min(o.x_min, x); o.x_max = std::max(o.x_max, x);
    o.y_min = std::min(o.y_min, y); o.y_max = std::max(o.y_max, y);
    o.z_min = std::min(o.z_min, z); o.z_max = std::max(o.z_max, z);
    o.intensity_min = std::min<double>(o.intensity_min, s.intensity[i]); o.intensity_max = std::max<double>(o.intensity_max, s.intensity[i]);
    o.ring_min = std::min<double>(o.ring_min, s.ring[i]); o.ring_max = std::max<double>(o.ring_max, s.ring[i]);
    o.t_min = std::min<double>(o.t_min, s.t[i]); o.t_max = std::max<double>(o.t_max, s.t[i]);
    if (s.ring[i] == std::floor(s.ring[i])) ++integer;
    r.push_back(std::sqrt(x * x + y * y + z * z));
  }
  std::nth_element(r.begin(), r.begin() + r.size() / 2, r.end());
  o.range_median = r[r.size() / 2];
  o.range_max = *std::max_element(r.begin(), r.end());
  o.ring_integer_share = static_cast<double>(integer) / static_cast<double>(n);
  return o;
}

// Range sanity of the layout (PREREG_P3.md section 5.1). Returns "" if the file looks like 6 x float32
// (x y z intensity ring t), otherwise the first rule that fails. These bounds only confirm the layout; they select nothing.
inline std::string layoutVerdict(const BoreasLayoutStats& s) {
  if (s.points < 1000) return "fewer than 1000 points";
  if (s.dropped_nonfinite * 100 > s.points) return "more than 1 % non-finite records";
  if (s.range_max > 500.0) return "a return farther than 500 m (the device reaches 300 m)";
  if (std::fabs(s.z_min) > 200.0 || std::fabs(s.z_max) > 200.0) return "z outside +-200 m";
  if (s.range_median < 1.0 || s.range_median > 100.0) return "median range outside 1 m to 100 m";
  if (s.intensity_min < 0.0 || s.intensity_max > 65535.0) return "intensity outside 0 to 65535";
  if (s.ring_integer_share < 0.999) return "ring column is not integer valued";
  if (s.ring_min < 0.0 || s.ring_max > 127.0) return "ring outside 0 to 127 (128 beams)";
  if (s.t_min < -0.2 || s.t_max > 0.2) return "relative time outside +-0.2 s";
  if (s.t_max - s.t_min < 0.05) return "relative time spans less than 0.05 s (one sweep is 0.1 s)";
  return "";
}

// ---------------------------------------------------------------------------- pose CSVs ----------

struct BoreasPoseRow { double t{0}; double east{0}, north{0}, up{0}, roll{0}, pitch{0}, heading{0}; };
struct GeoOrigin { double lat{0}, lon{0}; bool valid{false}; };   // radians

namespace csvdetail {
inline std::vector<std::string> splitCsv(const std::string& line) {
  std::vector<std::string> o; std::string cur; std::istringstream is(line);
  while (std::getline(is, cur, ',')) o.push_back(cur);
  if (!line.empty() && line.back() == ',') o.emplace_back();
  return o;
}
inline int colOf(const std::vector<std::string>& header, const std::string& name) {
  for (std::size_t i = 0; i < header.size(); ++i) {
    std::string h = header[i];
    while (!h.empty() && (h.back() == '\r' || h.back() == ' ')) h.pop_back();
    if (h == name) return static_cast<int>(i);
  }
  return -1;
}
}  // namespace csvdetail

// Reads rows with t in [t_lo, t_hi] (seconds after multiplying the first column by time_scale). The first data row of the
// file gives the origin (latitude, longitude) when the columns exist, whatever the window. Rows must increase in time;
// reading stops at the first row after t_hi.
inline std::vector<BoreasPoseRow> readBoreasPoseCsv(const std::string& path, double time_scale, double t_lo, double t_hi,
                                                    GeoOrigin* origin = nullptr) {
  std::ifstream f(path);
  if (!f) fail(path, "cannot open");
  std::string line;
  if (!std::getline(f, line)) fail(path, "empty file");
  const auto header = csvdetail::splitCsv(line);
  const int ct = csvdetail::colOf(header, "GPSTime"), ce = csvdetail::colOf(header, "easting"), cn = csvdetail::colOf(header, "northing"),
            cu = csvdetail::colOf(header, "altitude"), cr = csvdetail::colOf(header, "roll"), cp = csvdetail::colOf(header, "pitch"),
            ch = csvdetail::colOf(header, "heading"), clat = csvdetail::colOf(header, "latitude"), clon = csvdetail::colOf(header, "longitude");
  if (ct < 0 || ce < 0 || cn < 0 || cu < 0 || cr < 0 || cp < 0 || ch < 0) fail(path, "header lacks GPSTime, easting, northing, altitude, roll, pitch or heading");
  const std::size_t need = static_cast<std::size_t>(std::max({ct, ce, cn, cu, cr, cp, ch, clat, clon})) + 1;
  std::vector<BoreasPoseRow> rows;
  bool first = true; double prev = -1e300; std::size_t ln = 1;
  while (std::getline(f, line)) {
    ++ln;
    if (line.empty()) continue;
    const auto c = csvdetail::splitCsv(line);
    if (c.size() < need) fail(path, "line " + std::to_string(ln) + ": too few columns");
    char* end = nullptr;
    const double t = std::strtod(c[ct].c_str(), &end) * time_scale;
    if (end == c[ct].c_str() || !std::isfinite(t)) fail(path, "line " + std::to_string(ln) + ": bad time");
    if (!(t > prev)) fail(path, "line " + std::to_string(ln) + ": time does not increase");
    prev = t;
    if (first) {
      first = false;
      if (origin && clat >= 0 && clon >= 0) { origin->lat = std::strtod(c[clat].c_str(), nullptr); origin->lon = std::strtod(c[clon].c_str(), nullptr); origin->valid = true; }
    }
    if (t > t_hi) break;                   // rows increase in time: nothing later is wanted
    if (t < t_lo) continue;
    BoreasPoseRow r; r.t = t;
    r.east = std::strtod(c[ce].c_str(), nullptr); r.north = std::strtod(c[cn].c_str(), nullptr); r.up = std::strtod(c[cu].c_str(), nullptr);
    r.roll = std::strtod(c[cr].c_str(), nullptr); r.pitch = std::strtod(c[cp].c_str(), nullptr); r.heading = std::strtod(c[ch].c_str(), nullptr);
    if (!std::isfinite(r.east + r.north + r.up + r.roll + r.pitch + r.heading)) fail(path, "line " + std::to_string(ln) + ": non-finite value");
    rows.push_back(r);
  }
  return rows;
}

// Rotation of a Boreas pose row: yaw (heading) about z, then pitch about y, then roll about x (Rz Ry Rx).
inline Eigen::Quaterniond rotationOf(const BoreasPoseRow& r) {
  return Eigen::Quaterniond(Eigen::AngleAxisd(r.heading, Eigen::Vector3d::UnitZ()) *
                            Eigen::AngleAxisd(r.pitch, Eigen::Vector3d::UnitY()) *
                            Eigen::AngleAxisd(r.roll, Eigen::Vector3d::UnitX()));
}
inline Pose3 poseOf(const BoreasPoseRow& r) {
  Pose3 p; p.t = Eigen::Vector3d(r.east, r.north, r.up); p.q = rotationOf(r).normalized(); return p;
}
inline PoseTrack3 trackOf(const std::vector<BoreasPoseRow>& rows) {
  PoseTrack3 tr;
  for (const auto& r : rows) tr.add(r.t, poseOf(r));
  if (tr.size() < 2) fail("pose track", "fewer than two rows in the window");
  return tr;
}

// 3 or 4 lines of 4 numbers: the rotation block must be orthonormal within 1e-6; the last row (if present) is 0 0 0 1.
inline Pose3 readBoreasCalib(const std::string& path) {
  std::ifstream f(path);
  if (!f) fail(path, "cannot open");
  std::vector<double> v; double x;
  while (f >> x) v.push_back(x);
  if (v.size() != 12 && v.size() != 16) fail(path, "need 12 or 16 numbers, got " + std::to_string(v.size()));
  Eigen::Matrix3d R; Eigen::Vector3d t;
  for (int i = 0; i < 3; ++i) { for (int j = 0; j < 3; ++j) R(i, j) = v[4 * i + j]; t[i] = v[4 * i + 3]; }
  if (v.size() == 16 && (v[12] != 0 || v[13] != 0 || v[14] != 0 || v[15] != 1)) fail(path, "last row is not 0 0 0 1");
  if ((R * R.transpose() - Eigen::Matrix3d::Identity()).norm() > 1e-6 || std::fabs(R.determinant() - 1.0) > 1e-6) fail(path, "rotation block is not a rotation");
  Pose3 p; p.t = t; p.q = Eigen::Quaterniond(R).normalized();
  return p;
}

inline Pose3 compose(const Pose3& a, const Pose3& b) {      // a * b
  Pose3 o; o.q = (a.q * b.q).normalized(); o.t = a.t + a.q * b.t; return o;
}

// ---------------------------------------------------------------------------- common frame ----------

constexpr double kWgs84A = 6378137.0, kWgs84F = 1.0 / 298.257223563;

inline Eigen::Vector3d ecefOf(double lat, double lon, double h = 0.0) {
  const double e2 = kWgs84F * (2.0 - kWgs84F), s = std::sin(lat), c = std::cos(lat);
  const double N = kWgs84A / std::sqrt(1.0 - e2 * s * s);
  return Eigen::Vector3d((N + h) * c * std::cos(lon), (N + h) * c * std::sin(lon), (N * (1.0 - e2) + h) * s);
}
// Rows: east, north, up axes of the local frame at (lat, lon), in ECEF coordinates (maps ECEF vectors to ENU).
inline Eigen::Matrix3d enuFromEcef(double lat, double lon) {
  Eigen::Matrix3d R;
  R << -std::sin(lon), std::cos(lon), 0.0,
       -std::sin(lat) * std::cos(lon), -std::sin(lat) * std::sin(lon), std::cos(lat),
       std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat);
  return R;
}

// Position of (lat, lon) in the ENU frame at (ref_lat, ref_lon), height 0 for both.
inline Eigen::Vector3d enuOf(double lat, double lon, double ref_lat, double ref_lon) {
  return enuFromEcef(ref_lat, ref_lon) * (ecefOf(lat, lon) - ecefOf(ref_lat, ref_lon));
}

// Rigid map from the ENU frame of a session origin to the ENU frame of the reference origin:
//   p_ref = R * p_session + t,  R = R_ref * R_session^T,  t = R_ref * (e_session - e_ref)
// Heights of the two origins are taken as equal (the files carry no absolute height); z_offset is added to t.z.
inline Pose3 sessionToReference(const GeoOrigin& ref, const GeoOrigin& session, double z_offset = 0.0) {
  if (!ref.valid || !session.valid) fail("common frame", "origin latitude and longitude are missing");
  const Eigen::Matrix3d Rr = enuFromEcef(ref.lat, ref.lon), Rs = enuFromEcef(session.lat, session.lon);
  const Eigen::Matrix3d R = Rr * Rs.transpose();
  Pose3 p; p.q = Eigen::Quaterniond(R).normalized();
  p.t = Rr * (ecefOf(session.lat, session.lon) - ecefOf(ref.lat, ref.lon));
  p.t.z() += z_offset;
  return p;
}

// ---------------------------------------------------------------------------- scan conversion ----------

struct BoreasConvertOptions {
  double r_min{3.0}, r_max{50.0};     // lidar-frame range window (PREREG_P3.md section 5.2)
  double dt{0.0};                     // added to every point time
  double max_gap_s{0.05};             // widest bracket of the 200 Hz track that is interpolated
};
struct BoreasConvertReport {
  std::size_t scans{0}, scans_dropped_no_pose{0};
  std::size_t points_in{0}, points_nonfinite{0}, points_out_of_range{0}, points_no_pose{0}, points_used{0};
};

// Places one sweep in the common frame. Each point uses the pose at (frame_time + t_i + dt) (deskew). Returns false
// (counted) if the pose at the frame time is missing. The frame origin is the lidar position at frame_time.
inline bool convertBoreasScan(const BoreasScan& s, double frame_time, const PoseTrack3& applanix, const Pose3& T_app_lidar,
                              const Pose3& to_common, const BoreasConvertOptions& opt, std::uint32_t group, Frame& fr,
                              BoreasConvertReport& rep) {
  ++rep.scans;
  rep.points_in += s.points; rep.points_nonfinite += s.dropped_nonfinite;
  Pose3 pa0;
  if (!applanix.at(frame_time + opt.dt, pa0, opt.max_gap_s)) { ++rep.scans_dropped_no_pose; rep.points_no_pose += s.size(); return false; }
  const Pose3 l0 = compose(to_common, compose(pa0, T_app_lidar));
  fr = Frame(); fr.group = group; fr.origin = l0.t;
  fr.xyz.reserve(s.xyz.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    const double x = s.xyz[3 * i], y = s.xyz[3 * i + 1], z = s.xyz[3 * i + 2];
    const double r = std::sqrt(x * x + y * y + z * z);
    if (r < opt.r_min || r > opt.r_max) { ++rep.points_out_of_range; continue; }
    Pose3 pa;
    if (!applanix.at(frame_time + static_cast<double>(s.t[i]) + opt.dt, pa, opt.max_gap_s)) { ++rep.points_no_pose; continue; }
    const Pose3 lp = compose(to_common, compose(pa, T_app_lidar));
    const Eigen::Vector3d w = lp.t + lp.q * Eigen::Vector3d(x, y, z);
    const float v[3] = {static_cast<float>(w.x()), static_cast<float>(w.y()), static_cast<float>(w.z())};
    fr.xyz.insert(fr.xyz.end(), v, v + 3);
    ++rep.points_used;
  }
  return true;
}

// Frame time (seconds) from a lidar file name such as "1606417543533737.bin" (microseconds). NaN if not a number.
inline double boreasFrameTime(const std::string& name) {
  std::size_t b = name.find_last_of('/');
  b = (b == std::string::npos) ? 0 : b + 1;
  const std::size_t e = name.rfind(".bin");
  if (e == std::string::npos || e <= b) return std::nan("");
  const std::string s = name.substr(b, e - b);
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos) return std::nan("");
  return std::strtod(s.c_str(), nullptr) * 1e-6;
}

// ---------------------------------------------------------------------------- tile overlap ----------

struct TileKey { std::int64_t x, y; bool operator<(const TileKey& o) const { return x != o.x ? x < o.x : y < o.y; } };

inline std::map<TileKey, std::size_t> tileHistogram(const std::vector<Eigen::Vector2d>& xy, double tile_m) {
  std::map<TileKey, std::size_t> h;
  for (const auto& p : xy) ++h[{static_cast<std::int64_t>(std::floor(p.x() / tile_m)), static_cast<std::int64_t>(std::floor(p.y() / tile_m))}];
  return h;
}

struct TileOverlap {
  std::size_t tiles_a{0}, tiles_b{0}, shared_exact{0};
  double share_a_in_b{0}, share_b_in_a{0};                     // exact tiles
  double share_a_near_b{0}, share_b_near_a{0};                 // tiles of one with a tile of the other within Chebyshev distance 1
};

inline TileOverlap tileOverlap(const std::map<TileKey, std::size_t>& a, const std::map<TileKey, std::size_t>& b) {
  TileOverlap o; o.tiles_a = a.size(); o.tiles_b = b.size();
  auto near = [](const std::map<TileKey, std::size_t>& m, const TileKey& k) {
    for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) if (m.count({k.x + dx, k.y + dy})) return true;
    return false;
  };
  std::size_t a_in_b = 0, b_in_a = 0, a_near = 0, b_near = 0;
  for (const auto& kv : a) { if (b.count(kv.first)) ++a_in_b; if (near(b, kv.first)) ++a_near; }
  for (const auto& kv : b) { if (a.count(kv.first)) ++b_in_a; if (near(a, kv.first)) ++b_near; }
  o.shared_exact = a_in_b;
  if (!a.empty()) { o.share_a_in_b = double(a_in_b) / a.size(); o.share_a_near_b = double(a_near) / a.size(); }
  if (!b.empty()) { o.share_b_in_a = double(b_in_a) / b.size(); o.share_b_near_a = double(b_near) / b.size(); }
  return o;
}

// Constant z offset of session B relative to session A: the median over shared tiles of (ground_a - ground_b), where
// the ground height of a tile is a low percentile of the z of the returns in it (computed by the caller). NaN if no shared tile.
inline double estimateZOffset(const std::map<TileKey, double>& ground_a, const std::map<TileKey, double>& ground_b) {
  std::vector<double> d;
  for (const auto& kv : ground_a) { auto it = ground_b.find(kv.first); if (it != ground_b.end()) d.push_back(kv.second - it->second); }
  if (d.empty()) return std::nan("");
  std::sort(d.begin(), d.end());
  return d.size() % 2 ? d[d.size() / 2] : 0.5 * (d[d.size() / 2 - 1] + d[d.size() / 2]);
}

}  // namespace p3
}  // namespace realdata
