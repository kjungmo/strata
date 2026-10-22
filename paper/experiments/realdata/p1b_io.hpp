// Readers and tables for phase 1b (PREREG_P1B.md). Header-only, no ROS.
//   * a zip reader (stored and deflate members, zip64 sizes and offsets) for the Spires per-scan PCD archive
//   * binary PCD from memory (x y z and the per-point timestamp)
//   * SE(3) ground-truth track (TUM) with interpolation, extrinsic, deskewing conversion of a Spires scan
//   * a streaming reader of the TLS map that keeps only points inside the Keble 02 area
//   * voxel tables (open addressing), streaming frame-cache reader and writer
// Every reader throws std::runtime_error with the file and the reason. Data never enter the repository.
#pragma once
#include <zlib.h>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <istream>
#include <streambuf>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "realdata_io.hpp"

namespace realdata {

inline std::uint64_t splitmix64(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

// ------------------------------------------------------------ engine voxel indexing -----

// The arithmetic of Voxel3DBackend::voxelId (21 bits per axis, offset 2^20), usable without an engine object.
struct VoxIndex {
  static constexpr std::int64_t kOff = 1 << 20, kBits = 21;
  double res{0.2};
  static std::int64_t vc(double w, double s) { return static_cast<std::int64_t>(std::floor(w / s)); }
  std::uint64_t pack(std::int64_t x, std::int64_t y, std::int64_t z) const {
    return static_cast<std::uint64_t>(((x + kOff) << (2 * kBits)) | ((y + kOff) << kBits) | (z + kOff));
  }
  void coords(const Eigen::Vector3d& p, std::int64_t& x, std::int64_t& y, std::int64_t& z) const {
    x = vc(p.x(), res); y = vc(p.y(), res); z = vc(p.z(), res);
  }
  std::uint64_t id(const Eigen::Vector3d& p) const {
    std::int64_t x, y, z; coords(p, x, y, z); return pack(x, y, z);
  }
  static void unpack(std::uint64_t id, std::int64_t& x, std::int64_t& y, std::int64_t& z) {
    const std::int64_t mask = (1LL << kBits) - 1;
    z = static_cast<std::int64_t>(id & mask) - kOff;
    y = static_cast<std::int64_t>((id >> kBits) & mask) - kOff;
    x = static_cast<std::int64_t>((id >> (2 * kBits)) & mask) - kOff;
  }
  Eigen::Vector3d center(std::uint64_t id) const {
    std::int64_t x, y, z; unpack(id, x, y, z);
    return {(x + 0.5) * res, (y + 0.5) * res, (z + 0.5) * res};
  }
};

// Key of a 1 m x 1 m horizontal column (PREREG_P1.md section 3 ground rule; PREREG_P1B.md 4.6).
inline std::uint64_t columnKey(double x, double y, double column_m = 1.0) {
  const std::int64_t cx = static_cast<std::int64_t>(std::floor(x / column_m));
  const std::int64_t cy = static_cast<std::int64_t>(std::floor(y / column_m));
  return static_cast<std::uint64_t>((cx + (1LL << 30)) * (1LL << 31) + (cy + (1LL << 30)));
}

// -------------------------------------------------------------------- voxel table ------

// Open-addressing table: key -> (count, minz). Used for TLS voxels, columns, label sets.
class VoxTable {
 public:
  static constexpr std::uint64_t kEmpty = ~0ULL;
  struct Slot { std::uint64_t key{kEmpty}; std::uint32_t count{0}; float minz{0}; };
  explicit VoxTable(std::size_t cap = 1024) { reset(round(cap)); }
  std::size_t size() const { return n_; }
  Slot& upsert(std::uint64_t key) {
    if ((n_ + 1) * 10 > s_.size() * 6) grow();
    std::size_t i = splitmix64(key) & mask_;
    while (s_[i].key != kEmpty && s_[i].key != key) i = (i + 1) & mask_;
    if (s_[i].key == kEmpty) { s_[i].key = key; s_[i].count = 0; ++n_; }
    return s_[i];
  }
  const Slot* find(std::uint64_t key) const {
    std::size_t i = splitmix64(key) & mask_;
    while (s_[i].key != kEmpty) { if (s_[i].key == key) return &s_[i]; i = (i + 1) & mask_; }
    return nullptr;
  }
  std::uint32_t countOf(std::uint64_t key) const { const Slot* s = find(key); return s ? s->count : 0u; }
  const std::vector<Slot>& slots() const { return s_; }
 private:
  static std::size_t round(std::size_t c) { std::size_t p = 16; while (p < c) p <<= 1; return p; }
  void reset(std::size_t cap) { s_.assign(cap, Slot{}); mask_ = cap - 1; n_ = 0; }
  void grow() {
    std::vector<Slot> old; old.swap(s_);
    reset(old.size() * 2);
    for (const Slot& o : old) if (o.key != kEmpty) { Slot& d = upsert(o.key); d.count = o.count; d.minz = o.minz; }
  }
  std::vector<Slot> s_;
  std::size_t mask_{0}, n_{0};
};

inline void saveVoxTable(const std::string& path, const VoxTable& t, double res) {
  std::ofstream o(path, std::ios::binary);
  if (!o) fail(path, "cannot write");
  const char magic[4] = {'V', 'X', 'T', 'B'};
  const std::uint32_t ver = 1;
  const std::uint64_t n = t.size();
  o.write(magic, 4); o.write(reinterpret_cast<const char*>(&ver), 4);
  o.write(reinterpret_cast<const char*>(&res), 8); o.write(reinterpret_cast<const char*>(&n), 8);
  std::vector<VoxTable::Slot> v;
  v.reserve(n);
  for (const auto& s : t.slots()) if (s.key != VoxTable::kEmpty) v.push_back(s);
  std::sort(v.begin(), v.end(), [](const VoxTable::Slot& a, const VoxTable::Slot& b) { return a.key < b.key; });   // byte-stable
  for (const auto& s : v) {
    o.write(reinterpret_cast<const char*>(&s.key), 8);
    o.write(reinterpret_cast<const char*>(&s.count), 4);
    o.write(reinterpret_cast<const char*>(&s.minz), 4);
  }
  if (!o) fail(path, "write error");
}

inline VoxTable loadVoxTable(const std::string& path, double* res_out = nullptr) {
  std::ifstream f(path, std::ios::binary);
  if (!f) fail(path, "cannot open");
  char magic[4]; std::uint32_t ver = 0; double res = 0; std::uint64_t n = 0;
  f.read(magic, 4); f.read(reinterpret_cast<char*>(&ver), 4);
  f.read(reinterpret_cast<char*>(&res), 8); f.read(reinterpret_cast<char*>(&n), 8);
  if (!f || std::memcmp(magic, "VXTB", 4) != 0) fail(path, "not a voxel table");
  if (ver != 1) fail(path, "unsupported voxel table version");
  if (res_out) *res_out = res;
  VoxTable t(static_cast<std::size_t>(n * 2 + 16));
  for (std::uint64_t i = 0; i < n; ++i) {
    std::uint64_t k; std::uint32_t c; float z;
    f.read(reinterpret_cast<char*>(&k), 8); f.read(reinterpret_cast<char*>(&c), 4); f.read(reinterpret_cast<char*>(&z), 4);
    if (!f) fail(path, "truncated voxel table");
    auto& s = t.upsert(k); s.count = c; s.minz = z;
  }
  return t;
}

// ------------------------------------------------------------ streaming frame cache ----

class CacheWriter {
 public:
  CacheWriter(const std::string& path, int dim) : path_(path), o_(path, std::ios::binary) {
    if (!o_) fail(path, "cannot write");
    const char magic[4] = {'S', 'C', 'A', 'C'};
    const std::uint32_t version = 1, d = static_cast<std::uint32_t>(dim);
    const std::uint64_t n = 0;
    o_.write(magic, 4); o_.write(reinterpret_cast<const char*>(&version), 4);
    o_.write(reinterpret_cast<const char*>(&d), 4); o_.write(reinterpret_cast<const char*>(&n), 8);
  }
  void add(const Frame& fr) {
    const std::uint32_t np = static_cast<std::uint32_t>(fr.size());
    o_.write(reinterpret_cast<const char*>(&fr.group), 4);
    const double org[3] = {fr.origin.x(), fr.origin.y(), fr.origin.z()};
    o_.write(reinterpret_cast<const char*>(org), 24);
    o_.write(reinterpret_cast<const char*>(&np), 4);
    o_.write(reinterpret_cast<const char*>(fr.xyz.data()), static_cast<std::streamsize>(fr.xyz.size() * 4));
    ++n_;
  }
  void finish() {
    o_.seekp(12);
    o_.write(reinterpret_cast<const char*>(&n_), 8);
    o_.flush();
    if (!o_) fail(path_, "write error");
  }
  std::uint64_t frames() const { return n_; }
 private:
  std::string path_;
  std::ofstream o_;
  std::uint64_t n_{0};
};

class CacheReader {
 public:
  explicit CacheReader(const std::string& path) : path_(path), f_(path, std::ios::binary) {
    if (!f_) fail(path, "cannot open");
    char magic[4]; std::uint32_t version = 0;
    f_.read(magic, 4); f_.read(reinterpret_cast<char*>(&version), 4);
    f_.read(reinterpret_cast<char*>(&dim_), 4); f_.read(reinterpret_cast<char*>(&n_), 8);
    if (!f_ || std::memcmp(magic, "SCAC", 4) != 0) fail(path, "not a frame cache");
    if (version != 1) fail(path, "unsupported cache version");
  }
  std::uint64_t frames() const { return n_; }
  int dim() const { return static_cast<int>(dim_); }
  // Reads the next frame. A failed read of a frame (an error of the /mnt/d mount under load, PREREG_P3.md Amendment 11) is repeated from the start of
  // that frame up to kReadRetries times; a file that really is short fails after the last retry with the old message.
  static constexpr int kReadRetries = 6;
  static inline int retry_sleep_ms = 1000;          // tests set 0 or a few ms
  bool next(Frame& fr) {
    if (i_ >= n_) return false;
    const std::streamoff pos = f_.tellg();
    for (int attempt = 0;; ++attempt) {
      const char* what = readFrame(fr);
      if (!what) break;
      if (attempt >= kReadRetries) fail(path_, std::string(what) + " " + std::to_string(i_));
      ++retries_;
      std::fprintf(stderr, "read retry %d of frame %llu of %s (%s)\n", attempt + 1, static_cast<unsigned long long>(i_), path_.c_str(), what);
      f_.clear();
      if (retry_sleep_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(retry_sleep_ms) * (attempt + 1)));
      f_.seekg(pos);
    }
    ++i_;
    return true;
  }
  std::uint64_t retries() const { return retries_; }
 private:
  const char* readFrame(Frame& fr) {      // nullptr on success, else the reason
    std::uint32_t np = 0; double org[3];
    f_.read(reinterpret_cast<char*>(&fr.group), 4);
    f_.read(reinterpret_cast<char*>(org), 24);
    f_.read(reinterpret_cast<char*>(&np), 4);
    if (!f_) return "truncated frame header";
    fr.origin = Eigen::Vector3d(org[0], org[1], org[2]);
    fr.xyz.resize(static_cast<std::size_t>(np) * 3);
    f_.read(reinterpret_cast<char*>(fr.xyz.data()), static_cast<std::streamsize>(fr.xyz.size() * 4));
    if (!f_) return "truncated frame";
    return nullptr;
  }
  std::string path_;
  std::ifstream f_;
  std::uint32_t dim_{0};
  std::uint64_t n_{0}, i_{0};
  std::uint64_t retries_{0};
};

// ----------------------------------------------------------------------------- zip -----

class ZipArchive {
 public:
  struct Entry { std::string name; std::uint16_t method{0}; std::uint64_t csize{0}, usize{0}, offset{0}; };
  explicit ZipArchive(const std::string& path) : path_(path), f_(path, std::ios::binary) {
    if (!f_) fail(path, "cannot open");
    f_.seekg(0, std::ios::end);
    const std::int64_t size = static_cast<std::int64_t>(f_.tellg());
    if (size < 22) fail(path, "too small for a zip file");
    const std::int64_t span = std::min<std::int64_t>(size, 22 + 65535);
    std::vector<std::uint8_t> tail(static_cast<std::size_t>(span));
    f_.seekg(size - span);
    f_.read(reinterpret_cast<char*>(tail.data()), span);
    if (!f_) fail(path, "read error");
    std::int64_t eocd = -1;
    for (std::int64_t i = span - 22; i >= 0; --i)
      if (le32(tail.data() + i) == 0x06054b50u) { eocd = i; break; }
    if (eocd < 0) fail(path, "no end-of-central-directory record");
    std::uint64_t total = le16(tail.data() + eocd + 10), cd_size = le32(tail.data() + eocd + 12), cd_off = le32(tail.data() + eocd + 16);
    if (total == 0xFFFFu || cd_size == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu) {
      // zip64 (PREREG_P3.md Amendment 10): the locator sits 20 bytes before the end-of-central-directory record and points at the zip64 record
      const std::int64_t loc_pos = size - span + eocd - 20;
      if (loc_pos < 0) fail(path, "zip64 archive without a zip64 end-of-central-directory locator");
      std::uint8_t loc[20];
      f_.clear(); f_.seekg(loc_pos);
      f_.read(reinterpret_cast<char*>(loc), 20);
      if (!f_ || le32(loc) != 0x07064b50u) fail(path, "zip64 archive without a zip64 end-of-central-directory locator");
      const std::uint64_t z64_off = le64(loc + 8);
      std::uint8_t z64[56];
      f_.clear(); f_.seekg(static_cast<std::streamoff>(z64_off));
      f_.read(reinterpret_cast<char*>(z64), 56);
      if (!f_ || le32(z64) != 0x06064b50u) fail(path, "bad zip64 end-of-central-directory record");
      if (le32(z64 + 16) != 0 || le32(z64 + 20) != 0) fail(path, "multi-disk zip64 archives are not supported");
      total = le64(z64 + 32); cd_size = le64(z64 + 40); cd_off = le64(z64 + 48);
    }
    if (cd_size > (1ull << 31)) fail(path, "central directory larger than 2 GB");
    std::vector<std::uint8_t> cd(cd_size);
    f_.clear(); f_.seekg(cd_off);
    f_.read(reinterpret_cast<char*>(cd.data()), cd_size);
    if (!f_) fail(path, "cannot read the central directory");
    std::size_t p = 0;
    for (std::uint64_t k = 0; k < total; ++k) {
      if (p + 46 > cd.size() || le32(cd.data() + p) != 0x02014b50u) fail(path, "bad central directory entry " + std::to_string(k));
      Entry e;
      e.method = le16(cd.data() + p + 10);
      e.csize = le32(cd.data() + p + 20); e.usize = le32(cd.data() + p + 24);
      const std::uint32_t nl = le16(cd.data() + p + 28), xl = le16(cd.data() + p + 30), cl = le16(cd.data() + p + 32);
      e.offset = le32(cd.data() + p + 42);
      if (p + 46 + nl > cd.size()) fail(path, "central directory entry overruns");
      e.name.assign(reinterpret_cast<const char*>(cd.data() + p + 46), nl);
      if (p + 46 + nl + xl > cd.size()) fail(path, "central directory entry overruns");
      if (e.csize == 0xFFFFFFFFu || e.usize == 0xFFFFFFFFu || e.offset == 0xFFFFFFFFu) {
        // the zip64 extra field (id 0x0001) holds, in this order, only the values that are 0xFFFFFFFF in the header: usize, csize, offset (disk number not used)
        const std::uint8_t* x = cd.data() + p + 46 + nl;
        std::size_t q = 0; bool found = false;
        while (q + 4 <= xl) {
          const std::uint32_t id = le16(x + q), len = le16(x + q + 2);
          if (q + 4 + len > xl) break;
          if (id == 0x0001) {
            std::size_t r = q + 4; const std::size_t end = q + 4 + len;
            auto take = [&](std::uint64_t& v) { if (r + 8 > end) fail(path, "short zip64 extra field of " + e.name); v = le64(x + r); r += 8; };
            if (e.usize == 0xFFFFFFFFu) take(e.usize);
            if (e.csize == 0xFFFFFFFFu) take(e.csize);
            if (e.offset == 0xFFFFFFFFu) take(e.offset);
            found = true; break;
          }
          q += 4 + len;
        }
        if (!found) fail(path, "zip64 entry without a zip64 extra field: " + e.name);
      }
      p += 46 + nl + xl + cl;
      entries_.push_back(e);
    }
  }
  const std::vector<Entry>& entries() const { return entries_; }
  // Reads and inflates one member into out (uncompressed bytes).
  void read(const Entry& e, std::vector<std::uint8_t>& out) {
    std::uint8_t lh[30];
    f_.clear(); f_.seekg(static_cast<std::streamoff>(e.offset));
    f_.read(reinterpret_cast<char*>(lh), 30);
    if (!f_ || le32(lh) != 0x04034b50u) fail(path_, "bad local header of " + e.name);
    const std::uint32_t skip = le16(lh + 26) + le16(lh + 28);
    f_.seekg(static_cast<std::streamoff>(e.offset) + 30 + skip);
    comp_.resize(e.csize);
    f_.read(reinterpret_cast<char*>(comp_.data()), static_cast<std::streamsize>(e.csize));
    if (!f_) fail(path_, "truncated member " + e.name);
    out.resize(e.usize);
    if (e.method == 0) {
      if (e.csize != e.usize) fail(path_, "stored member with different sizes: " + e.name);
      std::memcpy(out.data(), comp_.data(), e.usize);
    } else if (e.method == 8) {
      z_stream zs; std::memset(&zs, 0, sizeof zs);
      if (inflateInit2(&zs, -15) != Z_OK) fail(path_, "inflateInit2 failed");
      zs.next_in = comp_.data(); zs.avail_in = static_cast<uInt>(e.csize);
      zs.next_out = out.data(); zs.avail_out = static_cast<uInt>(e.usize);
      const int rc = inflate(&zs, Z_FINISH);
      const bool ok = (rc == Z_STREAM_END) && zs.total_out == e.usize;
      inflateEnd(&zs);
      if (!ok) fail(path_, "inflate failed for " + e.name + " (zlib code " + std::to_string(rc) + ")");
    } else {
      fail(path_, "unsupported compression method " + std::to_string(e.method) + " in " + e.name);
    }
  }
 private:
  static std::uint32_t le16(const std::uint8_t* p) { return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8); }
  static std::uint32_t le32(const std::uint8_t* p) { return le16(p) | (le16(p + 2) << 16); }
  static std::uint64_t le64(const std::uint8_t* p) { return static_cast<std::uint64_t>(le32(p)) | (static_cast<std::uint64_t>(le32(p + 4)) << 32); }
  std::string path_;
  std::ifstream f_;
  std::vector<Entry> entries_;
  std::vector<std::uint8_t> comp_;
};

