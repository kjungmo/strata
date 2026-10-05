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
  input_timeout_s_ = declare_parameter<double>("input_timeout_s", 5.0);
  clock_ = std::make_unique<WindowClock>(window_period_s_);
  diag_pub_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>("/diagnostics", 10);
  start_wall_ = last_close_wall_ = last_log_wall_ = Steady::now();

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
    input_topic_ = declare_parameter<std::string>("scan_topic", "/scan");
    scan_sub_ = create_subscription<sensor_msgs::msg::LaserScan>(
        input_topic_, rclcpp::SensorDataQoS(),
        std::bind(&MappingNode::onScan, this, std::placeholders::_1));
  } else {  // voxel3d
    const double voxel_size = declare_parameter<double>("voxel_size", 0.2);
    voxel_ = std::make_unique<strata_core::Voxel3DBackend>(voxel_size, lp);
    map_ = voxel_.get();
    cloud_pub_ = create_publisher<sensor_msgs::msg::PointCloud2>("~/map_points", rclcpp::QoS(1));
    input_topic_ = declare_parameter<std::string>("points_topic", "/points");
    points_sub_ = create_subscription<sensor_msgs::msg::PointCloud2>(
        input_topic_, rclcpp::SensorDataQoS(),
        std::bind(&MappingNode::onPoints, this, std::placeholders::_1));
  }

  save_srv_ = create_service<std_srvs::srv::Trigger>(
      "~/save_map", std::bind(&MappingNode::onSave, this,
                              std::placeholders::_1, std::placeholders::_2));

  // Wall timer, so diagnostics keep flowing (and turn ERROR) when input stops, and
  // log throttling does not depend on a /clock that a live robot may not have.
  diag_timer_ = create_wall_timer(std::chrono::seconds(1), std::bind(&MappingNode::onDiagnostics, this));

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
    noteTfFailure();
    return;
  }
  const strata_core::Pose3D iso = tf2::transformToEigen(tf);
  Eigen::Vector3d origin;
  auto obs = scanToObservation(*msg, iso, origin);
  const std::int64_t t = rclcpp::Time(msg->header.stamp).nanoseconds();
  std::lock_guard<std::mutex> lk(mtx_);
  beforeIntegrate(t);
  grid_->integrate(obs, origin);
  afterIntegrate(msg->header.frame_id, t);
}

void MappingNode::onPoints(const sensor_msgs::msg::PointCloud2::SharedPtr msg) {
  geometry_msgs::msg::TransformStamped tf;
  try {
    tf = tf_buffer_->lookupTransform(global_frame_, msg->header.frame_id,
                                     msg->header.stamp, tf2::durationFromSec(0.1));
  } catch (const tf2::TransformException& e) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "points TF: %s", e.what());
    noteTfFailure();
    return;
  }
  const strata_core::Pose3D iso = tf2::transformToEigen(tf);
  auto obs = cloudToObservation(*msg, iso);
  const std::int64_t t = rclcpp::Time(msg->header.stamp).nanoseconds();
  std::lock_guard<std::mutex> lk(mtx_);
  beforeIntegrate(t);
  voxel_->integrate(obs, iso.translation());
  afterIntegrate(msg->header.frame_id, t);
}

void MappingNode::noteTfFailure() {
  std::lock_guard<std::mutex> lk(mtx_);
  ++tf_failures_window_;
  any_input_ = true;            // the sensor is alive even if its pose is not
  last_input_wall_ = Steady::now();
}

void MappingNode::beforeIntegrate(std::int64_t t_ns) {
  any_input_ = true;
  last_input_wall_ = Steady::now();
  ++msgs_since_close_;
  if (t_ns == 0) ++zero_stamps_;
  if (!origin_set_) { origin_ns_ = t_ns; origin_set_ = true; }
  if (window_mode_ != "time") return;
  // A message past the open window's end closes it (and any silent windows since)
  // before it is integrated into the window it belongs to.
  const int k = clock_->advance(t_ns);
  if (k == WindowClock::kReset) {
    RCLCPP_WARN(get_logger(), "message time jumped back more than half a window, or ahead by "
                "more than %ld windows (bag loop, clock reset or an unset stamp): time "
                "windows re-anchored", static_cast<long>(WindowClock::kMaxGapWindows));
    origin_ns_ = t_ns;
    for (auto& kv : rates_) kv.second.reset();
    last_close_wall_ = Steady::now();
    msgs_since_close_ = 1;
  } else if (k > 0) {
    map_->closeWindows(k);
    windowsClosed(k);
  }
}

