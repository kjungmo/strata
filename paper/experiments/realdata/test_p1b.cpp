// Tests of the phase 1b code on small synthetic fixtures. No real data are needed.
#include <gtest/gtest.h>
#include <zlib.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
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
