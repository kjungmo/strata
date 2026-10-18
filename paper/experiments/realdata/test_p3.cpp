// Tests of the phase 3 harness on small synthetic fixtures. No real data are needed.
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include "p3_boreas.hpp"
#include "p3_kth.hpp"

using namespace realdata;
using namespace realdata::p3;
using strata_core::CellId;

namespace {

std::string tmpDir() {
  static const std::string dir = [] {
    char t[] = "/tmp/p3_test_XXXXXX";
    const char* d = mkdtemp(t);
    return std::string(d ? d : "/tmp");
  }();
  return dir;
}
std::string tmpPath(const std::string& name) { return tmpDir() + "/" + name; }
// Relative paths in command line tests (error cases write "x" and "y") must not land in the directory the tests run from.
const int kChdir = (::chdir(tmpDir().c_str()), 0);
void makeDir(const std::string& p) { ::mkdir(p.c_str(), 0755); }

std::string readText(const std::string& path) {
  std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
std::map<std::string, std::string> readMeta(const std::string& path) {
  std::map<std::string, std::string> m; std::istringstream is(readText(path)); std::string line;
  while (std::getline(is, line)) { const auto e = line.find('='); if (e != std::string::npos) m[line.substr(0, e)] = line.substr(e + 1); }
  return m;
}
int runProbe(const std::string& args, std::string* out = nullptr) {
  const std::string cmd = std::string(P3_PROBE_EXE) + " " + args + " 2>&1";
  FILE* p = popen(cmd.c_str(), "r");
  std::string o; char buf[4096]; std::size_t n;
  while ((n = fread(buf, 1, sizeof buf, p)) > 0) o.append(buf, n);
  const int rc = pclose(p);
  if (out) *out = o;
  return WEXITSTATUS(rc);
}

// ------------------------------------------------------------------------------ arms ----------

TEST(P3Arms, TableMatchesTheDocumentedNumbers) {
  const ArmSpec s = armSpec("shipped_v0_1");
  EXPECT_DOUBLE_EQ(s.l_hit, 0.85); EXPECT_DOUBLE_EQ(s.l_miss, -0.4); EXPECT_DOUBLE_EQ(s.l_min, -5.0); EXPECT_DOUBLE_EQ(s.l_max, 5.0);
  EXPECT_DOUBLE_EQ(s.decay, 0.97); EXPECT_DOUBLE_EQ(s.graduate_prob, 0.8); EXPECT_DOUBLE_EQ(s.demote_prob, 0.45);
  EXPECT_EQ(s.min_observations, 3); EXPECT_DOUBLE_EQ(s.prune_prob, 0.05); EXPECT_FALSE(s.binary); EXPECT_FALSE(s.sprt);
  const ArmSpec y = armSpec("symmetric");
  EXPECT_DOUBLE_EQ(y.l_hit, 0.85); EXPECT_DOUBLE_EQ(y.l_miss, -0.85);
  EXPECT_DOUBLE_EQ(y.decay, s.decay); EXPECT_DOUBLE_EQ(y.graduate_prob, s.graduate_prob); EXPECT_EQ(y.min_observations, s.min_observations);
  const ArmSpec o = armSpec("octomap");
  EXPECT_NEAR(o.l_hit, 0.8472979, 1e-6); EXPECT_NEAR(o.l_miss, -0.4054651, 1e-6);
  EXPECT_NEAR(o.l_min, -1.9924302, 1e-6); EXPECT_NEAR(o.l_max, 3.4760987, 1e-6);
  EXPECT_DOUBLE_EQ(o.decay, 1.0); EXPECT_EQ(o.min_observations, 1); EXPECT_DOUBLE_EQ(o.prune_prob, 0.0); EXPECT_TRUE(o.binary);
  EXPECT_GT(o.graduate_prob, 0.5); EXPECT_LT(o.graduate_prob, 0.5 + 1e-9); EXPECT_DOUBLE_EQ(o.demote_prob, 0.5);
  const ArmSpec e = armSpec("elite_like");
  EXPECT_NEAR(e.l_hit, 2.1972246, 1e-6); EXPECT_NEAR(e.l_miss, -2.1972246, 1e-6); EXPECT_DOUBLE_EQ(e.decay, 1.0); EXPECT_TRUE(e.binary);
  EXPECT_DOUBLE_EQ(e.l_min, -50.0); EXPECT_DOUBLE_EQ(e.l_max, 50.0); EXPECT_EQ(e.min_observations, 1); EXPECT_DOUBLE_EQ(e.prune_prob, 0.0);
  EXPECT_GT(e.graduate_prob, 0.5); EXPECT_LT(e.graduate_prob, 0.5 + 1e-9); EXPECT_DOUBLE_EQ(e.demote_prob, 0.5);
  const ArmSpec p = armSpec("sprt");
  EXPECT_TRUE(p.sprt); EXPECT_DOUBLE_EQ(p.m0, 0.20); EXPECT_DOUBLE_EQ(p.m1, 0.70); EXPECT_DOUBLE_EQ(p.alpha, 0.01); EXPECT_DOUBLE_EQ(p.beta, 0.01);
  EXPECT_EQ(p.n_max_undecided, 80);
  EXPECT_DOUBLE_EQ(armSpec("sprt_low_end").m1, 0.65); EXPECT_DOUBLE_EQ(armSpec("sprt_high_end").m0, 0.30);
  EXPECT_EQ(armNames().size(), 5u);
  EXPECT_THROW(armSpec("nope"), std::runtime_error);
}

TEST(P3Arms, ToParamsCarriesEveryField) {
  for (const std::string& n : {"shipped_v0_1", "symmetric", "octomap", "elite_like"}) {
    const ArmSpec a = armSpec(n);
    const auto p = toParams(a, 15, false);
    EXPECT_EQ(p.layer_interval, 15); EXPECT_FALSE(p.enable_periodicity);
    EXPECT_DOUBLE_EQ(p.l_hit, a.l_hit); EXPECT_DOUBLE_EQ(p.l_miss, a.l_miss); EXPECT_DOUBLE_EQ(p.l_min, a.l_min); EXPECT_DOUBLE_EQ(p.l_max, a.l_max);
    EXPECT_DOUBLE_EQ(p.survival_decay, a.decay); EXPECT_DOUBLE_EQ(p.graduate_prob, a.graduate_prob); EXPECT_DOUBLE_EQ(p.demote_prob, a.demote_prob);
    EXPECT_EQ(p.min_observations, a.min_observations); EXPECT_DOUBLE_EQ(p.prune_prob, a.prune_prob);
  }
  EXPECT_TRUE(toParams(armSpec("shipped_v0_1"), 10, true).enable_periodicity);
  EXPECT_THROW(toParams(armSpec("shipped_v0_1"), 0, true), std::runtime_error);
}

TEST(P3Arms, SprtArmNeedsTheSprtEngine) {
  if (!engineHasSprt()) {
    EXPECT_THROW(toParams(armSpec("sprt"), 10, true), std::runtime_error);
    GTEST_SKIP() << "this strata_core has no evidence_model = sprt; the sprt arm is checked when the phase 2 engine is in the tree";
  }
  const auto p = toParams(armSpec("sprt"), 15, true);
  EXPECT_TRUE(p.evidence_model == std::decay_t<decltype(p.evidence_model)>::Sprt);
  EXPECT_DOUBLE_EQ(p.m0, 0.20); EXPECT_DOUBLE_EQ(p.m1, 0.70); EXPECT_DOUBLE_EQ(p.alpha, 0.01); EXPECT_DOUBLE_EQ(p.beta, 0.01);
  EXPECT_EQ(p.n_max_undecided, 80); EXPECT_EQ(p.layer_interval, 15);
  const auto l = toParams(armSpec("shipped_v0_1"), 15, true);                   // the other arms must run the legacy rule
  EXPECT_TRUE(l.evidence_model == std::decay_t<decltype(l.evidence_model)>::Legacy);
}

TEST(P3Prereg, ArmsAreDocumented) {
  // the block "```text arms" of PREREG_P3.md holds exactly the lines of describe() for the five arms and the two extra arms
  const std::string doc = readText(P3_PREREG_PATH);
  const std::string open = "```text arms\n";
  const std::size_t b = doc.find(open);
  ASSERT_NE(b, std::string::npos) << "no arms block in " << P3_PREREG_PATH;
  const std::size_t e = doc.find("```", b + open.size());
  ASSERT_NE(e, std::string::npos);
  std::string expected;
  for (const auto& n : armNames()) expected += describe(armSpec(n)) + "\n";
  for (const auto& n : extraArmNames()) expected += describe(armSpec(n)) + "\n";
  EXPECT_EQ(doc.substr(b + open.size(), e - b - open.size()), expected);
}

TEST(P3Arms, DescribeIsStableText) {
  EXPECT_EQ(describe(armSpec("shipped_v0_1")),
            "shipped_v0_1: legacy l_hit=0.85 l_miss=-0.4 l_min=-5 l_max=5 decay=0.97 graduate_prob=0.8 demote_prob=0.45 min_observations=3 prune_prob=0.05 binary=0");
  EXPECT_EQ(describe(armSpec("sprt")), "sprt: sprt m0=0.2 m1=0.7 alpha=0.01 beta=0.01 n_max_undecided=80 binary=0");
}

// --------------------------------------------------------------------------- verdicts ----------

TEST(P3Verdict, ThreeStatesAndTheBinaryRule) {
  // shipped rule, one tick per window: cell 1 is hit in every window and graduates (Static); cell 2 is hit once and then
  // missed 11 times: its log-odds reach -3.09 in window 12, below logit(0.05) = -2.94, and the engine erases it (Rejected);
  // cell 3 is hit once in the last window (Transient: Undecided); cell 99 was never held (Rejected).
  strata_core::LayeredMapParams p = toParams(armSpec("shipped_v0_1"), 1, false);
  strata_core::LayeredMap m(p);
  for (int w = 0; w < 12; ++w) { m.observeHit(1); if (w == 0) m.observeHit(2); else m.observeMiss(2); m.tick(); }
  EXPECT_EQ(m.classify(2), strata_core::CellClass::Unknown);
  m.observeHit(1); m.observeHit(3); m.tick();
  EXPECT_EQ(verdictOf(m, 1, false), Verdict::Static);
  EXPECT_EQ(verdictOf(m, 2, false), Verdict::Rejected);
  EXPECT_EQ(verdictOf(m, 3, false), Verdict::Undecided);
  EXPECT_EQ(verdictOf(m, 99, false), Verdict::Rejected);
  // binary arms: no Undecided; everything that is not Static is Rejected
  EXPECT_EQ(verdictOf(m, 3, true), Verdict::Rejected);
  EXPECT_EQ(verdictOf(m, 1, true), Verdict::Static);
}

TEST(P3Verdict, OctomapArmOccupiedIffLogOddsPositive) {
  strata_core::LayeredMap m(toParams(armSpec("octomap"), 1, false));
  // one hit: l = +0.847 -> Static; then two misses: 0.847 - 0.811 = +0.036 -> still Static; a third miss: -0.37 -> not Static
  m.observeHit(7); m.tick();
  EXPECT_TRUE(m.isStatic(7));
  m.observeMiss(7); m.tick(); m.observeMiss(7); m.tick();
  EXPECT_TRUE(m.isStatic(7));
  m.observeMiss(7); m.tick();
  EXPECT_FALSE(m.isStatic(7));
  EXPECT_EQ(verdictOf(m, 7, true), Verdict::Rejected);
  EXPECT_EQ(m.cellCount(), 1u);                                         // never pruned
}

TEST(P3Verdict, ExportPolicies) {
  EXPECT_TRUE(keeps(Policy::StaticOnly, Verdict::Static));
  EXPECT_FALSE(keeps(Policy::StaticOnly, Verdict::Undecided));
  EXPECT_FALSE(keeps(Policy::StaticOnly, Verdict::Rejected));
  EXPECT_TRUE(keeps(Policy::NotRejected, Verdict::Static));
  EXPECT_TRUE(keeps(Policy::NotRejected, Verdict::Undecided));
  EXPECT_FALSE(keeps(Policy::NotRejected, Verdict::Rejected));
  EXPECT_TRUE(keeps(Policy::All, Verdict::Rejected));
}

// ----------------------------------------------------------------------- tally and drive ----------

TEST(P3Tally, CountsWindowsAndFirstStatic) {
  strata_core::GridMeta g; g.width = g.height = 100; g.resolution = 0.1; g.origin_x = g.origin_y = 0;
  strata_core::Grid2DBackend b(g, toParams(armSpec("shipped_v0_1"), 2, false));
  LabelMap watch;
  const CellId wall = strata_core::gridCellId(g, 50, 50), other = strata_core::gridCellId(g, 20, 20), unwatched = strata_core::gridCellId(g, 70, 70);
  watch[wall] = 1; watch[other] = 2;
  TallyObserver tally(&watch, true);
  b.setWindowObserver(&tally);
  int step = 0;
  std::vector<Frame> frames;
  for (int k = 0; k < 12; ++k) {
    Frame f; f.group = k; f.origin = Eigen::Vector3d(0.5, 0.5, 0);
    f.xyz = {5.05f, 5.05f, 0.f};                                                    // wall cell: a hit every step
    if (k % 6 == 0) { f.xyz.push_back(2.05f); f.xyz.push_back(2.05f); f.xyz.push_back(0.f); }    // other: a hit in 1 of 3 windows
    f.xyz.push_back(7.05f); f.xyz.push_back(7.05f); f.xyz.push_back(0.f);
    frames.push_back(f);
  }
  const DriveReport r = drive(b, [&](Frame& f) { if (step >= static_cast<int>(frames.size())) return false; f = frames[step++]; return true; }, &g, &tally);
  EXPECT_EQ(r.frames, 12u); EXPECT_EQ(r.steps, 12u); EXPECT_EQ(r.windows, 6u); EXPECT_EQ(r.hits_off_grid, 0u);
  const auto& t = tally.tally();
  ASSERT_TRUE(t.count(wall));
  EXPECT_EQ(t.at(wall).n, 6u); EXPECT_EQ(t.at(wall).h, 6u);
  EXPECT_EQ(t.at(wall).first_static, 3);                                            // 3 observations needed (min_observations)
  EXPECT_EQ(t.at(wall).first_touch, 1);
  EXPECT_EQ(t.at(other).first_touch, 1);
  ASSERT_TRUE(t.count(other));
  EXPECT_EQ(t.at(other).h, 2u);
  EXPECT_EQ(t.at(other).first_static, -1);
  EXPECT_FALSE(t.count(unwatched));                                                 // not watched, not tallied
  EXPECT_TRUE(b.layered().isStatic(wall));
}

TEST(P3Tally, UnwatchedMeansAllAndNoTrackingMeansNoLatency) {
  strata_core::GridMeta g; g.width = g.height = 50; g.resolution = 0.1;
  strata_core::Grid2DBackend b(g, toParams(armSpec("shipped_v0_1"), 1, false));
  TallyObserver tally(nullptr, false);
  b.setWindowObserver(&tally);
  int k = 0;
  drive(b, [&](Frame& f) { if (k >= 5) return false; f = Frame(); f.group = k++; f.xyz = {1.05f, 1.05f, 0.f}; return true; }, &g, &tally);
  const CellId c = strata_core::gridCellId(g, 10, 10);
  ASSERT_TRUE(tally.tally().count(c));
  EXPECT_EQ(tally.tally().at(c).n, 5u);
  EXPECT_EQ(tally.tally().at(c).first_static, -1);
}

TEST(P3Tally, LabelledDumpRows) {
  strata_core::GridMeta g; g.width = g.height = 50; g.resolution = 0.1;
  strata_core::Grid2DBackend b(g, toParams(armSpec("shipped_v0_1"), 1, false));
  LabelMap labels;
  const CellId hitc = strata_core::gridCellId(g, 10, 12), never = strata_core::gridCellId(g, 30, 31);
  labels[hitc] = 1; labels[never] = 2;
  TallyObserver tally(&labels, true);
  b.setWindowObserver(&tally);
  int k = 0;
  drive(b, [&](Frame& f) { if (k >= 5) return false; f = Frame(); f.group = k++; f.xyz = {1.05f, 1.25f, 0.f}; return true; }, &g, &tally);
  const std::string path = tmpPath("dump.csv");
  writeLabelledDump(path, labels, b.layered(), tally, false, false, g.width);
  EXPECT_EQ(readText(path),
            "label,ix,iy,iz,n,h,verdict,first_static,first_touch\n"
            "1,10,12,0,5,5,static,3,1\n"
            "2,30,31,0,0,0,rejected,-1,-1\n");
}

// ----------------------------------------------------------------------------- PCD writer ----------

TEST(P3Pcd, StreamWriterRoundTripAndEmpty) {
  const std::string path = tmpPath("w.pcd");
  {
    PcdStreamWriter w(path);
    const float a[3] = {1.f, 2.f, 3.f}, b[3] = {-4.5f, 5.25f, 0.f};
    w.add(a); w.add(b);
    EXPECT_EQ(w.count(), 2u);
    w.close();
    w.close();                                                                      // closing twice is harmless
  }
  const PcdCloud c = readPcd(path);
  ASSERT_EQ(c.xyz.size(), 6u);
  EXPECT_EQ(c.hdr.points, 2u);
  EXPECT_FLOAT_EQ(c.xyz[0], 1.f); EXPECT_FLOAT_EQ(c.xyz[3], -4.5f); EXPECT_FLOAT_EQ(c.xyz[4], 5.25f);
  // the header is plain decimal and has the same length for every count
  auto headerOf = [&](std::size_t n) {
    const std::string pth = tmpPath("h" + std::to_string(n) + ".pcd");
    { PcdStreamWriter w(pth); const float p[3] = {1.f, 2.f, 3.f}; for (std::size_t i = 0; i < n; ++i) w.add(p); }
    return readText(pth);
  };
  const std::string h0 = headerOf(0), h2 = headerOf(2), h123456 = headerOf(123456 % 1000);
  const std::size_t len0 = h0.size();                                                // no points: the file is the header
  EXPECT_EQ(h2.size(), len0 + 24u);
  EXPECT_EQ(h123456.size(), len0 + 456u * 12u);
  EXPECT_NE(h2.find("WIDTH 2\n"), std::string::npos); EXPECT_NE(h2.find("POINTS 2\n"), std::string::npos);
  EXPECT_NE(h123456.find("POINTS 456\n"), std::string::npos);
  EXPECT_EQ(h2.find("DATA binary\n"), h0.find("DATA binary\n"));                    // the data start at the same offset
  const std::string empty = tmpPath("e.pcd");
  { PcdStreamWriter w(empty); }                                                     // destructor closes
  EXPECT_EQ(readPcd(empty).hdr.points, 0u);
}

// ------------------------------------------------------------------------ KTH exporter ----------

// A sequence directory with frames in the world frame: the sensor sits still at the origin (VIEWPOINT 0 0 0 1 0 0 0).
// Per frame: a wall point at 10 m in every frame, a mover at 12 m in the first two frames only, a point at 49.99 m
// (kept by the range rule) and a point at exactly 50.0 m (dropped: the rule is distance < 50).
std::string makeKthSequence(const std::string& name, int frames) {
  const std::string dir = tmpPath(name);
  makeDir(dir); makeDir(dir + "/pcd");
  const double vp[7] = {0, 0, 0, 1, 0, 0, 0};
  for (int k = 0; k < frames; ++k) {
    std::vector<float> xyz = {10.05f, 0.05f, 0.05f};
    if (k < 2) { xyz.push_back(12.05f); xyz.push_back(3.05f); xyz.push_back(0.05f); }
    xyz.push_back(49.99f); xyz.push_back(0.f); xyz.push_back(0.f);
    xyz.push_back(50.0f); xyz.push_back(0.f); xyz.push_back(0.f);
    char nm[32]; std::snprintf(nm, sizeof nm, "/pcd/%06d.pcd", k);
    writePcd(dir + nm, xyz, nullptr, vp);
  }
  return dir;
}

TEST(P3Kth, ExportsOriginalPointsByPolicy) {
  const std::string dir = makeKthSequence("kth1", 12);
  KthExportOptions o; o.res = 0.2; o.window_steps = 2; o.periodicity = false; o.max_range = 50.0;
  o.policies = {Policy::StaticOnly, Policy::NotRejected, Policy::All};
  const KthExportReport r = kthExport(dir, armSpec("shipped_v0_1"), o, tmpPath("kth1_out"));
  EXPECT_EQ(r.frames, 12u); EXPECT_EQ(r.windows, 6u);
  EXPECT_EQ(r.points_in_range, 12u * 2u + 2u);                                      // wall + 49.99 m point per frame, plus the 2 mover points
  EXPECT_EQ(r.points_out_of_range, 12u);                                            // the 50.0 m point of every frame
  ASSERT_EQ(r.kept.size(), 3u);
  EXPECT_EQ(r.kept[2], r.points_in_range);                                          // policy all = remove nothing
  // static_only: the wall points (12) and the 49.99 m points (12, a hit in every frame) graduate; the mover is hit in one window only
  EXPECT_EQ(r.kept[0], 24u);
  EXPECT_EQ(r.kept[1], 26u);                                                        // the 2 mover points sit in a Transient voxel: not Rejected, kept
  const PcdCloud c = readPcd(r.out_paths[0]);
  ASSERT_EQ(c.xyz.size() / 3, 24u);
  for (std::size_t i = 0; i < c.xyz.size(); i += 3) {                               // exact copies of frame points, not voxel centres
    const bool wall = c.xyz[i] == 10.05f && c.xyz[i + 1] == 0.05f && c.xyz[i + 2] == 0.05f;
    const bool far = c.xyz[i] == 49.99f && c.xyz[i + 1] == 0.f && c.xyz[i + 2] == 0.f;
    EXPECT_TRUE(wall || far) << c.xyz[i] << " " << c.xyz[i + 1];
  }
  EXPECT_EQ(readPcd(r.out_paths[2]).hdr.points, r.points_in_range);
  EXPECT_EQ(r.verdict_points[0] + r.verdict_points[1] + r.verdict_points[2], r.points_in_range);
}

TEST(P3Kth, MoverRemovedByNotRejectedOnlyWhenTheEngineRejectsIt) {
  // A mover seen in 1 of 6 windows then missed by the wall-ray of later frames is not what this test is about; the point is
  // that policy not_rejected never keeps a point whose voxel the arm prunes. Build a voxel hit once and then cleared by rays.
  const std::string dir = tmpPath("kth2");
  makeDir(dir); makeDir(dir + "/pcd");
  const double vp[7] = {0, 0, 0, 1, 0, 0, 0};
  for (int k = 0; k < 12; ++k) {
    std::vector<float> xyz = {20.05f, 0.05f, 0.05f};                                // a far wall behind the mover: its rays cross the mover cell
    if (k == 0) { xyz.push_back(10.05f); xyz.push_back(0.05f); xyz.push_back(0.05f); }   // the mover, hit in frame 0 only
    char nm[32]; std::snprintf(nm, sizeof nm, "/pcd/%06d.pcd", k);
    writePcd(dir + nm, xyz, nullptr, vp);
  }
  KthExportOptions o; o.res = 0.2; o.window_steps = 1; o.periodicity = false;
  o.policies = {Policy::StaticOnly, Policy::NotRejected, Policy::All};
  const KthExportReport r = kthExport(dir, armSpec("shipped_v0_1"), o, tmpPath("kth2_out"));
  // far wall: 12 points (one per frame); mover: 1 point. The mover voxel is hit in window 1 and missed by the far-wall ray in
  // windows 2..12: log-odds 0.82, 0.41, 0.01, -0.38, ..., -3.09 (below -2.94 in window 12): the engine erases the cell.
  EXPECT_EQ(r.kept[2], 13u);
  EXPECT_EQ(r.kept[0], 12u);                                                        // far wall only
  EXPECT_EQ(r.kept[1], 12u);                                                        // the mover voxel is Rejected
  EXPECT_EQ(r.verdict_points[0], 1u);                                               // exactly one point sits in a Rejected voxel
}

TEST(P3Kth, RangeRuleAndRaysUseTheViewpoint) {
  // the sensor sits at (30, 0, 0). Points: a far wall at 31.05 (hit every frame), a point 49.9 m ahead of the sensor
  // (kept by the range rule), one 50.1 m ahead (dropped), and a mover at x = 20.05 hit in frame 0 only. The mover lies between
  // the origin (0, 0, 0) and the wall, but not between the sensor and the wall: rays from the wrong origin would erase it.
  const std::string dir = tmpPath("kth5");
  makeDir(dir); makeDir(dir + "/pcd");
  const double vp[7] = {30, 0, 0, 1, 0, 0, 0};
  for (int k = 0; k < 12; ++k) {
    std::vector<float> xyz = {31.05f, 0.05f, 0.05f, 79.9f, 0.f, 0.f, 80.1f, 0.f, 0.f};
    if (k == 0) { xyz.push_back(20.05f); xyz.push_back(0.05f); xyz.push_back(0.05f); }
    char nm[32]; std::snprintf(nm, sizeof nm, "/pcd/%06d.pcd", k);
    writePcd(dir + nm, xyz, nullptr, vp);
  }
  KthExportOptions o; o.res = 0.2; o.window_steps = 1; o.periodicity = false; o.max_range = 50.0;
  o.policies = {Policy::StaticOnly, Policy::NotRejected, Policy::All};
  const KthExportReport r = kthExport(dir, armSpec("shipped_v0_1"), o, tmpPath("kth5_out"));
  EXPECT_EQ(r.points_out_of_range, 12u);                                            // 80.1 m is 50.1 m from the sensor
  EXPECT_EQ(r.points_in_range, 12u + 12u + 1u);                                     // wall, the point 49.9 m ahead, the mover
  EXPECT_EQ(r.kept[2], 25u);
  EXPECT_EQ(r.kept[0], 24u);                                                        // wall and the point 49.9 m ahead graduate
  EXPECT_EQ(r.kept[1], 25u);                                                        // the mover is hit in one window and never missed: Undecided, kept
}

TEST(P3Kth, NoRangeCutByDefault) {
  // the ground-truth cloud holds every frame point, so the default exports every point, far ones included
  const std::string dir = makeKthSequence("kth6", 12);
  KthExportOptions o; o.res = 0.2; o.window_steps = 2; o.periodicity = false; o.policies = {Policy::All, Policy::StaticOnly};
  EXPECT_DOUBLE_EQ(o.max_range, 0.0);
  const KthExportReport r = kthExport(dir, armSpec("shipped_v0_1"), o, tmpPath("kth6_out"));
  EXPECT_EQ(r.points_out_of_range, 0u);
  EXPECT_EQ(r.points_in_range, 12u * 3u + 2u);
  EXPECT_EQ(r.kept[0], r.points_in_range);                                          // policy all = every frame point, the GT cloud of the sequence
  EXPECT_EQ(r.kept[1], 36u);                                                        // wall, 49.99 m point and the 50.0 m point all graduate
  EXPECT_EQ(readPcd(r.out_paths[0]).hdr.points, 38u);
}

TEST(P3Kth, MissingViewpointAndMissingFramesFailLoudly) {
  const std::string dir = tmpPath("kth3");
  makeDir(dir); makeDir(dir + "/pcd");
  KthExportOptions o;
  EXPECT_THROW(kthExport(dir, armSpec("shipped_v0_1"), o, tmpPath("x")), std::runtime_error);        // no frames
  writePcd(dir + "/pcd/000000.pcd", {1.f, 1.f, 1.f}, nullptr, nullptr);
  EXPECT_THROW(kthExport(dir, armSpec("shipped_v0_1"), o, tmpPath("x")), std::runtime_error);        // no VIEWPOINT
  EXPECT_THROW(listPcdFiles(tmpPath("does_not_exist")), std::runtime_error);
}

TEST(P3Kth, MaxFramesLimitsTheRun) {
  const std::string dir = makeKthSequence("kth4", 12);
  KthExportOptions o; o.res = 0.2; o.window_steps = 2; o.periodicity = false; o.max_frames = 5; o.policies = {Policy::All};
  const KthExportReport r = kthExport(dir, armSpec("shipped_v0_1"), o, tmpPath("kth4_out"));
  EXPECT_EQ(r.frames, 5u); EXPECT_EQ(r.windows, 2u);                                // 5 frames, windows of 2: the last partial window is not closed
}

// ----------------------------------------------------------------------------- Boreas ----------

std::vector<std::uint8_t> binOf(const std::vector<std::array<float, 6>>& pts) {
  std::vector<std::uint8_t> b(pts.size() * 24);
  for (std::size_t i = 0; i < pts.size(); ++i) std::memcpy(b.data() + 24 * i, pts[i].data(), 24);
  return b;
}

TEST(P3Boreas, BinParsing) {
  const auto b = binOf({{1, 2, 3, 10, 5, -0.05f}, {4, 5, 6, 20, 6, 0.04f}});
  const BoreasScan s = parseBoreasBin(b, "x.bin");
  EXPECT_EQ(s.points, 2u); ASSERT_EQ(s.size(), 2u);
  EXPECT_FLOAT_EQ(s.xyz[3], 4.f); EXPECT_FLOAT_EQ(s.intensity[1], 20.f); EXPECT_FLOAT_EQ(s.ring[0], 5.f); EXPECT_FLOAT_EQ(s.t[0], -0.05f);
  auto bad = b; bad.pop_back();
  EXPECT_THROW(parseBoreasBin(bad, "x.bin"), std::runtime_error);                   // not a multiple of 24
  EXPECT_THROW(parseBoreasBin({}, "x.bin"), std::runtime_error);
  const auto nan = binOf({{1, 2, 3, 10, 5, 0.f}, {std::nanf(""), 0, 0, 0, 0, 0}, {1, 2, 3, 10, 5, INFINITY}});
  const BoreasScan sn = parseBoreasBin(nan, "n.bin");
  EXPECT_EQ(sn.points, 3u); EXPECT_EQ(sn.size(), 1u); EXPECT_EQ(sn.dropped_nonfinite, 2u);
  EXPECT_EQ(sn.xyz.size(), sn.intensity.size() * 3);                                // arrays stay aligned
}

BoreasScan sweep(std::size_t n, float rmax, float zshift = 0) {
  std::mt19937 g(5); std::uniform_real_distribution<float> u(-1.f, 1.f);
  std::vector<std::array<float, 6>> p;
  for (std::size_t i = 0; i < n; ++i) p.push_back({u(g) * rmax, u(g) * rmax, u(g) * 2.f + zshift, float(i % 200), float(i % 128), -0.05f + 0.1f * float(i) / float(n)});
  return parseBoreasBin(binOf(p), "s.bin");
}

TEST(P3Boreas, LayoutVerdictRules) {
  EXPECT_EQ(layoutVerdict(layoutStats(sweep(5000, 30.f))), "");
  EXPECT_NE(layoutVerdict(layoutStats(sweep(500, 30.f))).find("1000"), std::string::npos);                // too few points
  { BoreasScan s = sweep(5000, 30.f); s.xyz[0] = 600.f; EXPECT_NE(layoutVerdict(layoutStats(s)).find("500 m"), std::string::npos); }
  { BoreasScan s = sweep(5000, 30.f); for (auto& v : s.xyz) v *= 0.02f; EXPECT_NE(layoutVerdict(layoutStats(s)).find("median range"), std::string::npos); }
  { BoreasScan s = sweep(5000, 30.f); for (auto& v : s.xyz) v *= 5.f; EXPECT_NE(layoutVerdict(layoutStats(s)).find("median range"), std::string::npos); }
  { BoreasScan s = sweep(5000, 30.f, 400.f); EXPECT_NE(layoutVerdict(layoutStats(s)).find("z outside"), std::string::npos); }
  { BoreasScan s = sweep(5000, 30.f); s.intensity[3] = -1.f; EXPECT_NE(layoutVerdict(layoutStats(s)).find("intensity"), std::string::npos); }
  { BoreasScan s = sweep(5000, 30.f); s.intensity[3] = 70000.f; EXPECT_NE(layoutVerdict(layoutStats(s)).find("intensity"), std::string::npos); }
  { BoreasScan s = sweep(5000, 30.f); for (auto& v : s.ring) v += 0.5f; EXPECT_NE(layoutVerdict(layoutStats(s)).find("integer"), std::string::npos); }
  { BoreasScan s = sweep(5000, 30.f); s.ring[1] = 128.f; EXPECT_NE(layoutVerdict(layoutStats(s)).find("ring outside"), std::string::npos); }
  { BoreasScan s = sweep(5000, 30.f); s.ring[1] = -1.f; EXPECT_NE(layoutVerdict(layoutStats(s)).find("ring outside"), std::string::npos); }
  { BoreasScan s = sweep(5000, 30.f); s.t[2] = 0.25f; EXPECT_NE(layoutVerdict(layoutStats(s)).find("relative time outside"), std::string::npos); }
  { BoreasScan s = sweep(5000, 30.f); s.t[2] = -0.25f; EXPECT_NE(layoutVerdict(layoutStats(s)).find("relative time outside"), std::string::npos); }
  { BoreasScan s = sweep(5000, 30.f); for (auto& v : s.t) v = 0.01f; EXPECT_NE(layoutVerdict(layoutStats(s)).find("spans less"), std::string::npos); }
  { BoreasScan s = sweep(5000, 30.f); s.dropped_nonfinite = 100; EXPECT_NE(layoutVerdict(layoutStats(s)).find("non-finite"), std::string::npos); }
  // the layout test must reject a different record layout: 4 x float32 (x y z i) read as 6 x float32 shifts every column
  std::vector<float> four;
  std::mt19937 g(9); std::uniform_real_distribution<float> u(-1.f, 1.f);
  for (int i = 0; i < 6000; ++i) { four.push_back(u(g) * 30.f); four.push_back(u(g) * 30.f); four.push_back(u(g) * 2.f); four.push_back(float(i % 200)); }
  std::vector<std::uint8_t> raw(four.size() * 4); std::memcpy(raw.data(), four.data(), raw.size());
  raw.resize(raw.size() / 24 * 24);
  EXPECT_NE(layoutVerdict(layoutStats(parseBoreasBin(raw, "four.bin"))), "");
}

BoreasLayoutStats goodStats() {
  BoreasLayoutStats s;
  s.points = 1000; s.dropped_nonfinite = 0; s.z_min = -2; s.z_max = 2; s.range_max = 100; s.range_median = 20;
  s.intensity_min = 0; s.intensity_max = 255; s.ring_min = 0; s.ring_max = 127; s.ring_integer_share = 1.0; s.t_min = -0.05; s.t_max = 0.05;
  return s;
}

TEST(P3Boreas, LayoutVerdictBoundariesOnBothSides) {
  EXPECT_EQ(layoutVerdict(goodStats()), "");
  auto with = [&](auto mutate) { BoreasLayoutStats s = goodStats(); mutate(s); return layoutVerdict(s); };
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.points = 1000; }), "");
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.points = 999; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.points = 5000; s.dropped_nonfinite = 50; }), "");               // exactly 1 %
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.points = 5000; s.dropped_nonfinite = 51; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.range_max = 500.0; }), "");
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.range_max = 500.01; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.z_max = 200.0; s.range_max = 250; }), "");
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.z_max = 200.01; s.range_max = 250; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.z_min = -200.0; s.range_max = 250; }), "");
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.z_min = -200.01; s.range_max = 250; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.range_median = 1.0; }), "");
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.range_median = 0.99; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.range_median = 100.0; }), "");
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.range_median = 100.01; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.intensity_min = 0.0; }), "");
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.intensity_min = -0.01; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.intensity_max = 65535.0; }), "");
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.intensity_max = 65535.5; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.ring_integer_share = 0.999; }), "");
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.ring_integer_share = 0.9989; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.ring_min = 0.0; }), "");
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.ring_min = -1.0; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.ring_max = 127.0; }), "");
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.ring_max = 128.0; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.t_min = -0.2; }), "");
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.t_min = -0.21; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.t_max = 0.2; }), "");
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.t_max = 0.21; }), "");
  EXPECT_EQ(with([](BoreasLayoutStats& s) { s.t_min = 0.0; s.t_max = 0.05; }), "");                          // spans exactly 0.05 s
  EXPECT_NE(with([](BoreasLayoutStats& s) { s.t_min = 0.0; s.t_max = 0.049; }), "");
}

