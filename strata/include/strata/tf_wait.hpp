#pragma once
#include <cmath>
#include <cstdint>
namespace strata {

// When to stop waiting for a TF lookup at a message stamp. ROS-free so it can be
// unit-tested; times are passed in, steady time in seconds and ROS time in ns.
//
// The budget is 0.1 s of ROS time, capped at 1 s of steady time; a clock unchanged
// for 1 s is treated as frozen and no longer waited on (the callback where it stops
// can wait up to the cap). So a scan slightly ahead of its transform resolves the
// same at playback rates down to 0.1x (below that the cap shortens it), a coarse sim
// clock that steps less often than every 0.1 s of wall time is still waited on (as
// long as it steps at least once per second of wall time; a sparser clock counts as
// frozen), and use_sim_time without /clock cannot hang the callback. When a coarse
// clock steps past the budget, the lookup is retried for 20 ms more of steady time,
// for a transform published with that step: a clock that moves less than 10 ms
// between two polls (2 ms apart: a live robot, or playback below about 5x) stops at
// the budget; otherwise the extra 20 ms applies.

// When the ROS clock was last seen to change, across calls (TF waits and the
// diagnostics timer both feed it).
class RosClockWatch {
 public:
  void observe(double steady_s, std::int64_t ros_ns) {
    if (!seen_ || ros_ns != last_ros_ns_) {
      seen_ = true;
      last_ros_ns_ = ros_ns;
      changed_steady_s_ = steady_s;
    }
  }
  // Steady seconds since the clock last changed (0 before the first observation).
  double unchangedFor(double steady_s) const { return seen_ ? steady_s - changed_steady_s_ : 0.0; }

 private:
  bool seen_{false};
  std::int64_t last_ros_ns_{0};
  double changed_steady_s_{0.0};
};

struct TfWaitRule {
  double budget_s{0.1};          // ROS time to wait for a late transform
  double frozen_after_s{1.0};    // clock unchanged this long (steady): frozen, do not wait
  double hard_cap_s{1.0};        // steady time no wait exceeds
  // A coarse clock reaches the budget in one step (>= step_min_s between two polls),
  // usually together with a transform stamped at that step that is still in flight on
  // /tf: keep retrying for step_grace_s of steady time. A clock that moves less than
  // 10 ms between two polls (2 ms apart: a live robot, or playback below about 5x)
  // stops at the budget; otherwise the extra 20 ms applies.
  double step_min_s{0.01};
  double step_grace_s{0.02};
};

// One wait. stop() is called after each failed lookup with the current steady and ROS
// time; it also feeds the shared clock watch.
class TfWait {
 public:
  TfWait(const TfWaitRule& rule, RosClockWatch& watch, double steady_s, std::int64_t ros_ns)
      : rule_(rule), watch_(watch), steady0_(steady_s), ros0_ns_(ros_ns), last_ros_ns_(ros_ns) {
    watch_.observe(steady_s, ros_ns);
  }

  bool stop(double steady_s, std::int64_t ros_ns) {
    watch_.observe(steady_s, ros_ns);
    if (ros_ns < last_ros_ns_) ros0_ns_ = ros_ns;   // clock jumped back: count from here
    const std::int64_t moved = ros_ns - last_ros_ns_;
    last_ros_ns_ = ros_ns;
    if (steady_s - steady0_ >= rule_.hard_cap_s) return true;
    if (ros_ns - ros0_ns_ >= ns(rule_.budget_s)) {
      if (grace_from_ < 0.0) {
        if (moved < ns(rule_.step_min_s)) return true;   // moved < 10 ms since the last poll: stop at the budget
        grace_from_ = steady_s;                           // stepped past it: brief grace
      }
      return steady_s - grace_from_ >= rule_.step_grace_s;
    }
    return watch_.unchangedFor(steady_s) >= rule_.frozen_after_s;
  }

 private:
  static std::int64_t ns(double s) { return static_cast<std::int64_t>(std::llround(s * 1e9)); }
  TfWaitRule rule_;
  RosClockWatch& watch_;
  double steady0_;
  std::int64_t ros0_ns_, last_ros_ns_;
  double grace_from_{-1.0};
};

}  // namespace strata
