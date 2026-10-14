// Readers for the real-data hit-rate probe (phase 1). Header-only, no ROS.
//   * PCD (binary, v0.7) with the VIEWPOINT pose           -> KTH DynamicMap frames and GT cloud
//   * ROS 1 bag v2 (uncompressed chunks) with LaserScan and TFMessage decoders -> LILocBench
//   * TUM trajectory files and SE(2) pose interpolation    -> LILocBench ground-truth poses
//   * a compact frame cache (scans already placed in the map frame)
// Every reader throws std::runtime_error with the file and the reason on malformed input.
// Data files never enter the repository; tests build tiny synthetic files in a temp dir.
#pragma once
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace realdata {

[[noreturn]] inline void fail(const std::string& where, const std::string& why) {
  throw std::runtime_error(where + ": " + why);
}

// ---------------------------------------------------------------- PCD ---------------

struct PcdField { std::string name; int size{4}; char type{'F'}; int count{1}; std::size_t offset{0}; };

struct PcdHeader {
  std::vector<PcdField> fields;
  std::size_t points{0};
  std::size_t record_bytes{0};
  std::size_t data_offset{0};           // byte offset of the first record
  bool has_viewpoint{false};
  Eigen::Vector3d t{0, 0, 0};           // VIEWPOINT tx ty tz
  Eigen::Quaterniond q{1, 0, 0, 0};     // VIEWPOINT qw qx qy qz (as stored, not normalised)
  double q_norm{1.0};
};

struct PcdCloud {
  PcdHeader hdr;
  std::vector<float> xyz;               // 3 per kept point
  std::vector<float> intensity;         // 1 per kept point, empty unless requested and present
  std::size_t dropped_nonfinite{0};
};

inline PcdHeader readPcdHeader(std::ifstream& f, const std::string& path) {
  PcdHeader h;
  std::string line;
  std::size_t consumed = 0;
  bool binary = false, got_points = false;
  std::vector<std::string> names; std::vector<int> sizes, counts; std::vector<char> types;
  while (std::getline(f, line)) {
    consumed += line.size() + 1;
    std::istringstream is(line);
    std::string key; is >> key;
    if (key == "FIELDS") { std::string n; while (is >> n) names.push_back(n); }
    else if (key == "SIZE") { int v; while (is >> v) sizes.push_back(v); }
    else if (key == "TYPE") { std::string v; while (is >> v) types.push_back(v.empty() ? '?' : v[0]); }
    else if (key == "COUNT") { int v; while (is >> v) counts.push_back(v); }
    else if (key == "POINTS") { long long v = -1; is >> v; if (v < 0) fail(path, "bad POINTS"); h.points = static_cast<std::size_t>(v); got_points = true; }
    else if (key == "VIEWPOINT") {
      double v[7];
      for (double& x : v) if (!(is >> x)) fail(path, "bad VIEWPOINT (need 7 numbers)");
      h.has_viewpoint = true;
      h.t = Eigen::Vector3d(v[0], v[1], v[2]);
      h.q = Eigen::Quaterniond(v[3], v[4], v[5], v[6]);
      h.q_norm = std::sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5] + v[6] * v[6]);
    } else if (key == "DATA") {
      std::string mode; is >> mode;
      if (mode != "binary") fail(path, "only DATA binary is supported, got '" + mode + "'");
      binary = true;
      break;
    }
  }
  if (!binary) fail(path, "no DATA line");
  if (names.empty() || names.size() != sizes.size() || names.size() != types.size() ||
      (!counts.empty() && counts.size() != names.size()))
    fail(path, "inconsistent FIELDS/SIZE/TYPE/COUNT");
  if (!got_points) fail(path, "no POINTS line");
  std::size_t off = 0;
  for (std::size_t i = 0; i < names.size(); ++i) {
    PcdField fld; fld.name = names[i]; fld.size = sizes[i]; fld.type = types[i];
    fld.count = counts.empty() ? 1 : counts[i]; fld.offset = off;
    if (fld.size <= 0 || fld.count <= 0) fail(path, "bad SIZE or COUNT");
    off += static_cast<std::size_t>(fld.size) * fld.count;
    h.fields.push_back(fld);
  }
  h.record_bytes = off;
  h.data_offset = consumed;
  return h;
}