void writeText(const std::string& path, const std::string& s) { std::ofstream(path) << s; }

TEST(P3Boreas, PoseCsvWindowOriginAndTimeScale) {
  const std::string p = tmpPath("gps.csv");
  std::string s = "GPSTime,easting,northing,altitude,vel_east,vel_north,vel_up,roll,pitch,heading,angvel_z,angvel_y,angvel_x,accelz,accely,accelx,latitude,longitude\n";
  for (int i = 0; i < 10; ++i) {
    std::ostringstream l; l.precision(12);
    l << 100.0 + 0.005 * i << "," << i << "," << 2 * i << "," << 0.1 * i << ",0,0,0,0.01,0.02,0.5,0,0,0,0,0,0,0.76,-1.38\n";
    s += l.str();
  }
  writeText(p, s);
  GeoOrigin org;
  auto rows = readBoreasPoseCsv(p, 1.0, 100.01, 100.03, &org);
  ASSERT_EQ(rows.size(), 5u);                                                       // 100.010 .. 100.030 inclusive
  EXPECT_NEAR(rows.front().t, 100.010, 1e-9); EXPECT_NEAR(rows.back().t, 100.030, 1e-9);
  EXPECT_DOUBLE_EQ(rows.front().east, 2); EXPECT_DOUBLE_EQ(rows.front().north, 4); EXPECT_NEAR(rows.front().up, 0.2, 1e-12);
  EXPECT_DOUBLE_EQ(rows.front().heading, 0.5); EXPECT_DOUBLE_EQ(rows.front().roll, 0.01);
  ASSERT_TRUE(org.valid); EXPECT_DOUBLE_EQ(org.lat, 0.76); EXPECT_DOUBLE_EQ(org.lon, -1.38);
  EXPECT_DOUBLE_EQ(org.east, 0.0); EXPECT_DOUBLE_EQ(org.north, 0.0);                  // the file coordinates of the first row (not of the window)
  EXPECT_DOUBLE_EQ(rows.front().lat, 0.76); EXPECT_DOUBLE_EQ(rows.front().lon, -1.38);
  // the window edges are inclusive on both sides
  EXPECT_EQ(readBoreasPoseCsv(p, 1.0, 100.01 - 1e-9, 100.01 + 1e-9).size(), 1u);
  EXPECT_EQ(readBoreasPoseCsv(p, 1.0, 100.011, 100.014).size(), 0u);
  // origin only: nothing wanted, still the first row's latitude and longitude
  GeoOrigin o2;
  EXPECT_EQ(readBoreasPoseCsv(p, 1.0, 1e300, -1e300, &o2).size(), 0u);
  EXPECT_TRUE(o2.valid);
  // microsecond files
  const std::string q = tmpPath("lp.csv");
  writeText(q, "GPSTime,easting,northing,altitude,vel_east,vel_north,vel_up,roll,pitch,heading,angvel_z,angvel_y,angvel_x\n1606417543533737,1,2,3,0,0,0,0,0,0.1,0,0,0\n1606417543637467,2,3,4,0,0,0,0,0,0.2,0,0,0\n");
  GeoOrigin o3;
  const auto lr = readBoreasPoseCsv(q, 1e-6, 1606417543.0, 1606417544.0, &o3);
  ASSERT_EQ(lr.size(), 2u); EXPECT_NEAR(lr[0].t, 1606417543.533737, 1e-6); EXPECT_FALSE(o3.valid);
  EXPECT_FALSE(std::isfinite(lr[0].lat)); EXPECT_FALSE(std::isfinite(lr[0].lon));       // no latitude/longitude columns: NaN, never a silent zero
  // malformed input fails loudly
  writeText(tmpPath("b1.csv"), "GPSTime,easting\n1,2\n");
  EXPECT_THROW(readBoreasPoseCsv(tmpPath("b1.csv"), 1.0, 0, 10), std::runtime_error);                 // columns missing
  writeText(tmpPath("b2.csv"), "GPSTime,easting,northing,altitude,roll,pitch,heading\n2,1,1,1,0,0,0\n1,1,1,1,0,0,0\n");
  EXPECT_THROW(readBoreasPoseCsv(tmpPath("b2.csv"), 1.0, 0, 10), std::runtime_error);                 // time goes backwards
  writeText(tmpPath("b2b.csv"), "GPSTime,easting,northing,altitude,roll,pitch,heading\n1,1,1,1,0,0,0\n1,1,1,1,0,0,0\n");
  EXPECT_THROW(readBoreasPoseCsv(tmpPath("b2b.csv"), 1.0, 0, 10), std::runtime_error);                // a repeated time is not an increase
  writeText(tmpPath("b3.csv"), "GPSTime,easting,northing,altitude,roll,pitch,heading\n1,1,1,1,0,0\n");
  EXPECT_THROW(readBoreasPoseCsv(tmpPath("b3.csv"), 1.0, 0, 10), std::runtime_error);                 // short row
  writeText(tmpPath("b4.csv"), "GPSTime,easting,northing,altitude,roll,pitch,heading\n1,nan,1,1,0,0,0\n");
  EXPECT_THROW(readBoreasPoseCsv(tmpPath("b4.csv"), 1.0, 0, 10), std::runtime_error);                 // non-finite
  EXPECT_THROW(readBoreasPoseCsv(tmpPath("missing.csv"), 1.0, 0, 10), std::runtime_error);
}

