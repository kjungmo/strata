// Tests of the phase 1b code on small synthetic fixtures. No real data are needed.
#include <gtest/gtest.h>
#include <zlib.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include "p1b_core.hpp"
#include "strata_core/voxel3d_backend.hpp"

using namespace realdata;

namespace {

std::string tmpPath(const std::string& name) {
  static const std::string dir = [] {
    char t[] = "/tmp/p1b_test_XXXXXX";
    const char* d = mkdtemp(t);
    return std::string(d ? d : "/tmp");
  }();
  return dir + "/" + name;
}

// ---- helpers: a tiny zip writer (stored or deflate) and PCD bytes ----
void put16(std::string& s, std::uint32_t v) { s.push_back(char(v & 255)); s.push_back(char((v >> 8) & 255)); }
void put32(std::string& s, std::uint32_t v) { put16(s, v & 0xFFFF); put16(s, v >> 16); }

std::string deflateRaw(const std::string& in) {
  z_stream zs; std::memset(&zs, 0, sizeof zs);
  EXPECT_EQ(deflateInit2(&zs, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY), Z_OK);
  std::string out(deflateBound(&zs, in.size()), '\0');
  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data())); zs.avail_in = in.size();
  zs.next_out = reinterpret_cast<Bytef*>(&out[0]); zs.avail_out = out.size();
  EXPECT_EQ(deflate(&zs, Z_FINISH), Z_STREAM_END);
  out.resize(zs.total_out);
  deflateEnd(&zs);
  return out;
}

void writeZip(const std::string& path, const std::vector<std::pair<std::string, std::string>>& members, bool deflate) {
  std::string body, cd;
  for (const auto& m : members) {
    const std::string& name = m.first; const std::string& data = m.second;
    const std::string comp = deflate ? deflateRaw(data) : data;
    const std::uint32_t crc = crc32(0, reinterpret_cast<const Bytef*>(data.data()), data.size());
    const std::uint32_t off = body.size();
    put32(body, 0x04034b50); put16(body, 20); put16(body, 0); put16(body, deflate ? 8 : 0); put16(body, 0); put16(body, 0);
    put32(body, crc); put32(body, comp.size()); put32(body, data.size()); put16(body, name.size()); put16(body, 0);
    body += name; body += comp;
    put32(cd, 0x02014b50); put16(cd, 20); put16(cd, 20); put16(cd, 0); put16(cd, deflate ? 8 : 0); put16(cd, 0); put16(cd, 0);
    put32(cd, crc); put32(cd, comp.size()); put32(cd, data.size()); put16(cd, name.size()); put16(cd, 0); put16(cd, 0);
    put16(cd, 0); put16(cd, 0); put32(cd, 0); put32(cd, off); cd += name;
  }
  std::string eocd;
  put32(eocd, 0x06054b50); put16(eocd, 0); put16(eocd, 0); put16(eocd, members.size()); put16(eocd, members.size());
  put32(eocd, cd.size()); put32(eocd, body.size()); put16(eocd, 0);
  std::ofstream o(path, std::ios::binary);
  o << body << cd << eocd;
}

// PCD with fields x y z intensity timestamp ring (like the Spires scans)
std::vector<std::uint8_t> spiresPcdBytes(const std::vector<std::array<double, 4>>& pts /* x y z t */, bool with_ts = true) {
  std::string h = "# .PCD v0.7\nVERSION 0.7\n";
  if (with_ts) h += "FIELDS x y z intensity timestamp ring\nSIZE 4 4 4 4 8 2\nTYPE F F F F F U\nCOUNT 1 1 1 1 1 1\n";
  else h += "FIELDS x y z intensity\nSIZE 4 4 4 4\nTYPE F F F F\nCOUNT 1 1 1 1\n";
  h += "WIDTH " + std::to_string(pts.size()) + "\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS " + std::to_string(pts.size()) + "\nDATA binary\n";
  std::vector<std::uint8_t> b(h.begin(), h.end());
  for (const auto& p : pts) {
    const float f[4] = {float(p[0]), float(p[1]), float(p[2]), 7.0f};
    const std::uint8_t* q = reinterpret_cast<const std::uint8_t*>(f);
    b.insert(b.end(), q, q + 16);
    if (with_ts) {
      const double t = p[3]; const std::uint16_t ring = 3;
      const std::uint8_t* tq = reinterpret_cast<const std::uint8_t*>(&t);
      b.insert(b.end(), tq, tq + 8);
      const std::uint8_t* rq = reinterpret_cast<const std::uint8_t*>(&ring);
      b.insert(b.end(), rq, rq + 2);
    }
  }
  return b;
}

}  // namespace

// ------------------------------------------------------------------ voxel indexing ----

TEST(VoxIndex, MatchesEngineVoxelIdAndCentre) {
  std::mt19937_64 rng(1);
  std::uniform_real_distribution<double> u(-80, 80);
  for (double res : {0.05, 0.1, 0.2}) {
    strata_core::Voxel3DBackend b(res, strata_core::LayeredMapParams());
    VoxIndex vi; vi.res = res;
    for (int i = 0; i < 2000; ++i) {
      const Eigen::Vector3d p(u(rng), u(rng), u(rng));
      ASSERT_EQ(vi.id(p), static_cast<std::uint64_t>(b.voxelId(p)));
      const Eigen::Vector3d c = vi.center(vi.id(p)), e = b.voxelCenter(b.voxelId(p));
      ASSERT_NEAR((c - e).norm(), 0.0, 1e-12);
    }
  }
}

TEST(VoxTable, InsertGrowFindSaveLoad) {
  VoxTable t(4);
  for (std::uint64_t i = 0; i < 5000; ++i) { auto& s = t.upsert(i * 7919 + 3); s.count = static_cast<std::uint32_t>(i); s.minz = float(i) * 0.5f; }
  EXPECT_EQ(t.size(), 5000u);
  for (std::uint64_t i = 0; i < 5000; ++i) { const auto* s = t.find(i * 7919 + 3); ASSERT_NE(s, nullptr); EXPECT_EQ(s->count, i); }
  EXPECT_EQ(t.find(4), nullptr);
  EXPECT_EQ(t.countOf(4), 0u);
  t.upsert(3).count += 1;                     // upsert of an existing key does not duplicate
  EXPECT_EQ(t.size(), 5000u);
  saveVoxTable(tmpPath("t.tbl"), t, 0.2);
  double res = 0;
  VoxTable u = loadVoxTable(tmpPath("t.tbl"), &res);
  EXPECT_EQ(res, 0.2);
  EXPECT_EQ(u.size(), 5000u);
  EXPECT_EQ(u.countOf(3), 1u);                // slot of key 3: i = 0 so count 0, +1
  EXPECT_FLOAT_EQ(u.find(2 * 7919 + 3)->minz, 1.0f);
}

// ------------------------------------------------------------------------ zip ---------

TEST(Zip, ReadsStoredAndDeflatedMembers) {
  std::string big(100000, 'a');
  for (std::size_t i = 0; i < big.size(); i += 7) big[i] = char('b' + i % 11);
  for (bool deflate : {false, true}) {
    const std::string path = tmpPath(deflate ? "d.zip" : "s.zip");
    writeZip(path, {{"lidar-clouds/2.5.pcd", "hello"}, {"lidar-clouds/1.5.pcd", big}, {"other.txt", ""}}, deflate);
    ZipArchive z(path);
    ASSERT_EQ(z.entries().size(), 3u);
    std::vector<std::uint8_t> out;
    z.read(z.entries()[1], out);
    EXPECT_EQ(std::string(out.begin(), out.end()), big);
    z.read(z.entries()[0], out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "hello");
    z.read(z.entries()[2], out);
    EXPECT_TRUE(out.empty());
  }
}

TEST(Zip, RejectsNonZipAndCorruptMember) {
  { std::ofstream o(tmpPath("bad.zip"), std::ios::binary); o << std::string(100, 'x'); }
  EXPECT_THROW(ZipArchive(tmpPath("bad.zip")), std::runtime_error);
  EXPECT_THROW(ZipArchive(tmpPath("missing.zip")), std::runtime_error);
  const std::string path = tmpPath("c.zip");
  writeZip(path, {{"a.pcd", std::string(5000, 'q')}}, true);
  std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
  f.seekp(40); f.put('\xFF'); f.put('\xFF'); f.put('\xFF');           // corrupt the deflate stream
  f.close();
  ZipArchive z(path);
  std::vector<std::uint8_t> out;
  EXPECT_THROW(z.read(z.entries()[0], out), std::runtime_error);
}

TEST(ScanTime, ParsesNames) {
  EXPECT_NEAR(scanTimeFromName("lidar-clouds/1710255656.047839539.pcd"), 1710255656.047839539, 1e-6);
  EXPECT_TRUE(std::isnan(scanTimeFromName("lidar-clouds/readme.txt")));
  EXPECT_TRUE(std::isnan(scanTimeFromName("lidar-clouds/abc.pcd")));
  EXPECT_NEAR(scanTimeFromName("12.5.pcd"), 12.5, 1e-12);
}