inline const PcdField* findField(const PcdHeader& h, const std::string& name) {
  for (const auto& f : h.fields) if (f.name == name) return &f;
  return nullptr;
}

// Reads the whole cloud. Non-finite points are dropped (and counted). Intensity is read
// only if want_intensity and the field exists as a 4-byte float.
inline PcdCloud readPcd(const std::string& path, bool want_intensity = false) {
  std::ifstream f(path, std::ios::binary);
  if (!f) fail(path, "cannot open");
  PcdCloud c;
  c.hdr = readPcdHeader(f, path);
  const PcdField *fx = findField(c.hdr, "x"), *fy = findField(c.hdr, "y"), *fz = findField(c.hdr, "z");
  if (!fx || !fy || !fz) fail(path, "missing x, y or z field");
  for (const PcdField* p : {fx, fy, fz})
    if (p->type != 'F' || p->size != 4 || p->count != 1) fail(path, "x, y, z must be 4-byte floats");
  const PcdField* fi = want_intensity ? findField(c.hdr, "intensity") : nullptr;
  if (want_intensity && (!fi || fi->type != 'F' || fi->size != 4 || fi->count != 1))
    fail(path, "intensity must be a 4-byte float");
  f.seekg(0, std::ios::end);
  const std::size_t file_size = static_cast<std::size_t>(f.tellg());
  const std::size_t need = c.hdr.data_offset + c.hdr.points * c.hdr.record_bytes;
  if (file_size < need)
    fail(path, "truncated: " + std::to_string(file_size) + " bytes, header needs " + std::to_string(need));
  f.seekg(static_cast<std::streamoff>(c.hdr.data_offset), std::ios::beg);
  std::vector<char> buf(c.hdr.record_bytes * 4096);
  c.xyz.reserve(c.hdr.points * 3);
  if (fi) c.intensity.reserve(c.hdr.points);
  std::size_t left = c.hdr.points;
  while (left > 0) {
    const std::size_t n = std::min<std::size_t>(left, 4096);
    f.read(buf.data(), static_cast<std::streamsize>(n * c.hdr.record_bytes));
    if (!f) fail(path, "read error");
    for (std::size_t i = 0; i < n; ++i) {
      const char* r = buf.data() + i * c.hdr.record_bytes;
      float v[3];
      std::memcpy(&v[0], r + fx->offset, 4);
      std::memcpy(&v[1], r + fy->offset, 4);
      std::memcpy(&v[2], r + fz->offset, 4);
      if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) { ++c.dropped_nonfinite; continue; }
      c.xyz.insert(c.xyz.end(), v, v + 3);
      if (fi) { float in; std::memcpy(&in, r + fi->offset, 4); c.intensity.push_back(in); }
    }
    left -= n;
  }
  return c;
}

// Writes a binary PCD with fields x y z (and intensity if given). For tests and tools.
inline void writePcd(const std::string& path, const std::vector<float>& xyz,
                     const std::vector<float>* intensity, const double* viewpoint7) {
  const std::size_t n = xyz.size() / 3;
  std::ofstream o(path, std::ios::binary);
  if (!o) fail(path, "cannot write");
  o << "# .PCD v0.7 - Point Cloud Data file format\nVERSION 0.7\n";
  o << (intensity ? "FIELDS x y z intensity\nSIZE 4 4 4 4\nTYPE F F F F\nCOUNT 1 1 1 1\n"
                  : "FIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\n");
  o << "WIDTH " << n << "\nHEIGHT 1\n";
  if (viewpoint7) {
    o.precision(17);
    o << "VIEWPOINT";
    for (int i = 0; i < 7; ++i) o << " " << viewpoint7[i];
    o << "\n";
  }
  o << "POINTS " << n << "\nDATA binary\n";
  for (std::size_t i = 0; i < n; ++i) {
    o.write(reinterpret_cast<const char*>(&xyz[3 * i]), 12);
    if (intensity) o.write(reinterpret_cast<const char*>(&(*intensity)[i]), 4);
  }
}

// -------------------------------------------------------- frames and the cache ------