TEST(P3Boreas, RotationConventionAndCalibration) {
  BoreasPoseRow r; r.heading = M_PI / 2;
  EXPECT_NEAR((rotationOf(r) * Eigen::Vector3d::UnitX() - Eigen::Vector3d::UnitY()).norm(), 0.0, 1e-12);       // heading is yaw about up, counter-clockwise
  r = BoreasPoseRow(); r.pitch = 0.1;                                                                         // pitch about y: x tilts toward -z for positive pitch
  EXPECT_LT((rotationOf(r) * Eigen::Vector3d::UnitX()).z(), 0.0);
  r = BoreasPoseRow(); r.roll = 0.1;                                                                          // roll about x: y tilts toward +z
  EXPECT_GT((rotationOf(r) * Eigen::Vector3d::UnitY()).z(), 0.0);
  r = BoreasPoseRow(); r.roll = 0.3; r.pitch = -0.2; r.heading = 1.1;                                        // order Rz Ry Rx
  const Eigen::Matrix3d expect = (Eigen::AngleAxisd(1.1, Eigen::Vector3d::UnitZ()) * Eigen::AngleAxisd(-0.2, Eigen::Vector3d::UnitY()) * Eigen::AngleAxisd(0.3, Eigen::Vector3d::UnitX())).toRotationMatrix();
  EXPECT_NEAR((rotationOf(r).toRotationMatrix() - expect).norm(), 0.0, 1e-12);
  // the calibration of a real session: yaw +42.6 degrees, z 0.13
  writeText(tmpPath("T3.txt"), "7.360555651493132512e-01 -6.769211217083995757e-01 0 0\n6.769211217083995757e-01 7.360555651493132512e-01 0 0\n0 0 1 1.3e-01\n");
  const Pose3 T = readBoreasCalib(tmpPath("T3.txt"));
  EXPECT_NEAR(T.t.z(), 0.13, 1e-12); EXPECT_NEAR(T.t.x(), 0.0, 1e-12);
  EXPECT_NEAR((T.q * Eigen::Vector3d::UnitX() - Eigen::Vector3d(0.7360555651, 0.6769211217, 0)).norm(), 0.0, 1e-8);
  // the lidar pose from the applanix pose: yaw shift on the world side, translation added in the world frame (see lidarPoseFromApplanix)
  BoreasPoseRow tilt; tilt.roll = 0.1; tilt.pitch = -0.05; tilt.heading = 1.0;
  Pose3 pa = poseOf(tilt); pa.t = Eigen::Vector3d(10, 20, 30);
  const Pose3 L = lidarPoseFromApplanix(pa, T);
  const double th = std::atan2(0.6769211217083995757, 0.7360555651493132512);
  const Eigen::Matrix3d expect_R = Eigen::AngleAxisd(-th, Eigen::Vector3d::UnitZ()).toRotationMatrix() * pa.q.toRotationMatrix();
  EXPECT_NEAR((L.q.toRotationMatrix() - expect_R).norm(), 0.0, 1e-12);
  EXPECT_NEAR((L.t - Eigen::Vector3d(10, 20, 30.13)).norm(), 0.0, 1e-12);                                // not rotated by the tilt
  // not the physical composition R_applanix * R_f^-1: at this tilt it differs by more than a degree
  EXPECT_GT(L.q.angularDistance(Eigen::Quaterniond(pa.q.toRotationMatrix() * T.q.inverse().toRotationMatrix())), 0.02);
  // without tilt the two agree, and the yaw is the applanix yaw minus 42.6 degrees
  BoreasPoseRow flat; flat.heading = 1.0;
  const Pose3 Lf = lidarPoseFromApplanix(poseOf(flat), T);
  const Eigen::Vector3d fwd = Lf.q * Eigen::Vector3d::UnitX();
  EXPECT_NEAR(std::atan2(fwd.y(), fwd.x()), 1.0 - th, 1e-12);
  writeText(tmpPath("T4.txt"), "1 0 0 1\n0 1 0 2\n0 0 1 3\n0 0 0 1\n");
  EXPECT_NEAR(readBoreasCalib(tmpPath("T4.txt")).t.y(), 2.0, 1e-12);
  writeText(tmpPath("T5.txt"), "1 0 0 1\n0 1 0 2\n0 0 1 3\n0 0 1 1\n");
  EXPECT_THROW(readBoreasCalib(tmpPath("T5.txt")), std::runtime_error);                                         // last row not 0 0 0 1
  writeText(tmpPath("T6.txt"), "2 0 0 1\n0 1 0 2\n0 0 1 3\n");
  EXPECT_THROW(readBoreasCalib(tmpPath("T6.txt")), std::runtime_error);                                         // not a rotation
  writeText(tmpPath("T6b.txt"), "1 0 0 1\n0 1 0 2\n0 0 -1 3\n");                                              // orthonormal, determinant -1: a reflection
  EXPECT_THROW(readBoreasCalib(tmpPath("T6b.txt")), std::runtime_error);
  writeText(tmpPath("T6c.txt"), "1 1 0 1\n0 1 0 2\n0 0 1 3\n");                                               // determinant 1, not orthonormal: a shear
  EXPECT_THROW(readBoreasCalib(tmpPath("T6c.txt")), std::runtime_error);
  writeText(tmpPath("T7.txt"), "1 0 0 1\n0 1 0 2\n");
  EXPECT_THROW(readBoreasCalib(tmpPath("T7.txt")), std::runtime_error);                                         // wrong count
  // compose: (a*b) maps p to a(b(p))
  Pose3 a, b;
  a.q = Eigen::Quaterniond(Eigen::AngleAxisd(M_PI / 2, Eigen::Vector3d::UnitZ())); a.t = Eigen::Vector3d(1, 0, 0);
  b.q = Eigen::Quaterniond::Identity(); b.t = Eigen::Vector3d(0, 2, 0);
  const Pose3 c = compose(a, b);
  EXPECT_NEAR((c.t - Eigen::Vector3d(-1, 0, 0)).norm(), 0.0, 1e-12);
  // rotations: (a * b) p = a (b p) with both non-trivial: b turns x to -z (pitch 90), a turns the result about z (yaw 90)
  a.q = Eigen::Quaterniond(Eigen::AngleAxisd(M_PI / 2, Eigen::Vector3d::UnitZ())); a.t = Eigen::Vector3d(0, 0, 0);
  b.q = Eigen::Quaterniond(Eigen::AngleAxisd(M_PI / 2, Eigen::Vector3d::UnitY())); b.t = Eigen::Vector3d(0, 0, 0);
  const Pose3 d = compose(a, b);
  EXPECT_NEAR((d.q * Eigen::Vector3d::UnitX() - a.q * (b.q * Eigen::Vector3d::UnitX())).norm(), 0.0, 1e-12);
  EXPECT_GT((d.q * Eigen::Vector3d::UnitX() - b.q * (a.q * Eigen::Vector3d::UnitX())).norm(), 0.5);      // the other order gives another vector
}

