#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <rclcpp/rclcpp.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>
#include <tf2_eigen/tf2_eigen.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <std_srvs/srv/trigger.hpp>
#include "prism_map_core/types.hpp"
#include "prism_map_core/layered_map.hpp"
#include "prism_map_core/grid2d_backend.hpp"
#include "prism_map_core/voxel3d_backend.hpp"

namespace prism_map {

prism_map_core::Observation scanToObservation(
    const sensor_msgs::msg::LaserScan& scan,
    const prism_map_core::Pose3D& sensor_to_map,
    Eigen::Vector3d& sensor_origin_out);

prism_map_core::Observation cloudToObservation(
    const sensor_msgs::msg::PointCloud2& msg,
    const prism_map_core::Pose3D& sensor_to_map);

class MappingNode : public rclcpp::Node {
 public:
  explicit MappingNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

 private:
  prism_map_core::LayeredMapParams readLayerParams();
  void onScan(const sensor_msgs::msg::LaserScan::SharedPtr msg);
  void onPoints(const sensor_msgs::msg::PointCloud2::SharedPtr msg);
  void onPublish();
  void onSave(const std::shared_ptr<std_srvs::srv::Trigger::Request> req,
              std::shared_ptr<std_srvs::srv::Trigger::Response> res);

  std::string backend_, global_frame_, save_path_;

  std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;

  std::unique_ptr<prism_map_core::Grid2DBackend> grid_;
  std::unique_ptr<prism_map_core::Voxel3DBackend> voxel_;

  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_sub_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr points_sub_;
  rclcpp::Publisher<nav_msgs::msg::OccupancyGrid>::SharedPtr grid_pub_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_pub_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr save_srv_;
  rclcpp::TimerBase::SharedPtr pub_timer_;
  std::mutex mtx_;
};

}  // namespace prism_map
