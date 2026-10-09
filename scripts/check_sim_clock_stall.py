#!/usr/bin/env python3
"""A node launched with use_sim_time but no /clock and no TF must stay responsive.

Publishes LaserScans (no /clock, no TF) to a running grid2d node launched with
use_sim_time:=true, and checks that
  * `ros2 param get <node> use_sim_time` answers within --answer-within seconds while
    the scans keep coming (a TF wait counted on the frozen ROS clock would block the
    scan callback forever, and with it every service and timer of the node), and
  * /diagnostics keeps arriving and reports WARN or worse saying the ROS clock is not
    advancing while messages arrive.

Usage: check_sim_clock_stall.py [--node /strata] [--topic /scan] [--seconds 12]
Run against `ros2 launch strata grid2d.launch.py rviz:=false use_sim_time:=true`.
"""
import argparse
import math
import subprocess
import sys
import time

import rclpy
from diagnostic_msgs.msg import DiagnosticArray
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import LaserScan


def level(status):
    return status.level if isinstance(status.level, int) else ord(status.level)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--node', default='/strata')
    ap.add_argument('--topic', default='/scan')
    ap.add_argument('--seconds', type=float, default=12.0, help='how long to publish scans')
    ap.add_argument('--answer-within', type=float, default=8.0,
                    help='seconds `ros2 param get` may take (CLI start-up included)')
    args = ap.parse_args()

    rclpy.init()
    node = rclpy.create_node('strata_clock_stall_check')
    pub = node.create_publisher(LaserScan, args.topic, qos_profile_sensor_data)
    diags = []
    name = args.node.rsplit('/', 1)[-1] + ': input rate'
    node.create_subscription(DiagnosticArray, '/diagnostics',
                             lambda m: diags.extend((time.monotonic(), s) for s in m.status if s.name == name), 50)
    end = time.monotonic() + 30.0
    while pub.get_subscription_count() == 0:
        if time.monotonic() > end:
            sys.exit(f'FAIL: the node never subscribed to {args.topic}')
        rclpy.spin_once(node, timeout_sec=0.1)

    scan = LaserScan()
    scan.header.frame_id = 'sensor'                       # no TF from map to it exists
    scan.angle_min, scan.angle_increment = 0.0, math.radians(1.0)
    scan.angle_max = math.radians(359.0)
    scan.range_min, scan.range_max = 0.1, 20.0
    scan.ranges = [5.0] * 360
    start = time.monotonic()
    first_scan = start
    query = None
    answered = None
    stamp = 1000.0
    while time.monotonic() - start < args.seconds:
        stamp += 0.1
        scan.header.stamp.sec, scan.header.stamp.nanosec = int(stamp), int((stamp % 1) * 1e9)
        pub.publish(scan)
        if query is None and time.monotonic() - first_scan > 3.0:    # the node has been fed for 3 s
            query_t = time.monotonic()
            query = subprocess.Popen(['ros2', 'param', 'get', args.node, 'use_sim_time'],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if query is not None and answered is None and query.poll() is not None:
            answered = time.monotonic() - query_t
        if query is not None and answered is None and time.monotonic() - query_t > args.answer_within:
            query.kill()
            answered = float('inf')
        rclpy.spin_once(node, timeout_sec=0.1)
    if query is not None and answered is None:
        try:
            query.wait(timeout=max(0.1, args.answer_within - (time.monotonic() - query_t)))
            answered = time.monotonic() - query_t
        except subprocess.TimeoutExpired:
            query.kill()
            answered = float('inf')
    out = query.stdout.read().strip() if query is not None else ''

    results = []
    ok = math.isfinite(answered) and query.returncode == 0 and 'True' in out
    results.append((ok, f'`ros2 param get {args.node} use_sim_time` answered in {answered:.1f} s '
                        f'(need <= {args.answer_within:g} s): "{out[:120]}"'))
    during = [s for t, s in diags if t >= first_scan + 1.0]
    results.append((len(during) >= int(args.seconds) - 3,
                    f'/diagnostics statuses while scans arrived: {len(during)} over {args.seconds - 1:g} s '
                    f'(need >= {int(args.seconds) - 3}; a blocked callback starves the 1 Hz timer)'))
    stall = [s for s in during if level(s) >= 1 and 'ROS clock has not advanced' in s.message]
    results.append((bool(stall), 'diagnostics report the stalled sim clock: '
                    + (f'level {level(stall[-1])}, "{stall[-1].message}"' if stall else
                       f'no (last: "{during[-1].message if during else ""}")')))
    for ok, line in results:
        print(('ok    ' if ok else 'FAIL  ') + line)
    node.destroy_node()
    rclpy.shutdown()
    if not all(ok for ok, _ in results):
        sys.exit('FAIL: the node is not responsive under use_sim_time without /clock')
    print('PASS: the node stays responsive and reports the stalled clock')


if __name__ == '__main__':
    main()
