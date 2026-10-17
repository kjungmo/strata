// Unit tests of the real-data readers, labels, recorder and statistics helpers. All inputs
// are tiny synthetic files written into a temp directory; no real data is read.
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include "probe_core.hpp"
#include "realdata_io.hpp"

using namespace realdata;
using strata_core::CellId;

namespace {

std::string tmpdir() {
  static std::string d;
  if (d.empty()) {
    char tmpl[] = "/tmp/realdata_test_XXXXXX";
    d = mkdtemp(tmpl);
  }
  return d;
}
std::string path(const std::string& n) { return tmpdir() + "/" + n; }

std::string le32(std::uint32_t v) { return std::string(reinterpret_cast<const char*>(&v), 4); }
template <class T> std::string raw(T v) { return std::string(reinterpret_cast<const char*>(&v), sizeof v); }
std::string field(const std::string& name, const std::string& value) {
  return le32(static_cast<std::uint32_t>(name.size() + 1 + value.size())) + name + "=" + value;
}
std::string rec(const std::string& header, const std::string& data) { return le32(header.size()) + header + le32(data.size()) + data; }
std::string str(const std::string& s) { return le32(s.size()) + s; }

std::string laserBytes(std::uint32_t sec, std::uint32_t nsec, const std::vector<float>& ranges, float tinc = 0.001f) {
  std::string b = le32(7) + le32(sec) + le32(nsec) + str("scan_front_link");
  for (float v : {-1.0f, 1.0f, 0.5f, tinc, 0.05f, 0.1f, 30.0f}) b += raw(v);   // amin amax ainc tinc scan_time rmin rmax
  b += le32(ranges.size());
  for (float r : ranges) b += raw(r);
  b += le32(0);                                                                 // no intensities
  return b;
}
std::string tfBytes(const std::string& parent, const std::string& child, double x, double y, double qz, double qw) {
  std::string b = le32(1) + le32(1) + le32(5) + le32(0) + str(parent) + str(child);
  for (double v : {x, y, 0.0, 0.0, 0.0, qz, qw}) b += raw(v);
  return b;
}
std::string connRecord(std::uint32_t id, const std::string& topic, const std::string& type) {
  return rec(field("op", std::string(1, '\x07')) + field("conn", le32(id)) + field("topic", topic),
             field("topic", topic) + field("type", type) + field("md5sum", "x") + field("message_definition", "x"));
}
std::string msgRecord(std::uint32_t id, std::uint32_t sec, std::uint32_t nsec, const std::string& data) {
  return rec(field("op", std::string(1, '\x02')) + field("conn", le32(id)) + field("time", le32(sec) + le32(nsec)), data);
}
std::string makeBag(const std::string& chunk_payload, const std::string& compression = "none", bool add_bag_header = true) {
  std::string out = "#ROSBAG V2.0\n";
  if (add_bag_header)
    out += rec(field("op", std::string(1, '\x03')) + field("index_pos", raw<std::uint64_t>(0)) + field("conn_count", le32(2)) +
               field("chunk_count", le32(1)), std::string(40, ' '));
  out += rec(field("op", std::string(1, '\x05')) + field("compression", compression) + field("size", le32(chunk_payload.size())), chunk_payload);
  return out;
}
void writeFile(const std::string& p, const std::string& content) { std::ofstream(p, std::ios::binary) << content; }

}  // namespace

// ----------------------------------------------------------------------- PCD --------

TEST(Pcd, RoundTripWithViewpointAndIntensity) {
  const std::vector<float> xyz = {1, 2, 3, 4, 5, 6, -7, 8, 9};
  const std::vector<float> in = {0, 1, 0};
  const double vp[7] = {1.5, -2.5, 0.25, 0.5, 0.5, 0.5, 0.5};
  writePcd(path("a.pcd"), xyz, &in, vp);
  const PcdCloud c = readPcd(path("a.pcd"), true);
  EXPECT_EQ(c.xyz, xyz);
  EXPECT_EQ(c.intensity, in);
  EXPECT_TRUE(c.hdr.has_viewpoint);
  EXPECT_DOUBLE_EQ(c.hdr.t.x(), 1.5); EXPECT_DOUBLE_EQ(c.hdr.t.y(), -2.5); EXPECT_DOUBLE_EQ(c.hdr.t.z(), 0.25);
  EXPECT_DOUBLE_EQ(c.hdr.q.w(), 0.5); EXPECT_DOUBLE_EQ(c.hdr.q.z(), 0.5);   // stored order is w x y z
  EXPECT_NEAR(c.hdr.q_norm, 1.0, 1e-12);
  EXPECT_EQ(c.hdr.points, 3u);
}

TEST(Pcd, ViewpointOrderIsTranslationThenWxyz) {
  const std::vector<float> xyz = {0, 0, 0};
  const double vp[7] = {10, 20, 30, 0.1, 0.2, 0.3, 0.4};
  writePcd(path("vp.pcd"), xyz, nullptr, vp);
  const PcdCloud c = readPcd(path("vp.pcd"));
  EXPECT_DOUBLE_EQ(c.hdr.t.x(), 10); EXPECT_DOUBLE_EQ(c.hdr.t.y(), 20); EXPECT_DOUBLE_EQ(c.hdr.t.z(), 30);
  EXPECT_DOUBLE_EQ(c.hdr.q.w(), 0.1); EXPECT_DOUBLE_EQ(c.hdr.q.x(), 0.2);
  EXPECT_DOUBLE_EQ(c.hdr.q.y(), 0.3); EXPECT_DOUBLE_EQ(c.hdr.q.z(), 0.4);
  EXPECT_NEAR(c.hdr.q_norm, std::sqrt(0.01 + 0.04 + 0.09 + 0.16), 1e-12);
}

TEST(Pcd, DropsNonFinitePointsAndCountsThem) {
  const float nan = std::nanf(""), inf = INFINITY;
  const std::vector<float> xyz = {1, 1, 1, nan, 0, 0, 2, inf, 2, 3, 3, 3};
  writePcd(path("nan.pcd"), xyz, nullptr, nullptr);
  const PcdCloud c = readPcd(path("nan.pcd"));
  EXPECT_EQ(c.xyz.size(), 6u);
  EXPECT_EQ(c.dropped_nonfinite, 2u);
  EXPECT_FALSE(c.hdr.has_viewpoint);
}

TEST(Pcd, RejectsMalformedFiles) {
  writeFile(path("ascii.pcd"), "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nPOINTS 1\nDATA ascii\n1 2 3\n");
  EXPECT_THROW(readPcd(path("ascii.pcd")), std::runtime_error);
  writeFile(path("nofield.pcd"), "VERSION 0.7\nFIELDS x y\nSIZE 4 4\nTYPE F F\nCOUNT 1 1\nPOINTS 1\nDATA binary\nabcdefgh");
  EXPECT_THROW(readPcd(path("nofield.pcd")), std::runtime_error);
  const std::vector<float> xyz = {1, 2, 3, 4, 5, 6};
  writePcd(path("full.pcd"), xyz, nullptr, nullptr);
  std::ifstream f(path("full.pcd"), std::ios::binary); std::stringstream ss; ss << f.rdbuf();
  std::string whole = ss.str();
  writeFile(path("trunc.pcd"), whole.substr(0, whole.size() - 5));
  EXPECT_THROW(readPcd(path("trunc.pcd")), std::runtime_error);
  EXPECT_THROW(readPcd(path("missing.pcd")), std::runtime_error);
  writeFile(path("badvp.pcd"), "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nVIEWPOINT 1 2 3\nPOINTS 0\nDATA binary\n");
  EXPECT_THROW(readPcd(path("badvp.pcd")), std::runtime_error);
  EXPECT_THROW(readPcd(path("full.pcd"), true), std::runtime_error);   // no intensity field but requested
}

TEST(Pcd, ZeroPointsIsValid) {
  writePcd(path("empty.pcd"), {}, nullptr, nullptr);
  const PcdCloud c = readPcd(path("empty.pcd"));
  EXPECT_TRUE(c.xyz.empty());
}

TEST(Pcd, ExtraFieldsAreSkippedByRecordSize) {
  // x y z intensity + a 2-byte field: records are 18 bytes wide.
  std::string content = "VERSION 0.7\nFIELDS x y z intensity ring\nSIZE 4 4 4 4 2\nTYPE F F F F U\nCOUNT 1 1 1 1 1\nPOINTS 2\nDATA binary\n";
  for (int i = 0; i < 2; ++i) {
    content += raw<float>(i + 1.0f) + raw<float>(i + 2.0f) + raw<float>(i + 3.0f) + raw<float>(i * 10.0f) + raw<std::uint16_t>(7);
  }
  writeFile(path("ring.pcd"), content);
  const PcdCloud c = readPcd(path("ring.pcd"), true);
  ASSERT_EQ(c.xyz.size(), 6u);
  EXPECT_FLOAT_EQ(c.xyz[3], 2.0f); EXPECT_FLOAT_EQ(c.xyz[5], 4.0f);
  EXPECT_FLOAT_EQ(c.intensity[1], 10.0f);
}

TEST(PoseSanity, FlagsBadQuaternionAndJumps) {
  auto mk = [](double x, double qw, double qz) {
    PcdHeader h; h.has_viewpoint = true; h.t = Eigen::Vector3d(x, 0, 0);
    h.q = Eigen::Quaterniond(qw, 0, 0, qz); h.q_norm = std::sqrt(qw * qw + qz * qz); return h;
  };
  std::vector<PcdHeader> good = {mk(0, 1, 0), mk(1, 1, 0), mk(2, 1, 0)};
  EXPECT_TRUE(checkPoses(good).ok());
  EXPECT_DOUBLE_EQ(checkPoses(good).max_step_m, 1.0);
  std::vector<PcdHeader> jump = {mk(0, 1, 0), mk(5.0001, 1, 0)};
  EXPECT_EQ(checkPoses(jump).big_translation_step, 1u);
  std::vector<PcdHeader> edge = {mk(0, 1, 0), mk(5.0, 1, 0)};
  EXPECT_EQ(checkPoses(edge).big_translation_step, 0u);          // exactly at the limit passes
  std::vector<PcdHeader> badq = {mk(0, 1, 0), mk(1, 1.01, 0)};
  EXPECT_EQ(checkPoses(badq).bad_quaternion, 1u);
  std::vector<PcdHeader> okq = {mk(0, 1, 0), mk(1, 1.0005, 0)};
  EXPECT_EQ(checkPoses(okq).bad_quaternion, 0u);
  const double half = 11.0 * M_PI / 180.0 / 2;                   // an 11 degree turn
  std::vector<PcdHeader> turn = {mk(0, 1, 0), mk(1, std::cos(half), std::sin(half))};
  EXPECT_EQ(checkPoses(turn).big_rotation_step, 1u);
  const double half9 = 9.0 * M_PI / 180.0 / 2;
  std::vector<PcdHeader> turn9 = {mk(0, 1, 0), mk(1, std::cos(half9), std::sin(half9))};
  EXPECT_EQ(checkPoses(turn9).big_rotation_step, 0u);
  EXPECT_FALSE(checkPoses({}).ok());                             // no frames: not ok
  PcdHeader nov = mk(0, 1, 0); nov.has_viewpoint = false;
  EXPECT_EQ(checkPoses({nov}).bad_quaternion, 1u);
}