// ------------------------------------------------------------------------ PCD ---------

TEST(SpiresPcd, ReadsTheRealRecordLayoutWithPaddingFields) {
  // FIELDS x y z _ intensity _ timestamp ring _ ; SIZE 4 4 4 1 4 1 8 2 1 ; COUNT 1 1 1 4 1 4 1 1 14 : 48 bytes per record
  std::string h = "# .PCD v0.7\nVERSION 0.7\nFIELDS x y z _ intensity _ timestamp ring _\nSIZE 4 4 4 1 4 1 8 2 1\nTYPE F F F U F U F U U\n"
                  "COUNT 1 1 1 4 1 4 1 1 14\nWIDTH 2\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS 2\nDATA binary\n";
  std::vector<std::uint8_t> b(h.begin(), h.end());
  for (int i = 0; i < 2; ++i) {
    std::vector<std::uint8_t> r(48, 0);
    const float xyz[3] = {1.0f + i, 2.0f, 3.0f}; std::memcpy(r.data(), xyz, 12);
    const float inten = 9.0f; std::memcpy(r.data() + 16, &inten, 4);
    const double ts = 1710255656.5 + i; std::memcpy(r.data() + 24, &ts, 8);
    const std::uint16_t ring = 5; std::memcpy(r.data() + 32, &ring, 2);
    b.insert(b.end(), r.begin(), r.end());
  }
  b.insert(b.end(), 3856, 0);
  SpiresPcd s = parsePcdBuffer(b, "real");
  ASSERT_EQ(s.xyz.size(), 6u);
  EXPECT_FLOAT_EQ(s.xyz[3], 2.0f);
  ASSERT_EQ(s.ts.size(), 2u);
  EXPECT_DOUBLE_EQ(s.ts[1], 1710255657.5);
}

TEST(SpiresPcd, ParsesTimestampAndDropsNonFinite) {
  auto b = spiresPcdBytes({{1, 2, 3, 100.25}, {std::nan(""), 0, 0, 1}, {4, 5, 6, 100.5}});
  SpiresPcd s = parsePcdBuffer(b, "x");
  EXPECT_EQ(s.points, 3u);
  EXPECT_EQ(s.dropped_nonfinite, 1u);
  ASSERT_EQ(s.xyz.size(), 6u);
  ASSERT_EQ(s.ts.size(), 2u);
  EXPECT_FLOAT_EQ(s.xyz[3], 4.0f);
  EXPECT_DOUBLE_EQ(s.ts[1], 100.5);
  auto nb = spiresPcdBytes({{1, 2, 3, 0}}, false);
  SpiresPcd n = parsePcdBuffer(nb, "y");
  EXPECT_TRUE(n.ts.empty());
  EXPECT_EQ(n.xyz.size(), 3u);
  auto padded = b; padded.insert(padded.end(), 3856, 0);
  EXPECT_EQ(parsePcdBuffer(padded, "z1").xyz.size(), 6u);                 // a block of zero bytes after the last record is accepted
  auto junk = b; junk.push_back(7);
  EXPECT_THROW(parsePcdBuffer(junk, "z2"), std::runtime_error);        // other bytes after the records: layout not understood
  auto longtail = b; longtail.insert(longtail.end(), 70000, 0);
  EXPECT_THROW(parsePcdBuffer(longtail, "z3"), std::runtime_error);    // an implausibly long tail
  b.resize(b.size() - 5);
  EXPECT_THROW(parsePcdBuffer(b, "z"), std::runtime_error);
}

// ------------------------------------------------------------------------ poses -------

TEST(PoseTrack3, InterpolatesAndRefusesHolesAndExtrapolation) {
  PoseTrack3 tr;
  Pose3 a, b, c;
  a.t = {0, 0, 0}; b.t = {2, 0, 0}; c.t = {2, 5, 0};
  b.q = Eigen::Quaterniond(Eigen::AngleAxisd(M_PI / 2, Eigen::Vector3d::UnitZ()));
  c.q = b.q;
  tr.add(10.0, a); tr.add(11.0, b); tr.add(11.5, c);
  Pose3 o;
  ASSERT_TRUE(tr.at(10.5, o, 0.25 + 1.0));
  EXPECT_NEAR(o.t.x(), 1.0, 1e-12);
  EXPECT_NEAR(Eigen::AngleAxisd(o.q).angle(), M_PI / 4, 1e-9);
  EXPECT_TRUE(tr.at(10.0, o, 1.0));                     // exactly the first sample
  EXPECT_TRUE(tr.at(11.5, o, 1.0));                     // exactly the last sample
  EXPECT_FALSE(tr.at(9.999, o, 1.0));                   // before the track
  EXPECT_FALSE(tr.at(11.5001, o, 1.0));                 // after the track
  EXPECT_FALSE(tr.at(10.5, o, 0.9));                    // bracket wider than max_gap
  EXPECT_TRUE(tr.at(11.25, o, 0.6));
  EXPECT_THROW(tr.add(11.5, c), std::runtime_error);    // not strictly increasing
  EXPECT_THROW(tr.add(12.0, [] { Pose3 p; p.t = {std::nan(""), 0, 0}; return p; }()), std::runtime_error);
}

TEST(PoseTrack3, ReadsTumAndRejectsMalformed) {
  { std::ofstream o(tmpPath("gt.txt")); o << "# c\n1.0 0 0 0 0 0 0 1\n2.0 1 0 0 0 0 0.7071068 0.7071068\n"; }
  PoseTrack3 tr = readTumTrack3(tmpPath("gt.txt"));
  EXPECT_EQ(tr.size(), 2u);
  EXPECT_NEAR(tr.samples()[1].pose.q.norm(), 1.0, 1e-12);
  { std::ofstream o(tmpPath("gt2.txt")); o << "1.0 0 0 0 0 0 1\n2.0 1 0 0 0 0 0 1\n"; }
  EXPECT_THROW(readTumTrack3(tmpPath("gt2.txt")), std::runtime_error);
  { std::ofstream o(tmpPath("gt3.txt")); o << "1.0 0 0 0 0 0 0 1\n"; }
  EXPECT_THROW(readTumTrack3(tmpPath("gt3.txt")), std::runtime_error);
  { std::ofstream o(tmpPath("gt4.txt")); o << "1.0 0 0 0 0 0 0 0\n2.0 0 0 0 0 0 0 1\n"; }
  EXPECT_THROW(readTumTrack3(tmpPath("gt4.txt")), std::runtime_error);   // zero quaternion
}

// ------------------------------------------------------------ scan conversion ---------

namespace {
// Sensor base moves along +x at 1 m/s from t = 100, identity orientation. Lidar frame point of world point W at time t.
PoseTrack3 movingTrack(double speed = 1.0) {
  PoseTrack3 tr;
  for (int i = 0; i <= 40; ++i) { Pose3 p; p.t = {speed * i * 0.25, 0, 0}; tr.add(100.0 + i * 0.25, p); }
  return tr;
}
Eigen::Vector3d toLidar(const Eigen::Vector3d& W, double t, const SpiresExtrinsic& ex, double speed = 1.0) {
  const Eigen::Vector3d base(speed * (t - 100.0), 0, 0);
  const Eigen::Vector3d pb = W - base;                       // identity orientation
  return ex.q.conjugate() * (pb - ex.t);
}
}  // namespace

TEST(SpiresConvert, DeskewPlacesTheSamePointOnTheSameWorldPoint) {
  PoseTrack3 gt = movingTrack();
  SpiresExtrinsic ex;
  const Eigen::Vector3d W(5.0, 2.0, 1.0);
  std::vector<std::array<double, 4>> pts;
  for (double t : {101.0, 101.05, 101.1}) { const Eigen::Vector3d l = toLidar(W, t, ex); pts.push_back({l.x(), l.y(), l.z(), t}); }
  SpiresPcd s = parsePcdBuffer(spiresPcdBytes(pts), "s");
  SpiresConvertOptions co; Frame fr; SpiresConvertReport rep;
  ASSERT_TRUE(convertSpiresScan(s, 101.05, gt, ex, co, 7, fr, rep));
  ASSERT_EQ(fr.size(), 3u);
  EXPECT_EQ(fr.group, 7u);
  for (std::size_t i = 0; i < 3; ++i) EXPECT_NEAR((Eigen::Vector3d(fr.xyz[3 * i], fr.xyz[3 * i + 1], fr.xyz[3 * i + 2]) - W).norm(), 0.0, 1e-4);
  // origin: lidar position at the median point time 101.05: base (1.05, 0, 0) + ex.t
  EXPECT_NEAR((fr.origin - Eigen::Vector3d(1.05, 0, 0.124)).norm(), 0.0, 1e-9);
  EXPECT_EQ(rep.points_used, 3u);
}

