#include <gtest/gtest.h>
#include <cmath>
#include <sensor_msgs/msg/laser_scan.hpp>
#include "strata_core/types.hpp"
#include "strata_core/map_backend.hpp"
namespace strata {
strata_core::Observation scanToObservation(const sensor_msgs::msg::LaserScan&,
    const strata_core::Pose3D&, Eigen::Vector3d&);
}
TEST(ScanAdapter, SingleBeamUnderYawTranslation) {
  sensor_msgs::msg::LaserScan scan;
  scan.angle_min = 0.0; scan.angle_increment = M_PI/2; scan.range_min = 0.1; scan.range_max = 30.0;
  scan.ranges = {2.0};                                  // beam along sensor +x
  strata_core::Pose3D iso = strata_core::Pose3D::Identity();
  iso.translation() = Eigen::Vector3d(1.0, 0.0, 0.5);
  iso.linear() = Eigen::AngleAxisd(M_PI/2, Eigen::Vector3d::UnitZ()).toRotationMatrix();  // +x -> +y
  Eigen::Vector3d origin;
  auto obs = strata::scanToObservation(scan, iso, origin);
  ASSERT_EQ(obs.hits.size(), 1u);
  EXPECT_NEAR(obs.hits[0].x(), 1.0, 1e-6);              // x unchanged
  EXPECT_NEAR(obs.hits[0].y(), 2.0, 1e-6);              // 2m beam rotated into +y
  EXPECT_NEAR(origin.z(), 0.5, 1e-6);                   // 6-DoF origin preserved
}