// ------------------------------------------------------------------------- cache ----

TEST(Cache, RoundTripAndRejectsBadFiles) {
  std::vector<Frame> fr(2);
  fr[0].group = 4; fr[0].origin = Eigen::Vector3d(1, 2, 3); fr[0].xyz = {1, 2, 3, 4, 5, 6};
  fr[1].group = 4; fr[1].origin = Eigen::Vector3d(-1, 0, 0.5); fr[1].xyz = {};
  writeCache(path("c.cache"), 2, fr);
  int dim = 0;
  const auto back = readCache(path("c.cache"), &dim);
  EXPECT_EQ(dim, 2);
  ASSERT_EQ(back.size(), 2u);
  EXPECT_EQ(back[0].group, 4u); EXPECT_EQ(back[0].xyz, fr[0].xyz); EXPECT_EQ(back[0].origin, fr[0].origin);
  EXPECT_TRUE(back[1].xyz.empty()); EXPECT_EQ(back[1].origin, fr[1].origin);
  std::ifstream f(path("c.cache"), std::ios::binary); std::stringstream ss; ss << f.rdbuf();
  writeFile(path("c_trunc.cache"), ss.str().substr(0, ss.str().size() - 3));
  EXPECT_THROW(readCache(path("c_trunc.cache")), std::runtime_error);
  writeFile(path("c_bad.cache"), "NOPE0000000000000000");
  EXPECT_THROW(readCache(path("c_bad.cache")), std::runtime_error);
}

// -------------------------------------------------------------------------- bag ------

TEST(Bag, ReadsMessagesAndDecodesLaserScanAndTf) {
  std::string chunk;
  chunk += connRecord(0, "/laser_scan_front/scan", "sensor_msgs/LaserScan");
  chunk += connRecord(1, "/tf", "tf2_msgs/TFMessage");
  chunk += msgRecord(0, 100, 500000000, laserBytes(100, 500000000, {1.0f, 2.0f, 0.0f, std::nanf("")}));
  chunk += msgRecord(1, 100, 600000000, tfBytes("base_link", "scan_front_link", 0.3, -0.006, 0.0096, 0.99995));
  std::string bag = makeBag(chunk);
  bag += connRecord(0, "/laser_scan_front/scan", "sensor_msgs/LaserScan");    // index section repeats connections
  writeFile(path("a.bag"), bag);
  std::vector<std::string> topics; LaserScanMsg scan; std::vector<TfEdge> tf; double t0 = 0;
  readBag(path("a.bag"), [&](const BagMessage& m) {
    topics.push_back(m.topic);
    if (m.type == "sensor_msgs/LaserScan") { scan = decodeLaserScan(m.data, m.size); t0 = m.time(); }
    if (m.topic == "/tf") tf = decodeTfMessage(m.data, m.size);
  });
  ASSERT_EQ(topics.size(), 2u);
  EXPECT_EQ(topics[0], "/laser_scan_front/scan"); EXPECT_EQ(topics[1], "/tf");
  EXPECT_DOUBLE_EQ(t0, 100.5);
  EXPECT_EQ(scan.frame_id, "scan_front_link");
  EXPECT_FLOAT_EQ(scan.angle_min, -1.0f); EXPECT_FLOAT_EQ(scan.angle_increment, 0.5f);
  EXPECT_FLOAT_EQ(scan.range_min, 0.1f); EXPECT_FLOAT_EQ(scan.range_max, 30.0f);
  EXPECT_DOUBLE_EQ(scan.stamp(), 100.5);
  ASSERT_EQ(scan.ranges.size(), 4u);
  EXPECT_FLOAT_EQ(scan.ranges[1], 2.0f); EXPECT_TRUE(std::isnan(scan.ranges[3]));
  ASSERT_EQ(tf.size(), 1u);
  EXPECT_EQ(tf[0].parent, "base_link"); EXPECT_EQ(tf[0].child, "scan_front_link");
  EXPECT_DOUBLE_EQ(tf[0].t.x(), 0.3); EXPECT_DOUBLE_EQ(tf[0].t.y(), -0.006);
  EXPECT_DOUBLE_EQ(tf[0].q.z(), 0.0096); EXPECT_DOUBLE_EQ(tf[0].q.w(), 0.99995);
}

TEST(Bag, RejectsCompressedTruncatedAndForeignFiles) {
  std::string chunk = connRecord(0, "/a", "t/T") + msgRecord(0, 1, 0, "xxxx");
  writeFile(path("bz.bag"), makeBag(chunk, "bz2"));
  EXPECT_THROW(readBag(path("bz.bag"), [](const BagMessage&) {}), std::runtime_error);
  const std::string ok = makeBag(chunk);
  writeFile(path("tr.bag"), ok.substr(0, ok.size() - 6));
  EXPECT_THROW(readBag(path("tr.bag"), [](const BagMessage&) {}), std::runtime_error);
  writeFile(path("no.bag"), "not a bag\n");
  EXPECT_THROW(readBag(path("no.bag"), [](const BagMessage&) {}), std::runtime_error);
  writeFile(path("unk.bag"), makeBag(msgRecord(9, 1, 0, "xx")));     // message on a connection never declared
  EXPECT_THROW(readBag(path("unk.bag"), [](const BagMessage&) {}), std::runtime_error);
  EXPECT_THROW(readBag(path("nonexistent.bag"), [](const BagMessage&) {}), std::runtime_error);
}

TEST(Bag, DecodersRejectTruncatedMessages) {
  const std::string l = laserBytes(1, 0, {1.0f, 2.0f});
  EXPECT_NO_THROW(decodeLaserScan(reinterpret_cast<const std::uint8_t*>(l.data()), l.size()));
  EXPECT_THROW(decodeLaserScan(reinterpret_cast<const std::uint8_t*>(l.data()), l.size() - 12), std::runtime_error);
  const std::string t = tfBytes("a", "b", 0, 0, 0, 1);
  EXPECT_THROW(decodeTfMessage(reinterpret_cast<const std::uint8_t*>(t.data()), t.size() - 1), std::runtime_error);
}

// ------------------------------------------------------------------ poses -----------

TEST(PoseTrack, InterpolatesAndRefusesOutsideAndHoles) {
  PoseTrack tr;
  tr.add(0.0, Se2{0, 0, 0}); tr.add(1.0, Se2{2, 4, 0.5}); tr.add(3.0, Se2{2, 4, 0.5});
  Se2 p;
  ASSERT_TRUE(tr.at(0.5, p, 1.5));
  EXPECT_DOUBLE_EQ(p.x, 1.0); EXPECT_DOUBLE_EQ(p.y, 2.0); EXPECT_DOUBLE_EQ(p.yaw, 0.25);
  ASSERT_TRUE(tr.at(0.0, p, 1.5)); EXPECT_DOUBLE_EQ(p.x, 0.0);          // first sample exactly
  ASSERT_TRUE(tr.at(3.0, p, 2.5)); EXPECT_DOUBLE_EQ(p.x, 2.0);          // last sample exactly
  EXPECT_FALSE(tr.at(-0.001, p, 5.0));
  EXPECT_FALSE(tr.at(3.001, p, 5.0));
  EXPECT_FALSE(tr.at(2.0, p, 1.5));                                     // inside the 2 s hole, gap 2 > 1.5
  EXPECT_TRUE(tr.at(2.0, p, 2.0));                                      // gap exactly at the limit passes
}

TEST(PoseTrack, InterpolatesYawAcrossPi) {
  PoseTrack tr;
  tr.add(0.0, Se2{0, 0, 3.1}); tr.add(1.0, Se2{0, 0, -3.1});
  Se2 p; ASSERT_TRUE(tr.at(0.5, p, 2.0));
  EXPECT_NEAR(std::fabs(p.yaw), M_PI, 1e-9);                            // the short way round, not through 0
  ASSERT_TRUE(tr.at(0.25, p, 2.0));
  EXPECT_NEAR(p.yaw, 3.1 + 0.25 * (2 * M_PI - 6.2), 1e-9);
}

TEST(PoseTrack, RejectsNonIncreasingAndNonFinite) {
  PoseTrack tr; tr.add(1.0, Se2{});
  EXPECT_THROW(tr.add(1.0, Se2{}), std::runtime_error);                 // duplicate stamp
  EXPECT_THROW(tr.add(0.5, Se2{}), std::runtime_error);                 // backward stamp
  EXPECT_THROW(tr.add(2.0, Se2{std::nan(""), 0, 0}), std::runtime_error);
  PoseTrack one; one.add(0, Se2{}); Se2 p; EXPECT_FALSE(one.at(0, p, 1));   // a single pose is not a track
}

TEST(Tum, ParsesAndRejectsBadLines) {
  writeFile(path("gt.txt"), "# comment\n1.0 1 2 3 0 0 0 1\n2.0 2 4 3 0 0 0.7071067811865476 0.7071067811865476\n");
  const PoseTrack tr = readTumTrack(path("gt.txt"));
  ASSERT_EQ(tr.size(), 2u);
  EXPECT_DOUBLE_EQ(tr.samples()[1].pose.x, 2.0);
  EXPECT_NEAR(tr.samples()[1].pose.yaw, M_PI / 2, 1e-9);
  writeFile(path("gt7.txt"), "1.0 1 2 3 0 0 0\n2.0 1 2 3 0 0 0\n");
  EXPECT_THROW(readTumTrack(path("gt7.txt")), std::runtime_error);
  writeFile(path("gt9.txt"), "1.0 1 2 3 0 0 0 1 9\n2.0 1 2 3 0 0 0 1 9\n");
  EXPECT_THROW(readTumTrack(path("gt9.txt")), std::runtime_error);
  writeFile(path("gtback.txt"), "2.0 1 2 3 0 0 0 1\n1.0 1 2 3 0 0 0 1\n");
  EXPECT_THROW(readTumTrack(path("gtback.txt")), std::runtime_error);
  writeFile(path("gtone.txt"), "1.0 1 2 3 0 0 0 1\n");
  EXPECT_THROW(readTumTrack(path("gtone.txt")), std::runtime_error);
  writeFile(path("gtnum.txt"), "1.0 a 2 3 0 0 0 1\n2.0 1 2 3 0 0 0 1\n");
  EXPECT_THROW(readTumTrack(path("gtnum.txt")), std::runtime_error);
}