// One sensor sweep already placed in the map frame. Frames with the same group form one
// integration step (the two lasers of one LILocBench time step).
struct Frame {
  std::uint32_t group{0};
  Eigen::Vector3d origin{0, 0, 0};
  std::vector<float> xyz;               // 3 floats per hit, map frame
  std::size_t size() const { return xyz.size() / 3; }
};

inline void writeCache(const std::string& path, int dim, const std::vector<Frame>& frames) {
  std::ofstream o(path, std::ios::binary);
  if (!o) fail(path, "cannot write");
  const char magic[4] = {'S', 'C', 'A', 'C'};
  const std::uint32_t version = 1, d = static_cast<std::uint32_t>(dim);
  const std::uint64_t n = frames.size();
  o.write(magic, 4); o.write(reinterpret_cast<const char*>(&version), 4);
  o.write(reinterpret_cast<const char*>(&d), 4); o.write(reinterpret_cast<const char*>(&n), 8);
  for (const Frame& fr : frames) {
    const std::uint32_t np = static_cast<std::uint32_t>(fr.size());
    o.write(reinterpret_cast<const char*>(&fr.group), 4);
    const double org[3] = {fr.origin.x(), fr.origin.y(), fr.origin.z()};
    o.write(reinterpret_cast<const char*>(org), 24);
    o.write(reinterpret_cast<const char*>(&np), 4);
    o.write(reinterpret_cast<const char*>(fr.xyz.data()), static_cast<std::streamsize>(fr.xyz.size() * 4));
  }
  if (!o) fail(path, "write error");
}

inline std::vector<Frame> readCache(const std::string& path, int* dim_out = nullptr) {
  std::ifstream f(path, std::ios::binary);
  if (!f) fail(path, "cannot open");
  char magic[4]; std::uint32_t version = 0, dim = 0; std::uint64_t n = 0;
  f.read(magic, 4); f.read(reinterpret_cast<char*>(&version), 4);
  f.read(reinterpret_cast<char*>(&dim), 4); f.read(reinterpret_cast<char*>(&n), 8);
  if (!f || std::memcmp(magic, "SCAC", 4) != 0) fail(path, "not a frame cache");
  if (version != 1) fail(path, "unsupported cache version " + std::to_string(version));
  if (dim_out) *dim_out = static_cast<int>(dim);
  std::vector<Frame> frames;
  frames.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(n, 1u << 20)));
  for (std::uint64_t i = 0; i < n; ++i) {
    Frame fr; std::uint32_t np = 0; double org[3];
    f.read(reinterpret_cast<char*>(&fr.group), 4);
    f.read(reinterpret_cast<char*>(org), 24);
    f.read(reinterpret_cast<char*>(&np), 4);
    if (!f) fail(path, "truncated frame header " + std::to_string(i));
    fr.origin = Eigen::Vector3d(org[0], org[1], org[2]);
    fr.xyz.resize(static_cast<std::size_t>(np) * 3);
    f.read(reinterpret_cast<char*>(fr.xyz.data()), static_cast<std::streamsize>(fr.xyz.size() * 4));
    if (!f) fail(path, "truncated frame " + std::to_string(i));
    frames.push_back(std::move(fr));
  }
  return frames;
}

// ----------------------------------------------------------- KTH frames ------------

struct PoseSanity {
  std::size_t frames{0};
  std::size_t bad_quaternion{0};        // | |q| - 1 | > 1e-3 or non-finite pose
  std::size_t big_translation_step{0};  // consecutive origins further apart than max_step_m
  std::size_t big_rotation_step{0};     // consecutive orientations further apart than max_rot_deg
  double max_step_m{0.0};
  double max_rot_deg{0.0};
  bool ok() const { return frames > 0 && bad_quaternion == 0 && big_translation_step == 0 && big_rotation_step == 0; }
};

