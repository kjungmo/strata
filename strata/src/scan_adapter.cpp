#include "strata/mapping_node.hpp"
#include <cmath>
#include "strata_core/types.hpp"
namespace strata {
strata_core::Observation scanToObservation(
    const sensor_msgs::msg::LaserScan& scan,
    const strata_core::Pose3D& sensor_to_map,
    Eigen::Vector3d& sensor_origin_out) {
  sensor_origin_out = sensor_to_map.translation();
  strata_core::Observation obs;
  obs.hits.reserve(scan.ranges.size());
  for (size_t i = 0; i < scan.ranges.size(); ++i) {
    const float r = scan.ranges[i];
    if (!std::isfinite(r) || r < scan.range_min || r > scan.range_max) continue;
    const double a = scan.angle_min + i * scan.angle_increment;
    const Eigen::Vector3d p_sensor(r * std::cos(a), r * std::sin(a), 0.0);  // beam in sensor frame
    obs.hits.push_back(sensor_to_map * p_sensor);                            // full 6-DoF -> map
  }
  return obs;
}
}  // namespace strata
