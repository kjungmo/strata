#include "strata/mapping_node.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <fstream>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl_conversions/pcl_conversions.h>
#include <tf2/exceptions.h>
#include <tf2/time.h>

namespace strata {

MappingNode::MappingNode(const rclcpp::NodeOptions& options)
    : rclcpp::Node("strata", options) {
  backend_ = declare_parameter<std::string>("backend", "grid2d");
  global_frame_ = declare_parameter<std::string>("global_frame", "map");
  save_path_ = declare_parameter<std::string>("save_path", "/tmp/strata");
  const double publish_period = declare_parameter<double>("publish_period", 1.0);

  const strata_core::LayeredMapParams lp = readLayerParams();
  layer_interval_ = lp.layer_interval;
  period_windows_ = lp.periodicity.period_windows;
  // Windows close every layer_interval integrated messages ("scans", the engine's own
  // rule) or every window_period_s of message time ("time"). Scan-counted windows
  // stretch when messages are dropped, which detunes period_windows; time windows do not.
  window_mode_ = declare_parameter<std::string>("window_mode", "scans");
  window_period_s_ = declare_parameter<double>("window_period_s", 1.0);
  expected_rate_hz_ = declare_parameter<double>("expected_scan_rate_hz", 0.0);
  drop_warn_ = declare_parameter<double>("rate_warn_drop_fraction", 0.05);
  if (window_mode_ != "scans" && window_mode_ != "time")
    throw std::invalid_argument("window_mode must be \"scans\" or \"time\", got \"" + window_mode_ + "\"");
  if (window_mode_ == "time" && !(window_period_s_ > 0.0))
    throw std::invalid_argument("window_period_s must be > 0 with window_mode \"time\"");
  clock_ = std::make_unique<WindowClock>(window_period_s_);
  rate_ = std::make_unique<RateMonitor>(expected_rate_hz_);
  diag_pub_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>("/diagnostics", 10);

  tf_buffer_ = std::make_unique<tf2_ros::Buffer>(get_clock());
  tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

  if (backend_ == "grid2d") {
    strata_core::GridMeta meta;
    meta.width = declare_parameter<int>("grid_width", 400);
    meta.height = declare_parameter<int>("grid_height", 400);
    meta.resolution = declare_parameter<double>("grid_resolution", 0.05);
    meta.origin_x = declare_parameter<double>("grid_origin_x", -10.0);
    meta.origin_y = declare_parameter<double>("grid_origin_y", -10.0);
    grid_ = std::make_unique<strata_core::Grid2DBackend>(meta, lp);
    map_ = grid_.get();
    const auto map_qos = rclcpp::QoS(1).transient_local().reliable();
    grid_pub_ = create_publisher<nav_msgs::msg::OccupancyGrid>("~/map", map_qos);
    scan_sub_ = create_subscription<sensor_msgs::msg::LaserScan>(
        declare_parameter<std::string>("scan_topic", "/scan"), rclcpp::SensorDataQoS(),
        std::bind(&MappingNode::onScan, this, std::placeholders::_1));
  } else {  // voxel3d
    const double voxel_size = declare_parameter<double>("voxel_size", 0.2);
    voxel_ = std::make_unique<strata_core::Voxel3DBackend>(voxel_size, lp);
    map_ = voxel_.get();
    cloud_pub_ = create_publisher<sensor_msgs::msg::PointCloud2>("~/map_points", rclcpp::QoS(1));
    points_sub_ = create_subscription<sensor_msgs::msg::PointCloud2>(
        declare_parameter<std::string>("points_topic", "/points"), rclcpp::SensorDataQoS(),
        std::bind(&MappingNode::onPoints, this, std::placeholders::_1));
  }

  save_srv_ = create_service<std_srvs::srv::Trigger>(
      "~/save_map", std::bind(&MappingNode::onSave, this,
                              std::placeholders::_1, std::placeholders::_2));

  pub_timer_ = create_wall_timer(
      std::chrono::duration<double>(publish_period),
      std::bind(&MappingNode::onPublish, this));

  RCLCPP_INFO(get_logger(), "strata up: backend=%s frame=%s window_mode=%s", backend_.c_str(),
              global_frame_.c_str(), window_mode_.c_str());
}

strata_core::LayeredMapParams MappingNode::readLayerParams() {
  strata_core::LayeredMapParams p;
  p.layer_interval     = declare_parameter<int>("layer_interval", 10);
  p.l_hit              = declare_parameter<double>("l_hit", 0.85);
  p.l_miss             = declare_parameter<double>("l_miss", -0.4);
  p.l_min              = declare_parameter<double>("l_min", -5.0);
  p.l_max              = declare_parameter<double>("l_max", 5.0);
  p.survival_decay     = declare_parameter<double>("survival_decay", 0.97);
  p.graduate_prob      = declare_parameter<double>("graduate_prob", 0.8);
  p.demote_prob        = declare_parameter<double>("demote_prob", 0.45);
  p.min_observations   = declare_parameter<int>("min_observations", 3);
  p.prune_prob         = declare_parameter<double>("prune_prob", 0.05);
  p.enable_periodicity = declare_parameter<bool>("enable_periodicity", true);
  p.periodic_amplitude_min = declare_parameter<double>("periodic_amplitude_min", 0.3);
  p.periodic_false_alarm   = declare_parameter<double>("periodic_false_alarm", 0.2);
  p.periodic_alpha_spending = declare_parameter<bool>("periodic_alpha_spending", true);
  p.periodicity.period_windows = declare_parameter<int>("period_windows", 24);
  p.periodicity.n_harmonics    = declare_parameter<int>("n_harmonics", 2);
  return p;
}

void MappingNode::onScan(const sensor_msgs::msg::LaserScan::SharedPtr msg) {
  geometry_msgs::msg::TransformStamped tf;
  try {
    tf = tf_buffer_->lookupTransform(global_frame_, msg->header.frame_id,
                                     msg->header.stamp, tf2::durationFromSec(0.1));
  } catch (const tf2::TransformException& e) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "scan TF: %s", e.what());
    return;
  }
  const strata_core::Pose3D iso = tf2::transformToEigen(tf);
  Eigen::Vector3d origin;
  auto obs = scanToObservation(*msg, iso, origin);
  const double t = rclcpp::Time(msg->header.stamp).seconds();
  std::lock_guard<std::mutex> lk(mtx_);
  beforeIntegrate(t);
  grid_->integrate(obs, origin);
  afterIntegrate(t);
}