// Limits are fixed in PREREG_P1.md: 10 Hz driving data, 5 m and 10 degrees between frames.
inline PoseSanity checkPoses(const std::vector<PcdHeader>& hdrs, double max_step_m = 5.0, double max_rot_deg = 10.0) {
  PoseSanity s; s.frames = hdrs.size();
  for (std::size_t i = 0; i < hdrs.size(); ++i) {
    const PcdHeader& h = hdrs[i];
    const bool finite = std::isfinite(h.t.x()) && std::isfinite(h.t.y()) && std::isfinite(h.t.z()) && std::isfinite(h.q_norm);
    if (!finite || !h.has_viewpoint || std::fabs(h.q_norm - 1.0) > 1e-3) { ++s.bad_quaternion; continue; }
    if (i == 0) continue;
    const PcdHeader& p = hdrs[i - 1];
    if (!std::isfinite(p.q_norm) || !p.has_viewpoint) continue;
    const double step = (h.t - p.t).norm();
    s.max_step_m = std::max(s.max_step_m, step);
    if (step > max_step_m) ++s.big_translation_step;
    const Eigen::Quaterniond qa = h.q.normalized(), qb = p.q.normalized();
    const double rot = qa.angularDistance(qb) * 180.0 / M_PI;
    s.max_rot_deg = std::max(s.max_rot_deg, rot);
    if (rot > max_rot_deg) ++s.big_rotation_step;
  }
  return s;
}

// ----------------------------------------------------------- ROS 1 bag v2 ----------

struct BagMessage {
  std::string topic;
  std::string type;
  std::uint32_t sec{0}, nsec{0};
  const std::uint8_t* data{nullptr};
  std::size_t size{0};
  double time() const { return sec + nsec * 1e-9; }
};

namespace detail {
inline std::uint32_t rd32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
// Splits a header block into name -> value bytes.
inline std::map<std::string, std::string> parseFields(const std::uint8_t* p, std::size_t n, const std::string& where) {
  std::map<std::string, std::string> out;
  std::size_t i = 0;
  while (i < n) {
    if (i + 4 > n) fail(where, "field length overruns the header");
    const std::uint32_t len = rd32(p + i); i += 4;
    if (len > n - i) fail(where, "field overruns the header");
    const std::string s(reinterpret_cast<const char*>(p + i), len); i += len;
    const std::size_t eq = s.find('=');
    if (eq == std::string::npos) fail(where, "field without '='");
    out[s.substr(0, eq)] = s.substr(eq + 1);
  }
  return out;
}
inline std::uint32_t u32of(const std::map<std::string, std::string>& m, const std::string& k, const std::string& where) {
  auto it = m.find(k);
  if (it == m.end() || it->second.size() != 4) fail(where, "missing 4-byte field " + k);
  return rd32(reinterpret_cast<const std::uint8_t*>(it->second.data()));
}
}  // namespace detail