// ------------------------------------------------------------------ PCD from memory ----

struct MemBuf : std::streambuf {
  MemBuf(const char* b, std::size_t n) { char* p = const_cast<char*>(b); setg(p, p, p + n); }
};

struct SpiresPcd {
  std::vector<float> xyz;       // finite points only
  std::vector<double> ts;       // per-point timestamp, empty if the field is absent
  std::size_t points{0};        // POINTS of the header
  std::size_t dropped_nonfinite{0};
};

// Parses a binary PCD held in memory. x, y, z must be 4-byte floats; a `timestamp` field (8-byte float) is read if present.
inline SpiresPcd parsePcdBuffer(const std::vector<std::uint8_t>& buf, const std::string& name) {
  MemBuf mb(reinterpret_cast<const char*>(buf.data()), buf.size());
  std::istream is(&mb);
  const PcdHeader h = readPcdHeader(is, name);
  const PcdField *fx = findField(h, "x"), *fy = findField(h, "y"), *fz = findField(h, "z"), *ft = findField(h, "timestamp");
  if (!fx || !fy || !fz) fail(name, "missing x, y or z field");
  for (const PcdField* p : {fx, fy, fz})
    if (p->type != 'F' || p->size != 4 || p->count != 1) fail(name, "x, y, z must be 4-byte floats");
  if (ft && (ft->type != 'F' || ft->size != 8 || ft->count != 1)) ft = nullptr;      // present but not a double: treated as absent
  const std::size_t need = h.data_offset + h.points * h.record_bytes;
  if (buf.size() < need) fail(name, "truncated: " + std::to_string(buf.size()) + " bytes, header needs " + std::to_string(need));
  // The Spires scans end with a block of zero bytes after the last record (3856 bytes in the scans inspected). Anything else after
  // the records, or a long tail, means the record layout is not understood.
  if (buf.size() - need > 65536) fail(name, "size " + std::to_string(buf.size()) + " bytes, header says " + std::to_string(need) + " (record layout not understood)");
  for (std::size_t i = need; i < buf.size(); ++i)
    if (buf[i] != 0) fail(name, "non-zero byte after the last record (record layout not understood)");
  SpiresPcd out;
  out.points = h.points;
  out.xyz.reserve(h.points * 3);
  if (ft) out.ts.reserve(h.points);
  for (std::size_t i = 0; i < h.points; ++i) {
    const std::uint8_t* r = buf.data() + h.data_offset + i * h.record_bytes;
    float v[3];
    std::memcpy(&v[0], r + fx->offset, 4); std::memcpy(&v[1], r + fy->offset, 4); std::memcpy(&v[2], r + fz->offset, 4);
    if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) { ++out.dropped_nonfinite; continue; }
    out.xyz.insert(out.xyz.end(), v, v + 3);
    if (ft) { double t; std::memcpy(&t, r + ft->offset, 8); out.ts.push_back(t); }
  }
  return out;
}