void MappingNode::afterIntegrate(const std::string& frame, std::int64_t t_ns) {
  auto it = rates_.try_emplace(frame, expected_rate_hz_).first;
  it->second.record(static_cast<double>(t_ns - origin_ns_) * 1e-9);
  if (window_mode_ == "scans" && map_->tick()) windowsClosed(1);
}

void MappingNode::windowsClosed(int k) {
  windows_closed_ += k;
  last_close_wall_ = Steady::now();
  msgs_since_close_ = 0;
  // Statistics per sensor frame (two sensors on one topic would otherwise look like
  // heavy loss); durations come from the sensor with the most messages.
  int scans = 0; long dropped = 0, recent_d = 0, recent_n = 0;
  double rate = 0.0, nominal = 0.0, duration = 0.0, mean = 0.0, cv = 0.0;
  int primary = -1; std::size_t seen = 0;
  for (auto& kv : rates_) {
    const RateMonitor::WindowStats s = kv.second.closeWindow();
    scans += s.scans; dropped += s.dropped; rate += s.rate_hz;
    if (s.nominal_interval_s > 0.0) nominal += 1.0 / s.nominal_interval_s;
    recent_d += kv.second.recentDropped(); recent_n += kv.second.recentScans();
    if (s.scans > primary) {
      primary = s.scans; duration = s.duration_s; mean = kv.second.meanWindowDuration();
      cv = kv.second.windowDurationCv(); seen = kv.second.windowsSeen();
    }
  }
  const double drop = (recent_d + recent_n) > 0 ? double(recent_d) / double(recent_d + recent_n) : 0.0;
  const bool time_mode = window_mode_ == "time";
  const double effective_period = period_windows_ * (time_mode ? window_period_s_ : mean);
  std::string why;
  char buf[240];
  if (seen >= 3) {
    if (time_mode) {
      // Time windows keep the period under loss; what hurts them is thin evidence.
      if (k > 1) {
        std::snprintf(buf, sizeof(buf), "%d window(s) saw no message; ", k - 1);
        why += buf;
      }
      const double expected = nominal * window_period_s_;
      if (expected >= 2.0 && scans < 0.5 * expected) {
        std::snprintf(buf, sizeof(buf), "window had %d messages, under half the %.0f expected, so "
                      "per-window evidence is thin; ", scans, expected);
        why += buf;
      }
    } else {
      if (drop > drop_warn_) {
        std::snprintf(buf, sizeof(buf), "%.0f%% of input messages lost: scan-counted windows "
                      "stretch, so period_windows is detuned (window_mode \"time\" avoids it); ",
                      100.0 * drop);
        why += buf;
      }
      if (cv > 0.2) {
        std::snprintf(buf, sizeof(buf), "window duration varies (cv %.2f), so the periodic test's "
                      "period in seconds varies; ", cv);
        why += buf;
      }
      if (expected_rate_hz_ > 0.0 && mean > 0.0) {
        const double expected = layer_interval_ / expected_rate_hz_;
        if (std::fabs(mean - expected) > 0.1 * expected) {
          std::snprintf(buf, sizeof(buf), "mean window %.3f s differs from layer_interval / "
                        "expected_scan_rate_hz = %.3f s; ", mean, expected);
          why += buf;
        }
      }
    }
    if (tf_failures_window_ > scans) {
      std::snprintf(buf, sizeof(buf), "%d messages failed the TF lookup, more than were "
                    "integrated; ", tf_failures_window_);
      why += buf;
    }
  }
  auto num = [](double v) { char b[32]; std::snprintf(b, sizeof(b), "%.4g", v); return std::string(b); };
  last_values_ = {
    {"window_mode", window_mode_},
    {"sensors", std::to_string(rates_.size())},
    {"scans_in_window", std::to_string(scans)},
    {"empty_windows", std::to_string(k - 1)},
    {"window_duration_s", num(duration)},
    {"input_rate_hz", num(rate)},
    {"nominal_rate_hz", num(nominal)},
    {"dropped_in_window", std::to_string(dropped)},
    {"tf_failures_in_window", std::to_string(tf_failures_window_)},
    {"recent_drop_fraction", num(drop)},
    {"mean_window_duration_s", num(mean)},
    {"window_duration_cv", num(cv)},
    {"effective_period_s", num(effective_period)},
  };
  last_why_ = why.empty() ? "" : why.substr(0, why.size() - 2);
  tf_failures_window_ = 0;
}

