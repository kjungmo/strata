// The evidence-model status on /diagnostics: every row of the table in evidence_status.hpp.
#include <gtest/gtest.h>
#include <map>
#include <string>
#include "strata/evidence_status.hpp"
using strata::EvidenceStatus;
using strata::evidenceStatus;
using strata::kEvidenceProvisional;
using strata_core::EvidenceModel;
using strata_core::LayeredMapParams;

namespace {
std::map<std::string, std::string> asMap(const EvidenceStatus& s) {
  std::map<std::string, std::string> m;
  for (const auto& kv : s.values) m[kv.first] = kv.second;
  return m;
}
bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
bool contains(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }
const std::string kProv = "provisional (PREREG not frozen: G1 failed)";
}  // namespace

TEST(EvidenceStatus, TheNoteIsTheDocumentedText) { EXPECT_EQ(std::string(kEvidenceProvisional), kProv); }

TEST(EvidenceStatus, Grid2dMeasuredPairSaysProvisionalInMessageAndValue) {
  LayeredMapParams p;                       // the shipped pair
  const EvidenceStatus s = evidenceStatus(p, "sprt", "measured_2d", "grid2d");
  EXPECT_TRUE(startsWith(s.message, kProv + "; sprt: clutter m0=0.2, structure m1=0.7")) << s.message;
  EXPECT_FALSE(contains(s.message, "NOT calibrated"));
  EXPECT_EQ(asMap(s).at("evidence_calibration"), "measured_2d, " + kProv);
  EXPECT_EQ(asMap(s).at("evidence_model"), "sprt");
  EXPECT_EQ(asMap(s).at("m0"), "0.2");
  EXPECT_EQ(asMap(s).at("m1"), "0.7");
  EXPECT_EQ(asMap(s).at("alpha"), "0.01");
  EXPECT_EQ(asMap(s).at("beta"), "0.01");
  EXPECT_EQ(asMap(s).at("windows_to_graduate_always_hit"), "4");
  EXPECT_EQ(asMap(s).at("all_miss_windows_to_demote"), "5");
  EXPECT_EQ(asMap(s).at("n_max_undecided"), "80");
}

TEST(EvidenceStatus, FallbackLabelOnTheTwoDimensionalBackendIsProvisionalButNotCalledUncalibrated) {
  LayeredMapParams p;
  const EvidenceStatus s = evidenceStatus(p, "sprt", "fallback_2d_pair", "grid2d");
  EXPECT_FALSE(contains(s.message, "NOT calibrated")) << s.message;
  EXPECT_TRUE(startsWith(s.message, kProv)) << s.message;
}

TEST(EvidenceStatus, Voxel3dFallbackSaysNotCalibratedThenProvisional) {
  LayeredMapParams p;
  const EvidenceStatus s = evidenceStatus(p, "sprt", "fallback_2d_pair", "voxel3d");
  EXPECT_TRUE(startsWith(s.message, "NOT calibrated for voxel3d (fallback: the 2D pair)")) << s.message;
  EXPECT_TRUE(contains(s.message, "stays undecided; " + kProv + "; sprt: clutter")) << s.message;
  EXPECT_EQ(asMap(s).at("evidence_calibration"), "fallback_2d_pair, " + kProv);
}

TEST(EvidenceStatus, UserCalibrationIsNotCalledProvisional) {
  LayeredMapParams p; p.m0 = 0.25;
  const EvidenceStatus s = evidenceStatus(p, "sprt", "user", "grid2d");
  EXPECT_TRUE(startsWith(s.message, "sprt: clutter m0=0.25")) << s.message;
  EXPECT_FALSE(contains(s.message, "provisional"));
  EXPECT_EQ(asMap(s).at("evidence_calibration"), "user");
}

TEST(EvidenceStatus, LegacyModelStatesNoPairAndNoProvisionalNote) {
  LayeredMapParams p; p.evidence_model = EvidenceModel::Legacy;
  const EvidenceStatus s = evidenceStatus(p, "legacy", "measured_2d", "grid2d");
  EXPECT_TRUE(startsWith(s.message, "legacy (v0.1 arithmetic)")) << s.message;
  EXPECT_FALSE(contains(s.message, "provisional"));
  const auto m = asMap(s);
  EXPECT_EQ(m.at("evidence_calibration"), "measured_2d");
  EXPECT_EQ(m.count("m0"), 0u);
  EXPECT_EQ(m.at("min_observations"), std::to_string(p.min_observations));
}

TEST(EvidenceStatus, ValueOrderIsStable) {
  LayeredMapParams p;
  const EvidenceStatus s = evidenceStatus(p, "sprt", "measured_2d", "grid2d");
  ASSERT_EQ(s.values.size(), 9u);
  const char* keys[] = {"evidence_model", "m0", "m1", "alpha", "beta", "windows_to_graduate_always_hit",
                        "all_miss_windows_to_demote", "n_max_undecided", "evidence_calibration"};
  for (size_t i = 0; i < 9; ++i) EXPECT_EQ(s.values[i].first, keys[i]) << i;
}