TEST(Se2, ComposeAndApply) {
  const Se2 a{1, 2, M_PI / 2}, b{3, 0, 0};
  const Se2 c = a * b;
  EXPECT_NEAR(c.x, 1.0, 1e-12); EXPECT_NEAR(c.y, 5.0, 1e-12); EXPECT_NEAR(c.yaw, M_PI / 2, 1e-12);
  const Eigen::Vector2d p = a.apply(1, 0);
  EXPECT_NEAR(p.x(), 1.0, 1e-12); EXPECT_NEAR(p.y(), 3.0, 1e-12);
  EXPECT_NEAR(Se2::wrap(3 * M_PI), M_PI, 1e-12);
  EXPECT_NEAR(Se2::wrap(-M_PI), M_PI, 1e-12);                           // (-pi, pi] convention
}

TEST(Convert, PlacesHitsDeskewsAndCountsInvalidBeams) {
  // Robot drives along +x at 1 m/s from t=0 to t=10, heading 0. Laser at the base origin.
  PoseTrack gt;
  for (int i = 0; i <= 100; ++i) gt.add(i * 0.1, Se2{i * 0.1, 0, 0});
  ScanRecord front, rear;
  front.stamp = 5.0; rear.stamp = 5.0;
  // front: three beams at angles -1, -0.5, 0 rad (angle_min -1, increment 0.5), tinc 0.1 s so beam i is i*0.1 s late.
  front.msg.angle_min = -1.0f; front.msg.angle_increment = 0.5f; front.msg.time_increment = 0.1f;
  front.msg.range_min = 0.05f; front.msg.range_max = 30.0f; front.msg.ranges = {2.0f, 0.0f, 3.0f, std::nanf(""), 40.0f};
  rear = front;
  std::vector<Frame> out; ConvertReport rep;
  ConvertOptions deskew; deskew.deskew = true;
  ASSERT_TRUE(convertScanPair(gt, Se2{}, Se2{0, 0, M_PI}, front, rear, 7, deskew, out, rep));
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].group, 7u); EXPECT_EQ(out[1].group, 7u);
  EXPECT_NEAR(out[0].origin.x(), 5.0, 1e-9); EXPECT_NEAR(out[1].origin.x(), 5.0, 1e-9);
  EXPECT_EQ(rep.beams_total, 10u);
  EXPECT_EQ(rep.beams_used, 4u);                    // 2 valid beams per scan: ranges 2.0 and 3.0 (0, NaN and 40 > range_max are no-return)
  EXPECT_EQ(rep.beams_invalid, 6u);
  ASSERT_EQ(out[0].size(), 2u);
  // beam 0: angle -1 rad, range 2 at pose x=5.0: (5 + 2 cos 1, 2 sin(-1)).
  EXPECT_NEAR(out[0].xyz[0], 5.0 + 2.0 * std::cos(1.0), 1e-4); EXPECT_NEAR(out[0].xyz[1], -2.0 * std::sin(1.0), 1e-4);
  // beam 2: angle 0, range 3, 0.2 s late so the robot is at x = 5.2.
  EXPECT_NEAR(out[0].xyz[3], 5.2 + 3.0, 1e-4); EXPECT_NEAR(out[0].xyz[4], 0.0, 1e-4);
  // rear laser looks backwards: beam 2 at angle 0 in its frame points to -x.
  EXPECT_NEAR(out[1].xyz[3], 5.2 - 3.0, 1e-4);
  std::vector<Frame> out2; ConvertReport rep2;
  ConvertOptions plain; plain.deskew = false;
  ASSERT_TRUE(convertScanPair(gt, Se2{}, Se2{}, front, front, 0, plain, out2, rep2));
  EXPECT_NEAR(out2[0].xyz[3], 5.0 + 3.0, 1e-4);     // without deskew the whole scan uses the stamp pose
}

TEST(Convert, DropsScansWithoutPoseAndBeamsPastTheTrack) {
  PoseTrack gt; gt.add(0.0, Se2{}); gt.add(0.9, Se2{0.9, 0, 0}); gt.add(1.0, Se2{1, 0, 0});
  ScanRecord s; s.stamp = 0.95;
  s.msg.angle_min = 0; s.msg.angle_increment = 0.1f; s.msg.time_increment = 0.04f;
  s.msg.range_min = 0.05f; s.msg.range_max = 30.0f; s.msg.ranges = {1, 1, 1, 1};
  std::vector<Frame> out; ConvertReport rep; ConvertOptions o;
  ASSERT_TRUE(convertScanPair(gt, Se2{}, Se2{}, s, s, 0, o, out, rep));
  EXPECT_EQ(rep.beams_used, 2u * 2u);               // beams at 0.95 and 0.99 are inside the track; 1.03 and 1.07 are not
  EXPECT_EQ(rep.beams_no_pose, 2u * 2u);
  ScanRecord late = s; late.stamp = 5.0;
  std::vector<Frame> out3; ConvertReport rep3;
  EXPECT_FALSE(convertScanPair(gt, Se2{}, Se2{}, late, late, 1, o, out3, rep3));
  EXPECT_EQ(rep3.scans_dropped_no_pose, 2u);
  EXPECT_TRUE(out3.empty());
}

// ------------------------------------------------------------------ ids and labels -----

TEST(CellIds, DecodeVoxelInvertsVoxelId) {
  strata_core::LayeredMapParams lp;
  strata_core::Voxel3DBackend b(0.2, lp);
  std::mt19937 rng(5);
  std::uniform_real_distribution<double> u(-150.0, 150.0);
  for (int i = 0; i < 500; ++i) {
    const Eigen::Vector3d p(u(rng), u(rng), u(rng) / 10);
    int ix, iy, iz; decodeVoxel(b.voxelId(p), ix, iy, iz);
    EXPECT_EQ(ix, static_cast<int>(std::floor(p.x() / 0.2)));
    EXPECT_EQ(iy, static_cast<int>(std::floor(p.y() / 0.2)));
    EXPECT_EQ(iz, static_cast<int>(std::floor(p.z() / 0.2)));
  }
}
TEST(CellIds, DecodeGridInvertsGridCellId) {
  strata_core::GridMeta m; m.width = 1200; m.height = 900;
  for (int gx : {0, 1, 599, 1199}) for (int gy : {0, 1, 450, 899}) {
    int ox, oy; decodeGrid(strata_core::gridCellId(m, gx, gy), m.width, ox, oy);
    EXPECT_EQ(ox, gx); EXPECT_EQ(oy, gy);
  }
}

TEST(Labels, RoundTripIsSortedAndRejectsBadFiles) {
  LabelMap m = {{5, 1}, {3, 2}, {99, 3}};
  writeLabels(path("l.bin"), 3, m);
  int dim = 0; const LabelMap back = readLabels(path("l.bin"), &dim);
  EXPECT_EQ(dim, 3); EXPECT_EQ(back, m);
  writeFile(path("l_bad.bin"), "XXXXXXXXXXXXXXXXXXXX");
  EXPECT_THROW(readLabels(path("l_bad.bin")), std::runtime_error);
  std::ifstream f(path("l.bin"), std::ios::binary); std::stringstream ss; ss << f.rdbuf();
  writeFile(path("l_tr.bin"), ss.str().substr(0, ss.str().size() - 2));
  EXPECT_THROW(readLabels(path("l_tr.bin")), std::runtime_error);
}

TEST(KthLabels, MovingGroundAndStructureRules) {
  strata_core::LayeredMapParams lp;
  strata_core::Voxel3DBackend vox(0.2, lp);
  std::vector<float> xyz, in;
  auto add = [&](float x, float y, float z, float lab) { xyz.insert(xyz.end(), {x, y, z}); in.push_back(lab); };
  for (int i = 0; i < 20; ++i) for (int j = 0; j < 20; ++j) add(0.1f + 0.2f * i, 10.1f + 0.2f * j, 0.0f, 0);   // static ground at z = 0
  add(0.5f, 5.0f, 0.25f, 0);      // pole, lowest point: sets the column minimum 0.25
  add(0.5f, 5.0f, 0.5f, 0);       // 0.25 above the column minimum: ground at the limit 0.25 (<=)
  add(0.5f, 5.0f, 0.8125f, 0);    // 0.5625 above: structure
  add(2.1f, 2.1f, 1.0f, 1);       // a dynamic point
  add(2.1f, 2.1f, 1.05f, 0);      // static point in the same voxel: the voxel is moving
  KthLabelParams p; p.resolution = 0.2; p.ground_height_m = 0.25;
  KthLabelCounts counts;
  const LabelMap lab = buildKthLabels(xyz, in, p, &counts);
  auto L = [&](float x, float y, float z) { return lab.at(vox.voxelId(Eigen::Vector3d(x, y, z))); };
  EXPECT_EQ(L(0.1f, 10.1f, 0.0f), kStaticGround);
  EXPECT_EQ(L(2.1f, 2.1f, 1.0f), kMoving);
  EXPECT_EQ(L(0.5f, 5.0f, 0.25f), kStaticGround);
  EXPECT_EQ(L(0.5f, 5.0f, 0.5f), kStaticGround);
  EXPECT_EQ(L(0.5f, 5.0f, 0.8125f), kStaticStructure);
  EXPECT_EQ(counts.moving, 1u); EXPECT_EQ(counts.structure, 1u); EXPECT_EQ(counts.ground, 400u + 2u);
  EXPECT_EQ(counts.gt_points, 400u + 3u + 2u); EXPECT_EQ(counts.gt_dynamic, 1u);
  p.ground_height_m = 0.2499;      // just below the limit: the same voxel becomes structure
  EXPECT_EQ(buildKthLabels(xyz, in, p).at(vox.voxelId(Eigen::Vector3d(0.5, 5.0, 0.5))), kStaticStructure);
}

TEST(KthLabels, DynamicPointNeverGroundAndBadIntensityThrows) {
  std::vector<float> xyz = {1.f, 1.f, 0.f, 1.f, 1.f, 0.0f}, in = {0.f, 1.f};      // ground-level voxel with a dynamic point
  const LabelMap lab = buildKthLabels(xyz, in, KthLabelParams{});
  ASSERT_EQ(lab.size(), 1u);
  EXPECT_EQ(lab.begin()->second, kMoving);
  EXPECT_THROW(buildKthLabels({1, 1, 1}, {0.5f}, KthLabelParams{}), std::runtime_error);
  EXPECT_THROW(buildKthLabels({1, 1, 1}, {2.0f}, KthLabelParams{}), std::runtime_error);
  EXPECT_THROW(buildKthLabels({1, 1, 1, 2, 2, 2}, {0.f}, KthLabelParams{}), std::runtime_error);
  EXPECT_TRUE(buildKthLabels({}, {}, KthLabelParams{}).empty());
}

