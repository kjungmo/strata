#include "strata/mapping_node.hpp"
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
namespace strata {
strata_core::Observation cloudToObservation(
    const sensor_msgs::msg::PointCloud2& msg, const strata_core::Pose3D& sensor_to_map) {
  pcl::PointCloud<pcl::PointXYZ> cloud; pcl::fromROSMsg(msg, cloud);
  strata_core::Observation obs; obs.hits.reserve(cloud.size());
  for (const auto& p : cloud) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
    obs.hits.push_back(sensor_to_map * Eigen::Vector3d(p.x, p.y, p.z));
  }
  return obs;
}
}  // namespace strata