// Reads a ROS 1 bag with uncompressed chunks and calls fn for every message in file
// order, with its topic and type. Compressed chunks (bz2, lz4) throw.
inline void readBag(const std::string& path, const std::function<void(const BagMessage&)>& fn) {
  std::ifstream f(path, std::ios::binary);
  if (!f) fail(path, "cannot open");
  std::string magic; std::getline(f, magic);
  if (magic != "#ROSBAG V2.0") fail(path, "not a ROS 1 bag v2 file");
  struct Conn { std::string topic, type; };
  std::map<std::uint32_t, Conn> conns;
  std::vector<std::uint8_t> hdr, data;
  auto readRecord = [&](std::map<std::string, std::string>& fields) -> bool {
    std::uint8_t lenbuf[4];
    f.read(reinterpret_cast<char*>(lenbuf), 4);
    if (f.gcount() == 0) return false;
    if (f.gcount() != 4) fail(path, "truncated record header length");
    const std::uint32_t hl = detail::rd32(lenbuf);
    hdr.resize(hl);
    f.read(reinterpret_cast<char*>(hdr.data()), hl);
    f.read(reinterpret_cast<char*>(lenbuf), 4);
    if (!f) fail(path, "truncated record header");
    const std::uint32_t dl = detail::rd32(lenbuf);
    data.resize(dl);
    f.read(reinterpret_cast<char*>(data.data()), dl);
    if (!f) fail(path, "truncated record data");
    fields = detail::parseFields(hdr.data(), hdr.size(), path);
    return true;
  };
  auto addConn = [&](const std::map<std::string, std::string>& hf, const std::vector<std::uint8_t>& d) {
    const std::uint32_t id = detail::u32of(hf, "conn", path);
    auto tp = hf.find("topic");
    if (tp == hf.end()) fail(path, "connection without topic");
    const auto df = detail::parseFields(d.data(), d.size(), path);
    auto ty = df.find("type");
    conns[id] = Conn{tp->second, ty == df.end() ? std::string() : ty->second};
  };
  std::map<std::string, std::string> fields;
  while (readRecord(fields)) {
    auto op = fields.find("op");
    if (op == fields.end() || op->second.size() != 1) fail(path, "record without op");
    const int code = static_cast<unsigned char>(op->second[0]);
    if (code == 7) { addConn(fields, data); continue; }
    if (code != 5) continue;      // bag header (3), index data (4), chunk info (6): not needed
    auto comp = fields.find("compression");
    if (comp == fields.end() || comp->second != "none")
      fail(path, "chunk compression '" + (comp == fields.end() ? std::string("?") : comp->second) + "' is not supported");
    const std::vector<std::uint8_t> chunk = data;          // inner records
    std::size_t i = 0;
    while (i < chunk.size()) {
      if (i + 4 > chunk.size()) fail(path, "chunk: truncated record");
      const std::uint32_t hl = detail::rd32(chunk.data() + i); i += 4;
      if (hl > chunk.size() - i) fail(path, "chunk: header overruns");
      const auto hf = detail::parseFields(chunk.data() + i, hl, path); i += hl;
      if (i + 4 > chunk.size()) fail(path, "chunk: truncated data length");
      const std::uint32_t dl = detail::rd32(chunk.data() + i); i += 4;
      if (dl > chunk.size() - i) fail(path, "chunk: data overruns");
      auto o2 = hf.find("op");
      if (o2 == hf.end() || o2->second.size() != 1) fail(path, "chunk: record without op");
      const int c2 = static_cast<unsigned char>(o2->second[0]);
      if (c2 == 7) {
        addConn(hf, std::vector<std::uint8_t>(chunk.begin() + i, chunk.begin() + i + dl));
      } else if (c2 == 2) {
        const std::uint32_t id = detail::u32of(hf, "conn", path);
        auto tm = hf.find("time");
        if (tm == hf.end() || tm->second.size() != 8) fail(path, "chunk: message without time");
        auto cn = conns.find(id);
        if (cn == conns.end()) fail(path, "chunk: message on unknown connection " + std::to_string(id));
        BagMessage m;
        m.topic = cn->second.topic; m.type = cn->second.type;
        m.sec = detail::rd32(reinterpret_cast<const std::uint8_t*>(tm->second.data()));
        m.nsec = detail::rd32(reinterpret_cast<const std::uint8_t*>(tm->second.data()) + 4);
        m.data = chunk.data() + i; m.size = dl;
        fn(m);
      }
      i += dl;
    }
  }
}

// Cursor over a serialized ROS 1 message.
struct Cursor {
  const std::uint8_t* p; std::size_t n; std::size_t i{0};
  Cursor(const std::uint8_t* d, std::size_t len) : p(d), n(len) {}
  void need(std::size_t k) const { if (k > n - i) fail("message", "truncated"); }
  std::uint32_t u32() { need(4); std::uint32_t v; std::memcpy(&v, p + i, 4); i += 4; return v; }
  float f32() { need(4); float v; std::memcpy(&v, p + i, 4); i += 4; return v; }
  double f64() { need(8); double v; std::memcpy(&v, p + i, 8); i += 8; return v; }
  std::string str() { const std::uint32_t l = u32(); need(l); std::string s(reinterpret_cast<const char*>(p + i), l); i += l; return s; }
};

struct LaserScanMsg {
  std::uint32_t seq{0}, sec{0}, nsec{0};
  std::string frame_id;
  float angle_min{0}, angle_max{0}, angle_increment{0}, time_increment{0}, scan_time{0}, range_min{0}, range_max{0};
  std::vector<float> ranges;
  double stamp() const { return sec + nsec * 1e-9; }
};

