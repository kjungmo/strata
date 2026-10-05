#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <rclcpp/rclcpp.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>
#include <tf2_eigen/tf2_eigen.hpp>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <std_msgs/msg/header.hpp>
#include <std_srvs/srv/trigger.hpp>
#include "strata_core/types.hpp"
#include "strata_core/layered_map.hpp"
#include "strata_core/grid2d_backend.hpp"
#include "strata_core/voxel3d_backend.hpp"
#include "strata/rate_monitor.hpp"
#include "strata/window_clock.hpp"

namespace strata {

strata_core::Observation scanToObservation(
    const sensor_msgs::msg::LaserScan& scan,
    const strata_core::Pose3D& sensor_to_map,
    Eigen::Vector3d& sensor_origin_out);

strata_core::Observation cloudToObservation(
    const sensor_msgs::msg::PointCloud2& msg,
    const strata_core::Pose3D& sensor_to_map);

class MappingNode : public rclcpp::Node {
 public:
  explicit MappingNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

 private:
  strata_core::LayeredMapParams readLayerParams();
  void onScan(const sensor_msgs::msg::LaserScan::SharedPtr msg);
  void onPoints(const sensor_msgs::msg::PointCloud2::SharedPtr msg);
  void onPublish();
  // Window bookkeeping around one integrated message; call with mtx_ held.
  bool beforeIntegrate(std::int64_t t_ns);   // false: drop the message (zero stamp, time windows)
  void afterIntegrate(const std::string& frame, std::int64_t t_ns);
  void windowsClosed(int k);
  void noteTfFailure();
  // TF global_frame <- sensor at the header stamp, waiting up to kTfWaitS of ROS time
  // (bounded on the steady clock, see TfWaitRule).
  bool lookupSensorTf(const std_msgs::msg::Header& h, geometry_msgs::msg::TransformStamped& out,
                      std::string& err);
  static constexpr double kTfWaitS = 0.1;
  static constexpr double kClockStallS = 3.0;   // sim clock frozen this long -> WARN
  void onDiagnostics();   // 1 Hz wall timer: publishes /diagnostics even without input
  void onSave(const std::shared_ptr<std_srvs::srv::Trigger::Request> req,
              std::shared_ptr<std_srvs::srv::Trigger::Response> res);

  std::string backend_, global_frame_, save_path_;
  strata_core::MapBackend* map_{nullptr};   // the selected backend
  std::string window_mode_;                 // "scans" (layer_interval) or "time" (window_period_s)
  double window_period_s_{1.0};
  double drop_warn_{0.05};
  double expected_rate_hz_{0.0};
  int layer_interval_{10};
  int period_windows_{24};
  std::unique_ptr<WindowClock> clock_;
  std::map<std::string, RateMonitor> rates_;   // one per sensor frame on the input topic
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diag_pub_;
  rclcpp::TimerBase::SharedPtr diag_timer_;
  std::string input_topic_;
  double input_timeout_s_{5.0}, startup_timeout_s_{30.0};
  bool use_sim_time_{false};
  rclcpp::Clock steady_clock_{RCL_STEADY_TIME};   // log throttling that needs no /clock
  std::int64_t last_ros_ns_{-1};                  // ROS clock at the last diagnostics tick
  std::chrono::steady_clock::time_point ros_moved_wall_{std::chrono::steady_clock::now()};
  static constexpr std::size_t kMaxSensors = 16;   // per-frame rate monitors kept at most
  static constexpr int kIdleWindowsToForget = 10;  // a frame silent this long is dropped
  using Steady = std::chrono::steady_clock;
  Steady::time_point start_wall_, last_input_wall_, last_close_wall_, last_log_wall_,
      last_integrate_wall_, last_reset_log_wall_;
  rclcpp::Time last_close_ros_;
  bool any_integrated_{false};
  long tf_failures_since_integrate_{0}, frames_over_cap_{0};
  bool any_input_{false}, origin_set_{false};
  std::int64_t origin_ns_{0};
  long windows_closed_{0}, msgs_since_close_{0}, zero_stamps_{0};
  int tf_failures_window_{0}, tf_failures_last_{0};
  std::vector<std::pair<std::string, std::string>> last_values_;   // from the last window close
  std::string last_why_;

  std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;

  std::unique_ptr<strata_core::Grid2DBackend> grid_;
  std::unique_ptr<strata_core::Voxel3DBackend> voxel_;

  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_sub_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr points_sub_;
  rclcpp::Publisher<nav_msgs::msg::OccupancyGrid>::SharedPtr grid_pub_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_pub_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr save_srv_;
  rclcpp::TimerBase::SharedPtr pub_timer_;
  std::mutex mtx_;
};

}  // namespace strata