TEST(P3Boreas, GeodesyAndTheCommonFrame) {
  EXPECT_NEAR((ecefOf(0, 0) - Eigen::Vector3d(kWgs84A, 0, 0)).norm(), 0.0, 1e-6);
  EXPECT_NEAR(ecefOf(M_PI / 2, 0).z(), kWgs84A * (1 - kWgs84F), 1e-3);              // polar radius
  const double lat0 = 0.7641426285, lon0 = -1.3869452891;
  EXPECT_NEAR(enuOf(lat0, lon0, lat0, lon0).norm(), 0.0, 1e-9);
  // northward: d_lat times the meridian radius of curvature; eastward: d_lon times the prime-vertical radius times cos(lat)
  const double e2 = kWgs84F * (2 - kWgs84F), s = std::sin(lat0);
  const double M = kWgs84A * (1 - e2) / std::pow(1 - e2 * s * s, 1.5), N = kWgs84A / std::sqrt(1 - e2 * s * s);
  const Eigen::Vector3d dn = enuOf(lat0 + 1e-5, lon0, lat0, lon0), de = enuOf(lat0, lon0 + 1e-5, lat0, lon0);
  EXPECT_NEAR(dn.y(), 1e-5 * M, 1e-3); EXPECT_NEAR(dn.x(), 0.0, 1e-3);
  EXPECT_NEAR(de.x(), 1e-5 * N * std::cos(lat0), 1e-3); EXPECT_NEAR(de.y(), 0.0, 2e-3);
  // the curvature of the Earth: 1 km east is about 1000^2 / (2 N) metres below the tangent plane
  const Eigen::Vector3d far = enuOf(lat0, lon0 + 1000.0 / (N * std::cos(lat0)), lat0, lon0);
  EXPECT_NEAR(far.z(), -1000.0 * 1000.0 / (2 * N), 0.01);
  // The common frame is the frame of the files: the identity, plus an optional vertical offset (review finding 1).
  const Pose3 id = commonFrame();
  EXPECT_EQ(id.t.norm(), 0.0); EXPECT_NEAR(id.q.angularDistance(Eigen::Quaterniond::Identity()), 0.0, 1e-15);
  const Pose3 tz = commonFrame(0.25);
  EXPECT_EQ(tz.t.x(), 0.0); EXPECT_EQ(tz.t.y(), 0.0); EXPECT_EQ(tz.t.z(), 0.25);
  // enuAgreement: rows whose file east/north equal the geodetic ENU at the reference origin agree; rows measured from another origin do not
  GeoOrigin ref{lat0, lon0, true};
  auto rowAt = [&](double east_m, double north_m, double file_east, double file_north) {
    BoreasPoseRow r; r.lat = lat0 + north_m / M; r.lon = lon0 + east_m / (N * std::cos(lat0)); r.east = file_east; r.north = file_north; return r;
  };
  std::vector<BoreasPoseRow> rows = {rowAt(0, 0, 0, 0), rowAt(100, 50, 100, 50), rowAt(-300, 20, -300, 20)};
  EnuAgreement ea = enuAgreement(rows, ref);
  EXPECT_EQ(ea.rows, 3u); EXPECT_LT(ea.worst_m, 0.01);
  // a session whose first row is at (-4.302, -0.149) of the shared frame, with files in the shared frame: agrees (the case of 2021-04-08)
  rows = {rowAt(-4.302, -0.149, -4.302, -0.149), rowAt(60, 10, 60, 10)};
  EXPECT_LT(enuAgreement(rows, ref).worst_m, 0.01);
  // the same rows with files measured from the session's own first row (the premise of the old frame): they miss by 4.3 m
  rows = {rowAt(-4.302, -0.149, 0, 0), rowAt(60, 10, 64.302, 10.149)};
  ea = enuAgreement(rows, ref);
  EXPECT_NEAR(ea.worst_m, std::hypot(4.302, 0.149), 0.01); EXPECT_NEAR(ea.mean_dx_m, 4.302, 0.01); EXPECT_NEAR(ea.mean_dy_m, 0.149, 0.01);
  EXPECT_GT(ea.worst_m, kEnuAgreementTolM);
  // a miss of 0.09 m passes, 0.11 m does not (both sides of the limit)
  rows = {rowAt(10, 0, 10.09, 0)}; EXPECT_LT(enuAgreement(rows, ref).worst_m, kEnuAgreementTolM);
  rows = {rowAt(10, 0, 10.11, 0)}; EXPECT_GT(enuAgreement(rows, ref).worst_m, kEnuAgreementTolM);
  // a miss in north alone counts; a diagonal miss whose parts are each below the limit but whose length is above it counts (horizontal distance, not one axis)
  rows = {rowAt(10, 0, 10, 0.2)}; EXPECT_NEAR(enuAgreement(rows, ref).worst_m, 0.2, 0.01);
  rows = {rowAt(10, 5, 10.08, 5.08)}; EXPECT_GT(enuAgreement(rows, ref).worst_m, kEnuAgreementTolM);
  EXPECT_NEAR(enuAgreement(rows, ref).worst_m, std::hypot(0.08, 0.08), 0.005);
  // height is ignored; rows without latitude and longitude are skipped and not counted; no usable row gives rows = 0
  { BoreasPoseRow r = rowAt(5, 5, 5, 5); r.up = 40.0; rows = {r}; EXPECT_LT(enuAgreement(rows, ref).worst_m, 0.01); }
  { BoreasPoseRow r; r.east = 1e6; rows = {r, rowAt(5, 5, 5, 5)}; ea = enuAgreement(rows, ref); EXPECT_EQ(ea.rows, 1u); EXPECT_LT(ea.worst_m, 0.01); }
  EXPECT_EQ(enuAgreement({}, ref).rows, 0u);
  EXPECT_THROW(enuAgreement(rows, GeoOrigin{}), std::runtime_error);
}

TEST(P3Boreas, AltitudeDifferenceBetweenTrajectories) {
  auto line = [](double up0, double slope, int n, double dy) {
    std::vector<BoreasPoseRow> v;
    for (int i = 0; i < n; ++i) { BoreasPoseRow r; r.east = i; r.north = dy; r.up = up0 + slope * i; v.push_back(r); }
    return v;
  };
  // B runs 1 m beside A and 0.4 m higher everywhere: every row of A finds a partner and the difference is 0.4
  AltitudeDifference d = altitudeDifference(line(10, 0.01, 100, 0), line(10.4, 0.01, 100, 1.0), 3.0);
  EXPECT_EQ(d.pairs, 100u); EXPECT_NEAR(d.median, 0.4, 1e-12); EXPECT_NEAR(d.p10, 0.4, 1e-12); EXPECT_NEAR(d.p90, 0.4, 1e-12);
  // the sign is up_B - up_A
  d = altitudeDifference(line(10.4, 0.01, 100, 1.0), line(10, 0.01, 100, 0), 3.0);
  EXPECT_NEAR(d.median, -0.4, 1e-12);
  // a partner farther than the radius does not count (3.0 m is inside, 3.5 m outside); no partner at all: no pairs, NaN
  EXPECT_EQ(altitudeDifference(line(0, 0, 10, 0), line(0, 0, 10, 3.0), 3.0).pairs, 10u);
  d = altitudeDifference(line(0, 0, 10, 0), line(0, 0, 10, 3.5), 3.0);
  EXPECT_EQ(d.pairs, 0u); EXPECT_FALSE(std::isfinite(d.median));
  EXPECT_EQ(altitudeDifference({}, line(0, 0, 10, 0), 3.0).pairs, 0u);
  EXPECT_EQ(altitudeDifference(line(0, 0, 10, 0), {}, 3.0).pairs, 0u);
  // partners in the neighbouring cells on the negative side are found (x and y)
  { std::vector<BoreasPoseRow> a3 = line(0, 0, 1, 0), b3; a3[0].east = 0.5; a3[0].north = 0.5; BoreasPoseRow r; r.east = -1.0; r.north = 0.5; r.up = 2.0; b3.push_back(r);
    EXPECT_EQ(altitudeDifference(a3, b3, 3.0).pairs, 1u); b3[0].east = 0.5; b3[0].north = -1.0; EXPECT_EQ(altitudeDifference(a3, b3, 3.0).pairs, 1u); EXPECT_NEAR(altitudeDifference(a3, b3, 3.0).median, 2.0, 1e-12); }
  // the nearest row decides: B holds two rows near the A row, 0.5 m away at +1 m height and 2 m away at +5 m
  std::vector<BoreasPoseRow> a1 = line(0, 0, 1, 0), b1;
  { BoreasPoseRow r; r.east = 0.5; r.north = 0; r.up = 1.0; b1.push_back(r); r.east = 2.0; r.up = 5.0; b1.push_back(r); }
  EXPECT_NEAR(altitudeDifference(a1, b1, 3.0).median, 1.0, 1e-12);
  // spread: half the road is 0.1 higher, half 0.9 higher -> p10 0.1, p90 0.9, median at the step
  std::vector<BoreasPoseRow> a2 = line(0, 0, 100, 0), b2 = line(0, 0, 100, 0.5);
  for (int i = 0; i < 100; ++i) b2[i].up = i < 50 ? 0.1 : 0.9;
  d = altitudeDifference(a2, b2, 3.0);
  EXPECT_NEAR(d.p10, 0.1, 1e-12); EXPECT_NEAR(d.p90, 0.9, 1e-12); EXPECT_GE(d.median, 0.1); EXPECT_LE(d.median, 0.9);
}