void MappingNode::onDiagnostics() {
  std::lock_guard<std::mutex> lk(mtx_);
  const auto now_w = Steady::now();
  auto secs = [&now_w](Steady::time_point t) { return std::chrono::duration<double>(now_w - t).count(); };
  const double since_input = any_input_ ? secs(last_input_wall_) : secs(start_wall_);
  using DS = diagnostic_msgs::msg::DiagnosticStatus;
  unsigned char lvl = last_why_.empty() ? DS::OK : DS::WARN;
  std::string msg = last_why_.empty() ? "ok" : last_why_;
  char buf[240];
  if (window_mode_ == "time" && any_input_ && msgs_since_close_ > 1 &&
      secs(last_close_wall_) > 10.0 * window_period_s_ && secs(last_close_wall_) > 2.0) {
    std::snprintf(buf, sizeof(buf), "no time window has closed for %.1f s although %ld messages "
                  "arrived: are header stamps advancing?", secs(last_close_wall_), msgs_since_close_);
    lvl = DS::WARN; msg = buf;
  }
  if (zero_stamps_ > 0) {
    std::snprintf(buf, sizeof(buf), "%ld message(s) had a zero header stamp; ", zero_stamps_);
    lvl = DS::WARN; msg = std::string(buf) + msg;
  }
  if (since_input > input_timeout_s_) {
    std::snprintf(buf, sizeof(buf), any_input_ ? "no input on %s for %.1f s (sensor, driver or link down?)"
                                               : "no input on %s yet after %.1f s",
                  input_topic_.c_str(), since_input);
    lvl = DS::ERROR; msg = buf;
  } else if (!any_input_) {
    msg = "waiting for input on " + input_topic_;
  }
  diagnostic_msgs::msg::DiagnosticArray arr;
  arr.header.stamp = now();
  DS st;
  st.name = std::string(get_fully_qualified_name()) + ": input rate";
  st.hardware_id = input_topic_;
  st.level = lvl;
  st.message = msg;
  auto kv = [&st](const std::string& k, const std::string& v) {
    diagnostic_msgs::msg::KeyValue p; p.key = k; p.value = v; st.values.push_back(p);
  };
  for (const auto& p : last_values_) kv(p.first, p.second);
  if (last_values_.empty()) kv("window_mode", window_mode_);
  std::snprintf(buf, sizeof(buf), "%.2f", since_input);
  kv("seconds_since_last_message", buf);
  kv("windows_closed_total", std::to_string(windows_closed_));
  arr.status.push_back(st);
  diag_pub_->publish(arr);
  // Throttled on the wall clock: a live robot without /clock still gets the warning.
  const double since_log = secs(last_log_wall_);
  if ((lvl != DS::OK && since_log >= 10.0) || since_log >= 30.0) {
    last_log_wall_ = now_w;
    if (lvl == DS::ERROR) RCLCPP_ERROR(get_logger(), "input rate: %s", msg.c_str());
    else if (lvl == DS::WARN) RCLCPP_WARN(get_logger(), "input rate: %s", msg.c_str());
    else RCLCPP_INFO(get_logger(), "input rate: ok (%s windows closed)", std::to_string(windows_closed_).c_str());
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