void MappingNode::onPoints(const sensor_msgs::msg::PointCloud2::SharedPtr msg) {
  geometry_msgs::msg::TransformStamped tf;
  try {
    tf = tf_buffer_->lookupTransform(global_frame_, msg->header.frame_id,
                                     msg->header.stamp, tf2::durationFromSec(0.1));
  } catch (const tf2::TransformException& e) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "points TF: %s", e.what());
    return;
  }
  const strata_core::Pose3D iso = tf2::transformToEigen(tf);
  auto obs = cloudToObservation(*msg, iso);
  const double t = rclcpp::Time(msg->header.stamp).seconds();
  std::lock_guard<std::mutex> lk(mtx_);
  beforeIntegrate(t);
  voxel_->integrate(obs, iso.translation());
  afterIntegrate(t);
}

void MappingNode::beforeIntegrate(double t) {
  if (window_mode_ != "time") return;
  // A message past the open window's end closes it (and any silent windows since)
  // before it is integrated into the window it belongs to.
  const int k = clock_->advance(t);
  if (k == WindowClock::kReset) {
    RCLCPP_WARN(get_logger(), "message time jumped back more than one window (bag loop or "
                "clock reset): time windows re-anchored at t=%.3f", t);
  } else if (k > 0) {
    map_->closeWindows(k);
    reportWindow();
  }
}

void MappingNode::afterIntegrate(double t) {
  rate_->record(t);
  if (window_mode_ == "scans" && map_->tick()) reportWindow();
}

void MappingNode::reportWindow() {
  const RateMonitor::WindowStats s = rate_->closeWindow();
  const double drop = rate_->recentDropFraction();
  const double mean = rate_->meanWindowDuration();
  const double cv = rate_->windowDurationCv();
  const bool time_mode = window_mode_ == "time";
  const double effective_period = period_windows_ * (time_mode ? window_period_s_ : mean);
  std::string why;
  if (rate_->windowsSeen() >= 3) {
    char buf[200];
    if (drop > drop_warn_) {
      std::snprintf(buf, sizeof(buf), "%.0f%% of input messages lost%s; ", 100.0 * drop,
                    time_mode ? " (time windows keep the period)"
                              : " (scan-counted windows stretch, period_windows is detuned)");
      why += buf;
    }
    if (!time_mode && cv > 0.2) {
      std::snprintf(buf, sizeof(buf), "window duration varies (cv %.2f), so the periodic test's "
                    "period in seconds varies; ", cv);
      why += buf;
    }
    if (!time_mode && expected_rate_hz_ > 0.0 && mean > 0.0) {
      const double expected = layer_interval_ / expected_rate_hz_;
      if (std::fabs(mean - expected) > 0.1 * expected) {
        std::snprintf(buf, sizeof(buf), "mean window %.3f s differs from layer_interval / "
                      "expected_scan_rate_hz = %.3f s; ", mean, expected);
        why += buf;
      }
    }
  }
  diagnostic_msgs::msg::DiagnosticArray arr;
  arr.header.stamp = now();
  diagnostic_msgs::msg::DiagnosticStatus st;
  st.name = "strata: input rate";
  st.hardware_id = backend_;
  st.level = why.empty() ? diagnostic_msgs::msg::DiagnosticStatus::OK
                         : diagnostic_msgs::msg::DiagnosticStatus::WARN;
  st.message = why.empty() ? "ok" : why.substr(0, why.size() - 2);
  auto kv = [&st](const std::string& k, const std::string& v) {
    diagnostic_msgs::msg::KeyValue p; p.key = k; p.value = v; st.values.push_back(p);
  };
  auto num = [](double v) { char b[32]; std::snprintf(b, sizeof(b), "%.4g", v); return std::string(b); };
  kv("window_mode", window_mode_);
  kv("scans_in_window", std::to_string(s.scans));
  kv("window_duration_s", num(s.duration_s));
  kv("input_rate_hz", num(s.rate_hz));
  kv("nominal_rate_hz", num(s.nominal_interval_s > 0.0 ? 1.0 / s.nominal_interval_s : 0.0));
  kv("dropped_in_window", std::to_string(s.dropped));
  kv("recent_drop_fraction", num(drop));
  kv("mean_window_duration_s", num(mean));
  kv("window_duration_cv", num(cv));
  kv("effective_period_s", num(effective_period));
  arr.status.push_back(st);
  diag_pub_->publish(arr);
  if (!why.empty()) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000, "input rate: %s", st.message.c_str());
  } else {
    RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 30000,
                         "input rate: %.1f Hz, window %.3f s, %.1f%% lost, periodic period %.1f s",
                         s.rate_hz, mean, 100.0 * drop, effective_period);
  }
}