TEST(P3Boreas, DeskewPlacesAStaticPointWhereItIs) {
  // the sensor drives along +x at 10 m/s; a wall point at world (30, 5, 1) is seen at relative times -0.05, 0 and +0.05
  PoseTrack3 tr;
  for (int i = 0; i <= 200; ++i) { Pose3 p; p.t = Eigen::Vector3d(10.0 * (0.005 * i), 0, 0); tr.add(100.0 + 0.005 * i, p); }
  const double frame = 100.5;
  BoreasScan sc; sc.points = 3;
  for (double dt : {-0.05, 0.0, 0.05}) {
    const double sx = 10.0 * (frame + dt - 100.0);
    sc.xyz.insert(sc.xyz.end(), {float(30.0 - sx), 5.f, 1.f}); sc.intensity.push_back(1); sc.ring.push_back(1); sc.t.push_back(float(dt));
  }
  Pose3 T; T.t = Eigen::Vector3d(0, 0, 0.13);
  Pose3 common; common.t = Eigen::Vector3d(1000, 0, 0);
  BoreasConvertOptions co; BoreasConvertReport rep; Frame fr;
  ASSERT_TRUE(convertBoreasScan(sc, frame, tr, T, common, co, 7, fr, rep));
  EXPECT_EQ(fr.group, 7u); ASSERT_EQ(fr.size(), 3u);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_NEAR(fr.xyz[3 * i], 1030.0, 1e-3); EXPECT_NEAR(fr.xyz[3 * i + 1], 5.0, 1e-4); EXPECT_NEAR(fr.xyz[3 * i + 2], 1.13, 1e-4);
  }
  EXPECT_NEAR((fr.origin - Eigen::Vector3d(1005, 0, 0.13)).norm(), 0.0, 1e-9);        // the lidar position at the frame time, in the common frame
  EXPECT_EQ(rep.points_used, 3u);
  // dt is added to every time: a static point seen with the sensor 0.1 s later is 1 m farther along the drive
  { BoreasConvertOptions c2; c2.dt = 0.1; Frame f3; BoreasConvertReport r3;
    ASSERT_TRUE(convertBoreasScan(sc, frame, tr, T, common, c2, 0, f3, r3));
    EXPECT_NEAR(f3.xyz[0], 1031.0, 1e-3);
    EXPECT_NEAR(f3.origin.x(), 1006.0, 1e-9); }
  // without per-point time the same sweep smears by 10 m/s * 0.1 s = 1 m: the test would notice a deskew that did nothing
  BoreasScan flat = sc; for (auto& t : flat.t) t = 0.f;
  Frame f2; BoreasConvertReport r2;
  ASSERT_TRUE(convertBoreasScan(flat, frame, tr, T, common, co, 0, f2, r2));
  EXPECT_NEAR(f2.xyz[0] - f2.xyz[6], 1.0, 1e-3);
}

TEST(P3Boreas, ConvertRotatesThePointsByThePose) {
  PoseTrack3 tr;
  for (int i = 0; i <= 100; ++i) { Pose3 p; p.q = Eigen::Quaterniond(Eigen::AngleAxisd(M_PI / 2, Eigen::Vector3d::UnitZ())); p.t = Eigen::Vector3d(5, 0, 0); tr.add(100.0 + 0.01 * i, p); }
  BoreasScan sc; sc.points = 1; sc.xyz = {4.f, 0.f, 0.f}; sc.intensity = {1}; sc.ring = {1}; sc.t = {0.f};
  BoreasConvertOptions co; BoreasConvertReport rep; Frame fr;
  ASSERT_TRUE(convertBoreasScan(sc, 100.5, tr, Pose3{}, Pose3{}, co, 0, fr, rep));
  EXPECT_NEAR(fr.xyz[0], 5.0, 1e-5); EXPECT_NEAR(fr.xyz[1], 4.0, 1e-5);          // x axis of the sensor points along world y
}

TEST(P3Boreas, ConvertRangeWindowTimeOffsetAndMissingPose) {
  PoseTrack3 tr;
  for (int i = 0; i <= 100; ++i) { Pose3 p; tr.add(100.0 + 0.01 * i, p); }
  BoreasScan sc; sc.points = 5;
  for (float r : {2.999f, 3.0f, 49.999f, 50.0f, 50.001f}) { sc.xyz.insert(sc.xyz.end(), {r, 0.f, 0.f}); sc.intensity.push_back(1); sc.ring.push_back(1); sc.t.push_back(0.f); }
  BoreasConvertOptions co; BoreasConvertReport rep; Frame fr;
  ASSERT_TRUE(convertBoreasScan(sc, 100.5, tr, Pose3{}, Pose3{}, co, 0, fr, rep));
  EXPECT_EQ(fr.size(), 3u);                                                           // 3.0, 49.999 and 50.0 stay: the window is [3, 50]
  EXPECT_EQ(rep.points_out_of_range, 2u);
  // no pose at the frame time: the scan is dropped and counted
  BoreasConvertReport r2; Frame f2;
  EXPECT_FALSE(convertBoreasScan(sc, 99.0, tr, Pose3{}, Pose3{}, co, 0, f2, r2));
  EXPECT_EQ(r2.scans_dropped_no_pose, 1u);
  // a point whose own time is outside the track is dropped, the scan stays
  BoreasScan late = sc; late.t[2] = 10.f;
  BoreasConvertReport r3; Frame f3;
  ASSERT_TRUE(convertBoreasScan(late, 100.5, tr, Pose3{}, Pose3{}, co, 0, f3, r3));
  EXPECT_EQ(r3.points_no_pose, 1u); EXPECT_EQ(f3.size(), 2u);
  // dt shifts every time; a gap wider than max_gap_s is not bridged
  PoseTrack3 gappy; gappy.add(100.0, Pose3{}); gappy.add(100.2, Pose3{});
  BoreasConvertReport r4; Frame f4;
  EXPECT_FALSE(convertBoreasScan(sc, 100.1, gappy, Pose3{}, Pose3{}, co, 0, f4, r4));
}

TEST(P3Boreas, FrameTimeFromName) {
  EXPECT_DOUBLE_EQ(boreasFrameTime("1606417543533737.bin"), 1606417543.533737);
  EXPECT_DOUBLE_EQ(boreasFrameTime("/a/b/1606417543533737.bin"), 1606417543.533737);
  EXPECT_TRUE(std::isnan(boreasFrameTime("1606417543533737.pcd")));
  EXPECT_TRUE(std::isnan(boreasFrameTime("12ab.bin")));
  EXPECT_TRUE(std::isnan(boreasFrameTime(".bin")));
}

TEST(P3Boreas, TileOverlap) {
  std::vector<Eigen::Vector2d> a, b;
  for (int i = 0; i < 100; ++i) a.emplace_back(i * 1.0, 0.5);                       // tiles 0..9 in x
  for (int i = 30; i < 130; ++i) b.emplace_back(i * 1.0, 0.5);                      // tiles 3..12
  const auto ha = tileHistogram(a, 10.0), hb = tileHistogram(b, 10.0);
  EXPECT_EQ(ha.size(), 10u); EXPECT_EQ(hb.size(), 10u); EXPECT_EQ(ha.at({0, 0}), 10u);
  const TileOverlap o = tileOverlap(ha, hb);
  EXPECT_EQ(o.shared_exact, 7u);
  EXPECT_DOUBLE_EQ(o.share_a_in_b, 0.7); EXPECT_DOUBLE_EQ(o.share_b_in_a, 0.7);
  EXPECT_DOUBLE_EQ(o.share_a_near_b, 0.8);                                          // a's tiles 2..9 have a b tile within one tile
  EXPECT_DOUBLE_EQ(o.share_b_near_a, 0.8);
  // different sizes: the shares divide by the size of the set they describe
  std::vector<Eigen::Vector2d> small;
  for (int i = 0; i < 40; ++i) small.emplace_back(i * 1.0, 0.5);                    // tiles 0..3, all inside a
  const TileOverlap os = tileOverlap(ha, tileHistogram(small, 10.0));
  EXPECT_DOUBLE_EQ(os.share_a_in_b, 0.4); EXPECT_DOUBLE_EQ(os.share_b_in_a, 1.0);
  EXPECT_DOUBLE_EQ(os.share_a_near_b, 0.5); EXPECT_DOUBLE_EQ(os.share_b_near_a, 1.0);
  // negative coordinates floor correctly: -0.1 is tile -1
  const auto hn = tileHistogram({Eigen::Vector2d(-0.1, -0.1), Eigen::Vector2d(0.1, 0.1)}, 10.0);
  EXPECT_EQ(hn.size(), 2u); EXPECT_TRUE(hn.count({-1, -1}));
  // sessions in raw local frames with different origins do not overlap
  std::vector<Eigen::Vector2d> far;
  for (int i = 0; i < 100; ++i) far.emplace_back(5000 + i, 0.5);
  EXPECT_EQ(tileOverlap(ha, tileHistogram(far, 10.0)).shared_exact, 0u);
  EXPECT_DOUBLE_EQ(tileOverlap({}, hb).share_a_in_b, 0.0);
}

// ----------------------------------------------------------------------------- the sprt arm ----------

TEST(P3Sprt, WallGraduatesInFourWindowsAndClutterIsRejectedOrUndecided) {
  if (!engineHasSprt()) GTEST_SKIP() << "no sprt engine in this build";
  strata_core::LayeredMap m(toParams(armSpec("sprt"), 1, false));
  strata_core::LayeredMap probe(toParams(armSpec("sprt"), 1, false));
  const CellId wall = 1, mover = 2, flicker = 3;
  for (int w = 0; w < 4; ++w) {
    m.observeHit(wall); m.tick();
    EXPECT_EQ(m.isStatic(wall), w == 3) << "window " << w + 1;                       // m0 = 0.2, m1 = 0.7, alpha = beta = 0.01: 4 windows (PREREG.md)
  }
  EXPECT_EQ(verdictOf(m, wall, false), Verdict::Static);
  // a mover hit once, then missed: rejected (erased) once it has n >= 24 touched windows, not before. A later miss would create a
  // new history for the voxel (a new Transient cell), so nothing observes the cell after window 24 here.
  strata_core::LayeredMap r(toParams(armSpec("sprt"), 1, false));
  for (int w = 0; w < 23; ++w) { if (w == 0) r.observeHit(mover); else r.observeMiss(mover); r.tick(); }
  EXPECT_EQ(verdictOf(r, mover, false), Verdict::Undecided);                          // 23 touched windows: the gate is not open
  r.observeMiss(mover); r.tick();
  EXPECT_EQ(verdictOf(r, mover, false), Verdict::Rejected);
  EXPECT_EQ(r.classify(mover), strata_core::CellClass::Unknown);
  r.observeMiss(mover); r.tick();                                                     // a ray through the erased voxel starts a new history
  EXPECT_EQ(verdictOf(r, mover, false), Verdict::Undecided);
  // a cell hit at the zero-drift rate m* = 0.4391 never reaches either threshold: Undecided, and Intermittent after 80 windows
  strata_core::LayeredMap u(toParams(armSpec("sprt"), 1, false));
  const double mstar = 0.4391263739;
  for (int w = 0; w < 79; ++w) { if (std::floor((w + 1) * mstar) > std::floor(w * mstar)) u.observeHit(flicker); else u.observeMiss(flicker); u.tick(); }
  EXPECT_EQ(u.classify(flicker), strata_core::CellClass::Transient);
  EXPECT_EQ(verdictOf(u, flicker, false), Verdict::Undecided);
  for (int w = 79; w < 100; ++w) { if (std::floor((w + 1) * mstar) > std::floor(w * mstar)) u.observeHit(flicker); else u.observeMiss(flicker); u.tick(); }
  EXPECT_EQ(u.classify(flicker), strata_core::CellClass::Intermittent);
  EXPECT_EQ(verdictOf(u, flicker, false), Verdict::Undecided);                      // Intermittent is still undecided, not rejected
  (void)probe;
}

TEST(P3Sprt, DriverRecordsTheGraduationWindow) {
  if (!engineHasSprt()) GTEST_SKIP() << "no sprt engine in this build";
  strata_core::GridMeta g; g.width = g.height = 50; g.resolution = 0.1;
  strata_core::Grid2DBackend b(g, toParams(armSpec("sprt"), 1, false));
  LabelMap watch; const CellId c = strata_core::gridCellId(g, 10, 10); watch[c] = 1;
  TallyObserver tally(&watch, true);
  b.setWindowObserver(&tally);
  int k = 0;
  drive(b, [&](Frame& f) { if (k >= 8) return false; f = Frame(); f.group = k++; f.xyz = {1.05f, 1.05f, 0.f}; return true; }, &g, &tally);
  EXPECT_EQ(tally.tally().at(c).first_static, 4);
  EXPECT_EQ(tally.tally().at(c).first_touch, 1);
  EXPECT_EQ(tally.tally().at(c).n, 8u);
}

// ------------------------------------------------------------------- the command line tool ----------
// ---- a tiny ROS 1 bag writer (uncompressed chunk) for the lil-validate tests ----
std::string le32(std::uint32_t v) { return std::string(reinterpret_cast<const char*>(&v), 4); }
template <class T> std::string rawBytes(T v) { return std::string(reinterpret_cast<const char*>(&v), sizeof v); }
std::string bagField(const std::string& name, const std::string& value) { return le32(static_cast<std::uint32_t>(name.size() + 1 + value.size())) + name + "=" + value; }
std::string bagRec(const std::string& header, const std::string& data) { return le32(header.size()) + header + le32(data.size()) + data; }
std::string rosStr(const std::string& s) { return le32(s.size()) + s; }
std::string laserBytes(double stamp, std::size_t beams) {
  const std::uint32_t sec = static_cast<std::uint32_t>(stamp), nsec = static_cast<std::uint32_t>((stamp - sec) * 1e9);
  std::string b = le32(7) + le32(sec) + le32(nsec) + rosStr("scan_front_link");
  for (float v : {-2.3562f, 2.3557f, 0.005817f, 0.000062f, 0.0667f, 0.05f, 100.0f}) b += rawBytes(v);
  b += le32(beams);
  for (std::size_t i = 0; i < beams; ++i) b += rawBytes(3.0f);
  b += le32(0);
  return b;
}
std::string tfBytes(const std::string& parent, const std::string& child, double x, double y, double qz, double qw) {
  std::string b = le32(1) + le32(1) + le32(5) + le32(0) + rosStr(parent) + rosStr(child);
  for (double v : {x, y, 0.0, 0.0, 0.0, qz, qw}) b += rawBytes(v);
  return b;
}
std::string connRecord(std::uint32_t id, const std::string& topic, const std::string& type) {
  return bagRec(bagField("op", std::string(1, '\x07')) + bagField("conn", le32(id)) + bagField("topic", topic),
                bagField("topic", topic) + bagField("type", type) + bagField("md5sum", "x") + bagField("message_definition", "x"));
}
std::string msgRecord(std::uint32_t id, double stamp, const std::string& data) {
  const std::uint32_t sec = static_cast<std::uint32_t>(stamp), nsec = static_cast<std::uint32_t>((stamp - sec) * 1e9);
  return bagRec(bagField("op", std::string(1, '\x02')) + bagField("conn", le32(id)) + bagField("time", le32(sec) + le32(nsec)), data);
}
// pairs scan pairs, front at 100 + 0.0667 k, rear at front + rear_lag; sensor extrinsics of the calibration sequences
void writeLilBag(const std::string& path, int pairs, double rear_lag, std::size_t beams = 811) {
  std::string chunk = connRecord(0, "/laser_scan_front/scan", "sensor_msgs/LaserScan") + connRecord(1, "/laser_scan_rear/scan", "sensor_msgs/LaserScan") +
                      connRecord(2, "/tf", "tf2_msgs/TFMessage");
  chunk += msgRecord(2, 99.9, tfBytes("base_link", "scan_front_link", 0.3046, -0.0060, 0.0048, 0.99999));
  chunk += msgRecord(2, 99.9, tfBytes("base_link", "scan_rear_link", -0.3027, -0.0016, 1.0, -0.0027));
  for (int k = 0; k < pairs; ++k) {
    const double f = 100.0 + 0.0667 * k;
    chunk += msgRecord(0, f, laserBytes(f, beams));
    chunk += msgRecord(1, f + rear_lag, laserBytes(f + rear_lag, beams));
  }
  std::string out = "#ROSBAG V2.0\n";
  out += bagRec(bagField("op", std::string(1, '\x03')) + bagField("index_pos", rawBytes<std::uint64_t>(0)) + bagField("conn_count", le32(3)) + bagField("chunk_count", le32(1)), std::string(40, ' '));
  out += bagRec(bagField("op", std::string(1, '\x05')) + bagField("compression", "none") + bagField("size", le32(chunk.size())), chunk);
  std::ofstream(path, std::ios::binary) << out;
}
void writeGt(const std::string& path, double t0, double t1) {
  std::ofstream g(path);
  g.precision(15);
  for (double t = t0; t <= t1 + 1e-9; t += 0.05) g << t << " " << (t - 100.0) * 0.5 << " 0 0 0 0 0 1\n";
}