TEST(KthLabels, EgoDynamicPointsAreExcludedUnlessAnotherDynamicPointIsInTheVoxel) {
  strata_core::LayeredMapParams lp;
  strata_core::Voxel3DBackend vox(0.2, lp);
  std::vector<float> xyz, in;
  auto add = [&](float x, float y, float z, float lab) { xyz.insert(xyz.end(), {x, y, z}); in.push_back(lab); };
  add(1.0f, 0.0f, 0.5f, 1);       // dynamic, 1.0 m from the origin: ego
  add(5.0f, 0.0f, 0.5f, 0);       // static ground-level point, far away
  add(2.0f, 0.0f, 0.5f, 1);       // dynamic exactly 2.0 m away: ego at radius 2.0 (<=)
  add(2.0001f, 3.0f, 0.5f, 1);    // dynamic, 3.6 m away: a mover
  add(1.0f, 0.0f, 0.55f, 0);      // static point in the ego voxel: the voxel is still ego, not static
  add(7.0f, 0.0f, 0.5f, 1);       // mover voxel that also holds an ego point:
  add(7.0f, 0.0f, 0.52f, 1);
  const std::vector<Eigen::Vector3d> origins = {Eigen::Vector3d(0, 0, 0.5)};
  KthLabelParams p; p.resolution = 0.2; p.ego_radius_m = 2.0;
  KthLabelCounts c;
  const LabelMap lab = buildKthLabels(xyz, in, p, &c, &origins);
  auto L = [&](float x, float y, float z) { return lab.at(vox.voxelId(Eigen::Vector3d(x, y, z))); };
  EXPECT_EQ(L(1.0f, 0.0f, 0.5f), kEgo);
  EXPECT_EQ(L(2.0f, 0.0f, 0.5f), kEgo);
  EXPECT_EQ(L(2.0001f, 3.0f, 0.5f), kMoving);
  EXPECT_EQ(L(5.0f, 0.0f, 0.5f), kStaticGround);
  EXPECT_EQ(L(7.0f, 0.0f, 0.5f), kMoving);
  EXPECT_EQ(c.gt_dynamic, 5u); EXPECT_EQ(c.gt_dynamic_ego, 2u);
  EXPECT_EQ(c.ego, 2u); EXPECT_EQ(c.moving, 2u);
  p.ego_radius_m = 1.9999;        // just inside 2.0: the voxel at 2.0 m is a mover again
  EXPECT_EQ(buildKthLabels(xyz, in, p, nullptr, &origins).at(vox.voxelId(Eigen::Vector3d(2.0, 0.0, 0.5))), kMoving);
  p.ego_radius_m = 0.0;           // rule off: no origins needed, everything dynamic is a mover
  EXPECT_EQ(buildKthLabels(xyz, in, p).at(vox.voxelId(Eigen::Vector3d(1.0, 0.0, 0.5))), kMoving);
  p.ego_radius_m = 2.0;           // rule on without origins is an error
  EXPECT_THROW(buildKthLabels(xyz, in, p), std::runtime_error);
}

// --------------------------------------------------------- runs, recorder, diversity ----

TEST(Runs, KnownSequences) {
  auto enc = [](const std::vector<int>& s) { RunBuilder r; for (int x : s) r.push(x != 0); return r; };
  EXPECT_EQ(enc({}).str(), "");
  EXPECT_TRUE(enc({}).runs().empty());
  EXPECT_EQ(enc({1}).str(), "h1");
  EXPECT_EQ(enc({0}).str(), "m1");
  EXPECT_EQ(enc({0, 0, 1, 1, 1, 0}).str(), "m2h3m1");
  EXPECT_EQ(enc({1, 0, 1, 0}).str(), "h1m1h1m1");
  EXPECT_EQ(enc({1, 1, 1, 1}).str(), "h4");
  const auto r = enc({0, 0, 1, 1, 1, 0}).runs();
  ASSERT_EQ(r.size(), 3u); EXPECT_EQ(r[0], 2); EXPECT_EQ(r[1], 3); EXPECT_EQ(r[2], 1);
  EXPECT_FALSE(enc({0, 1}).firstIsHit()); EXPECT_TRUE(enc({1, 0}).firstIsHit());
  RunBuilder big; for (int i = 0; i < 70000; ++i) big.push(true);
  EXPECT_EQ(big.str(), "h65535");                    // a run saturates at 65535 and is never wrapped
}

TEST(Diversity, GreedyCentresAtTheStatedDistance) {
  std::vector<Eigen::Vector3d> c;
  auto tryAdd = [&](double x) { const Eigen::Vector3d p(x, 0, 0); const bool o = opensNewCentre(c, p, 1.0); if (o) c.push_back(p); return o; };
  EXPECT_TRUE(tryAdd(0.0));
  EXPECT_FALSE(tryAdd(0.5));
  EXPECT_FALSE(tryAdd(0.999));
  EXPECT_TRUE(tryAdd(1.0));         // exactly 1 m from the first centre counts as apart
  EXPECT_FALSE(tryAdd(1.5));
  EXPECT_TRUE(tryAdd(3.0));
  EXPECT_EQ(c.size(), 3u);
}

TEST(Recorder, CountsRunsDiversityAndIgnoresUnlabelled) {
  LabelMap labels = {{1, kMoving}, {2, kStaticStructure}};
  CellRecorder rec(labels, 1.0);
  const double xs[7] = {0, 0.0, 0.5, 0.9, 1.0, 1.25, 5.0};            // origin x of windows 1..6 is xs[w]
  for (int w = 1; w <= 6; ++w) rec.setWindowOrigin(w, Eigen::Vector3d(xs[w], 0, 0));
  // cell 1: windows 1 (hit) 2 (hit) 4 (miss) 5 (hit); cell 2: only misses; cell 9: unlabelled
  rec.onTouchedCell(1, 1, true, false); rec.onTouchedCell(1, 2, false, true); rec.onTouchedCell(1, 9, true, false);
  rec.onTouchedCell(2, 1, true, false);
  rec.onTouchedCell(4, 1, false, true); rec.onTouchedCell(4, 2, false, true);
  rec.onTouchedCell(5, 1, true, false);
  ASSERT_EQ(rec.stats().size(), 2u);
  const CellStats& s1 = rec.stats().at(1);
  EXPECT_EQ(s1.n, 4u); EXPECT_EQ(s1.h, 3u); EXPECT_EQ(s1.first, 1); EXPECT_EQ(s1.last, 5);
  EXPECT_EQ(s1.runs.str(), "h2m1h1");
  // touched windows 1, 2, 4, 5 have origins x = 0, 0.5, 1.0, 1.25: centre at x = 0; 0.5 is near;
  // 1.0 is exactly 1 m from x = 0 and opens a centre; 1.25 is 0.25 m from that centre.
  EXPECT_EQ(s1.centre_windows.size(), 2u);
  EXPECT_EQ(s1.centre_windows[0], 1u); EXPECT_EQ(s1.centre_windows[1], 4u);
  const CellStats& s2 = rec.stats().at(2);
  EXPECT_EQ(s2.n, 2u); EXPECT_EQ(s2.h, 0u); EXPECT_EQ(s2.runs.str(), "m2");
  EXPECT_EQ(rec.reports(), 7u); EXPECT_EQ(rec.labelledReports(), 6u);
  EXPECT_EQ(rec.inconsistent(), 0u);
  rec.onTouchedCell(6, 1, true, true);              // both true can never come from the engine; it is counted
  EXPECT_EQ(rec.inconsistent(), 1u);
}

TEST(Recorder, WindowWithoutOriginIsCountedNotCrashed) {
  LabelMap labels = {{1, kMoving}};
  CellRecorder rec(labels, 1.0);
  rec.onTouchedCell(3, 1, true, false);
  EXPECT_EQ(rec.missingOrigin(), 1u);
  EXPECT_EQ(rec.stats().at(1).n, 1u);
  EXPECT_THROW(rec.setWindowOrigin(0, Eigen::Vector3d::Zero()), std::runtime_error);
}

TEST(Reference, ThresholdsAtAndBeyondTheLimits) {
  ReferenceRecorder ref;
  auto feed = [&](CellId id, int touches, int hits) { for (int i = 0; i < touches; ++i) ref.onTouchedCell(i + 1, id, i < hits, i >= hits); };
  feed(1, 10, 5);    // 0.5 at the minimum touches: occupied
  feed(2, 20, 1);    // 0.05: free
  feed(3, 20, 2);    // 0.10: neither
  feed(4, 9, 9);     // too few touches
  feed(5, 10, 4);    // 0.4: neither
  feed(6, 40, 0);    // free
  feed(7, 12, 12);   // occupied
  RefParams p;
  const LabelMap l = ref.labels(p);
  EXPECT_EQ(l.at(1), kRefOccupied); EXPECT_EQ(l.at(2), kRefFree); EXPECT_EQ(l.count(3), 0u);
  EXPECT_EQ(l.count(4), 0u); EXPECT_EQ(l.count(5), 0u); EXPECT_EQ(l.at(6), kRefFree); EXPECT_EQ(l.at(7), kRefOccupied);
  EXPECT_EQ(ref.cells(), 7u);
}