// Unix time of a member name such as "lidar-clouds/1710255656.047839539.pcd". NaN if the name has no number.
inline double scanTimeFromName(const std::string& name) {
  std::size_t b = name.find_last_of('/');
  b = (b == std::string::npos) ? 0 : b + 1;
  std::size_t e = name.rfind(".pcd");
  if (e == std::string::npos || e <= b) return std::nan("");
  const std::string s = name.substr(b, e - b);
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  return (end == s.c_str() || *end != '\0') ? std::nan("") : v;
}

// ------------------------------------------------------------------- SE(3) track -------

struct Pose3 { Eigen::Vector3d t{0, 0, 0}; Eigen::Quaterniond q{1, 0, 0, 0}; };

class PoseTrack3 {
 public:
  struct Sample { double t; Pose3 pose; };
  void add(double t, const Pose3& p) {
    if (!std::isfinite(t) || !p.t.allFinite() || !p.q.coeffs().allFinite()) fail("pose track", "non-finite pose");
    if (!s_.empty() && !(t > s_.back().t)) fail("pose track", "timestamps must increase strictly (t=" + std::to_string(t) + ")");
    s_.push_back({t, p});
  }
  std::size_t size() const { return s_.size(); }
  const std::vector<Sample>& samples() const { return s_; }
  double t0() const { return s_.empty() ? 0.0 : s_.front().t; }
  double t1() const { return s_.empty() ? 0.0 : s_.back().t; }
  // Linear in position, spherical in orientation. false outside the track or across a bracket wider than max_gap.
  bool at(double t, Pose3& out, double max_gap) const {
    if (s_.size() < 2 || !(t >= s_.front().t) || t > s_.back().t) return false;
    auto hi = std::lower_bound(s_.begin(), s_.end(), t, [](const Sample& a, double v) { return a.t < v; });
    if (hi == s_.begin()) { out = hi->pose; return true; }
    auto lo = hi - 1;
    if (hi->t - lo->t > max_gap) return false;
    const double a = (t - lo->t) / (hi->t - lo->t);
    out.t = lo->pose.t + a * (hi->pose.t - lo->pose.t);
    out.q = lo->pose.q.slerp(a, hi->pose.q);
    return true;
  }
 private:
  std::vector<Sample> s_;
};