TEST(SpiresConvert, TimeOffsetShiftsPointsByVelocityTimesDt) {
  PoseTrack3 gt = movingTrack();
  SpiresExtrinsic ex;
  const Eigen::Vector3d W(5.0, 2.0, 1.0);
  const Eigen::Vector3d l = toLidar(W, 101.0, ex);
  SpiresPcd s = parsePcdBuffer(spiresPcdBytes({{l.x(), l.y(), l.z(), 101.0}}), "s");
  SpiresConvertOptions co; co.dt = 0.1;
  Frame fr; SpiresConvertReport rep;
  ASSERT_TRUE(convertSpiresScan(s, 101.0, gt, ex, co, 0, fr, rep));
  EXPECT_NEAR(fr.xyz[0] - W.x(), 0.1, 1e-4);                // the sensor is 0.1 m further along: the point moves +0.1 m in x
}

TEST(SpiresConvert, RangeWindowFallbackNoPoseAndDroppedScan) {
  PoseTrack3 gt = movingTrack();
  SpiresExtrinsic ex; SpiresConvertOptions co;
  // ranges 1.0 (below r_min 1.5), 1.5 (kept: boundary), 20.0 (kept), 20.1 (dropped)
  SpiresPcd s = parsePcdBuffer(spiresPcdBytes({{1.0, 0, 0, 101}, {1.5, 0, 0, 101}, {20.0, 0, 0, 101}, {20.1, 0, 0, 101}}), "s");
  Frame fr; SpiresConvertReport rep;
  ASSERT_TRUE(convertSpiresScan(s, 101.0, gt, ex, co, 0, fr, rep));
  EXPECT_EQ(fr.size(), 2u);
  EXPECT_EQ(rep.points_out_of_range, 2u);
  // timestamps far outside the GT span for most points: fall back to the scan time
  SpiresPcd s2 = parsePcdBuffer(spiresPcdBytes({{3, 0, 0, 5000}, {3, 0, 0, 5000}, {3, 0, 0, 101}}), "s2");
  SpiresConvertReport r2;
  ASSERT_TRUE(convertSpiresScan(s2, 101.0, gt, ex, co, 0, fr, r2));
  EXPECT_EQ(r2.scans_scan_time_fallback, 1u);
  EXPECT_EQ(fr.size(), 3u);
  // no timestamp field: single pose per scan, counted
  SpiresPcd s3 = parsePcdBuffer(spiresPcdBytes({{3, 0, 0, 0}}, false), "s3");
  SpiresConvertReport r3;
  ASSERT_TRUE(convertSpiresScan(s3, 101.0, gt, ex, co, 0, fr, r3));
  EXPECT_EQ(r3.scans_no_timestamp_field, 1u);
  // scan time outside the track and no usable timestamps: the scan is dropped and counted
  SpiresConvertReport r4;
  EXPECT_FALSE(convertSpiresScan(s3, 900.0, gt, ex, co, 0, fr, r4));
  EXPECT_EQ(r4.scans_dropped_no_pose, 1u);
  // an unparsable file name and no timestamps: dropped
  SpiresConvertReport r5;
  EXPECT_FALSE(convertSpiresScan(s3, std::nan(""), gt, ex, co, 0, fr, r5));
  // points whose own pose is missing are dropped one by one
  SpiresPcd s6 = parsePcdBuffer(spiresPcdBytes({{3, 0, 0, 101.0}, {3, 0, 0, 111.0}, {3, 0, 0, 101.0}}), "s6");
  SpiresConvertReport r6; SpiresConvertOptions c6 = co; c6.span_slack_s = 100.0;
  ASSERT_TRUE(convertSpiresScan(s6, 101.0, gt, ex, c6, 0, fr, r6));
  EXPECT_EQ(fr.size(), 2u);
  EXPECT_EQ(r6.points_no_pose, 1u);
}

TEST(SpiresConvert, ExtrinsicIsA180DegreeYawWithLift) {
  SpiresExtrinsic ex;
  const Eigen::Vector3d l(1, 2, 3);
  const Eigen::Vector3d b = ex.q * l + ex.t;
  EXPECT_NEAR((b - Eigen::Vector3d(-1, -2, 3.124)).norm(), 0.0, 1e-12);
}

// ---------------------------------------------------------------------- cache io ------

TEST(Cache, StreamingWriterReaderRoundtripAndLegacyReader) {
  const std::string path = tmpPath("c.bin");
  {
    CacheWriter w(path, 3);
    for (std::uint32_t g = 0; g < 4; ++g) {
      Frame f; f.group = g; f.origin = {double(g), 1, 2};
      for (int i = 0; i < int(g) * 3; ++i) { f.xyz.push_back(float(i)); f.xyz.push_back(float(g)); f.xyz.push_back(0.5f); }
      w.add(f);
    }
    w.finish();
    EXPECT_EQ(w.frames(), 4u);
  }
  CacheReader r(path);
  EXPECT_EQ(r.frames(), 4u); EXPECT_EQ(r.dim(), 3);
  Frame f; std::size_t k = 0;
  while (r.next(f)) { EXPECT_EQ(f.group, k); EXPECT_EQ(f.size(), k * 3); EXPECT_DOUBLE_EQ(f.origin.x(), double(k)); ++k; }
  EXPECT_EQ(k, 4u);
  int dim = 0;
  const std::vector<Frame> all = readCache(path, &dim);      // the phase 1 reader reads the same file
  EXPECT_EQ(all.size(), 4u); EXPECT_EQ(dim, 3);
  { std::ofstream o(tmpPath("junk.bin"), std::ios::binary); o << "nope"; }
  EXPECT_THROW(CacheReader(tmpPath("junk.bin")), std::runtime_error);
}

// ------------------------------------------------------------------- TLS area ---------

TEST(DiscUnion, MatchesBruteForce) {
  std::mt19937_64 rng(5);
  std::uniform_real_distribution<double> pu(-40, 40), qu(-80, 80);
  std::vector<Eigen::Vector2d> pos;
  for (int i = 0; i < 60; ++i) pos.emplace_back(pu(rng), pu(rng) * 0.3);
  DiscUnion area(pos, 30.0);
  int inside = 0, outside = 0;
  for (int i = 0; i < 20000; ++i) {
    const double x = qu(rng), y = qu(rng) * 0.6;
    bool bf = false;
    for (const auto& p : pos) if (std::hypot(p.x() - x, p.y() - y) <= 30.0) { bf = true; break; }
    ASSERT_EQ(area.contains(x, y), bf) << x << " " << y;
    (bf ? inside : outside)++;
  }
  EXPECT_GT(inside, 1000); EXPECT_GT(outside, 1000);
  DiscUnion one({Eigen::Vector2d(0, 0)}, 30.0);
  EXPECT_TRUE(one.contains(30.0, 0.0));            // the boundary is inside
  EXPECT_FALSE(one.contains(30.0001, 0.0));
}

TEST(TlsArea, OnlyPointsInsideTheAreaEnterTheTables) {
  // PCD with fields Intensity rgb x y z (as the Keble TLS map)
  const std::string path = tmpPath("tls.pcd");
  std::vector<std::array<float, 3>> pts = {{1, 1, 0.1f}, {1.01f, 1.01f, 0.15f}, {1.02f, 1.0f, 0.12f}, {100, 100, 0}, {29.9f, 0, 2}, {30.5f, 0, 2}};
  {
    std::ofstream o(path, std::ios::binary);
    o << "VERSION 0.7\nFIELDS Intensity rgb x y z\nSIZE 4 4 4 4 4\nTYPE F U F F F\nCOUNT 1 1 1 1 1\nWIDTH 6\nHEIGHT 1\nPOINTS 6\nDATA binary\n";
    for (const auto& p : pts) { const float v[5] = {1, 0, p[0], p[1], p[2]}; o.write(reinterpret_cast<const char*>(v), 20); }
  }
  DiscUnion area({Eigen::Vector2d(0, 0)}, 30.0);
  TlsTables t = readTlsArea(path, area, {0.2, 0.05});
  EXPECT_EQ(t.points_read, 6u); EXPECT_EQ(t.points_kept, 4u); EXPECT_EQ(t.points_discarded, 2u);
  VoxIndex v2; v2.res = 0.2;
  EXPECT_EQ(t.vox[0].countOf(v2.id({1, 1, 0.1})), 3u);          // the three nearby points share one 0.2 m voxel
  EXPECT_FLOAT_EQ(t.vox[0].find(v2.id({1, 1, 0.1}))->minz, 0.1f);
  EXPECT_EQ(t.vox[0].countOf(v2.id({100, 100, 0})), 0u);        // discarded outside the area: not in any table
  EXPECT_EQ(t.columns.countOf(columnKey(100, 100)), 0u);
  EXPECT_EQ(t.columns.countOf(columnKey(1, 1)), 3u);
  EXPECT_FLOAT_EQ(t.columns.find(columnKey(1, 1))->minz, 0.1f);
}