void MappingNode::onPublish() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (backend_ == "grid2d") {
    if (!grid_) return;
    const strata_core::GridMap g = grid_->toOccupancyGrid();
    nav_msgs::msg::OccupancyGrid msg;
    msg.header.stamp = now();
    msg.header.frame_id = global_frame_;
    msg.info.width = g.meta.width;
    msg.info.height = g.meta.height;
    msg.info.resolution = g.meta.resolution;
    msg.info.origin.position.x = g.meta.origin_x;
    msg.info.origin.position.y = g.meta.origin_y;
    msg.info.origin.orientation.w = 1.0;
    msg.data = g.data;
    grid_pub_->publish(msg);
  } else {
    if (!voxel_) return;
    pcl::PointCloud<pcl::PointXYZ> cloud;
    for (const auto& pt : voxel_->staticPoints())
      cloud.push_back(pcl::PointXYZ(static_cast<float>(pt.x()),
                                    static_cast<float>(pt.y()),
                                    static_cast<float>(pt.z())));
    sensor_msgs::msg::PointCloud2 msg;
    pcl::toROSMsg(cloud, msg);
    msg.header.stamp = now();
    msg.header.frame_id = global_frame_;
    cloud_pub_->publish(msg);
  }
}

void MappingNode::onSave(const std::shared_ptr<std_srvs::srv::Trigger::Request> /*req*/,
                         std::shared_ptr<std_srvs::srv::Trigger::Response> res) {
  std::lock_guard<std::mutex> lk(mtx_);
  try {
    if (backend_ == "grid2d") {
      const strata_core::GridMap g = grid_->toOccupancyGrid();
      const std::string pgm = save_path_ + ".pgm";
      const std::string yaml = save_path_ + ".yaml";
      std::ofstream f(pgm, std::ios::binary);
      f << "P5\n" << g.meta.width << " " << g.meta.height << "\n255\n";
      // Map server convention: row 0 at the bottom -> write rows top-to-bottom.
      for (int row = g.meta.height - 1; row >= 0; --row) {
        for (int col = 0; col < g.meta.width; ++col) {
          const std::int8_t v = g.data[static_cast<std::size_t>(row) * g.meta.width + col];
          unsigned char px;
          if (v < 0) px = 205;            // unknown
          else if (v >= 100) px = 0;      // occupied (static)
          else if (v >= 50) px = 100;     // periodic/transient (grey)
          else px = 254;                  // free
          f.put(static_cast<char>(px));
        }
      }
      f.close();
      std::ofstream y(yaml);
      y << "image: " << pgm << "\n"
        << "resolution: " << g.meta.resolution << "\n"
        << "origin: [" << g.meta.origin_x << ", " << g.meta.origin_y << ", 0.0]\n"
        << "negate: 0\noccupied_thresh: 0.65\nfree_thresh: 0.196\n";
      y.close();
      res->success = true;
      res->message = "saved " + pgm + " + " + yaml;
    } else {
      pcl::PointCloud<pcl::PointXYZ> cloud;
      for (const auto& pt : voxel_->staticPoints())
        cloud.push_back(pcl::PointXYZ(static_cast<float>(pt.x()),
                                      static_cast<float>(pt.y()),
                                      static_cast<float>(pt.z())));
      const std::string pcd = save_path_ + ".pcd";
      if (cloud.empty()) {
        res->success = false;
        res->message = "no static voxels to save";
      } else {
        cloud.width = cloud.size();
        cloud.height = 1;
        cloud.is_dense = false;
        pcl::io::savePCDFileBinary(pcd, cloud);
        res->success = true;
        res->message = "saved " + pcd;
      }
    }
  } catch (const std::exception& e) {
    res->success = false;
    res->message = std::string("save failed: ") + e.what();
  }
  RCLCPP_INFO(get_logger(), "save_map: %s", res->message.c_str());
}

}  // namespace strata