TEST(P3Probe, LilValidateOnASyntheticBag) {
  writeLilBag(tmpPath("v1.bag"), 12, 0.017);
  writeGt(tmpPath("v1_gt.txt"), 100.2, 101.2);                   // starts after the first three pairs
  std::string out;
  EXPECT_EQ(runProbe("lil-validate --bag " + tmpPath("v1.bag") + " --gt " + tmpPath("v1_gt.txt") + " --n 10 --time-offset 0", &out), 0) << out;
  EXPECT_NE(out.find("VERDICT lilocbench reader OK"), std::string::npos);
  EXPECT_NE(out.find("pairs_with_a_scan_before_the_first_gt_pose=3"), std::string::npos) << out;
  EXPECT_NE(out.find("beams=811/811"), std::string::npos);
  // the offset moves the time used for the pose: with +0.1 s one more pair is before the track start... and with -0.1 s one fewer
  EXPECT_EQ(runProbe("lil-validate --bag " + tmpPath("v1.bag") + " --gt " + tmpPath("v1_gt.txt") + " --n 10 --time-offset -0.1", &out), 0) << out;
  EXPECT_NE(out.find("pairs_with_a_scan_before_the_first_gt_pose=5"), std::string::npos) << out;
  // a track that ends inside the first 10 pairs: scans dropped although the ground truth should cover them
  writeGt(tmpPath("v1_gt2.txt"), 100.0, 100.3);
  EXPECT_EQ(runProbe("lil-validate --bag " + tmpPath("v1.bag") + " --gt " + tmpPath("v1_gt2.txt") + " --n 10 --time-offset 0", &out), 3) << out;
  EXPECT_NE(out.find("a scan was dropped although the ground truth covers it"), std::string::npos);
  // a front/rear skew of 0.06 s is above the 0.05 s bound
  writeLilBag(tmpPath("v2.bag"), 12, 0.06);
  writeGt(tmpPath("v2_gt.txt"), 99.0, 102.0);
  EXPECT_EQ(runProbe("lil-validate --bag " + tmpPath("v2.bag") + " --gt " + tmpPath("v2_gt.txt") + " --n 10 --time-offset 0", &out), 3) << out;
  EXPECT_NE(out.find("skew above 0.05 s"), std::string::npos);
  // a different beam count is not the sensor of the calibration sequences
  writeLilBag(tmpPath("v3.bag"), 12, 0.017, 400);
  EXPECT_EQ(runProbe("lil-validate --bag " + tmpPath("v3.bag") + " --gt " + tmpPath("v2_gt.txt") + " --n 10 --time-offset 0", &out), 3) << out;
  EXPECT_NE(out.find("not 811 beams"), std::string::npos);
  // fewer pairs than asked for, and a missing file
  EXPECT_EQ(runProbe("lil-validate --bag " + tmpPath("v1.bag") + " --gt " + tmpPath("v1_gt.txt") + " --n 40 --time-offset 0", &out), 3) << out;
  EXPECT_NE(runProbe("lil-validate --bag " + tmpPath("nope.bag") + " --gt " + tmpPath("v1_gt.txt"), &out), 0);
}



TEST(P3Probe, ArmsListsFiveArmsAndTheExtras) {
  std::string out;
  EXPECT_EQ(runProbe("arms", &out), 0);
  for (const auto& n : armNames()) EXPECT_NE(out.find(describe(armSpec(n))), std::string::npos) << n;
  for (const auto& n : extraArmNames()) EXPECT_NE(out.find(describe(armSpec(n))), std::string::npos) << n;
  EXPECT_NE(out.find(std::string("engine_has_sprt=") + (engineHasSprt() ? "1" : "0")), std::string::npos);
}

TEST(P3Probe, KthValidateAcceptsAFrameAndRejectsBrokenOnes) {
  const std::string dir = tmpPath("kv1");
  makeDir(dir); makeDir(dir + "/pcd");
  std::mt19937 g(3); std::uniform_real_distribution<float> u(-1.f, 1.f);
  std::vector<float> xyz;
  for (int i = 0; i < 3000; ++i) { const float az = u(g) * 3.14159f, el = u(g) * 0.4f, r = 5.f + 10.f * std::fabs(u(g)); xyz.insert(xyz.end(), {r * std::cos(el) * std::cos(az), r * std::cos(el) * std::sin(az), r * std::sin(el)}); }
  const double vp[7] = {0, 0, 0, 1, 0, 0, 0};
  writePcd(dir + "/pcd/000000.pcd", xyz, nullptr, vp);
  std::string out;
  // the validator reads intensity; without it the frame is rejected by the reader (fail loudly)
  EXPECT_NE(runProbe("kth-validate --dir " + dir, &out), 0);
  std::vector<float> inten(xyz.size() / 3, 1.f);
  writePcd(dir + "/pcd/000000.pcd", xyz, &inten, vp);
  EXPECT_EQ(runProbe("kth-validate --dir " + dir + " --n 1", &out), 0) << out;
  EXPECT_NE(out.find("VERDICT kth reader OK"), std::string::npos);
  const double vp2[7] = {0, 0, 0, 0.5, 0, 0, 0};                                      // quaternion not unit
  writePcd(dir + "/pcd/000000.pcd", xyz, &inten, vp2);
  EXPECT_EQ(runProbe("kth-validate --dir " + dir + " --n 1", &out), 3);
  for (float scale : {0.01f, 12.f}) {                                                  // median range 0.1 m and 120 m: outside 0.5 m to 60 m, everything else fine
    std::vector<float> sc = xyz; for (auto& v : sc) v *= scale;
    writePcd(dir + "/pcd/000000.pcd", sc, &inten, vp);
    EXPECT_EQ(runProbe("kth-validate --dir " + dir + " --n 1", &out), 3) << scale << " " << out;
    EXPECT_NE(out.find("median sensor range outside"), std::string::npos);
  }
  const double vp3[7] = {500, 0, 0, 1, 0, 0, 0};                                      // viewpoint far from the points: not a world-frame cloud around it
  writePcd(dir + "/pcd/000000.pcd", xyz, &inten, vp3);
  EXPECT_EQ(runProbe("kth-validate --dir " + dir + " --n 1", &out), 3);
}

TEST(P3Probe, KthExportCommandWritesMapsAndMeta) {
  const std::string dir = makeKthSequence("kcli", 12);
  std::string out;
  EXPECT_EQ(runProbe("kth-export --dir " + dir + " --arm shipped_v0_1 --res 0.2 --window 2 --periodicity 0 --max-range 50 --policies static_only,all --out-prefix " + tmpPath("kcli_out") + " --meta " + tmpPath("kcli.meta"), &out), 0) << out;
  const auto m = readMeta(tmpPath("kcli.meta"));
  EXPECT_EQ(m.at("frames"), "12"); EXPECT_EQ(m.at("kept_static_only"), "24"); EXPECT_EQ(m.at("kept_all"), "26"); EXPECT_EQ(m.at("points_out_of_range"), "12");
  EXPECT_EQ(readPcd(m.at("out_static_only")).hdr.points, 24u);
  EXPECT_NE(runProbe("kth-export --dir " + dir + " --arm bogus --res 0.2 --window 2 --out-prefix x --meta y", &out), 0);
  EXPECT_NE(runProbe("kth-export --dir " + dir + " --arm shipped_v0_1 --res 0.2 --window 2 --policies nonsense --out-prefix x --meta y", &out), 0);
  if (!engineHasSprt()) EXPECT_NE(runProbe("kth-export --dir " + dir + " --arm sprt --res 0.2 --window 2 --out-prefix x --meta y", &out), 0);
}

TEST(P3Probe, ArmEvalCellsAndCounts) {
  strata_core::GridMeta g; g.width = g.height = 100; g.resolution = 0.1; g.origin_x = g.origin_y = -5;
  std::vector<Frame> frames;
  for (int k = 0; k < 30; ++k) {
    Frame f; f.group = k; f.origin = Eigen::Vector3d(0, 0, 0);
    f.xyz = {1.05f, 0.05f, 0.f};                                                      // wall: hit every step (10 windows of 3 steps)
    if (k % 3 == 0 && k >= 6 && k <= 12) { f.xyz.push_back(-1.95f); f.xyz.push_back(0.05f); f.xyz.push_back(0.f); }    // clutter A: hit in windows 3, 4, 5
    if (k % 3 == 0 && k <= 3) { f.xyz.push_back(-1.95f); f.xyz.push_back(1.05f); f.xyz.push_back(0.f); }    // clutter B: hit in windows 1, 2
    frames.push_back(f);
  }
  writeCache(tmpPath("c2.bin"), 2, frames);
  const CellId wall = strata_core::gridCellId(g, 60, 50), ca = strata_core::gridCellId(g, 30, 50), cb = strata_core::gridCellId(g, 30, 60), none = strata_core::gridCellId(g, 5, 5);
  LabelMap labels; labels[wall] = 1; labels[ca] = 2; labels[cb] = 2; labels[none] = 3;
  writeLabels(tmpPath("l2.bin"), 2, labels);
  std::string out;
  const std::string base = "arm-eval --cache " + tmpPath("c2.bin") + " --labels " + tmpPath("l2.bin") + " --arm shipped_v0_1 --res 0.1 --window 3 --periodicity 0 --nmin 3 --grid-origin -5 --grid-size 10";
  ASSERT_EQ(runProbe(base + " --out " + tmpPath("d2.csv") + " --meta " + tmpPath("d2.meta"), &out), 0) << out;
  std::istringstream is(readText(tmpPath("d2.csv"))); std::string line; std::vector<std::string> rows;
  std::getline(is, line);
  EXPECT_EQ(line, "label,ix,iy,iz,n,h,verdict,first_static,first_touch");
  while (std::getline(is, line)) rows.push_back(line);
  // ids are sorted: gy * 100 + gx -> (60,50)=5060, (30,50)=5030, (30,60)=6030, (5,5)=505
  ASSERT_EQ(rows.size(), 4u);
  EXPECT_EQ(rows[0], "3,5,5,0,0,0,rejected,-1,-1");
  EXPECT_EQ(rows[1], "2,30,50,0,3,3,static,5,3");                                     // first touched in window 3, graduates in window 5
  EXPECT_EQ(rows[2], "1,60,50,0,10,10,static,3,1");
  EXPECT_EQ(rows[3], "2,30,60,0,2,2,undecided,-1,1");                                   // two hit windows: not enough observations
  const auto m = readMeta(tmpPath("d2.meta"));
  EXPECT_EQ(m.at("windows_closed"), "10"); EXPECT_EQ(m.at("labelled_cells"), "4"); EXPECT_EQ(m.at("dim"), "2");
  // counts format: label, eligible (n >= nmin = 3), verdict
  ASSERT_EQ(runProbe(base + " --format counts --out " + tmpPath("c2.csv") + " --meta " + tmpPath("c2.meta"), &out), 0) << out;
  EXPECT_EQ(readText(tmpPath("c2.csv")),
            "label,eligible,verdict,count\n"
            "1,1,static,1\n"
            "2,0,undecided,1\n"                                                       // n = 2 < 3: not eligible
            "2,1,static,1\n"                                                          // n = 3 == nmin: eligible
            "3,0,rejected,1\n");
  EXPECT_EQ(readText(tmpPath("c2.csv.latency")), "label,windows_to_static,count\n1,3,1\n2,3,1\n");
  // no latency tracking: first_static stays -1
  ASSERT_EQ(runProbe(base + " --latency 0 --out " + tmpPath("d3.csv") + " --meta " + tmpPath("d3.meta"), &out), 0) << out;
  EXPECT_NE(readText(tmpPath("d3.csv")).find("1,60,50,0,10,10,static,-1,1"), std::string::npos);
  // dimension mismatch between cache and labels fails
  writeLabels(tmpPath("l3.bin"), 3, labels);
  EXPECT_NE(runProbe("arm-eval --cache " + tmpPath("c2.bin") + " --labels " + tmpPath("l3.bin") + " --arm shipped_v0_1 --res 0.1 --window 3 --out x --meta y", &out), 0);
  EXPECT_NE(runProbe(base + " --format nonsense --out " + tmpPath("d4.csv") + " --meta " + tmpPath("d4.meta"), &out), 0);
}

TEST(P3Probe, RefExportDecodesGridCells) {
  strata_core::GridMeta g; g.width = g.height = 100; g.resolution = 0.1; g.origin_x = g.origin_y = -5;
  LabelMap labels; labels[strata_core::gridCellId(g, 60, 50)] = 1; labels[strata_core::gridCellId(g, 3, 99)] = 3;
  writeLabels(tmpPath("l4.bin"), 2, labels);
  std::string out;
  ASSERT_EQ(runProbe("ref-export --labels " + tmpPath("l4.bin") + " --res 0.1 --grid-origin -5 --grid-size 10 --out " + tmpPath("l4.csv"), &out), 0) << out;
  EXPECT_EQ(readText(tmpPath("l4.csv")), "label,ix,iy\n1,60,50\n3,3,99\n");
}