TEST(Reference, FreeCellsNearOccupiedAreSetAsideAtTheMargin) {
  // 10 wide grid, row-major ids. Occupied at (5,5). Free cells at (7,5) [distance 2], (8,5) [3], (5,3) [2], (3,3) [2], (0,0).
  const int W = 10;
  ReferenceRecorder ref;
  auto feed = [&](int gx, int gy, int hits) { for (int i = 0; i < 20; ++i) ref.onTouchedCell(i + 1, static_cast<CellId>(gy) * W + gx, i < hits, i >= hits); };
  feed(5, 5, 20); feed(7, 5, 0); feed(8, 5, 0); feed(5, 3, 0); feed(3, 3, 0); feed(0, 0, 0); feed(6, 5, 20);
  RefParams p; p.margin_cells = 2;
  LabelMap l = ref.labels(p, W);
  auto id = [&](int gx, int gy) { return static_cast<CellId>(gy) * W + gx; };
  EXPECT_EQ(l.at(id(5, 5)), kRefOccupied);
  EXPECT_EQ(l.at(id(7, 5)), kRefFreeNearOccupied);      // 2 from (5,5) and 1 from (6,5)
  EXPECT_EQ(l.at(id(8, 5)), kRefFreeNearOccupied);      // 2 from (6,5)
  EXPECT_EQ(l.at(id(5, 3)), kRefFreeNearOccupied);
  EXPECT_EQ(l.at(id(3, 3)), kRefFreeNearOccupied);      // diagonal distance 2 (Chebyshev)
  EXPECT_EQ(l.at(id(0, 0)), kRefFree);
  p.margin_cells = 1;
  l = ref.labels(p, W);
  EXPECT_EQ(l.at(id(8, 5)), kRefFree);                  // 2 from (6,5): outside margin 1
  EXPECT_EQ(l.at(id(7, 5)), kRefFreeNearOccupied);      // 1 from (6,5)
  EXPECT_EQ(l.at(id(3, 3)), kRefFree);
  p.margin_cells = 0;
  EXPECT_EQ(ref.labels(p, W).at(id(7, 5)), kRefFree);   // margin 0 is the plain rule
}

// ---------------------------------------------------- real engine through the recorder ----

TEST(Engine, BlinkingVoxelSequenceMatchesTheSceneForTwoWindowLengths) {
  const bool present[6] = {true, true, false, false, true, false};
  const Eigen::Vector3d wall(5.1, 0.0, 0.0), blink(3.1, 1.1, 0.0), through(6.2, 2.2, 0.0);   // 'through' is collinear with 'blink'
  auto run = [&](int W) {
    strata_core::LayeredMapParams lp; lp.layer_interval = W;
    strata_core::Voxel3DBackend b(0.2, lp);
    LabelMap labels;
    labels[b.voxelId(blink)] = kMoving;
    labels[b.voxelId(wall)] = kStaticStructure;
    CellRecorder rec(labels, 1.0);
    b.setWindowObserver(&rec);
    int in_window = 0;
    for (int s = 0; s < 6; ++s) {
      if (in_window == 0) rec.setWindowOrigin(b.layered().windowCount() + 1, Eigen::Vector3d::Zero());
      strata_core::Observation obs;
      obs.hits.push_back(wall);
      obs.hits.push_back(present[s] ? blink : through);       // the blinker, or a ray that passes through its voxel
      b.integrate(obs, Eigen::Vector3d::Zero());
      ++in_window;
      if (b.tick()) in_window = 0;
    }
    std::map<std::string, std::string> out;
    out["blink"] = rec.stats().at(b.voxelId(blink)).runs.str();
    out["wall"] = rec.stats().at(b.voxelId(wall)).runs.str();
    out["blink_n"] = std::to_string(rec.stats().at(b.voxelId(blink)).n);
    return out;
  };
  const auto w1 = run(1);
  EXPECT_EQ(w1.at("blink"), "h2m2h1m1");       // present in scans 0, 1, 4
  EXPECT_EQ(w1.at("wall"), "h6");
  EXPECT_EQ(w1.at("blink_n"), "6");
  const auto w2 = run(2);                      // windows (0,1) (2,3) (4,5): hit wins inside a window
  EXPECT_EQ(w2.at("blink"), "h1m1h1");
  EXPECT_EQ(w2.at("wall"), "h3");
}

// ----------------------------------------------------------------------- statistics ----

TEST(Thickness, GaussianWallMatchesUniformWidthEquivalent) {
  std::mt19937_64 rng(1);
  std::normal_distribution<double> g(0.0, 1.0);
  std::uniform_real_distribution<double> u(0.0, 10.0);
  auto wall = [&](double sigma, bool three_d) {
    std::vector<float> p;
    for (int i = 0; i < 40000; ++i) {
      p.push_back(static_cast<float>(u(rng)));
      p.push_back(static_cast<float>(sigma * g(rng)));
      p.push_back(three_d ? static_cast<float>(u(rng) * 0.3) : 0.0f);
    }
    return p;
  };
  ThicknessParams prm; prm.seeds = 500;
  const ThicknessResult t2 = wallThickness(wall(0.01, false), false, prm);
  ASSERT_GT(t2.used, 100u);
  EXPECT_NEAR(percentile(t2.thickness_m, 50), std::sqrt(12.0) * 0.01, 0.1 * std::sqrt(12.0) * 0.01);
  const ThicknessResult t2b = wallThickness(wall(0.05, false), false, prm);
  ASSERT_GT(t2b.used, 100u);                                            // a smeared wall is still accepted
  EXPECT_NEAR(percentile(t2b.thickness_m, 50), std::sqrt(12.0) * 0.05, 0.1 * std::sqrt(12.0) * 0.05);
  prm.radius_m = 1.0; prm.min_points = 50; prm.seeds = 500;
  const ThicknessResult t3 = wallThickness(wall(0.02, true), true, prm);
  ASSERT_GT(t3.used, 100u);
  EXPECT_NEAR(percentile(t3.thickness_m, 50), std::sqrt(12.0) * 0.02, 0.1 * std::sqrt(12.0) * 0.02);
}

TEST(Thickness, SparseHorizontalAndEmptyInputsAreNotCounted) {
  ThicknessParams prm; prm.seeds = 200;
  EXPECT_EQ(wallThickness({}, false, prm).used, 0u);
  std::vector<float> sparse = {0, 0, 0, 5, 5, 0, 9, 9, 0};
  EXPECT_EQ(wallThickness(sparse, false, prm).used, 0u);                // fewer than min_points in every neighbourhood
  std::mt19937_64 rng(2);
  std::uniform_real_distribution<double> u(0.0, 10.0);
  std::vector<float> ground;                                            // horizontal plane z = 0
  for (int i = 0; i < 30000; ++i) { ground.push_back(u(rng)); ground.push_back(u(rng)); ground.push_back(0.0f); }
  prm.radius_m = 1.0; prm.min_points = 50; prm.seeds = 200;
  const ThicknessResult tg = wallThickness(ground, true, prm);
  EXPECT_EQ(tg.used, 0u);                                               // |n_z| = 1 > 0.25: ground is not a wall
  EXPECT_EQ(tg.seeds, 200u);
}

TEST(Percentile, KnownValues) {
  const std::vector<double> v = {5, 1, 4, 2, 3};
  EXPECT_DOUBLE_EQ(percentile(v, 0), 1.0);
  EXPECT_DOUBLE_EQ(percentile(v, 100), 5.0);
  EXPECT_DOUBLE_EQ(percentile(v, 50), 3.0);
  EXPECT_DOUBLE_EQ(percentile(v, 10), 1.4);
  EXPECT_DOUBLE_EQ(percentile(v, 90), 4.6);
  EXPECT_DOUBLE_EQ(percentile({7.0}, 33), 7.0);
  EXPECT_TRUE(std::isnan(percentile({}, 50)));
}

// ------------------------------------------------ time base, classes, offset search ------

TEST(Convert, AnchorsAndOffsetsPlaceBeamsAtTheStatedTimes) {
  PoseTrack gt;
  for (int i = 0; i <= 100; ++i) gt.add(i * 0.1, Se2{i * 0.1, 0, 0});      // 1 m/s along +x
  ScanRecord s; s.stamp = 5.0;
  s.msg.angle_min = 0.0f; s.msg.angle_increment = 0.0f; s.msg.time_increment = 0.1f;
  s.msg.range_min = 0.05f; s.msg.range_max = 30.0f; s.msg.ranges = {3.0f, 3.0f, 3.0f};
  auto run = [&](Anchor an, double dt, bool deskew, Frame& fr) {
    ConvertOptions o; o.anchor = an; o.time_offset_s = dt; o.deskew = deskew;
    ConvertReport rep;
    return convertScan(gt, Se2{}, s, 0, o, fr, rep);
  };
  Frame f;
  ASSERT_TRUE(run(Anchor::Start, 0.0, true, f));                 // run A: beam i at 5 + 0.1 i
  EXPECT_NEAR(f.xyz[0], 8.0, 1e-4); EXPECT_NEAR(f.xyz[3], 8.1, 1e-4); EXPECT_NEAR(f.xyz[6], 8.2, 1e-4);
  EXPECT_NEAR(f.origin.x(), 5.0, 1e-9);
  ASSERT_TRUE(run(Anchor::Start, -0.5, true, f));                // pose 0.5 s earlier
  EXPECT_NEAR(f.xyz[0], 7.5, 1e-4); EXPECT_NEAR(f.xyz[6], 7.7, 1e-4); EXPECT_NEAR(f.origin.x(), 4.5, 1e-9);
  ASSERT_TRUE(run(Anchor::End, 0.0, true, f));                   // the stamp marks the last beam: beam i at 5 - 0.1 (2 - i)
  EXPECT_NEAR(f.xyz[0], 7.8, 1e-4); EXPECT_NEAR(f.xyz[3], 7.9, 1e-4); EXPECT_NEAR(f.xyz[6], 8.0, 1e-4);
  EXPECT_NEAR(f.origin.x(), 4.8, 1e-6);
  ASSERT_TRUE(run(Anchor::Single, 1.0, true, f));                // one pose at 6 s for every beam
  EXPECT_NEAR(f.xyz[0], 9.0, 1e-4); EXPECT_NEAR(f.xyz[6], 9.0, 1e-4);
  ASSERT_TRUE(run(Anchor::Start, 1.0, false, f));                // deskew off is single, whatever the anchor
  EXPECT_NEAR(f.xyz[3], 9.0, 1e-4);
  ConvertOptions o; o.anchor = Anchor::Start; o.time_offset_s = 100.0;   // outside the track: scan dropped and counted
  ConvertReport rep;
  EXPECT_FALSE(convertScan(gt, Se2{}, s, 0, o, f, rep));
  EXPECT_EQ(rep.scans_dropped_no_pose, 1u); EXPECT_EQ(rep.beams_in_dropped_scans, 3u);
}

TEST(Offset, CellsHoldingFraction) {
  std::vector<std::uint64_t> keys;
  for (int i = 0; i < 5; ++i) keys.push_back(10);
  for (int i = 0; i < 3; ++i) keys.push_back(20);
  keys.push_back(30); keys.push_back(40);                        // counts 5 3 1 1, total 10
  EXPECT_EQ(cellsHoldingFraction(keys, 0.5), 1u);
  EXPECT_EQ(cellsHoldingFraction(keys, 0.8), 2u);                // 8 of 10 is enough
  EXPECT_EQ(cellsHoldingFraction(keys, 0.81), 3u);
  EXPECT_EQ(cellsHoldingFraction(keys, 0.9), 3u);
  EXPECT_EQ(cellsHoldingFraction(keys, 1.0), 4u);
  EXPECT_EQ(cellsHoldingFraction({}, 0.9), 0u);
  EXPECT_NE(cellKey2d(0.01, 0.01, 0.05), cellKey2d(0.06, 0.01, 0.05));
  EXPECT_EQ(cellKey2d(0.01, 0.01, 0.05), cellKey2d(0.04, 0.04, 0.05));
  EXPECT_NE(cellKey2d(-0.01, 0.01, 0.05), cellKey2d(0.01, 0.01, 0.05));    // floor, not truncation
}