// TUM: "t x y z qx qy qz qw" per line. Quaternions are normalised on reading.
inline PoseTrack3 readTumTrack3(const std::string& path) {
  std::ifstream f(path);
  if (!f) fail(path, "cannot open");
  PoseTrack3 tr; std::string line; std::size_t ln = 0;
  while (std::getline(f, line)) {
    ++ln;
    if (line.empty() || line[0] == '#') continue;
    std::istringstream is(line);
    double v[8]; int k = 0;
    while (k < 8 && (is >> v[k])) ++k;
    std::string extra;
    if (k != 8 || (is >> extra)) fail(path, "line " + std::to_string(ln) + ": need exactly 8 numbers");
    Eigen::Quaterniond q(v[7], v[4], v[5], v[6]);
    if (!(q.norm() > 0.5)) fail(path, "line " + std::to_string(ln) + ": quaternion not near unit length");
    Pose3 p; p.t = Eigen::Vector3d(v[1], v[2], v[3]); p.q = q.normalized();
    tr.add(v[0], p);
  }
  if (tr.size() < 2) fail(path, "fewer than two poses");
  return tr;
}

// ---------------------------------------------------------- Spires scan conversion -----

struct SpiresExtrinsic {                 // T_base_lidar: a point in the lidar frame maps to the base frame by q * p + t
  Eigen::Quaterniond q{0, 0, 0, 1};      // README of the dataset: xyz 0 0 0.124, quaternion xyzw 0 0 1 0 (w = 0)
  Eigen::Vector3d t{0, 0, 0.124};
};