TEST(P3Probe, StaticExportIndependentAndCumulative) {
  // a wall seen from two "sessions"; session 2 sees a second wall. independent: the second set has both walls only if session 2 saw both
  auto writeSession = [&](const std::string& name, bool second_wall) {
    std::vector<Frame> frames;
    for (int k = 0; k < 20; ++k) {
      Frame f; f.group = k; f.origin = Eigen::Vector3d(0, 0, 0);
      f.xyz = {10.05f, 0.05f, 0.05f};
      if (second_wall) { f.xyz.push_back(0.05f); f.xyz.push_back(15.05f); f.xyz.push_back(0.05f); }
      frames.push_back(f);
    }
    writeCache(tmpPath(name), 3, frames);
  };
  writeSession("s1.bin", false); writeSession("s2.bin", true);
  {  // session 3 sees only the second wall: independent maps differ from the cumulative one
    std::vector<Frame> frames;
    for (int k = 0; k < 20; ++k) { Frame f; f.group = k; f.xyz = {0.05f, 15.05f, 0.05f}; frames.push_back(f); }
    writeCache(tmpPath("s3.bin"), 3, frames);
  }
  std::string out;
  const std::string caches = tmpPath("s1.bin") + "," + tmpPath("s2.bin");
  ASSERT_EQ(runProbe("static-export --caches " + caches + " --arm shipped_v0_1 --res 0.2 --window 2 --periodicity 0 --mode independent --out-prefix " + tmpPath("ind") + " --meta " + tmpPath("ind.meta"), &out), 0) << out;
  auto m = readMeta(tmpPath("ind.meta"));
  EXPECT_EQ(m.at("session0_static_cells"), "1"); EXPECT_EQ(m.at("session1_static_cells"), "2");
  EXPECT_EQ(m.at("session1_windows_closed"), "10");                                   // a fresh map: the window count restarts
  ASSERT_EQ(runProbe("static-export --caches " + caches + " --arm shipped_v0_1 --res 0.2 --window 2 --periodicity 0 --mode cumulative --out-prefix " + tmpPath("cum") + " --meta " + tmpPath("cum.meta"), &out), 0) << out;
  m = readMeta(tmpPath("cum.meta"));
  EXPECT_EQ(m.at("session0_static_cells"), "1"); EXPECT_EQ(m.at("session1_static_cells"), "2");
  EXPECT_EQ(m.at("session1_windows_closed"), "10");
  {
    const std::string two = tmpPath("s1.bin") + "," + tmpPath("s3.bin");
    ASSERT_EQ(runProbe("static-export --caches " + two + " --arm shipped_v0_1 --res 0.2 --window 2 --periodicity 0 --mode independent --out-prefix " + tmpPath("ind2") + " --meta " + tmpPath("ind2.meta"), &out), 0) << out;
    EXPECT_EQ(readMeta(tmpPath("ind2.meta")).at("session1_static_cells"), "1");              // only the second wall
    ASSERT_EQ(runProbe("static-export --caches " + two + " --arm shipped_v0_1 --res 0.2 --window 2 --periodicity 0 --mode cumulative --out-prefix " + tmpPath("cum2") + " --meta " + tmpPath("cum2.meta"), &out), 0) << out;
    EXPECT_EQ(readMeta(tmpPath("cum2.meta")).at("session1_static_cells"), "2");              // the first wall is kept from session 1
  }
  // the id file: int64 little endian, sorted; decode the voxel of the first wall
  std::ifstream f(tmpPath("ind_s0_static.ids"), std::ios::binary);
  std::int64_t id = 0; f.read(reinterpret_cast<char*>(&id), 8);
  int ix, iy, iz; decodeVoxel(id, ix, iy, iz);
  EXPECT_EQ(ix, 50); EXPECT_EQ(iy, 0); EXPECT_EQ(iz, 0);
  f.read(reinterpret_cast<char*>(&id), 8);
  EXPECT_FALSE(f.good());                                                             // exactly one id
  EXPECT_NE(runProbe("static-export --caches " + caches + " --arm shipped_v0_1 --res 0.2 --window 2 --mode sideways --out-prefix x --meta y", &out), 0);
  EXPECT_NE(runProbe("static-export --caches " + tmpPath("c2.bin") + " --arm shipped_v0_1 --res 0.2 --window 2 --out-prefix x --meta y", &out), 0);   // a 2D cache
}

// Synthetic sessions with 12 frames each, a straight drive at 10 m/s east, as in the real files (review finding 1): the easting and northing of
// EVERY session are in one shared frame, whose origin (lat0, lon_ref) is the first row of the reference session. A session starts at
// (east0, north0) of that frame and its GPS rows carry latitude and longitude consistent with the shared-frame coordinates. lon_scale
// distorts the longitude (an inconsistent file); alt_offset raises the altitude columns; gps_time_shift moves the GPS rows in time (no row in the window); own_frame_error_m writes the files as if
// measured from the session's own first row (the premise of the old common frame): files in (east - east0, north - north0).
std::string makeBoreasSession(const std::string& id, double lat0, double lon_ref, double t0_us, double speed = 10.0, double lp_shift = 0.0, double lon_scale = 1.0,
                              double east0 = 0.0, double north0 = 0.0, double alt_offset = 0.0, bool own_frame_files = false, double gps_time_shift = 0.0) {
  const double e2 = kWgs84F * (2 - kWgs84F), s = std::sin(lat0), N = kWgs84A / std::sqrt(1 - e2 * s * s);
  const double M = kWgs84A * (1 - e2) / std::pow(1 - e2 * s * s, 1.5);
  const std::string d = tmpPath(id);
  makeDir(d); makeDir(d + "/lidar"); makeDir(d + "/applanix"); makeDir(d + "/calib");
  writeText(d + "/calib/T_applanix_lidar.txt", "1 0 0 0\n0 1 0 0\n0 0 1 0.13\n");
  const double tstart = t0_us * 1e-6 - 1.0;
  const double fe = own_frame_files ? east0 : 0.0, fn = own_frame_files ? north0 : 0.0;          // what to subtract from the shared-frame coordinates in the files
  std::string gps = "GPSTime,easting,northing,altitude,vel_east,vel_north,vel_up,roll,pitch,heading,angvel_z,angvel_y,angvel_x,accelz,accely,accelx,latitude,longitude\n";
  std::string lp = "GPSTime,easting,northing,altitude,vel_east,vel_north,vel_up,roll,pitch,heading,angvel_z,angvel_y,angvel_x\n";
  for (int i = 0; i < 1000; ++i) {                                                  // 200 Hz GPS
    const double x = speed * 0.005 * i;
    std::ostringstream l; l.precision(15);
    l << tstart + gps_time_shift + 0.005 * i << "," << east0 + x - fe << "," << north0 - fn << "," << alt_offset << ",10,0,0,0,0,0,0,0,0,0,0,0," << lat0 + north0 / M << ","
      << lon_ref + lon_scale * (east0 + x) / (N * std::cos(lat0)) << "\n";
    gps += l.str();
  }
  for (int k = 0; k < 12; ++k) {
    const long long us = static_cast<long long>(t0_us) + 100000LL * k;
    char nm[64]; std::snprintf(nm, sizeof nm, "%lld.bin", us);
    std::vector<std::array<float, 6>> pts;
    std::mt19937 g(k + 1); std::uniform_real_distribution<float> u(-1.f, 1.f);
    for (int i = 0; i < 4000; ++i) pts.push_back({u(g) * 30.f, u(g) * 30.f, u(g) * 2.f, float(i % 200), float(i % 128), -0.05f + 0.1f * float(i) / 4000.f});
    const auto b = binOf(pts);
    std::ofstream(d + "/lidar/" + nm, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), b.size());
    std::ostringstream l; l.precision(15);
    l << us << "," << east0 - fe + speed * (us * 1e-6 - tstart) + lp_shift << "," << north0 - fn << "," << 0.13 + alt_offset << ",10,0,0,0,0,0,0,0,0\n";
    lp += l.str();
  }
  writeText(d + "/applanix/gps_post_process.csv", gps);
  writeText(d + "/applanix/lidar_poses.csv", lp);
  return d;
}

TEST(P3Probe, TileGroundGivesThePercentileHeightPerTile) {
  // one frame at the origin: 700 points at z = 0.33 in tile (0, 0) within 25 m, 30 points at z = 6 in the same tile (4.1 %: below the 5th
  // percentile), 600 points at z = -1.23 in tile (-1, 0), 800 points beyond 25 m (ignored), 100 points in tile (0, 1) (too few)
  Frame f; f.group = 0; f.origin = Eigen::Vector3d(0, 0, 2);
  std::mt19937 g(4); std::uniform_real_distribution<float> u(0.5f, 9.5f);
  for (int i = 0; i < 700; ++i) f.xyz.insert(f.xyz.end(), {u(g), u(g), 0.33f});
  for (int i = 0; i < 30; ++i) f.xyz.insert(f.xyz.end(), {u(g), 2.f, 6.0f});
  for (int i = 0; i < 600; ++i) f.xyz.insert(f.xyz.end(), {-u(g), 2.f, -1.23f});
  for (int i = 0; i < 800; ++i) f.xyz.insert(f.xyz.end(), {30.f + u(g), 2.f, 0.0f});
  for (int i = 0; i < 100; ++i) f.xyz.insert(f.xyz.end(), {u(g), 10.f + u(g) * 0.5f, 0.33f});
  writeCache(tmpPath("tg.bin"), 3, {f});
  std::string out;
  ASSERT_EQ(runProbe("tile-ground --cache " + tmpPath("tg.bin") + " --out " + tmpPath("tg.csv"), &out), 0) << out;
  EXPECT_EQ(readText(tmpPath("tg.csv")), "tx,ty,count,ground_z\n-1,0,600,-1.25\n0,0,730,0.35\n");
  EXPECT_NE(out.find("points_used=1430"), std::string::npos) << out;
  ASSERT_EQ(runProbe("tile-ground --cache " + tmpPath("tg.bin") + " --out " + tmpPath("tg2.csv") + " --min-points 50 --radius 100", &out), 0) << out;
  EXPECT_NE(readText(tmpPath("tg2.csv")).find("0,1,100,0.35"), std::string::npos);
  EXPECT_NE(readText(tmpPath("tg2.csv")).find("3,0,800,"), std::string::npos);
  {  // a tile with exactly min-points returns is kept; the 5th percentile follows the low part of a 30/70 mix
    Frame g2; g2.group = 0; g2.origin = Eigen::Vector3d(0, 0, 2);
    for (int i = 0; i < 30; ++i) g2.xyz.insert(g2.xyz.end(), {u(g), u(g), -1.23f});
    for (int i = 0; i < 70; ++i) g2.xyz.insert(g2.xyz.end(), {u(g), u(g), 0.33f});
    writeCache(tmpPath("tg4.bin"), 3, {g2});
    ASSERT_EQ(runProbe("tile-ground --cache " + tmpPath("tg4.bin") + " --out " + tmpPath("tg4.csv") + " --min-points 100", &out), 0) << out;
    EXPECT_EQ(readText(tmpPath("tg4.csv")), "tx,ty,count,ground_z\n0,0,100,-1.25\n");
    ASSERT_EQ(runProbe("tile-ground --cache " + tmpPath("tg4.bin") + " --out " + tmpPath("tg5.csv") + " --min-points 101", &out), 0) << out;
    EXPECT_EQ(readText(tmpPath("tg5.csv")), "tx,ty,count,ground_z\n");
  }
  EXPECT_NE(runProbe("tile-ground --cache " + tmpPath("c2.bin") + " --out " + tmpPath("tg3.csv"), &out), 0);                  // a 2D cache
}