TEST(Offset, SelectionRuleTieBreakEdgeAndDroppedBeams) {
  auto mk = [](Anchor a, double dt, double score, double dropped = 0.0) {
    OffsetScore s; s.anchor = a; s.dt = dt; s.c_front = static_cast<std::size_t>(score); s.dropped_share = dropped; return s;
  };
  // Start -0.1 and End 0.0 and Single 0.05 are within 0.5 % of the lowest score 997: Start wins on the anchor order.
  OffsetChoice c = selectOffset({mk(Anchor::End, 0.0, 1000), mk(Anchor::Start, -0.1, 1000), mk(Anchor::Single, 0.05, 997)});
  ASSERT_TRUE(c.ok);
  EXPECT_EQ(c.best.anchor, Anchor::Start); EXPECT_DOUBLE_EQ(c.best.dt, -0.1);
  EXPECT_DOUBLE_EQ(c.lowest_score, 997.0);
  // Same anchor: the smaller |dt| wins among near ties; a score 1.5 % above the lowest is not a near tie.
  c = selectOffset({mk(Anchor::Start, 0.2, 1000), mk(Anchor::Start, -0.1, 1002), mk(Anchor::Start, 0.0, 1015), mk(Anchor::Start, 0.15, 1000)});
  ASSERT_TRUE(c.ok); EXPECT_DOUBLE_EQ(c.best.dt, -0.1);
  c = selectOffset({mk(Anchor::Start, 0.0, 1010), mk(Anchor::Start, 0.1, 1000)});        // 1 % above: not a tie
  EXPECT_DOUBLE_EQ(c.best.dt, 0.1);
  // The optimum on the edge of the grid is a failure, and so are too many beams without a pose.
  c = selectOffset({mk(Anchor::Start, -0.30, 900), mk(Anchor::Start, 0.0, 1000)}, 0.30);
  EXPECT_FALSE(c.ok); EXPECT_FALSE(c.reason.empty());
  c = selectOffset({mk(Anchor::Start, 0.0, 900, 0.0101)});
  EXPECT_FALSE(c.ok);
  c = selectOffset({mk(Anchor::Start, 0.0, 900, 0.01)});
  EXPECT_TRUE(c.ok);
  EXPECT_FALSE(selectOffset({}).ok);
}

namespace {
// A 16 m x 10 m room, robot driving back and forth along x, two lasers (front looks +x, rear -x).
double raycastRoom(double ox, double oy, double ang) {
  const double dx = std::cos(ang), dy = std::sin(ang);
  double best = 1e30;
  if (dx > 1e-12) best = std::min(best, (8.0 - ox) / dx);
  if (dx < -1e-12) best = std::min(best, (-8.0 - ox) / dx);
  if (dy > 1e-12) best = std::min(best, (5.0 - oy) / dy);
  if (dy < -1e-12) best = std::min(best, (-5.0 - oy) / dy);
  return best;
}
double roomX(double t) { return 3.0 * std::sin(0.5 * t); }

struct RoomData { PoseTrack gt; std::vector<ScanRecord> front, rear; Se2 ef{0.3, 0, 0}, er{-0.3, 0, M_PI}; };

// The GT clock and the scan clock differ: beam i is measured at `true_time(stamp, i, n)` on the GT clock.
RoomData makeRoom(const std::function<double(double, std::size_t, std::size_t)>& true_time) {
  RoomData d;
  for (int i = -200; i <= 4400; ++i) d.gt.add(i * 0.005, Se2{roomX(i * 0.005), 0, 0});
  const std::size_t n = 181;
  for (int k = 0; k < 190; ++k) {
    for (int laser = 0; laser < 2; ++laser) {
      ScanRecord s; s.stamp = 1.0 + 0.1 * k;
      s.msg.angle_min = static_cast<float>(-M_PI); s.msg.angle_increment = static_cast<float>(2 * M_PI / 180.0);
      s.msg.time_increment = 0.0005f; s.msg.range_min = 0.05f; s.msg.range_max = 40.0f;
      const Se2& ext = laser == 0 ? d.ef : d.er;
      for (std::size_t i = 0; i < n; ++i) {
        const Se2 laser_pose = Se2{roomX(true_time(s.stamp, i, n)), 0, 0} * ext;
        s.msg.ranges.push_back(static_cast<float>(raycastRoom(laser_pose.x, laser_pose.y, laser_pose.yaw + s.msg.angle_min + i * static_cast<double>(s.msg.angle_increment))));
      }
      (laser == 0 ? d.front : d.rear).push_back(std::move(s));
    }
  }
  return d;
}

OffsetChoice searchRoom(const RoomData& d, std::initializer_list<Anchor> anchors, std::vector<OffsetScore>* keep = nullptr, int half_steps = 30) {
  std::vector<OffsetScore> all;
  for (Anchor a : anchors)
    for (int k = -half_steps; k <= half_steps; ++k) all.push_back(scoreOffset(d.gt, d.ef, d.er, d.front, d.rear, a, k * 0.01, 1));
  if (keep) *keep = all;
  return selectOffset(all);
}
}  // namespace

TEST(Offset, RecoversAKnownStartOffsetAndPrefersDeskewOverSinglePose) {
  const RoomData d = makeRoom([](double s, std::size_t i, std::size_t) { return s - 0.08 + i * 0.0005; });   // start-type, dt* = -0.08
  std::vector<OffsetScore> all;
  const OffsetChoice c = searchRoom(d, {Anchor::Start, Anchor::Single}, &all);
  ASSERT_TRUE(c.ok) << c.reason;
  EXPECT_EQ(c.best.anchor, Anchor::Start);
  EXPECT_NEAR(c.best.dt, -0.08, 0.015);
  double best_single = 1e30, at_zero = 0;
  for (const auto& s : all) {
    if (s.anchor == Anchor::Single) best_single = std::min(best_single, s.score());
    if (s.anchor == Anchor::Start && std::fabs(s.dt) < 1e-9) at_zero = s.score();
  }
  EXPECT_LT(c.best.score(), best_single);                       // per-beam time matters when the scan takes 0.09 s
  EXPECT_LT(c.best.score(), at_zero);                           // the dt = 0 of run A is worse than the optimum
}

TEST(Offset, EndTypeTimingIsRecoveredAsTheEquivalentStartOffset) {
  // The stamp marks the last beam and the GT clock lags by 0.03 s: beam i at s + 0.03 - (n - 1 - i) ti. That is
  // the start offset 0.03 - 180 * 0.0005 = -0.06, which the tie-break reports under the anchor Start.
  const RoomData d = makeRoom([](double s, std::size_t i, std::size_t n) { return s + 0.03 - (n - 1 - i) * 0.0005; });
  const OffsetChoice c = searchRoom(d, {Anchor::Start, Anchor::End, Anchor::Single});
  ASSERT_TRUE(c.ok) << c.reason;
  EXPECT_EQ(c.best.anchor, Anchor::Start);
  EXPECT_NEAR(c.best.dt, -0.06, 0.015);
}

TEST(Offset, EdgeOptimumFailsTheSearch) {
  // True offset -0.08 s, searched on a grid of +-0.05 s: the lowest score is on the edge of the grid. With the
  // 0.5 % tie band a flat score curve could still pick an interior near-tie (a property of the rule, PREREG
  // section 11.1), so the edge rule is checked with the band switched off.
  const RoomData d = makeRoom([](double s, std::size_t i, std::size_t) { return s - 0.08 + i * 0.0005; });
  std::vector<OffsetScore> all;
  searchRoom(d, {Anchor::Start}, &all, 5);
  const OffsetScore* lowest = &all[0];
  for (const auto& s : all) if (s.score() < lowest->score()) lowest = &s;
  EXPECT_NEAR(lowest->dt, -0.05, 1e-9);
  const OffsetChoice c = selectOffset(all, 0.05, 0.0);
  EXPECT_FALSE(c.ok);
  EXPECT_NE(c.reason.find("edge"), std::string::npos);
}

TEST(KthLabels, ClassGroupsByMajorityClassWithTieToTheSmallerCode) {
  strata_core::LayeredMapParams lp;
  strata_core::Voxel3DBackend vox(0.2, lp);
  std::vector<float> xyz, in; std::vector<std::uint16_t> cls;
  auto add = [&](float x, float y, float z, float lab, int c) { xyz.insert(xyz.end(), {x, y, z}); in.push_back(lab); cls.push_back(static_cast<std::uint16_t>(c)); };
  for (int i = 0; i < 10; ++i) for (int j = 0; j < 10; ++j) add(20.1f + 0.2f * i, 20.1f + 0.2f * j, 0.0f, 0, 40);   // ground far away
  add(0.1f, 0.1f, 0.0f, 0, 40);                                  // column minimum at the cells below
  add(0.1f, 0.1f, 1.0f, 0, 80);                                  // pole: solid
  add(2.1f, 0.1f, 0.0f, 0, 40);
  add(2.1f, 0.1f, 1.0f, 0, 10); add(2.1f, 0.1f, 1.02f, 0, 10);   // car: vehicle
  add(4.1f, 0.1f, 0.0f, 0, 40);
  add(4.1f, 0.1f, 1.0f, 0, 70); add(4.1f, 0.1f, 1.02f, 0, 70); add(4.1f, 0.1f, 1.04f, 0, 50);   // majority vegetation over one building point
  add(6.1f, 0.1f, 0.0f, 0, 40);
  add(6.1f, 0.1f, 1.0f, 0, 70); add(6.1f, 0.1f, 1.02f, 0, 50);    // tie 70 against 50: smaller code, building: solid
  add(8.1f, 0.1f, 0.0f, 0, 40);
  add(8.1f, 0.1f, 1.0f, 0, 0);                                    // unlabelled: other
  add(10.1f, 0.1f, 0.0f, 0, 40);
  add(10.1f, 0.1f, 1.0f, 1, 253);                                 // a mover: moving, no group
  add(12.1f, 0.1f, 0.0f, 0, 40);
  add(12.1f, 0.1f, 1.0f, 0, 72);                                  // terrain class above the ground band: other
  KthLabelParams p; p.resolution = 0.2;
  KthLabelCounts c;
  const LabelMap lab = buildKthLabels(xyz, in, p, &c, nullptr, &cls);
  auto L = [&](float x, float y, float z) { return lab.at(vox.voxelId(Eigen::Vector3d(x, y, z))); };
  EXPECT_EQ(L(0.1f, 0.1f, 1.0f), kStaticStructure | (kGroupSolid << 4));
  EXPECT_EQ(L(2.1f, 0.1f, 1.0f), kStaticStructure | (kGroupVehicle << 4));
  EXPECT_EQ(L(4.1f, 0.1f, 1.0f), kStaticStructure | (kGroupVegetation << 4));
  EXPECT_EQ(L(6.1f, 0.1f, 1.0f), kStaticStructure | (kGroupSolid << 4));
  EXPECT_EQ(L(8.1f, 0.1f, 1.0f), kStaticStructure | (kGroupOther << 4));
  EXPECT_EQ(L(10.1f, 0.1f, 1.0f), kMoving);
  EXPECT_EQ(L(12.1f, 0.1f, 1.0f), kStaticStructure | (kGroupOther << 4));
  EXPECT_EQ(L(0.1f, 0.1f, 0.0f), kStaticGround);
  EXPECT_EQ(labelBase(L(2.1f, 0.1f, 1.0f)), kStaticStructure); EXPECT_EQ(labelGroup(L(2.1f, 0.1f, 1.0f)), kGroupVehicle);
  EXPECT_EQ(c.structure, 6u); EXPECT_EQ(c.structure_solid, 2u); EXPECT_EQ(c.structure_vehicle, 1u);
  EXPECT_EQ(c.structure_vegetation, 1u); EXPECT_EQ(c.structure_other, 2u);
  EXPECT_EQ(c.structure_solid + c.structure_vehicle + c.structure_vegetation + c.structure_other, c.structure);
  cls.pop_back();                                                 // size mismatch is an error
  EXPECT_THROW(buildKthLabels(xyz, in, p, nullptr, nullptr, &cls), std::runtime_error);
}