// ----------------------------------------------------------------------- labels -------

TEST(SpiresLabels, ClutterNeedsNoTlsNearbyAndASupportedColumn) {
  const double res = 0.2;
  VoxIndex vi; vi.res = res;
  VoxTable tls(64), cols(64), hits(64);
  auto addTls = [&](const Eigen::Vector3d& p, int n) {
    auto& s = tls.upsert(vi.id(p)); s.count += n; s.minz = float(p.z());
    auto& c = cols.upsert(columnKey(p.x(), p.y())); if (c.count == 0 || p.z() < c.minz) c.minz = float(p.z()); c.count += n;
  };
  addTls({0.1, 0.1, 0.1}, 50);       // ground voxel (lowest of its column)
  addTls({0.1, 0.1, 1.5}, 50);       // structure (1.4 m above the column minimum)
  addTls({3.1, 0.1, 0.1}, 2);        // TLS-present but not occupied (2 points)
  addTls({6.1, 0.1, 0.1}, 50);       // ground in another column: support for clutter at x ~ 6.5
  auto hit = [&](const Eigen::Vector3d& p) { hits.upsert(vi.id(p)).count = 1; };
  hit({0.1, 0.1, 1.5});              // TLS-occupied: not clutter
  hit({0.1, 0.1, 1.7});              // adjacent voxel of the structure: buffer
  hit({0.1, 0.1, 2.5});              // 1 m above: clutter (column supported by the TLS ground)
  hit({3.1, 0.1, 0.1});              // TLS-present (2 points): not clutter
  hit({3.1, 0.3, 0.1});              // next to a TLS-present voxel: buffer
  hit({6.1, 0.1, 3.0});              // supported column: clutter
  hit({40.1, 0.1, 0.1});             // column without any TLS point: unlabelled
  SpiresLabelParams lp; lp.res = res;
  SpiresLabelCounts c;
  LabelMap lab = buildSpiresLabels(tls, cols, hits, lp, &c);
  EXPECT_EQ(lab.at(vi.id({0.1, 0.1, 0.1})), kStaticGround);
  EXPECT_EQ(lab.at(vi.id({0.1, 0.1, 1.5})), kStaticStructure);
  EXPECT_EQ(lab.count(vi.id({3.1, 0.1, 0.1})), 0u);             // 2 points < 3: not occupied, not structure
  EXPECT_EQ(lab.at(vi.id({0.1, 0.1, 2.5})), kMoving);
  EXPECT_EQ(lab.at(vi.id({6.1, 0.1, 3.0})), kMoving);
  EXPECT_EQ(lab.count(vi.id({0.1, 0.1, 1.7})), 0u);
  EXPECT_EQ(lab.count(vi.id({3.1, 0.3, 0.1})), 0u);
  EXPECT_EQ(lab.count(vi.id({40.1, 0.1, 0.1})), 0u);
  EXPECT_EQ(c.clutter, 2u); EXPECT_EQ(c.structure, 1u); EXPECT_EQ(c.ground, 2u);
  EXPECT_EQ(c.hit_voxels, 7u); EXPECT_EQ(c.hit_voxels_tls_present, 2u); EXPECT_EQ(c.hit_voxels_buffer, 2u); EXPECT_EQ(c.hit_voxels_unsupported, 1u);
}

TEST(SpiresLabels, ThinningIsDeterministicAndReportsTheKeptFraction) {
  const double res = 0.2;
  VoxIndex vi; vi.res = res;
  VoxTable tls(64), cols(64), hits(64);
  for (int cx = 0; cx < 10; ++cx) for (int cy = 0; cy < 80; ++cy) { auto& c0 = cols.upsert(columnKey(cx + 0.5, cy + 0.5)); c0.count = 1; c0.minz = 0; }
  for (int i = 0; i < 4000; ++i) {
    auto& s = tls.upsert(vi.pack(i % 50, i / 50, 20)); s.count = 5; s.minz = 5.0f;     // far above the column minimum 0: structure
  }
  SpiresLabelParams lp; lp.res = res; lp.quota = 1000;
  SpiresLabelCounts a, b;
  LabelMap l1 = buildSpiresLabels(tls, cols, hits, lp, &a), l2 = buildSpiresLabels(tls, cols, hits, lp, &b);
  EXPECT_EQ(l1, l2);
  EXPECT_NEAR(a.keep_structure, 0.25, 1e-12);
  EXPECT_GT(a.structure, 800u); EXPECT_LT(a.structure, 1200u);
  EXPECT_EQ(a.structure_all, 4000u);
  EXPECT_TRUE(thinKeep(123, 1.0));
  EXPECT_FALSE(thinKeep(123, 0.0));
}

// ---------------------------------------------------------------- lean probe ----------

namespace {
struct Scene {
  std::vector<Frame> frames;
  LabelMap labels;
};
Scene randomScene(double res, std::uint64_t seed) {
  std::mt19937_64 rng(seed);
  std::uniform_real_distribution<double> box(-3, 3), yaw(0, 6.28);
  Scene sc;
  VoxIndex vi; vi.res = res;
  for (int f = 0; f < 24; ++f) {
    Frame fr; fr.group = f; fr.origin = Eigen::Vector3d(0.2 * f, 0.1 * (f % 3), 0);
    for (int i = 0; i < 400; ++i) {
      const Eigen::Vector3d p(box(rng) + fr.origin.x(), box(rng), box(rng) * 0.5);
      // rays that end on a few "wall" planes more often than not, so that voxels see hits and misses
      const Eigen::Vector3d q = (i % 3 == 0) ? Eigen::Vector3d(std::round(p.x() / 1.0) * 1.0, p.y(), p.z()) : p;
      fr.xyz.push_back(float(q.x())); fr.xyz.push_back(float(q.y())); fr.xyz.push_back(float(q.z()));
    }
    sc.frames.push_back(std::move(fr));
  }
  for (int i = 0; i < 3000; ++i) {
    const Eigen::Vector3d p(box(rng) + 2, box(rng), box(rng) * 0.5);
    sc.labels[vi.id(p)] = static_cast<std::uint8_t>(1 + i % 3);
  }
  return sc;
}
}  // namespace

TEST(LeanProbe, TodaysMissEqualsTheRealEngineCellForCell) {
  for (double res : {0.2, 0.4}) {
    for (int W : {1, 3, 5}) {
      Scene sc = randomScene(res, 11);
      // the real engine with the phase 1 recorder
      strata_core::LayeredMapParams lp; lp.layer_interval = W; lp.enable_periodicity = false;
      strata_core::Voxel3DBackend eng(res, lp);
      CellRecorder rec_real(sc.labels, 1.0);
      eng.setWindowObserver(&rec_real);
      for (const Frame& fr : sc.frames) {
        rec_real.setWindowOrigin(eng.layered().windowCount() + 1, fr.origin);
        strata_core::Observation obs;
        for (std::size_t i = 0; i < fr.size(); ++i) obs.hits.emplace_back(fr.xyz[3 * i], fr.xyz[3 * i + 1], fr.xyz[3 * i + 2]);
        eng.integrate(obs, fr.origin);
        eng.tick();
      }
      LeanVoxelProbe probe(res, sc.labels, {{W, false}, {W, true}});
      for (const Frame& fr : sc.frames) { probe.beginFrame(fr.origin); probe.integrate(fr.xyz, fr.origin); probe.endFrame(); }
      const CellRecorder& rl = *probe.channel(0).rec;
      ASSERT_EQ(rl.stats().size(), rec_real.stats().size()) << "res " << res << " W " << W;
      ASSERT_GT(rl.stats().size(), 100u);
      for (const auto& kv : rec_real.stats()) {
        auto it = rl.stats().find(kv.first);
        ASSERT_NE(it, rl.stats().end());
        EXPECT_EQ(it->second.n, kv.second.n); EXPECT_EQ(it->second.h, kv.second.h);
        EXPECT_EQ(it->second.first, kv.second.first); EXPECT_EQ(it->second.last, kv.second.last);
        EXPECT_EQ(it->second.runs.str(), kv.second.runs.str());
        EXPECT_EQ(it->second.centre_windows.size(), kv.second.centre_windows.size());
      }
      EXPECT_EQ(probe.channel(0).windows, eng.layered().windowCount());
      EXPECT_EQ(rl.inconsistent(), 0u); EXPECT_EQ(rl.missingOrigin(), 0u);
      // the refined channel never touches more windows than today's
      std::size_t n_today = 0, n_ref = 0;
      for (const auto& kv : rl.stats()) n_today += kv.second.n;
      for (const auto& kv : probe.channel(1).rec->stats()) n_ref += kv.second.n;
      EXPECT_LE(n_ref, n_today);
      EXPECT_LT(n_ref, n_today);                 // and on this scene it removes some
    }
  }
}