struct SpiresConvertOptions {
  double dt{0.0};                        // added to every point time (section 4.5)
  double r_min{1.5}, r_max{20.0};        // lidar-frame range window (section 4.3)
  double max_gap_s{0.25};                // widest GT bracket that is interpolated
  double span_slack_s{1.0};              // PREREG_P1B.md 4.3 fallback: timestamps this far outside the GT span
  double span_fallback_share{0.01};      // ... for more than this share of the kept points: use the scan time
};

struct SpiresConvertReport {
  std::size_t scans{0}, scans_dropped_no_pose{0}, scans_scan_time_fallback{0}, scans_no_timestamp_field{0};
  std::size_t points_in{0}, points_nonfinite{0}, points_out_of_range{0}, points_no_pose{0}, points_used{0};
};

// Converts one scan. Returns false (scan dropped, counted) if no pose exists for the frame origin.
// scan_time is the file-name time used by the fallback; NaN means the name had no number.
inline bool convertSpiresScan(const SpiresPcd& s, double scan_time, const PoseTrack3& gt, const SpiresExtrinsic& ex,
                              const SpiresConvertOptions& opt, std::uint32_t group, Frame& fr, SpiresConvertReport& rep) {
  ++rep.scans;
  rep.points_in += s.points; rep.points_nonfinite += s.dropped_nonfinite;
  const std::size_t n = s.xyz.size() / 3;
  std::vector<std::uint32_t> keep;
  keep.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double r = std::sqrt(static_cast<double>(s.xyz[3 * i]) * s.xyz[3 * i] + static_cast<double>(s.xyz[3 * i + 1]) * s.xyz[3 * i + 1] +
                               static_cast<double>(s.xyz[3 * i + 2]) * s.xyz[3 * i + 2]);
    if (r < opt.r_min || r > opt.r_max) { ++rep.points_out_of_range; continue; }
    keep.push_back(static_cast<std::uint32_t>(i));
  }
  bool per_point = !s.ts.empty();
  if (s.ts.empty()) ++rep.scans_no_timestamp_field;
  if (per_point && !keep.empty()) {
    std::size_t outside = 0;
    for (std::uint32_t i : keep) if (s.ts[i] < gt.t0() - opt.span_slack_s || s.ts[i] > gt.t1() + opt.span_slack_s) ++outside;
    if (static_cast<double>(outside) > opt.span_fallback_share * keep.size()) { per_point = false; ++rep.scans_scan_time_fallback; }
  }
  if (!per_point && !std::isfinite(scan_time)) { ++rep.scans_dropped_no_pose; return false; }
  auto timeOf = [&](std::uint32_t i) { return (per_point ? s.ts[i] : scan_time) + opt.dt; };
  // reference time: median point time of the kept points
  double tref;
  if (keep.empty()) tref = std::isfinite(scan_time) ? scan_time + opt.dt : 0.0;
  else if (per_point) {
    std::vector<double> t; t.reserve(keep.size());
    for (std::uint32_t i : keep) t.push_back(timeOf(i));
    std::nth_element(t.begin(), t.begin() + t.size() / 2, t.end());
    tref = t[t.size() / 2];
  } else tref = scan_time + opt.dt;
  Pose3 p0;
  if (!gt.at(tref, p0, opt.max_gap_s)) { ++rep.scans_dropped_no_pose; rep.points_no_pose += keep.size(); return false; }
  fr = Frame(); fr.group = group;
  fr.origin = p0.t + p0.q * ex.t;
  fr.xyz.reserve(keep.size() * 3);
  Pose3 p;
  for (std::uint32_t i : keep) {
    if (per_point) { if (!gt.at(timeOf(i), p, opt.max_gap_s)) { ++rep.points_no_pose; continue; } }
    else p = p0;
    const Eigen::Vector3d pl(s.xyz[3 * i], s.xyz[3 * i + 1], s.xyz[3 * i + 2]);
    const Eigen::Vector3d w = p.t + p.q * (ex.q * pl + ex.t);
    const float v[3] = {static_cast<float>(w.x()), static_cast<float>(w.y()), static_cast<float>(w.z())};
    fr.xyz.insert(fr.xyz.end(), v, v + 3);
    ++rep.points_used;
  }
  return true;
}