inline LaserScanMsg decodeLaserScan(const std::uint8_t* d, std::size_t n) {
  Cursor c(d, n);
  LaserScanMsg m;
  m.seq = c.u32(); m.sec = c.u32(); m.nsec = c.u32(); m.frame_id = c.str();
  m.angle_min = c.f32(); m.angle_max = c.f32(); m.angle_increment = c.f32();
  m.time_increment = c.f32(); m.scan_time = c.f32(); m.range_min = c.f32(); m.range_max = c.f32();
  const std::uint32_t nr = c.u32();
  c.need(static_cast<std::size_t>(nr) * 4);
  m.ranges.resize(nr);
  std::memcpy(m.ranges.data(), c.p + c.i, static_cast<std::size_t>(nr) * 4);
  return m;                              // intensities follow and are not needed
}

struct TfEdge {
  double stamp{0}; std::string parent, child;
  Eigen::Vector3d t{0, 0, 0}; Eigen::Quaterniond q{1, 0, 0, 0};
};

inline std::vector<TfEdge> decodeTfMessage(const std::uint8_t* d, std::size_t n) {
  Cursor c(d, n);
  const std::uint32_t cnt = c.u32();
  std::vector<TfEdge> out;
  for (std::uint32_t k = 0; k < cnt; ++k) {
    TfEdge e;
    c.u32(); const std::uint32_t s = c.u32(), ns = c.u32();
    e.stamp = s + ns * 1e-9;
    e.parent = c.str(); e.child = c.str();
    const double x = c.f64(), y = c.f64(), z = c.f64();
    const double qx = c.f64(), qy = c.f64(), qz = c.f64(), qw = c.f64();
    e.t = Eigen::Vector3d(x, y, z); e.q = Eigen::Quaterniond(qw, qx, qy, qz);
    out.push_back(e);
  }
  return out;
}

// ------------------------------------------------------------ SE(2) poses ----------

struct Se2 {
  double x{0}, y{0}, yaw{0};
  Se2 operator*(const Se2& o) const {      // this * o
    const double c = std::cos(yaw), s = std::sin(yaw);
    return Se2{x + c * o.x - s * o.y, y + s * o.x + c * o.y, wrap(yaw + o.yaw)};
  }
  Eigen::Vector2d apply(double px, double py) const {
    const double c = std::cos(yaw), s = std::sin(yaw);
    return Eigen::Vector2d(x + c * px - s * py, y + s * px + c * py);
  }
  static double wrap(double a) { while (a > M_PI) a -= 2 * M_PI; while (a <= -M_PI) a += 2 * M_PI; return a; }
};

inline double yawOf(const Eigen::Quaterniond& q) {
  const double w = q.w(), x = q.x(), y = q.y(), z = q.z();
  return std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
}

class PoseTrack {
 public:
  struct Sample { double t; Se2 pose; };
  void add(double t, const Se2& p) {
    if (!std::isfinite(t) || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.yaw)) fail("pose track", "non-finite pose");
    if (!s_.empty() && !(t > s_.back().t)) fail("pose track", "timestamps must increase strictly (t=" + std::to_string(t) + ")");
    s_.push_back({t, p});
  }
  std::size_t size() const { return s_.size(); }
  const std::vector<Sample>& samples() const { return s_; }
  double t0() const { return s_.empty() ? 0.0 : s_.front().t; }
  double t1() const { return s_.empty() ? 0.0 : s_.back().t; }
  // Interpolates at t. Returns false if t lies outside the track or the bracketing
  // samples are further apart than max_gap (no extrapolation, no bridging of holes).
  bool at(double t, Se2& out, double max_gap) const {
    if (s_.size() < 2 || t < s_.front().t || t > s_.back().t) return false;
    auto hi = std::lower_bound(s_.begin(), s_.end(), t, [](const Sample& a, double v) { return a.t < v; });
    if (hi == s_.begin()) { out = hi->pose; return true; }
    auto lo = hi - 1;
    if (hi->t - lo->t > max_gap) return false;
    const double a = (t - lo->t) / (hi->t - lo->t);
    out.x = lo->pose.x + a * (hi->pose.x - lo->pose.x);
    out.y = lo->pose.y + a * (hi->pose.y - lo->pose.y);
    out.yaw = Se2::wrap(lo->pose.yaw + a * Se2::wrap(hi->pose.yaw - lo->pose.yaw));
    return true;
  }
 private:
  std::vector<Sample> s_;
};