TEST(LeanProbe, RefinedMissDropsMissesWithinOneVoxelOfTheEndpoint) {
  const double res = 0.2;
  VoxIndex vi; vi.res = res;
  LabelMap labels;
  labels[vi.pack(19, 0, 0)] = kStaticStructure;     // next to the endpoint voxel (Chebyshev 1)
  labels[vi.pack(18, 0, 0)] = kStaticStructure;     // two voxels before the endpoint (Chebyshev 2)
  labels[vi.pack(20, 0, 0)] = kStaticStructure;     // the endpoint voxel itself
  labels[vi.pack(10, 0, 0)] = kMoving;              // far
  LeanVoxelProbe probe(res, labels, {{1, false}, {1, true}});
  std::vector<float> xyz = {4.19f, 0.05f, 0.05f};    // endpoint voxel (20, 0, 0); the march has samples inside it
  probe.beginFrame({0, 0.05, 0.05});
  probe.integrate(xyz, {0, 0.05, 0.05});
  probe.endFrame();
  const auto& t = probe.channel(0).rec->stats();
  const auto& r = probe.channel(1).rec->stats();
  // today: voxel 19 and 18 and 10 are misses; the endpoint voxel is hit (hit wins over its own march samples)
  ASSERT_TRUE(t.count(vi.pack(19, 0, 0))); EXPECT_EQ(t.at(vi.pack(19, 0, 0)).h, 0u); EXPECT_EQ(t.at(vi.pack(19, 0, 0)).n, 1u);
  ASSERT_TRUE(t.count(vi.pack(18, 0, 0))); EXPECT_EQ(t.at(vi.pack(18, 0, 0)).n, 1u);
  ASSERT_TRUE(t.count(vi.pack(20, 0, 0))); EXPECT_EQ(t.at(vi.pack(20, 0, 0)).h, 1u);
  ASSERT_TRUE(t.count(vi.pack(10, 0, 0)));
  // refined: voxel 19 is untouched, 18 stays a miss, the endpoint is still a hit
  EXPECT_FALSE(r.count(vi.pack(19, 0, 0)));
  ASSERT_TRUE(r.count(vi.pack(18, 0, 0))); EXPECT_EQ(r.at(vi.pack(18, 0, 0)).h, 0u);
  ASSERT_TRUE(r.count(vi.pack(20, 0, 0))); EXPECT_EQ(r.at(vi.pack(20, 0, 0)).h, 1u);
  ASSERT_TRUE(r.count(vi.pack(10, 0, 0)));
  // diagnostics: structure miss events on 19, 18 and 20 (endpoint voxel samples); those within one voxel of the endpoint are 19 and 20
  const auto& c = probe.counters();
  EXPECT_EQ(c.structure_miss_events, 3u);
  EXPECT_EQ(c.structure_miss_events_graze, 2u);
  EXPECT_EQ(probe.channel(0).structure_miss_windows, 2u);                // 19 and 18 are miss windows; 20 is a hit window
  EXPECT_EQ(probe.channel(0).structure_miss_windows_with_graze, 1u);     // only 19
}

TEST(LeanProbe, PartialLastWindowIsNotClosedAndOriginsAreRecorded) {
  const double res = 0.2;
  VoxIndex vi; vi.res = res;
  LabelMap labels; labels[vi.pack(20, 0, 0)] = kStaticStructure;
  LeanVoxelProbe probe(res, labels, {{2, false}});
  std::vector<float> xyz = {4.19f, 0.05f, 0.05f};
  for (int f = 0; f < 3; ++f) { probe.beginFrame({double(f), 0.05, 0.05}); probe.integrate(xyz, {0.0, 0.05, 0.05}); probe.endFrame(); }
  EXPECT_EQ(probe.channel(0).windows, 1);                      // frames 0,1 closed; frame 2 is a partial window
  EXPECT_EQ(probe.channel(0).rec->stats().at(vi.pack(20, 0, 0)).n, 1u);
  EXPECT_EQ(probe.channel(0).rec->missingOrigin(), 0u);
}

// --------------------------------------------------------------------- gates ----------

namespace {
std::vector<float> noisyPlane3d(double sigma, int n, std::uint64_t seed, int axis /*0: x=const (vertical), 2: z=const (floor)*/) {
  std::mt19937_64 rng(seed);
  std::normal_distribution<double> g(0, sigma);
  std::uniform_real_distribution<double> u(0, 6);
  std::vector<float> v;
  for (int i = 0; i < n; ++i) {
    double p[3] = {u(rng), u(rng), u(rng)};
    p[axis] = 3.0 + g(rng);
    v.push_back(float(p[0])); v.push_back(float(p[1])); v.push_back(float(p[2]));
  }
  return v;
}
}  // namespace

TEST(G1, ThicknessOfAVerticalPlaneIsSqrt12Sigma) {
  ShapeParams sp; sp.three_d = true; sp.radius_m = 2.0; sp.seeds = 3000;
  const ShapedResult r = shapedThickness(noisyPlane3d(0.03, 80000, 3, 0), sp);
  ASSERT_GT(r.accepted, 500u);
  EXPECT_NEAR(percentile(r.thickness_m, 50), std::sqrt(12.0) * 0.03, 0.01);
  const GateVerdictG1 v = verdictG1(r, 0.20, 0.40);
  EXPECT_TRUE(v.pass);
  EXPECT_FALSE(verdictG1(r, 0.05, 0.40).pass);               // median above the limit
  EXPECT_FALSE(verdictG1(r, 0.20, 0.10).pass);               // p90 above the limit
  EXPECT_FALSE(verdictG1(r, 0.20, 0.40, r.accepted + 1).pass);   // too few patches
}

TEST(G1, FilterRejectsFloorsBlobsAndSmearedWalls) {
  ShapeParams sp; sp.three_d = true; sp.radius_m = 2.0; sp.seeds = 3000;
  EXPECT_EQ(shapedThickness(noisyPlane3d(0.03, 80000, 3, 2), sp).accepted, 0u);       // horizontal floor: not vertical
  std::mt19937_64 rng(2); std::normal_distribution<double> g(0, 1.0);
  std::vector<float> blob;
  for (int i = 0; i < 60000; ++i) for (int k = 0; k < 3; ++k) blob.push_back(float(g(rng)));
  EXPECT_EQ(shapedThickness(blob, sp).accepted, 0u);                                    // isotropic: not flat
  // a smeared wall (sigma 0.2, thickness 0.69 m) exceeds the shape cap and is not accepted at all
  const ShapedResult smear = shapedThickness(noisyPlane3d(0.2, 80000, 3, 0), sp);
  EXPECT_LT(smear.accepted, 500u);
  std::vector<float> line;                                                              // a line is not a plane
  for (int i = 0; i < 20000; ++i) { line.push_back(float(i * 0.0005)); line.push_back(float(g(rng) * 0.01)); line.push_back(float(g(rng) * 0.01)); }
  EXPECT_EQ(shapedThickness(line, sp).accepted, 0u);
  EXPECT_EQ(shapedThickness({}, sp).seeds, 0u);
}

TEST(G1, TwoDimensionalLine) {
  std::mt19937_64 rng(4); std::normal_distribution<double> g(0, 0.02); std::uniform_real_distribution<double> u(0, 8);
  std::vector<float> v;
  for (int i = 0; i < 20000; ++i) { v.push_back(float(u(rng))); v.push_back(float(3.0 + g(rng))); v.push_back(0.0f); }
  ShapeParams sp; sp.three_d = false; sp.radius_m = 1.0; sp.seeds = 2000;
  const ShapedResult r = shapedThickness(v, sp);
  ASSERT_GT(r.accepted, 500u);
  EXPECT_NEAR(percentile(r.thickness_m, 50), std::sqrt(12.0) * 0.02, 0.01);
  EXPECT_TRUE(verdictG1(r, 0.10, 0.20).pass);
  // a short stub (extent < 1 m) is rejected: points only within 0.4 m
  std::vector<float> stub;
  for (int i = 0; i < 4000; ++i) { stub.push_back(float(u(rng) * 0.05)); stub.push_back(float(g(rng))); stub.push_back(0.0f); }
  EXPECT_EQ(shapedThickness(stub, sp).accepted, 0u);
}