TEST(P3Probe, BoreasValidateOverlapAndConvertOnSyntheticSessions) {
  const double lat0 = 0.7641426285, lon0 = -1.3869452891;
  // A is the reference session (first row = the shared origin). B starts 1000 m east in the shared frame; C starts at (-4.302, -0.149)
  // of the shared frame as the real 2021-04-08 session does, and is otherwise the same drive as A.
  const std::string a = makeBoreasSession("bsA", lat0, lon0, 1606417543533737.0);
  const std::string b = makeBoreasSession("bsB", lat0, lon0, 1606417543533737.0, 10.0, 0.0, 1.0, 1000.0);
  const std::string c = makeBoreasSession("bsC", lat0, lon0, 1606417543533737.0, 10.0, 0.0, 1.0, -4.302, -0.149);
  std::string out;
  EXPECT_EQ(runProbe("boreas-validate --seq " + a + " --ref " + a + " --n 5", &out), 0) << out;
  EXPECT_NE(out.find("VERDICT boreas reader OK"), std::string::npos);
  EXPECT_NE(out.find("layout_ok"), std::string::npos);
  EXPECT_NE(out.find("enu_from_latlon_vs_files"), std::string::npos);
  // the validator checks a session that does NOT start at the reference origin, against the reference origin (the gap of the old validator)
  EXPECT_EQ(runProbe("boreas-validate --seq " + c + " --ref " + a + " --n 5", &out), 0) << out;
  EXPECT_NE(out.find("first_row: file east,north=(-4.302,-0.149)"), std::string::npos) << out;
  EXPECT_EQ(runProbe("boreas-validate --seq " + b + " --ref " + a + " --n 5", &out), 0) << out;
  EXPECT_NE(runProbe("boreas-validate --seq " + a + " --n 5", &out), 0);                  // --ref is required: no silent self-reference
  EXPECT_NE(runProbe("boreas-validate --seq " + a + " --ref " + tmpPath("nope") + " --n 5", &out), 0);
  // B is 1000 m east of A in the shared frame: the overlap gate fails on the tiles, not on the frame check
  EXPECT_EQ(runProbe("boreas-overlap --seqs " + a + "," + b, &out), 3) << out;
  EXPECT_NE(out.find("VERDICT boreas shared frame and tile overlap FAIL"), std::string::npos);
  EXPECT_NE(out.find("file,bsA,bsB,"), std::string::npos) << out;
  EXPECT_NE(out.find("tile overlap: FAIL"), std::string::npos);
  EXPECT_NE(out.find("enu agreement of every session: ok"), std::string::npos);
  EXPECT_EQ(out.find("raw_local"), std::string::npos);                                      // one frame only
  EXPECT_NE(out.find("enu_check,bsB,rows="), std::string::npos) << out;
  EXPECT_EQ(runProbe("boreas-overlap --seqs " + a + "," + a, &out), 0) << out;
  EXPECT_NE(out.find("VERDICT boreas shared frame and tile overlap PASS"), std::string::npos);
  EXPECT_NE(out.find("file,bsA,bsA,2,2,"), std::string::npos) << out;               // the tiles come from lidar_poses.csv (x from 10 m to 21 m: tiles 1 and 2), not from the longer GPS track
  // A and C start 4.3 m apart: the same road, the gate passes, and the frame check passes for C although C's first row is not (0, 0)
  EXPECT_EQ(runProbe("boreas-overlap --seqs " + a + "," + c, &out), 0) << out;
  EXPECT_NE(out.find("enu_check,bsC,rows="), std::string::npos);
  EXPECT_NE(out.find("first_row_east,north=(-4.302,-0.149)"), std::string::npos) << out;
  EXPECT_EQ(out.find("FAIL"), std::string::npos) << out;
  EXPECT_EQ(runProbe("boreas-overlap --seqs " + a, &out), 1);                         // one sequence is not an overlap
  // conversion: no session is moved. A into its own frame; C keeps its shared-frame position (-4.302 + 10 m at the first frame, not shifted by 4.3 m again).
  ASSERT_EQ(runProbe("boreas-convert --seq " + a + " --ref " + a + " --out " + tmpPath("bsA.cache") + " --meta " + tmpPath("bsA.meta"), &out), 0) << out;
  auto m = readMeta(tmpPath("bsA.meta"));
  EXPECT_EQ(m.at("frames_written"), "12"); EXPECT_EQ(m.at("scans_dropped_no_pose"), "0");
  EXPECT_EQ(std::stod(m.at("to_common_t_x")), 0.0); EXPECT_EQ(std::stod(m.at("to_common_t_y")), 0.0); EXPECT_EQ(std::stod(m.at("to_common_t_z")), 0.0);
  EXPECT_LT(std::stod(m.at("enu_worst_horizontal_diff_m")), 0.01); EXPECT_GT(std::stoi(m.at("enu_rows_checked")), 100);
  ASSERT_EQ(runProbe("boreas-convert --seq " + c + " --ref " + a + " --out " + tmpPath("bsC.cache") + " --meta " + tmpPath("bsC.meta") + " --max-frames 3", &out), 0) << out;
  m = readMeta(tmpPath("bsC.meta"));
  EXPECT_EQ(m.at("frames_written"), "3");
  EXPECT_EQ(std::stod(m.at("to_common_t_x")), 0.0); EXPECT_EQ(std::stod(m.at("to_common_t_y")), 0.0);
  const auto fa = readCache(tmpPath("bsA.cache")), fc = readCache(tmpPath("bsC.cache"));
  ASSERT_EQ(fa.size(), 12u); ASSERT_EQ(fc.size(), 3u);
  EXPECT_NEAR(fa[0].origin.x(), 10.0, 1e-3);                                          // the car is 1 s x 10 m/s from the start of its track at the first frame
  EXPECT_NEAR(fc[0].origin.x() - fa[0].origin.x(), -4.302, 1e-3);                     // the real offset of the starts, once
  EXPECT_NEAR(fc[0].origin.y() - fa[0].origin.y(), -0.149, 1e-3);
  for (std::size_t i = 0; i < fa[0].xyz.size(); i += 3) {                              // every point within [3, 50] m of the sensor, in the common frame
    const double r = (Eigen::Vector3d(fa[0].xyz[i], fa[0].xyz[i + 1], fa[0].xyz[i + 2]) - fa[0].origin).norm();
    ASSERT_LE(r, 50.2);
    ASSERT_GE(r, 2.8);
  }
  // a vertical offset moves z only
  ASSERT_EQ(runProbe("boreas-convert --seq " + a + " --ref " + a + " --z-offset 0.25 --max-frames 2 --out " + tmpPath("bsAz.cache") + " --meta " + tmpPath("bsAz.meta"), &out), 0) << out;
  EXPECT_EQ(std::stod(readMeta(tmpPath("bsAz.meta")).at("to_common_t_z")), 0.25);
  const auto fz = readCache(tmpPath("bsAz.cache"));
  EXPECT_NEAR(fz[0].origin.z() - fa[0].origin.z(), 0.25, 1e-9); EXPECT_NEAR(fz[0].origin.x() - fa[0].origin.x(), 0.0, 1e-9);
  // converting A against B's origin: A's files do not agree with the geodetic ENU at B's origin (B's first row is 1000 m east) -> refused
  EXPECT_NE(runProbe("boreas-convert --seq " + a + " --ref " + b + " --out " + tmpPath("bsAB.cache") + " --meta " + tmpPath("bsAB.meta") + " --max-frames 3", &out), 0);
  EXPECT_NE(out.find("not in one frame"), std::string::npos) << out;
  // every second frame: frames 0, 2, 4, ... and groups renumbered
  ASSERT_EQ(runProbe("boreas-convert --seq " + a + " --ref " + a + " --frame-step 2 --out " + tmpPath("bsA2.cache") + " --meta " + tmpPath("bsA2.meta"), &out), 0) << out;
  EXPECT_EQ(readMeta(tmpPath("bsA2.meta")).at("frames_written"), "6");
  EXPECT_EQ(readMeta(tmpPath("bsA2.meta")).at("frame_step"), "2");
  const auto f2 = readCache(tmpPath("bsA2.cache"));
  ASSERT_EQ(f2.size(), 6u);
  EXPECT_NEAR(f2[1].origin.x(), fa[2].origin.x(), 1e-9); EXPECT_EQ(f2[1].group, 1u);
  EXPECT_NE(runProbe("boreas-convert --seq " + a + " --ref " + a + " --frame-step 0 --out x --meta y", &out), 0);
  EXPECT_NE(runProbe("boreas-convert --seq " + a + " --ref " + tmpPath("nope") + " --out x --meta y", &out), 0);
  // broken layout: a frame of the wrong size is reported
  { std::ofstream f(a + "/lidar/1606417543533737.bin", std::ios::binary | std::ios::trunc); f << std::string(1000, 'x'); }
  EXPECT_NE(runProbe("boreas-validate --seq " + a + " --ref " + a + " --n 1", &out), 0);
}

TEST(P3Probe, BoreasGateAndValidatorFailures) {
  const double lat0 = 0.7641426285, lon0 = -1.3869452891;
  const std::string a = makeBoreasSession("bsGA", lat0, lon0, 1606417543533737.0);
  const std::string c = makeBoreasSession("bsGC", lat0, lon0, 1606417543533737.0, 20.0);          // twice as fast: a longer track from the same start
  std::string out;
  // A lies inside C, C does not lie inside A: one direction below 0.8 is enough to fail
  EXPECT_EQ(runProbe("boreas-overlap --seqs " + a + "," + c, &out), 3) << out;
  EXPECT_EQ(runProbe("boreas-overlap --seqs " + c + "," + a, &out), 3) << out;
  // lidar_poses.csv 0.5 m away from the GPS pose: the validator refuses the session
  const std::string d = makeBoreasSession("bsGD", lat0, lon0, 1606417543533737.0, 10.0, 0.5);
  EXPECT_EQ(runProbe("boreas-validate --seq " + d + " --ref " + a + " --n 5", &out), 3) << out;
  EXPECT_NE(out.find("lidar_poses.csv by more than 0.10 m"), std::string::npos);
  // latitude and longitude that do not match east and north
  const std::string e = makeBoreasSession("bsGE", lat0, lon0, 1606417543533737.0, 10.0, 0.0, 3.0);
  EXPECT_EQ(runProbe("boreas-validate --seq " + e + " --ref " + a + " --n 5", &out), 3) << out;
  EXPECT_NE(out.find("geodetic ENU"), std::string::npos);
  EXPECT_EQ(runProbe("boreas-validate --seq " + a + " --ref " + a + " --n 5", &out), 0) << out;
  // Review finding 1: a session whose files are measured from its own first row (4.302 m, 0.149 m from the shared origin) is NOT in the shared frame.
  // The old validator looked only at the reference session and missed this; now every session is checked against the reference origin.
  const std::string o = makeBoreasSession("bsGO", lat0, lon0, 1606417543533737.0, 10.0, 0.0, 1.0, -4.302, -0.149, 0.0, true);
  EXPECT_EQ(runProbe("boreas-validate --seq " + o + " --ref " + a + " --n 5", &out), 3) << out;
  EXPECT_NE(out.find("reference origin by more than 0.10 m"), std::string::npos) << out;
  EXPECT_EQ(runProbe("boreas-overlap --seqs " + a + "," + o, &out), 3) << out;
  EXPECT_NE(out.find("enu_check,bsGO,"), std::string::npos);
  EXPECT_NE(out.find(",FAIL\n"), std::string::npos) << out;
  EXPECT_NE(out.find("enu agreement of every session: FAIL"), std::string::npos) << out;
  EXPECT_NE(out.find("tile overlap: ok"), std::string::npos) << out;                          // the tiles alone would have passed: the frame check is what refuses
  EXPECT_NE(runProbe("boreas-convert --seq " + o + " --ref " + a + " --out " + tmpPath("bsGO.cache") + " --meta " + tmpPath("bsGO.meta"), &out), 0);
  EXPECT_NE(out.find("not in one frame"), std::string::npos) << out;
  // altitude columns on another datum are reported, not refused
  // no GPS row in the window of the frames: the frame check has nothing to check and fails (it does not pass on an empty set)
  const std::string t = makeBoreasSession("bsGT", lat0, lon0, 1606417543533737.0, 10.0, 0.0, 1.0, 0.0, 0.0, 0.0, false, 100.0);
  EXPECT_EQ(runProbe("boreas-overlap --seqs " + a + "," + t, &out), 3) << out;
  EXPECT_NE(out.find("enu_check,bsGT,rows=0,"), std::string::npos) << out;
  EXPECT_NE(out.find("enu agreement of every session: FAIL"), std::string::npos) << out;
  const std::string h = makeBoreasSession("bsGH", lat0, lon0, 1606417543533737.0, 10.0, 0.0, 1.0, 0.0, 0.0, 0.5);
  EXPECT_EQ(runProbe("boreas-overlap --seqs " + a + "," + h, &out), 0) << out;
  EXPECT_NE(out.find("altitude,bsGA,bsGH,12,0.5,0.5,0.5"), std::string::npos) << out;
}

TEST(P3Probe, SpiresValidateOnASyntheticZip) {
  // a stored zip with two PCD scans that carry x y z intensity timestamp ring, and a GT track around them
  auto put16 = [](std::string& s, std::uint32_t v) { s.push_back(char(v & 255)); s.push_back(char((v >> 8) & 255)); };
  auto put32 = [&](std::string& s, std::uint32_t v) { put16(s, v & 0xFFFF); put16(s, v >> 16); };
  auto pcd = [&](double t) {
    std::ostringstream h;
    const int n = 2000;
    h << "# .PCD v0.7\nVERSION 0.7\nFIELDS x y z intensity timestamp ring\nSIZE 4 4 4 4 8 2\nTYPE F F F F F U\nCOUNT 1 1 1 1 1 1\nWIDTH " << n << "\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS " << n << "\nDATA binary\n";
    std::string s = h.str();
    std::mt19937 g(1); std::uniform_real_distribution<float> u(-1.f, 1.f);
    for (int i = 0; i < n; ++i) {
      float v[4] = {u(g) * 15.f, u(g) * 15.f, u(g) * 2.f, 1.f}; double ts = t + 0.1 * i / n; std::uint16_t ring = i % 64;
      s.append(reinterpret_cast<char*>(v), 16); s.append(reinterpret_cast<char*>(&ts), 8); s.append(reinterpret_cast<char*>(&ring), 2);
    }
    return s;
  };
  std::string body, cd;
  const std::vector<std::pair<std::string, std::string>> members = {{"lidar-clouds/1710255656.000000000.pcd", pcd(1710255656.0)}, {"lidar-clouds/1710255656.100000000.pcd", pcd(1710255656.1)}};
  for (const auto& m : members) {
    const std::uint32_t crc = crc32(0, reinterpret_cast<const Bytef*>(m.second.data()), m.second.size()), off = body.size();
    put32(body, 0x04034b50); put16(body, 20); put16(body, 0); put16(body, 0); put16(body, 0); put16(body, 0); put32(body, crc);
    put32(body, m.second.size()); put32(body, m.second.size()); put16(body, m.first.size()); put16(body, 0); body += m.first; body += m.second;
    put32(cd, 0x02014b50); put16(cd, 20); put16(cd, 20); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0); put32(cd, crc);
    put32(cd, m.second.size()); put32(cd, m.second.size()); put16(cd, m.first.size()); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0); put32(cd, 0); put32(cd, off); cd += m.first;
  }
  std::string eocd;
  put32(eocd, 0x06054b50); put16(eocd, 0); put16(eocd, 0); put16(eocd, 2); put16(eocd, 2); put32(eocd, cd.size()); put32(eocd, body.size()); put16(eocd, 0);
  { std::ofstream o(tmpPath("sp.zip"), std::ios::binary); o << body << cd << eocd; }
  std::string tum;
  for (int i = 0; i <= 40; ++i) { std::ostringstream l; l.precision(15); l << 1710255655.0 + 0.05 * i << " 0 0 0 0 0 0 1\n"; tum += l.str(); }
  writeText(tmpPath("sp_gt.txt"), tum);
  std::string out;
  EXPECT_EQ(runProbe("spires-validate --zip " + tmpPath("sp.zip") + " --gt " + tmpPath("sp_gt.txt") + " --n 2", &out), 0) << out;
  EXPECT_NE(out.find("VERDICT spires reader OK"), std::string::npos);
  EXPECT_NE(out.find("members=2"), std::string::npos);
  // a GT track that starts after the first scan: that scan has no pose by the data (reported), the second has one: the reader is fine
  std::string lategt;
  for (int i = 0; i <= 40; ++i) { std::ostringstream l; l.precision(15); l << 1710255656.06 + 0.05 * i << " 0 0 0 0 0 0 1\n"; lategt += l.str(); }
  writeText(tmpPath("sp_gt3.txt"), lategt);
  EXPECT_EQ(runProbe("spires-validate --zip " + tmpPath("sp.zip") + " --gt " + tmpPath("sp_gt3.txt") + " --n 2", &out), 0) << out;
  EXPECT_NE(out.find("(before the first GT pose)"), std::string::npos);
  EXPECT_NE(out.find("scans_before_first_gt_pose=1"), std::string::npos);
  // a GT track that ends before the scans: pose missing, the validator fails
  std::string shortgt;
  for (int i = 0; i <= 10; ++i) { std::ostringstream l; l.precision(15); l << 1710255655.0 + 0.05 * i << " 0 0 0 0 0 0 1\n"; shortgt += l.str(); }
  writeText(tmpPath("sp_gt2.txt"), shortgt);
  EXPECT_EQ(runProbe("spires-validate --zip " + tmpPath("sp.zip") + " --gt " + tmpPath("sp_gt2.txt") + " --n 2", &out), 3) << out;
}

}  // namespace