// --------------------------------------------------------------- Keble 02 TLS area -----

// Union of discs of radius R around the (x, y) positions of a track. contains() is exact; a memo of 0.25 m
// cells decides most queries without looking at the positions.
class DiscUnion {
 public:
  DiscUnion(const std::vector<Eigen::Vector2d>& pos, double radius) : pos_(pos), r_(radius) {
    for (std::size_t i = 0; i < pos_.size(); ++i) tiles_[tileKey(pos_[i].x(), pos_[i].y())].push_back(static_cast<std::uint32_t>(i));
  }
  bool contains(double x, double y) const {
    const std::int64_t cx = static_cast<std::int64_t>(std::floor(x / kCell)), cy = static_cast<std::int64_t>(std::floor(y / kCell));
    const std::uint64_t key = static_cast<std::uint64_t>((cx + (1LL << 30)) * (1LL << 31) + (cy + (1LL << 30)));
    auto it = memo_.find(key);
    std::uint8_t st;
    if (it == memo_.end()) {
      const double mx = (cx + 0.5) * kCell, my = (cy + 0.5) * kCell;
      const double d = nearest(mx, my);
      const double half_diag = 0.7072 * kCell;
      st = (d + half_diag <= r_) ? 1 : (d - half_diag > r_) ? 0 : 2;
      memo_.emplace(key, st);
    } else st = it->second;
    if (st != 2) return st == 1;
    return nearest(x, y) <= r_;
  }
 private:
  static constexpr double kCell = 0.25;
  std::uint64_t tileKey(double x, double y) const {
    return columnKey(x, y, r_);
  }
  double nearest(double x, double y) const {
    const std::int64_t tx = static_cast<std::int64_t>(std::floor(x / r_)), ty = static_cast<std::int64_t>(std::floor(y / r_));
    double best = 1e300;
    for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) {
      const std::uint64_t key = static_cast<std::uint64_t>((tx + dx + (1LL << 30)) * (1LL << 31) + (ty + dy + (1LL << 30)));
      auto it = tiles_.find(key);
      if (it == tiles_.end()) continue;
      for (std::uint32_t i : it->second) best = std::min(best, std::hypot(pos_[i].x() - x, pos_[i].y() - y));
    }
    return best;
  }
  std::vector<Eigen::Vector2d> pos_;
  double r_;
  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> tiles_;
  mutable std::unordered_map<std::uint64_t, std::uint8_t> memo_;
};