TEST(G2, MatchedShareAndDistancesFollowPoseError) {
  // 2D: a wall seen from 80 steps; later steps carry a pose error of `shift` in y
  auto build = [](double shift) {
    std::vector<std::vector<float>> steps(80);
    std::mt19937_64 rng(9); std::normal_distribution<double> g(0, 0.005); std::uniform_real_distribution<double> u(0, 10);
    for (int s = 0; s < 80; ++s) {
      const double err = (s >= 40) ? shift : 0.0;
      for (int i = 0; i < 400; ++i) { steps[s].push_back(float(u(rng))); steps[s].push_back(float(2.0 + err + g(rng))); steps[s].push_back(0.0f); }
    }
    return steps;
  };
  // query step 0 only; its references (lags 45 to 60) all lie in the shifted half
  ConsistencyParams cp; cp.three_d = false; cp.query_every = 40; cp.lag_lo = 45; cp.lag_hi = 60;
  const ConsistencyResult good = poseConsistency(build(0.0), cp);
  EXPECT_EQ(good.queries, 1u);
  EXPECT_GT(good.matchedShare(), 0.99);
  EXPECT_LT(percentile(good.d_m, 50), 0.01);
  EXPECT_TRUE(verdictG2(good, 0.05, 0.10).pass);
  const ConsistencyResult bad = poseConsistency(build(0.2), cp);
  EXPECT_GT(percentile(bad.d_m, 90), 0.1);
  EXPECT_FALSE(verdictG2(bad, 0.05, 0.10).pass);
  // an offset beyond the cap is "unmatched", not a distance: the share falls and the gate has no evidence
  const ConsistencyResult far = poseConsistency(build(1.5), cp);
  EXPECT_LT(far.matchedShare(), 0.9);
  EXPECT_FALSE(verdictG2(far, 0.05, 0.10).pass);
  // lags: with a lag range that excludes every other step there is no reference and no query is counted
  ConsistencyParams none = cp; none.lag_lo = 200; none.lag_hi = 300;
  EXPECT_EQ(poseConsistency(build(0.0), none).queries, 0u);
  EXPECT_FALSE(verdictG2(poseConsistency(build(0.0), none), 0.05, 0.10).pass);
}

TEST(G2, FrameHelperAppliesTheStrideAndTheGroup) {
  std::vector<std::vector<float>> steps;
  Frame f; f.group = 3;
  for (int i = 0; i < 10; ++i) { f.xyz.push_back(float(i)); f.xyz.push_back(0); f.xyz.push_back(0); }
  addFrameToSteps(steps, f, 4);
  ASSERT_EQ(steps.size(), 4u);
  EXPECT_EQ(steps[3].size(), 9u);                        // returns 0, 4, 8
  EXPECT_FLOAT_EQ(steps[3][3], 4.0f);
  EXPECT_TRUE(steps[0].empty());
}

// ------------------------------------------------------------- offset selection --------

TEST(SpiresOffset, SelectionRulePrefersSmallOffsetWithinTheTieBand) {
  std::vector<OffsetScore3> v;
  for (int i = -3; i <= 3; ++i) { OffsetScore3 s; s.dt = i * 0.01; s.score = 0.5; v.push_back(s); }
  v[5].score = 0.80;                                      // dt = +0.02 is the best
  v[3].score = 0.797;                                     // dt = 0 within 0.5 % of the best: preferred
  EXPECT_EQ(selectSpiresOffset(v), 3);
  v[3].score = 0.79;                                      // below the band: the best wins
  EXPECT_EQ(selectSpiresOffset(v), 5);
  v[1].score = 0.80; v[5].score = 0.80;                   // dt = -0.02 and +0.02 tie: the first found keeps the pick
  EXPECT_EQ(std::fabs(v[selectSpiresOffset(v)].dt), 0.02);
  EXPECT_EQ(selectSpiresOffset({}), -1);
}

TEST(SpiresOffset, ScoreRecoversATimeOffsetOfTheRightSign) {
  // TLS: a wall x = 8 m (0.05 m voxels, 5 points each). The sensor moves in +x at 4 m/s and sees the wall at the lidar range
  // 8 - x_base(t_true). The recorded timestamps are late by 0.07 s (t_stamp = t_true + 0.07), so a correction dt = -0.07 is right.
  PoseTrack3 gt = movingTrack(4.0);
  SpiresExtrinsic ex; SpiresConvertOptions co; co.r_max = 20;
  VoxIndex vi; vi.res = 0.05;
  VoxTable tls(1024);
  for (int iy = -200; iy <= 200; ++iy) for (int iz = -40; iz <= 40; ++iz) tls.upsert(vi.pack(160, iy, iz)).count = 5;   // x in [8.0, 8.05)
  std::vector<SpiresPcd> scans; std::vector<double> times;
  for (int s = 0; s < 6; ++s) {
    const double t_true = 100.2 + 0.2 * s;
    std::vector<std::array<double, 4>> pts;
    for (int i = 0; i < 200; ++i) {
      const Eigen::Vector3d W(8.02, -4.0 + 0.04 * i, 0.3 + 0.003 * i);
      const Eigen::Vector3d l = toLidar(W, t_true, ex, 4.0);
      pts.push_back({l.x(), l.y(), l.z(), t_true + 0.07});
    }
    scans.push_back(parsePcdBuffer(spiresPcdBytes(pts), "s")); times.push_back(t_true + 0.07);
  }
  std::vector<OffsetScore3> all;
  for (int k = -15; k <= 15; ++k) all.push_back(scoreSpiresOffset(scans, times, gt, ex, co, k * 0.01, tls, 2, 1));
  const int pick = selectSpiresOffset(all);
  ASSERT_GE(pick, 0);
  EXPECT_NEAR(all[pick].dt, -0.07, 0.025);                 // the face-neighbour tolerance is about 0.035 s on each side at 4 m/s
  EXPECT_GT(all[pick].score, 0.99);
  EXPECT_LT(all[15].score, 0.6);                           // dt = 0 puts the wall 28 cm too far
  // alignment share: all returns are within the TLS neighbourhood at the right dt, none at +0.15 s
  VoxTable t01(1024); VoxIndex v1; v1.res = 0.1;
  for (int iy = -100; iy <= 100; ++iy) for (int iz = -20; iz <= 20; ++iz) t01.upsert(v1.pack(80, iy, iz)).count = 5;
  EXPECT_GT(alignmentShare(scans, times, gt, ex, co, -0.07, t01, 1), 0.99);
  EXPECT_LT(alignmentShare(scans, times, gt, ex, co, 0.15, t01, 1), 0.01);
}

// ------------------------------------------------------------ command line, end to end ----

namespace {
// A small synthetic sequence in the layout of the real one: a zip of PCD scans (48-byte records with padding fields, a zero tail),
// a TUM track, a binary TLS map with fields Intensity rgb x y z. The sensor moves along +x at 2 m/s; a wall at x = 20 and the ground at
// z = -1.5 are in the TLS map; a small cylinder at (10, 2) is not, and it is seen only in windows 0 and 3 (scans 0..9 and 30..39).
struct SyntheticSequence {
  std::string zip, gt, tls, dir;
  int scans{60};
  double stamp_late{0.06};
};

std::string pcdScanBytes(const std::vector<std::array<double, 4>>& pts /* lidar x y z, timestamp */) {
  std::string h = "# .PCD v0.7 - Point Cloud Data file format\nVERSION 0.7\nFIELDS x y z _ intensity _ timestamp ring _\nSIZE 4 4 4 1 4 1 8 2 1\n"
                  "TYPE F F F U F U F U U\nCOUNT 1 1 1 4 1 4 1 1 14\nWIDTH " + std::to_string(pts.size()) + "\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS " +
                  std::to_string(pts.size()) + "\nDATA binary\n";
  std::string out = h;
  for (const auto& p : pts) {
    std::string r(48, '\0');
    const float xyz[3] = {float(p[0]), float(p[1]), float(p[2])};
    std::memcpy(&r[0], xyz, 12);
    const float inten = 50.0f; std::memcpy(&r[16], &inten, 4);
    std::memcpy(&r[24], &p[3], 8);
    const std::uint16_t ring = 1; std::memcpy(&r[32], &ring, 2);
    out += r;
  }
  out += std::string(3856, '\0');
  return out;
}

SyntheticSequence makeSequence(const std::string& tag) {
  SyntheticSequence s;
  s.dir = tmpPath(tag);
  EXPECT_EQ(system(("mkdir -p " + s.dir).c_str()), 0);
  s.zip = s.dir + "/scans.zip"; s.gt = s.dir + "/gt.txt"; s.tls = s.dir + "/tls.pcd";
  const double T0 = 1710255656.0, speed = 2.0;
  SpiresExtrinsic ex;
  std::mt19937_64 rng(77);
  std::uniform_real_distribution<double> u(0, 1);
  std::vector<std::pair<std::string, std::string>> members;
  for (int k = 0; k < s.scans; ++k) {
    const double tk = T0 + 0.1 * k;
    std::vector<std::array<double, 4>> pts;
    auto add = [&](const Eigen::Vector3d& W, double t_true) {
      const Eigen::Vector3d base(speed * (t_true - T0), 0, 0);
      const Eigen::Vector3d pl = ex.q.conjugate() * (W - base - ex.t);
      pts.push_back({pl.x(), pl.y(), pl.z(), t_true + s.stamp_late});
    };
    for (int i = 0; i < 3000; ++i) {
      const double t = tk + 0.1 * i / 3000.0;
      const int kind = i % 3;
      if (kind == 0) add({20.0 + 0.004 * u(rng), -5 + 10 * u(rng), -1.0 + 3.5 * u(rng)}, t);
      else if (kind == 1) add({speed * (t - T0) + 2 + 12 * u(rng), -5 + 10 * u(rng), -1.5 + 0.004 * u(rng)}, t);
      else if ((k / 10) % 3 == 0) { const double a = 6.2832 * u(rng); add({10 + 0.3 * std::cos(a), 2 + 0.3 * std::sin(a), -1.5 + u(rng)}, t); }
    }
    char name[64]; std::snprintf(name, sizeof name, "lidar-clouds/%.9f.pcd", tk);
    members.emplace_back(name, pcdScanBytes(pts));
  }
  writeZip(s.zip, members, true);
  { std::ofstream o(s.gt); o.precision(12);
    for (double t = T0 - 0.5; t <= T0 + 0.1 * s.scans + 0.6; t += 0.04) o << std::fixed << t << " " << speed * (t - T0) << " 0 0 0 0 0 1\n"; }
  { std::ofstream o(s.tls, std::ios::binary);
    std::vector<std::array<float, 5>> pts;
    for (int iy = 0; iy < 1200; ++iy) for (int iz = 0; iz < 400; ++iz) pts.push_back({1, 0, 20.0f + 0.003f * float((iy + iz) % 3), -6.0f + 0.01f * iy, -1.0f + 0.01f * iz});
    for (int ix = 0; ix < 1200; ++ix) for (int iy = 0; iy < 600; ++iy) pts.push_back({1, 0, 0.02f * ix, -6.0f + 0.02f * iy, -1.5f + 0.001f * float((ix + iy) % 3)});
    o << "VERSION 0.7\nFIELDS Intensity rgb x y z\nSIZE 4 4 4 4 4\nTYPE F U F F F\nCOUNT 1 1 1 1 1\nWIDTH " << pts.size() << "\nHEIGHT 1\nPOINTS "
      << pts.size() << "\nDATA binary\n";
    for (const auto& p : pts) o.write(reinterpret_cast<const char*>(p.data()), 20); }
  return s;
}

std::map<std::string, std::string> readMetaFile(const std::string& path) {
  std::map<std::string, std::string> m; std::ifstream f(path); std::string line;
  while (std::getline(f, line)) { auto e = line.find('='); if (e != std::string::npos) m[line.substr(0, e)] = line.substr(e + 1); }
  return m;
}
int sh(const std::string& cmd) { return system(cmd.c_str()); }
}  // namespace