TEST(Class, OfIntensityTakesTheLowerSixteenBitsAndGroupsMatchThePrereg) {
  EXPECT_EQ(classOfIntensity(40.0f), 40);
  EXPECT_EQ(classOfIntensity(static_cast<float>((0xD0u << 16) | 10u)), 10);        // instance 0xD0, class 10 (car)
  EXPECT_EQ(classOfIntensity(static_cast<float>((0x1F8u << 16) | 10u)), 10);
  EXPECT_EQ(classOfIntensity(static_cast<float>((4u << 16) | 254u)), 254);
  EXPECT_EQ(classOfIntensity(0.0f), 0);
  EXPECT_THROW(classOfIntensity(-1.0f), std::runtime_error);
  EXPECT_THROW(classOfIntensity(std::nanf("")), std::runtime_error);
  for (int c : {50, 51, 52, 80, 71}) EXPECT_EQ(groupOfClass(c), kGroupSolid) << c;
  for (int c : {10, 11, 13, 15, 16, 18, 20}) EXPECT_EQ(groupOfClass(c), kGroupVehicle) << c;
  EXPECT_EQ(groupOfClass(70), kGroupVegetation);
  for (int c : {0, 1, 40, 44, 48, 49, 60, 72, 81, 99, 252, 259}) EXPECT_EQ(groupOfClass(c), kGroupOther) << c;
}

// ------------------------------------------------------------- the command line tool ----

#ifdef HITRATE_PROBE_EXE
TEST(Cli, KthLabelsAndMeasureEndToEndOnSyntheticFrames) {
  const std::string root = path("kth_seq");
  mkdir(root.c_str(), 0755); mkdir((root + "/pcd").c_str(), 0755);
  // Sensor at the origin. A wall point at (5.1, 0) is hit in every frame; a blinker at
  // (3.1, 1.1) is present in frames 0 and 1 and replaced by a ray through its voxel in
  // frames 2 and 3. Frames carry VIEWPOINT = sensor position.
  const bool present[4] = {true, true, false, false};
  for (int f = 0; f < 4; ++f) {
    std::vector<float> xyz = {5.1f, 0.0f, 0.0f};
    if (present[f]) xyz.insert(xyz.end(), {3.1f, 1.1f, 0.0f}); else xyz.insert(xyz.end(), {6.2f, 2.2f, 0.0f});
    const double vp[7] = {0, 0, 0, 1, 0, 0, 0};
    char nm[32]; std::snprintf(nm, sizeof nm, "/pcd/%06d.pcd", 100 + f);
    writePcd(root + nm, xyz, nullptr, vp);
  }
  const std::vector<float> gt_in = {0.0f, 1.0f};                        // wall static, blinker dynamic
  writePcd(root + "/gt_cloud.pcd", {5.1f, 0.0f, 0.0f, 3.1f, 1.1f, 0.0f}, &gt_in, nullptr);
  const std::string exe = HITRATE_PROBE_EXE;
  ASSERT_EQ(std::system((exe + " kth-labels --gt " + root + "/gt_cloud.pcd --res 0.2 --out " + root + "/labels.bin --meta " + root + "/labels.meta").c_str()), 0);
  ASSERT_EQ(std::system((exe + " kth-measure --dir " + root + " --labels " + root + "/labels.bin --res 0.2 --window 1 --out " +
                         root + "/cells.csv --meta " + root + "/cells.meta").c_str()), 0);
  // Ego rule through the command line: the blinker is 3.3 m from the origin, so radius 3.5 makes it ego (label 4).
  ASSERT_EQ(std::system((exe + " kth-labels --gt " + root + "/gt_cloud.pcd --res 0.2 --ego-radius 3.5 --dir " + root + " --out " + root +
                         "/labels_ego.bin --meta " + root + "/labels_ego.meta").c_str()), 0);
  {
    const LabelMap le = readLabels(root + "/labels_ego.bin");
    int ego = 0, mov = 0, other = 0;
    for (const auto& kv : le) { if (kv.second == kEgo) ++ego; else if (kv.second == kMoving) ++mov; else ++other; }
    EXPECT_EQ(ego, 1); EXPECT_EQ(mov, 0); EXPECT_EQ(other, 1);
  }
  EXPECT_NE(std::system((exe + " kth-labels --gt " + root + "/gt_cloud.pcd --res 0.2 --ego-radius 3.5 --out " + root + "/x.bin --meta " + root + "/x.meta 2>/dev/null").c_str()), 0);
  std::ifstream f(root + "/cells.csv"); std::string line; std::vector<std::string> rows;
  while (std::getline(f, line)) rows.push_back(line);
  ASSERT_EQ(rows.size(), 3u);                                           // header and the two labelled voxels
  EXPECT_EQ(rows[0], "label,ix,iy,iz,N,H,first,last,div,runs");
  bool saw_wall = false, saw_blink = false;
  for (std::size_t i = 1; i < rows.size(); ++i) {
    if (rows[i] == "2,25,0,0,4,4,1,4,1,h4") saw_wall = true;           // wall voxel: lowest point = column minimum, so ground (label 2)
    if (rows[i] == "1,15,5,0,4,2,1,4,1,h2m2") saw_blink = true;        // moving voxel: hit, hit, miss, miss
  }
  EXPECT_TRUE(saw_wall) << rows[1] << " | " << rows[2];
  EXPECT_TRUE(saw_blink) << rows[1] << " | " << rows[2];
  std::ifstream m(root + "/cells.meta"); std::string all, l2;
  while (std::getline(m, l2)) all += l2 + "\n";
  EXPECT_NE(all.find("windows_closed=4"), std::string::npos) << all;
  EXPECT_NE(all.find("observer_inconsistent=0"), std::string::npos) << all;
  EXPECT_NE(all.find("window_origin_missing=0"), std::string::npos) << all;
}

namespace {
std::vector<std::string> readLines(const std::string& p) {
  std::ifstream f(p); std::string line; std::vector<std::string> rows;
  while (std::getline(f, line)) rows.push_back(line);
  return rows;
}
std::string readAll(const std::string& p) { std::string s; for (const auto& l : readLines(p)) s += l + "\n"; return s; }
}  // namespace

TEST(Cli, ViewpointDiversityFollowsTheWindowOriginsOfEachWindow) {
  // The sensor moves 2 m along +x between frames; a wall point far ahead is hit in every window (W = 1).
  // The cell is touched from four origins at least 1 m apart, so div = 4. If the window origin were
  // registered under the wrong window index, the count would differ.
  const std::string root = path("div_seq");
  mkdir(root.c_str(), 0755); mkdir((root + "/pcd").c_str(), 0755);
  for (int f = 0; f < 4; ++f) {
    const double vp[7] = {2.0 * f, 0, 0, 1, 0, 0, 0};
    char nm[32]; std::snprintf(nm, sizeof nm, "/pcd/%06d.pcd", 200 + f);
    writePcd(root + nm, {20.1f, 0.0f, 0.0f}, nullptr, vp);
  }
  const std::vector<float> gt_in = {0.0f};
  writePcd(root + "/gt_cloud.pcd", {20.1f, 0.0f, 0.0f}, &gt_in, nullptr);
  const std::string exe = HITRATE_PROBE_EXE;
  ASSERT_EQ(std::system((exe + " kth-labels --gt " + root + "/gt_cloud.pcd --res 0.2 --out " + root + "/l.bin --meta " + root + "/l.meta").c_str()), 0);
  ASSERT_EQ(std::system((exe + " kth-measure --dir " + root + " --labels " + root + "/l.bin --res 0.2 --window 1 --out " + root + "/c.csv --meta " + root + "/c.meta").c_str()), 0);
  const auto rows = readLines(root + "/c.csv");
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[1], "2,100,0,0,4,4,1,4,4,h4");                  // label, ix, iy, iz, N, H, first, last, div = 4, runs
  EXPECT_NE(readAll(root + "/c.meta").find("window_origin_missing=0"), std::string::npos);
  // Window length 2: two windows, origins of the first steps of each (0 m and 4 m): div = 2.
  ASSERT_EQ(std::system((exe + " kth-measure --dir " + root + " --labels " + root + "/l.bin --res 0.2 --window 2 --out " + root + "/c2.csv --meta " + root + "/c2.meta").c_str()), 0);
  EXPECT_EQ(readLines(root + "/c2.csv").at(1), "2,100,0,0,2,2,1,2,2,h2");
}