// TUM format: "t x y z qx qy qz qw" per line, '#' comments allowed. Uses x, y, yaw.
inline PoseTrack readTumTrack(const std::string& path) {
  std::ifstream f(path);
  if (!f) fail(path, "cannot open");
  PoseTrack tr; std::string line; std::size_t ln = 0;
  while (std::getline(f, line)) {
    ++ln;
    if (line.empty() || line[0] == '#') continue;
    std::istringstream is(line);
    double v[8]; int k = 0;
    while (k < 8 && (is >> v[k])) ++k;
    std::string extra;
    if (k != 8 || (is >> extra)) fail(path, "line " + std::to_string(ln) + ": need exactly 8 numbers");
    tr.add(v[0], Se2{v[1], v[2], yawOf(Eigen::Quaterniond(v[7], v[4], v[5], v[6]).normalized())});
  }
  if (tr.size() < 2) fail(path, "fewer than two poses");
  return tr;
}

inline Se2 se2FromTf(const TfEdge& e) { return Se2{e.t.x(), e.t.y(), yawOf(e.q.normalized())}; }

// ------------------------------------------------- LILocBench scan conversion ------

struct ScanRecord {
  double stamp{0};
  LaserScanMsg msg;
};

struct ConvertOptions {
  bool deskew{true};            // pose per beam at stamp + i * time_increment; false: one pose at stamp
  double max_gt_gap_s{0.2};     // largest GT hole bridged by interpolation
  double max_pair_skew_s{0.03}; // front and rear stamps of one step must agree to this
  double max_range_m{1e9};      // hits beyond this are dropped (the adapter keeps everything up to range_max)
};

struct ConvertReport {
  std::size_t steps{0};
  std::size_t scans_dropped_no_pose{0};
  std::size_t beams_total{0};
  std::size_t beams_used{0};
  std::size_t beams_invalid{0};         // not finite, below range_min or above range_max (no-return)
  std::size_t beams_no_pose{0};
  double max_pair_skew_s{0.0};
  double max_scan_gap_s{0.0};
  bool counts_match{true};
  bool pair_skew_ok{true};
};

inline bool convertScanPair(const PoseTrack& gt, const Se2& base_to_front, const Se2& base_to_rear,
                            const ScanRecord& front, const ScanRecord& rear, std::uint32_t group,
                            const ConvertOptions& opt, std::vector<Frame>& out, ConvertReport& rep) {
  bool any = false;
  const ScanRecord* scans[2] = {&front, &rear};
  const Se2* ext[2] = {&base_to_front, &base_to_rear};
  for (int s = 0; s < 2; ++s) {
    const LaserScanMsg& m = scans[s]->msg;
    Se2 base0;
    if (!gt.at(scans[s]->stamp, base0, opt.max_gt_gap_s)) { ++rep.scans_dropped_no_pose; rep.beams_total += m.ranges.size(); continue; }
    Frame fr; fr.group = group;
    const Se2 laser0 = base0 * (*ext[s]);
    fr.origin = Eigen::Vector3d(laser0.x, laser0.y, 0.0);
    fr.xyz.reserve(m.ranges.size() * 3);
    for (std::size_t i = 0; i < m.ranges.size(); ++i) {
      ++rep.beams_total;
      const float r = m.ranges[i];
      if (!std::isfinite(r) || r < m.range_min || r > m.range_max || r > opt.max_range_m) { ++rep.beams_invalid; continue; }
      Se2 base = base0;
      if (opt.deskew && i > 0 && !gt.at(scans[s]->stamp + i * static_cast<double>(m.time_increment), base, opt.max_gt_gap_s)) {
        ++rep.beams_no_pose; continue;
      }
      const double a = m.angle_min + i * static_cast<double>(m.angle_increment);
      const Eigen::Vector2d p = (base * (*ext[s])).apply(r * std::cos(a), r * std::sin(a));
      const float v[3] = {static_cast<float>(p.x()), static_cast<float>(p.y()), 0.0f};
      fr.xyz.insert(fr.xyz.end(), v, v + 3);
      ++rep.beams_used;
    }
    out.push_back(std::move(fr));
    any = true;
  }
  return any;
}

}  // namespace realdata