TEST(Cli, SpiresPipelineOnASyntheticSequence) {
  const SyntheticSequence s = makeSequence("cli_seq");
  const std::string exe = P1B_PROBE_EXE, d = s.dir;
  ASSERT_EQ(sh(exe + " spires-tls --pcd " + s.tls + " --gt " + s.gt + " --radius 30 --out-dir " + d + " --meta " + d + "/tls.meta 2>/dev/null"), 0);
  auto tls = readMetaFile(d + "/tls.meta");
  EXPECT_EQ(tls["tls_points_read"], "1200000");
  EXPECT_EQ(std::stoul(tls["tls_points_kept"]) + std::stoul(tls["tls_points_discarded_outside_area"]), 1200000ul);
  EXPECT_GT(std::stoul(tls["tls_points_kept"]), 0ul);
  // the offset search: the stamps are 0.06 s late, so the right correction is -0.06 s
  ASSERT_EQ(sh(exe + " spires-offset --zip " + s.zip + " --gt " + s.gt + " --tls-dir " + d + " --stride 4 --csv " + d + "/off.csv --meta " + d + "/off.meta 2>/dev/null"), 0);
  auto off = readMetaFile(d + "/off.meta");
  EXPECT_EQ(off["valid"], "1"); EXPECT_EQ(off["alignment_ok"], "1");
  const double dt = std::stod(off["selected_dt_s"]);
  EXPECT_LT(dt, -0.01);                 // the plateau of the score (voxel and face neighbours) is [-0.085, -0.0125] s; the rule takes its smallest |dt|
  EXPECT_GT(dt, -0.09);
  EXPECT_GT(std::stod(off["selected_score"]), std::stod(off["score_at_dt0"]));
  ASSERT_EQ(sh(exe + " spires-convert --zip " + s.zip + " --gt " + s.gt + " --dt " + off["selected_dt_s"] + " --out " + d + "/cache.bin --meta " + d + "/conv.meta 2>/dev/null"), 0);
  auto conv = readMetaFile(d + "/conv.meta");
  EXPECT_EQ(conv["scans_in_zip"], "60"); EXPECT_EQ(conv["scans_written"], "60");
  EXPECT_EQ(conv["scans_scan_time_fallback"], "0");
  ASSERT_EQ(sh(exe + " spires-labels --tls-dir " + d + " --cache " + d + "/cache.bin --res 0.2 --out " + d + "/lab.bin --meta " + d + "/lab.meta 2>/dev/null"), 0);
  auto lab = readMetaFile(d + "/lab.meta");
  EXPECT_GT(std::stoul(lab["structure_kept"]), 100ul);
  EXPECT_GT(std::stoul(lab["ground_kept"]), 100ul);
  EXPECT_GT(std::stoul(lab["clutter_kept"]), 5ul);
  ASSERT_EQ(sh(exe + " spires-measure --cache " + d + "/cache.bin --labels " + d + "/lab.bin --res 0.2 --windows 10,20 --out-prefix " + d + "/m 2>/dev/null"), 0);
  EXPECT_EQ(readMetaFile(d + "/m_W10_today.meta")["windows_closed"], "6");
  EXPECT_EQ(readMetaFile(d + "/m_W20_refined.meta")["windows_closed"], "3");
  // cell tables: structure (label 3) is hit in almost every window, clutter (label 1) only in the windows that contain the cylinder
  auto meanRate = [&](const std::string& csv, int label, int nmin, int* cells) {
    std::ifstream f(csv); std::string line; std::getline(f, line);
    double sum = 0; int n = 0;
    while (std::getline(f, line)) {
      std::stringstream ss(line); std::string tok; std::vector<std::string> v;
      while (std::getline(ss, tok, ',')) v.push_back(tok);
      if (std::stoi(v[0]) != label) continue;
      const int N = std::stoi(v[4]), H = std::stoi(v[5]);
      if (N < nmin) continue;
      sum += double(H) / N; ++n;
    }
    *cells = n; return n ? sum / n : -1.0;
  };
  int ns = 0, nc = 0;
  const double rs = meanRate(d + "/m_W10_today.csv", 3, 5, &ns), rc = meanRate(d + "/m_W10_today.csv", 1, 3, &nc);
  EXPECT_GT(ns, 50);
  EXPECT_GT(nc, 3);
  EXPECT_GT(rs, 0.85);
  EXPECT_LT(rc, 0.7);
  EXPECT_LT(rc, rs);
  int nr = 0;
  meanRate(d + "/m_W10_refined.csv", 3, 5, &nr);
  EXPECT_GT(nr, 0);
  // the gate step runs on the cache without error and writes its verdicts
  ASSERT_EQ(sh(exe + " gate-spires --cache " + d + "/cache.bin --stride 5 --meta " + d + "/gate.meta 2>/dev/null"), 0);
  auto gate = readMetaFile(d + "/gate.meta");
  EXPECT_TRUE(gate.count("g1_pass") && gate.count("g2_pass") && gate.count("gates_pass"));
}

TEST(Cli, FailuresAreReportedNotHidden) {
  const std::string exe = P1B_PROBE_EXE, d = tmpPath("cli_fail");
  ASSERT_EQ(sh("mkdir -p " + d), 0);
  EXPECT_NE(sh(exe + " spires-convert --zip " + d + "/missing.zip --gt " + d + "/missing.txt --dt 0 --out " + d + "/c.bin --meta " + d + "/c.meta 2>/dev/null"), 0);
  EXPECT_NE(sh(exe + " nonsense 2>/dev/null"), 0);
  EXPECT_NE(sh(exe + " gate2d --cache " + d + "/nofile.bin --meta " + d + "/g.meta 2>/dev/null"), 0);
  EXPECT_NE(sh(exe + " gate2d --cache " + d + "/nofile.bin --laser sideways --meta " + d + "/g.meta 2>/dev/null"), 0);
}

// ------------------------------------------------ boundary tests added after the first mutation pass ----

TEST(G1, VerdictBoundariesAreInclusive) {
  ShapedResult r;
  r.accepted = 500;
  r.thickness_m.assign(1000, 0.10);                            // median and p90 are exactly 0.10
  EXPECT_TRUE(verdictG1(r, 0.10, 0.10, 500).pass);             // exactly at all three limits: pass
  EXPECT_FALSE(verdictG1(r, 0.0999, 0.10, 500).pass);          // median above its limit
  EXPECT_FALSE(verdictG1(r, 0.10, 0.0999, 500).pass);          // p90 above its limit
  EXPECT_FALSE(verdictG1(r, 0.10, 0.10, 501).pass);            // one patch too few
  r.accepted = 499;
  EXPECT_FALSE(verdictG1(r, 0.10, 0.10, 500).pass);
}

