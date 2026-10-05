#!/usr/bin/env python3
"""Scans slightly ahead of TF under a coarse, slow sim clock must not be dropped.

Plays a simulator that runs at --rtf of real time and publishes /clock in steps of
--step s of sim time (10 Hz of sim time at 0.5x: one step every 0.2 s of wall time).
With every step it broadcasts the map -> sensor transform stamped at the new clock
time, then publishes a LaserScan stamped --lead s of sim time ahead of it, so the
transform the scan needs arrives with the next step. The node's TF wait must keep
waiting until the ROS clock has advanced (here 0.2 s of wall time later) instead of
taking the coarse clock for a frozen one. Checks, from the node's /diagnostics, that
no message failed the TF lookup over the windows that closed while scans arrived.

Usage: check_coarse_clock.py [--seconds 14] [--rtf 0.5] [--step 0.1] [--lead 0.03]
Run against `ros2 launch strata grid2d.launch.py rviz:=false use_sim_time:=true`.
"""
import argparse
import math
import sys
import time

import rclpy
from diagnostic_msgs.msg import DiagnosticArray
from geometry_msgs.msg import TransformStamped
from rclpy.qos import qos_profile_sensor_data
from rosgraph_msgs.msg import Clock
from sensor_msgs.msg import LaserScan
from tf2_ros import TransformBroadcaster


def stamp_of(t):
    sec = int(t)
    return sec, int(round((t - sec) * 1e9))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--node', default='/strata')
    ap.add_argument('--topic', default='/scan')
    ap.add_argument('--seconds', type=float, default=14.0, help='wall time to publish scans')
    ap.add_argument('--warmup', type=float, default=3.0, help='wall time of clock and TF before the first scan')
    ap.add_argument('--rtf', type=float, default=0.5, help='real-time factor of the simulated clock')
    ap.add_argument('--step', type=float, default=0.1, help='sim seconds per /clock step')
    ap.add_argument('--lead', type=float, default=0.03, help='scan stamp ahead of the newest TF, sim s')
    args = ap.parse_args()

    rclpy.init()
    node = rclpy.create_node('strata_coarse_clock_check')
    clock_pub = node.create_publisher(Clock, '/clock', 10)
    scan_pub = node.create_publisher(LaserScan, args.topic, qos_profile_sensor_data)
    tf = TransformBroadcaster(node)
    name = args.node.rsplit('/', 1)[-1] + ': input rate'
    diags = []
    node.create_subscription(DiagnosticArray, '/diagnostics',
                             lambda m: diags.extend(s for s in m.status if s.name == name), 50)
    end = time.monotonic() + 30.0
    while scan_pub.get_subscription_count() == 0:
        if time.monotonic() > end:
            sys.exit(f'FAIL: the node never subscribed to {args.topic}')
        rclpy.spin_once(node, timeout_sec=0.1)

    scan = LaserScan()
    scan.header.frame_id = 'sensor'
    scan.angle_min, scan.angle_increment = 0.0, math.radians(1.0)
    scan.angle_max = math.radians(359.0)
    scan.range_min, scan.range_max = 0.1, 20.0
    scan.ranges = [5.0] * 360
    sim = 100.0
    wall_per_step = args.step / args.rtf
    sent = 0
    start = time.monotonic()
    warmup = start + args.warmup   # clock and TF only, so the node's TF listener is connected
    nxt = start
    while time.monotonic() - start < args.seconds + args.warmup:
        sim += args.step
        c = Clock()
        c.clock.sec, c.clock.nanosec = stamp_of(sim)
        clock_pub.publish(c)
        t = TransformStamped()
        t.header.stamp = c.clock
        t.header.frame_id, t.child_frame_id = 'map', 'sensor'
        t.transform.translation.x, t.transform.translation.y = 0.013, 0.021
        t.transform.rotation.w = 1.0
        tf.sendTransform(t)
        if time.monotonic() >= warmup:
            scan.header.stamp.sec, scan.header.stamp.nanosec = stamp_of(sim + args.lead)
            scan_pub.publish(scan)
            sent += 1
        nxt += wall_per_step
        while time.monotonic() < nxt:
            rclpy.spin_once(node, timeout_sec=max(0.0, min(0.05, nxt - time.monotonic())))
    # The last scan's transform arrives with one more step.
    sim += args.step
    c = Clock()
    c.clock.sec, c.clock.nanosec = stamp_of(sim)
    clock_pub.publish(c)
    t.header.stamp = c.clock
    tf.sendTransform(t)
    end = time.monotonic() + 2.5
    while time.monotonic() < end:
        rclpy.spin_once(node, timeout_sec=0.1)

    def vals(s):
        return {kv.key: kv.value for kv in s.values}
    windows = {}
    for s in diags:   # one entry per window close (the status repeats until the next one)
        v = vals(s)
        if 'tf_failures_in_window' in v:
            windows[int(v['windows_closed_total'])] = (int(v['tf_failures_in_window']), int(v['scans_in_window']))
    failures = sum(f for f, _ in windows.values())
    integrated = sum(n for _, n in windows.values())
    results = [
        (len(windows) >= 3, f'{len(windows)} window closes reported on /diagnostics (need >= 3)'),
        (failures == 0, f'TF failures over those windows: {failures}, scans integrated in them: {integrated}, '
                        f'scans sent: {sent} at {args.rtf:g}x with a {args.step:g} s sim clock step and '
                        f'stamps {args.lead:g} s ahead of TF (need 0 failures)'),
        # The open window at the end is not reported yet, so not every scan shows up.
        (integrated >= 0.6 * sent, f'scans integrated in closed windows: {integrated}/{sent} (need >= 60 %)'),
    ]
    for ok, line in results:
        print(('ok    ' if ok else 'FAIL  ') + line)
    node.destroy_node()
    rclpy.shutdown()
    if not all(ok for ok, _ in results):
        sys.exit('FAIL: scans were dropped for TF under a coarse sim clock')
    print('PASS: no scan dropped for TF under a coarse sim clock')


if __name__ == '__main__':
    main()