TEST(Cli, KthLabelsWithClassesReadsTheFrameIntensityAndChecksTheConcatenation) {
  const std::string root = path("cls_seq");
  mkdir(root.c_str(), 0755); mkdir((root + "/pcd").c_str(), 0755);
  // Two frames. Frame 0: ground (class 40), a building point (class 50 with instance bits) above it, a car point.
  // Frame 1: ground, a vegetation point, a mover (class 253) point.
  struct P { float x, y, z, in; };
  const float k_building = static_cast<float>((7u << 16) | 50u), k_car = static_cast<float>((0xD0u << 16) | 10u);
  const std::vector<std::vector<P>> frames = {
      {{1.1f, 0.1f, 0.0f, 40.0f}, {1.1f, 0.1f, 1.0f, k_building}, {3.1f, 0.1f, 0.0f, 40.0f}, {3.1f, 0.1f, 1.0f, k_car}},
      {{5.1f, 0.1f, 0.0f, 40.0f}, {5.1f, 0.1f, 1.0f, 70.0f}, {7.1f, 0.1f, 0.0f, 40.0f}, {7.1f, 0.1f, 1.0f, 253.0f}}};
  std::vector<float> gt_xyz, gt_in;
  for (std::size_t f = 0; f < frames.size(); ++f) {
    std::vector<float> xyz, in;
    for (const P& p : frames[f]) {
      xyz.insert(xyz.end(), {p.x, p.y, p.z}); in.push_back(p.in);
      gt_xyz.insert(gt_xyz.end(), {p.x, p.y, p.z}); gt_in.push_back(p.in == 253.0f ? 1.0f : 0.0f);
    }
    const double vp[7] = {0, 0, 0, 1, 0, 0, 0};
    char nm[32]; std::snprintf(nm, sizeof nm, "/pcd/%06d.pcd", 300 + static_cast<int>(f));
    writePcd(root + nm, xyz, &in, vp);
  }
  writePcd(root + "/gt_cloud.pcd", gt_xyz, &gt_in, nullptr);
  const std::string exe = HITRATE_PROBE_EXE;
  ASSERT_EQ(std::system((exe + " kth-labels --gt " + root + "/gt_cloud.pcd --dir " + root + " --classes 1 --res 0.2 --out " + root + "/l.bin --meta " + root + "/l.meta").c_str()), 0);
  strata_core::LayeredMapParams lp; strata_core::Voxel3DBackend vox(0.2, lp);
  const LabelMap lab = readLabels(root + "/l.bin");
  auto L = [&](float x, float y, float z) { return lab.at(vox.voxelId(Eigen::Vector3d(x, y, z))); };
  EXPECT_EQ(L(1.1f, 0.1f, 1.0f), kStaticStructure | (kGroupSolid << 4));
  EXPECT_EQ(L(3.1f, 0.1f, 1.0f), kStaticStructure | (kGroupVehicle << 4));
  EXPECT_EQ(L(5.1f, 0.1f, 1.0f), kStaticStructure | (kGroupVegetation << 4));
  EXPECT_EQ(L(7.1f, 0.1f, 1.0f), kMoving);
  EXPECT_EQ(L(1.1f, 0.1f, 0.0f), kStaticGround);
  const std::string meta = readAll(root + "/l.meta");
  EXPECT_NE(meta.find("class_groups=1"), std::string::npos) << meta;
  EXPECT_NE(meta.find("cells_structure_solid=1"), std::string::npos) << meta;
  EXPECT_NE(meta.find("class_gt_dynamic_not_moving_class=0"), std::string::npos) << meta;
  EXPECT_NE(meta.find("class_moving_class_not_gt_dynamic=0"), std::string::npos) << meta;
  // Without --classes the label bytes carry no group bits.
  ASSERT_EQ(std::system((exe + " kth-labels --gt " + root + "/gt_cloud.pcd --res 0.2 --out " + root + "/l0.bin --meta " + root + "/l0.meta").c_str()), 0);
  EXPECT_EQ(readLabels(root + "/l0.bin").at(vox.voxelId(Eigen::Vector3d(1.1, 0.1, 1.0))), kStaticStructure);
  // A frame point that differs from the GT cloud makes the run fail.
  std::vector<float> bad_xyz = gt_xyz; bad_xyz[0] += 0.001f;
  writePcd(root + "/gt_bad.pcd", bad_xyz, &gt_in, nullptr);
  EXPECT_NE(std::system((exe + " kth-labels --gt " + root + "/gt_bad.pcd --dir " + root + " --classes 1 --res 0.2 --out " + root + "/lb.bin --meta " + root + "/lb.meta 2>/dev/null").c_str()), 0);
  // Fewer GT points than frame points also fails.
  const std::vector<float> short_in(gt_in.begin(), gt_in.end() - 1);
  writePcd(root + "/gt_short.pcd", std::vector<float>(gt_xyz.begin(), gt_xyz.end() - 3), &short_in, nullptr);
  EXPECT_NE(std::system((exe + " kth-labels --gt " + root + "/gt_short.pcd --dir " + root + " --classes 1 --res 0.2 --out " + root + "/ls.bin --meta " + root + "/ls.meta 2>/dev/null").c_str()), 0);
}

TEST(Cli, LilConvertAppliesAnchorOffsetAndBothPairSkewBounds) {
  const std::string root = path("lil_seq");
  mkdir(root.c_str(), 0755);
  // Robot at x = t along +x. Front laser at the base origin, rear laser rotated by pi. Each scan has 3 beams at
  // angles -1, -0.5, 0 rad with range 3 and time increment 0.001 s. Rear stamps lag the front by 0.04 s.
  std::string chunk = connRecord(0, "/laser_scan_front/scan", "sensor_msgs/LaserScan") + connRecord(1, "/laser_scan_rear/scan", "sensor_msgs/LaserScan") +
                      connRecord(2, "/tf", "tf2_msgs/TFMessage");
  chunk += msgRecord(2, 4, 0, tfBytes("base_link", "scan_front_link", 0.0, 0.0, 0.0, 1.0));
  chunk += msgRecord(2, 4, 0, tfBytes("base_link", "scan_rear_link", 0.0, 0.0, 1.0, 0.0));
  for (int k = 0; k < 3; ++k) {
    chunk += msgRecord(0, 5 + k, 0, laserBytes(5 + k, 0, {3.0f, 3.0f, 3.0f}, 0.001f));
    chunk += msgRecord(1, 5 + k, 40000000, laserBytes(5 + k, 40000000, {3.0f, 3.0f, 3.0f}, 0.001f));
  }
  writeFile(path("lil_seq/a.bag"), makeBag(chunk));
  {
    std::ofstream g(root + "/gt.txt");
    for (int i = 0; i <= 200; ++i) g << i * 0.1 << " " << i * 0.1 << " 0 0 0 0 0 1\n";
  }
  const std::string exe = HITRATE_PROBE_EXE;
  const std::string base = exe + " lil-convert --bag " + root + "/a.bag --gt " + root + "/gt.txt --deskew 1 ";
  ASSERT_EQ(std::system((base + "--out " + root + "/c0.bin --meta " + root + "/c0.meta").c_str()), 0);
  ASSERT_EQ(std::system((base + "--anchor start --time-offset 0.1 --out " + root + "/c1.bin --meta " + root + "/c1.meta").c_str()), 0);
  ASSERT_EQ(std::system((base + "--anchor end --time-offset 0 --out " + root + "/c2.bin --meta " + root + "/c2.meta").c_str()), 0);
  EXPECT_NE(std::system((base + "--anchor sideways --out " + root + "/c3.bin --meta " + root + "/c3.meta 2>/dev/null").c_str()), 0);
  const std::vector<Frame> f0 = readCache(root + "/c0.bin"), f1 = readCache(root + "/c1.bin"), f2 = readCache(root + "/c2.bin");
  ASSERT_EQ(f0.size(), 6u);                                        // 3 steps x 2 lasers
  // Front scan 0 at stamp 5, beam 2 at angle 0, range 3: x = pose(5.002) + 3 = 8.002 for run A (start, 0).
  EXPECT_NEAR(f0[0].xyz[6], 8.002, 1e-3); EXPECT_NEAR(f0[0].origin.x(), 5.0, 1e-9);
  EXPECT_NEAR(f1[0].xyz[6], 8.102, 1e-3); EXPECT_NEAR(f1[0].origin.x(), 5.1, 1e-9);     // offset +0.1 s
  EXPECT_NEAR(f2[0].xyz[6], 8.0, 1e-3); EXPECT_NEAR(f2[0].origin.x(), 4.998, 1e-9);      // end anchor: beam 2 is at the stamp
  const std::string m0 = readAll(root + "/c0.meta"), m1 = readAll(root + "/c1.meta");
  EXPECT_NE(m0.find("anchor=start"), std::string::npos); EXPECT_NE(m0.find("time_offset_s=0\n"), std::string::npos);
  EXPECT_NE(m1.find("time_offset_s=0.1"), std::string::npos);
  EXPECT_NE(readAll(root + "/c2.meta").find("anchor=end"), std::string::npos);
  // The skew of every pair is 0.04 s: inside the preregistered 0.05 s, outside the 0.03 s that run A used.
  EXPECT_NE(m0.find("max_front_rear_stamp_skew_s=0.04"), std::string::npos) << m0;
  EXPECT_NE(m0.find("pair_skew_ok=1\n"), std::string::npos) << m0;
  EXPECT_NE(m0.find("pair_skew_bound_s=0.05"), std::string::npos) << m0;
  EXPECT_NE(m0.find("pair_skew_ok_at_0.03=0\n"), std::string::npos) << m0;
  // lil-offset runs on the same files and writes one row per candidate (3 anchors x 11 offsets).
  const int rc_off = std::system((exe + " lil-offset --bag " + root + "/a.bag --gt " + root + "/gt.txt --stride 1 --half-range 0.05 --step 0.01 --out " +
                         root + "/off.csv --meta " + root + "/off.meta > /dev/null 2>&1").c_str()); (void)rc_off;
  const auto rows = readLines(root + "/off.csv");
  ASSERT_EQ(rows.size(), 1u + 33u);
  EXPECT_EQ(rows[0], "anchor,dt_s,c_front,c_rear,score,dropped_share,hits");
  EXPECT_NE(readAll(root + "/off.meta").find("candidates=33"), std::string::npos);
}

TEST(Cli, RejectsBadArgumentsAndMissingFiles) {
  const std::string exe = HITRATE_PROBE_EXE;
  EXPECT_NE(std::system((exe + " kth-measure --dir /nonexistent --labels x --res 0.2 --window 1 --out o --meta m 2>/dev/null").c_str()), 0);
  EXPECT_NE(std::system((exe + " nonsense 2>/dev/null").c_str()), 0);
  EXPECT_NE(std::system((exe + " kth-labels --gt 2>/dev/null").c_str()), 0);
}
#endif