TEST(G1, AStripThatIsAPlaneOnlyInTheNameIsRejected) {
  // a vertical strip 1.2 m wide and 12 m long: flat and vertical, extent along the second axis 1.2 m, but line-like (l2 / l1 < 0.5)
  std::mt19937_64 rng(8); std::normal_distribution<double> g(0, 0.02); std::uniform_real_distribution<double> u(0, 1);
  std::vector<float> strip, square;
  for (int i = 0; i < 60000; ++i) {
    strip.push_back(float(3.0 + g(rng))); strip.push_back(float(12 * u(rng))); strip.push_back(float(1.2 * u(rng)));
    square.push_back(float(3.0 + g(rng))); square.push_back(float(6 * u(rng))); square.push_back(float(6 * u(rng)));
  }
  ShapeParams sp; sp.three_d = true; sp.radius_m = 2.0; sp.seeds = 2000;
  EXPECT_EQ(shapedThickness(strip, sp).accepted, 0u);
  EXPECT_GT(shapedThickness(square, sp).accepted, 500u);        // the same noise on a square plane is accepted
}

TEST(G2, VerdictBoundariesAreInclusive) {
  ConsistencyResult r;
  r.queries = 3; r.query_points = 10; r.matched = 5;
  r.d_m.assign(5, 0.05);                                       // median and p90 exactly 0.05
  EXPECT_TRUE(verdictG2(r, 0.05, 0.05, 0.5).pass);             // matched share exactly 0.5 and both limits met with equality
  EXPECT_FALSE(verdictG2(r, 0.05, 0.05, 0.5001).pass);
  EXPECT_FALSE(verdictG2(r, 0.0499, 0.05, 0.5).pass);
  EXPECT_FALSE(verdictG2(r, 0.05, 0.0499, 0.5).pass);
  r.queries = 0;
  EXPECT_FALSE(verdictG2(r, 0.05, 0.05, 0.5).pass);            // no query, no evidence
}

TEST(G2, LagRangeAndCapAreInclusive) {
  ConsistencyParams cp; cp.three_d = false; cp.query_every = 1000; cp.lag_lo = 15; cp.lag_hi = 60;
  // the query step is index 0 when query_every is large
  auto at0 = [&](int ref_step, double dx) {
    std::vector<std::vector<float>> s(120);
    s[0] = {0.0f, 0.0f, 0.0f};
    s[ref_step] = {float(dx), 0.0f, 0.0f};
    return s;
  };
  EXPECT_EQ(poseConsistency(at0(15, 0.01), cp).matched, 1u);    // lag exactly lag_lo is used
  EXPECT_EQ(poseConsistency(at0(14, 0.01), cp).queries, 0u);    // lag below lag_lo: no reference
  EXPECT_EQ(poseConsistency(at0(60, 0.01), cp).matched, 1u);    // lag exactly lag_hi is used
  EXPECT_EQ(poseConsistency(at0(61, 0.01), cp).queries, 0u);    // lag above lag_hi: no reference
  EXPECT_EQ(poseConsistency(at0(30, 0.5), cp).matched, 1u);     // distance exactly the cap: matched
  EXPECT_EQ(poseConsistency(at0(30, 0.5001), cp).matched, 0u);  // beyond the cap: unmatched
  // backwards: a query at step 40 and a reference 15 steps earlier
  std::vector<std::vector<float>> s(120);
  s[40] = {0.0f, 0.0f, 0.0f}; s[25] = {0.01f, 0.0f, 0.0f};
  cp.query_every = 40;
  const ConsistencyResult back = poseConsistency(s, cp);
  EXPECT_EQ(back.matched, 1u);
}

TEST(SpiresLabels, ThresholdsAreInclusive) {
  const double res = 0.2;
  VoxIndex vi; vi.res = res;
  VoxTable tls(64), cols(64), hits(64);
  auto addTls = [&](const Eigen::Vector3d& p, int n, float minz) {
    auto& s = tls.upsert(vi.id(p)); s.count += n; s.minz = minz;
    auto& c = cols.upsert(columnKey(p.x(), p.y())); if (c.count == 0 || minz < c.minz) c.minz = minz; c.count += n;
  };
  addTls({0.1, 0.1, 1.0}, 3, 1.0f);       // exactly 3 points: occupied
  addTls({3.1, 0.1, 1.0}, 2, 1.0f);       // 2 points: present, not occupied
  addTls({6.1, 0.1, 1.0}, 1, 1.0f);       // 1 point: present
  addTls({9.1, 0.1, 0.8}, 5, 0.8f);       // column minimum 0.75 below, see the second voxel of this column
  addTls({9.1, 0.1, 0.55}, 5, 0.55f);     // lowest voxel of the column
  hits.upsert(vi.id({6.1, 0.1, 1.0})).count = 1;          // a hit inside a voxel with one TLS point: TLS-present, not clutter
  SpiresLabelParams lp; lp.res = res; lp.ground_height_m = 0.25;
  SpiresLabelCounts c;
  LabelMap lab = buildSpiresLabels(tls, cols, hits, lp, &c);
  EXPECT_EQ(lab.count(vi.id({0.1, 0.1, 1.0})), 1u);       // 3 points are enough
  EXPECT_EQ(lab.count(vi.id({3.1, 0.1, 1.0})), 0u);
  EXPECT_EQ(lab.count(vi.id({6.1, 0.1, 1.0})), 0u);       // not structure and not clutter
  EXPECT_EQ(c.hit_voxels_tls_present, 1u);
  // ground rule: minz - column minimum <= ground_height; the column at x = 9.1 has minimum 0.55
  EXPECT_EQ(lab.at(vi.id({9.1, 0.1, 0.55})), kStaticGround);       // difference 0
  EXPECT_EQ(lab.at(vi.id({9.1, 0.1, 0.8})), kStaticGround);        // difference exactly 0.25: ground (inclusive)
  lp.ground_height_m = 0.2499;
  LabelMap lab2 = buildSpiresLabels(tls, cols, hits, lp, nullptr);
  EXPECT_EQ(lab2.at(vi.id({9.1, 0.1, 0.8})), kStaticStructure);    // just above: structure
}

TEST(SpiresLabels, QuotaEqualToTheCountKeepsEverything) {
  const double res = 0.2;
  VoxIndex vi; vi.res = res;
  VoxTable tls(64), cols(64), hits(64);
  for (int cx = 0; cx < 10; ++cx) for (int cy = 0; cy < 80; ++cy) { auto& c0 = cols.upsert(columnKey(cx + 0.5, cy + 0.5)); c0.count = 1; c0.minz = 0; }
  for (int i = 0; i < 800; ++i) { auto& s = tls.upsert(vi.pack(i % 50, i / 50, 20)); s.count = 5; s.minz = 5.0f; }
  SpiresLabelParams lp; lp.res = res; lp.quota = 800;
  SpiresLabelCounts c;
  buildSpiresLabels(tls, cols, hits, lp, &c);
  EXPECT_EQ(c.structure_all, 800u);
  EXPECT_EQ(c.structure, 800u);
  EXPECT_DOUBLE_EQ(c.keep_structure, 1.0);
}

TEST(SpiresOffset, TieBandIsInclusiveAndTheScoreThresholdToo) {
  std::vector<OffsetScore3> v;
  for (int i = -2; i <= 2; ++i) { OffsetScore3 s; s.dt = i * 0.01; s.score = 0.5; v.push_back(s); }
  v[4].score = 1.0;                                       // dt = +0.02 is the best
  v[2].score = 0.995;                                     // dt = 0 exactly at 0.995 times the best: inside the band
  EXPECT_EQ(selectSpiresOffset(v), 2);
  // the TLS voxel threshold: voxels with exactly min_points points count
  PoseTrack3 gt;
  for (int i = 0; i <= 40; ++i) { Pose3 p; p.t = {0.0, 0.0, 0.0}; gt.add(100.0 + i * 0.25, p); }       // a sensor that does not move
  SpiresExtrinsic ex; SpiresConvertOptions co;
  VoxIndex vi; vi.res = 0.05;
  VoxTable tls(64);
  const Eigen::Vector3d W(5.02, 0.02, 0.02);
  tls.upsert(vi.id(W)).count = 2;
  const Eigen::Vector3d l = ex.q.conjugate() * (W - ex.t);
  std::vector<SpiresPcd> scans; scans.push_back(parsePcdBuffer(spiresPcdBytes({{l.x(), l.y(), l.z(), 101.0}}), "s"));
  std::vector<double> times = {101.0};
  EXPECT_EQ(scoreSpiresOffset(scans, times, gt, ex, co, 0.0, tls, 2, 1).score, 1.0);     // count == min_points: a hit
  EXPECT_EQ(scoreSpiresOffset(scans, times, gt, ex, co, 0.0, tls, 3, 1).score, 0.0);     // count < min_points: no hit
}