struct TlsTables {
  std::vector<double> resolutions;        // e.g. 0.2, 0.1, 0.05
  std::vector<VoxTable> vox;              // per resolution: count and lowest z per voxel
  VoxTable columns;                       // 1 m columns: count and lowest z
  std::uint64_t points_read{0}, points_kept{0}, points_discarded{0}, points_nonfinite{0};
};

// Reads a binary PCD sequentially; only points inside the area enter the tables, all others are discarded
// at read time (PREREG_P1B.md 4.2).
inline TlsTables readTlsArea(const std::string& path, const DiscUnion& area, const std::vector<double>& resolutions) {
  std::ifstream f(path, std::ios::binary);
  if (!f) fail(path, "cannot open");
  const PcdHeader h = readPcdHeader(f, path);
  const PcdField *fx = findField(h, "x"), *fy = findField(h, "y"), *fz = findField(h, "z");
  if (!fx || !fy || !fz) fail(path, "missing x, y or z field");
  for (const PcdField* p : {fx, fy, fz})
    if (p->type != 'F' || p->size != 4 || p->count != 1) fail(path, "x, y, z must be 4-byte floats");
  f.seekg(0, std::ios::end);
  const std::size_t file_size = static_cast<std::size_t>(f.tellg());
  const std::size_t need = h.data_offset + h.points * h.record_bytes;
  if (file_size != need) fail(path, "size " + std::to_string(file_size) + " bytes, header says " + std::to_string(need) + " (record layout not understood)");
  f.seekg(static_cast<std::streamoff>(h.data_offset), std::ios::beg);
  TlsTables t;
  t.resolutions = resolutions;
  t.columns = VoxTable(1 << 16);
  for (std::size_t i = 0; i < resolutions.size(); ++i) t.vox.emplace_back(1 << 16);
  std::vector<VoxIndex> idx;
  for (double r : resolutions) { VoxIndex v; v.res = r; idx.push_back(v); }
  std::vector<char> buf(h.record_bytes * 8192);
  std::size_t left = h.points;
  while (left > 0) {
    const std::size_t n = std::min<std::size_t>(left, 8192);
    f.read(buf.data(), static_cast<std::streamsize>(n * h.record_bytes));
    if (!f) fail(path, "read error");
    for (std::size_t i = 0; i < n; ++i) {
      const char* r = buf.data() + i * h.record_bytes;
      float v[3];
      std::memcpy(&v[0], r + fx->offset, 4); std::memcpy(&v[1], r + fy->offset, 4); std::memcpy(&v[2], r + fz->offset, 4);
      ++t.points_read;
      if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) { ++t.points_nonfinite; continue; }
      if (!area.contains(v[0], v[1])) { ++t.points_discarded; continue; }
      ++t.points_kept;
      const Eigen::Vector3d p(v[0], v[1], v[2]);
      for (std::size_t k = 0; k < idx.size(); ++k) {
        auto& s = t.vox[k].upsert(idx[k].id(p));
        if (s.count == 0 || v[2] < s.minz) s.minz = v[2];
        if (s.count < 0xFFFFFFFFu) ++s.count;
      }
      auto& c = t.columns.upsert(columnKey(v[0], v[1]));
      if (c.count == 0 || v[2] < c.minz) c.minz = v[2];
      if (c.count < 0xFFFFFFFFu) ++c.count;
    }
    left -= n;
  }
  return t;
}

}  // namespace realdata
