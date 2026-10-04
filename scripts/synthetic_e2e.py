#!/usr/bin/env python3
"""End-to-end check of a running STRATA node on a synthetic scene.

Feeds the node a seeded synthetic stream on its real ROS interfaces (static TF,
/clock, LaserScan for grid2d or PointCloud2 for voxel3d) and checks what comes out
of /strata/map (grid2d) or /strata/map_points (voxel3d), then calls ~/save_map.

The scene, seen from a sensor at a fixed pose:
  * wall   -- a ring at WALL_R, always present          -> must graduate to Static
  * door   -- a sector at DOOR_R, closed for half of each PERIOD_WINDOWS-window
              period and open (beams pass to the wall) for the other half
                                                        -> grid2d: Periodic (75);
                                                           voxel3d: kept out of the static map
  * mover  -- an object at MOVER_R that steps more than one voxel to a new bearing
              every window for MOVER_WINDOWS windows, then leaves
                                                        -> never Static; grid2d shows
                                                           it Transient (50), then pruned

One window is `layer_interval` scans (read from the params YAML), so the door state
is switched on window boundaries. Door cells are tested only where an open-door ray
actually walks through them (the backend's own Bresenham line or half-voxel march):
a door cell the ray skips is never observed free, so it rightly graduates to Static. This is a ROS-path test (topics -> node -> map),
not a field result: it shows the shipped parameters classify a clean scene as the
paper's E1-E3 harness does, on Humble, through the real node.

Usage: synthetic_e2e.py grid2d|voxel3d PARAMS_YAML [--windows N]
Run against `ros2 launch strata <backend>.launch.py rviz:=false` (use_sim_time).
"""
import argparse
import math
import os
import struct
import sys
import time

import rclpy
import yaml
from geometry_msgs.msg import TransformStamped
from nav_msgs.msg import OccupancyGrid
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
from rosgraph_msgs.msg import Clock
from sensor_msgs.msg import LaserScan, PointCloud2, PointField
from std_srvs.srv import Trigger
from tf2_ros.static_transform_broadcaster import StaticTransformBroadcaster

SENSOR = (0.013, 0.021, 0.0)       # sensor pose in map; off-grid to avoid cell-boundary ties
WALL_R, DOOR_R, MOVER_R = 5.0, 3.0, 2.0
DOOR_DEG = (-15, 15)               # door sector (inclusive), degrees
# The mover steps further than one 0.2 m voxel per window, so no cell or voxel holds
# it for min_observations windows (a 3-window stop would rightly graduate it).
MOVER_START_DEG, MOVER_STEP_DEG, MOVER_WIDTH_DEG = 100, 9, 2
MOVER_WINDOWS = 20
ELEV_DEG = (2.0, 6.0, 10.0)        # voxel3d: beam elevations; door and wall share each beam
SCAN_RATE_HZ = 50.0


def door_closed(window, period):
    return (window // (period // 2)) % 2 == 0


def mover_bearings(window):
    """Bearings (deg) the mover occupies in this window, or [] once it has left."""
    if window >= MOVER_WINDOWS:
        return []
    start = MOVER_START_DEG + MOVER_STEP_DEG * window
    return list(range(start, start + MOVER_WIDTH_DEG + 1))


def mover_sector():
    last = MOVER_START_DEG + MOVER_STEP_DEG * (MOVER_WINDOWS - 1) + MOVER_WIDTH_DEG
    return MOVER_START_DEG, last


def in_sector(deg, sector):
    return sector[0] <= deg <= sector[1]


def ranges_for(window, period):
    """Range per integer bearing 0..359 (deg) for one scan of this window."""
    movers = set(mover_bearings(window))
    closed = door_closed(window, period)
    out = []
    for d in range(360):
        signed = d if d < 180 else d - 360
        if d in movers:
            out.append(MOVER_R)
        elif closed and in_sector(signed, DOOR_DEG):
            out.append(DOOR_R)
        else:
            out.append(WALL_R)
    return out


def beam(deg, elev_deg=0.0):
    """Unit beam direction in the sensor frame."""
    a, e = math.radians(deg), math.radians(elev_deg)
    return (math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e))


def point(r, deg, elev_deg=0.0):
    """Beam end point in the map frame."""
    b = beam(deg, elev_deg)
    return tuple(SENSOR[i] + r * b[i] for i in range(3))


class Driver:
    def __init__(self, node, backend, params):
        self.node, self.backend, self.p = node, backend, params
        self.maps = []
        self.clock_pub = node.create_publisher(Clock, '/clock', 10)
        self.tf = StaticTransformBroadcaster(node)
        if backend == 'grid2d':
            self.scan_pub = node.create_publisher(LaserScan, params.get('scan_topic', '/scan'), 10)
            qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE,
                             durability=DurabilityPolicy.TRANSIENT_LOCAL)
            node.create_subscription(OccupancyGrid, '/strata/map', self.maps.append, qos)
        else:
            self.cloud_pub = node.create_publisher(PointCloud2, params.get('points_topic', '/points'), 10)
            node.create_subscription(PointCloud2, '/strata/map_points', self.maps.append, 10)
        self.frame = 'sensor'
        self.t = 0.0

    def send_tf(self):
        t = TransformStamped()
        t.header.frame_id = self.p.get('global_frame', 'map')
        t.child_frame_id = self.frame
        t.transform.translation.x, t.transform.translation.y, t.transform.translation.z = SENSOR
        t.transform.rotation.w = 1.0
        self.tf.sendTransform(t)

    def stamp(self):
        self.t += 1.0 / SCAN_RATE_HZ
        clock = Clock()
        clock.clock.sec = int(self.t)
        clock.clock.nanosec = int((self.t - int(self.t)) * 1e9)
        self.clock_pub.publish(clock)
        return clock.clock

    def scan_msg(self, window, period):
        m = LaserScan()
        m.header.stamp = self.stamp()
        m.header.frame_id = self.frame
        m.angle_min, m.angle_increment = 0.0, math.radians(1.0)
        m.angle_max = math.radians(359.0)
        m.range_min, m.range_max = 0.1, 20.0
        m.ranges = [float(r) for r in ranges_for(window, period)]
        return m

    def cloud_msg(self, window, period):
        pts = []
        for d, r in enumerate(ranges_for(window, period)):
            for e in ELEV_DEG:
                pts.append(tuple(r * c for c in beam(d, e)))      # sensor frame
        m = PointCloud2()
        m.header.stamp = self.stamp()
        m.header.frame_id = self.frame
        m.height, m.width = 1, len(pts)
        m.fields = [PointField(name=n, offset=4 * i, datatype=PointField.FLOAT32, count=1)
                    for i, n in enumerate('xyz')]
        m.is_bigendian, m.point_step = False, 12
        m.row_step, m.is_dense = 12 * len(pts), True
        m.data = b''.join(struct.pack('<fff', *p) for p in pts)
        return m

    def wait_for_subscriber(self, timeout):
        pub = self.scan_pub if self.backend == 'grid2d' else self.cloud_pub
        end = time.monotonic() + timeout
        while pub.get_subscription_count() == 0:
            if time.monotonic() > end:
                sys.exit('FAIL: the node never subscribed to the sensor topic')
            rclpy.spin_once(self.node, timeout_sec=0.1)

    def run(self, windows):
        interval = int(self.p.get('layer_interval', 10))
        period = int(self.p.get('period_windows', 24))
        self.send_tf()
        rclpy.spin_once(self.node, timeout_sec=0.5)
        for scan in range(windows * interval):
            window = scan // interval
            if self.backend == 'grid2d':
                self.scan_pub.publish(self.scan_msg(window, period))
            else:
                self.cloud_pub.publish(self.cloud_msg(window, period))
            rclpy.spin_once(self.node, timeout_sec=0.0)
            time.sleep(1.0 / SCAN_RATE_HZ)
        return period

    def latest_map(self, after_count, timeout):
        """Wait for a map published after the stream ended (publish_period timer)."""
        end = time.monotonic() + timeout
        while len(self.maps) <= after_count:
            if time.monotonic() > end:
                sys.exit('FAIL: no map published after the stream ended')
            rclpy.spin_once(self.node, timeout_sec=0.1)
        return self.maps[-1]


def grid_xy(grid, x, y):
    info = grid.info
    return (math.floor((x - info.origin.position.x) / info.resolution),
            math.floor((y - info.origin.position.y) / info.resolution))


def bresenham(x0, y0, x1, y1):
    """Cells Grid2DBackend::raycastClear marks free (start inclusive, end exclusive)."""
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err, x, y, out = dx + dy, x0, y0, set()
    while (x, y) != (x1, y1):
        out.add((x, y))
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x += sx
        if e2 <= dx:
            err += dx
            y += sy
    return out


def door_bearings():
    return range(DOOR_DEG[0], DOOR_DEG[1] + 1)


def wall_bearings():
    sec = mover_sector()
    return [d for d in range(360)
            if not in_sector(d if d < 180 else d - 360, DOOR_DEG) and not in_sector(d, sec)]


def split_by_ray(hit_cells, traversed):
    """Hit cells an open-door ray walks through (testable) and those it skips."""
    return sorted(hit_cells & traversed), sorted(hit_cells - traversed)


def frac(cells, value_of, ok):
    hit = sum(1 for c in cells if ok(value_of(c)))
    return (hit / len(cells) if cells else 0.0), hit


def check_grid(maps, final):
    w = final.info.width
    def val(m):
        return lambda c: m.data[c[1] * w + c[0]]
    s = grid_xy(final, *SENSOR[:2])
    open_rays = set()
    for d in door_bearings():
        open_rays |= bresenham(*s, *grid_xy(final, *point(WALL_R, d)[:2]))
    door, skipped = split_by_ray({grid_xy(final, *point(DOOR_R, d)[:2]) for d in door_bearings()}, open_rays)
    wall = sorted({grid_xy(final, *point(WALL_R, d)[:2]) for d in wall_bearings()})
    mover = sorted({grid_xy(final, *point(MOVER_R, d)[:2])
                    for win in range(MOVER_WINDOWS) for d in mover_bearings(win)})
    v = val(final)
    results = []
    f, n = frac(wall, v, lambda x: x == 100)
    results.append((f >= 0.95, f'wall cells Static (100): {n}/{len(wall)} = {f:.3f} (need >= 0.95)'))
    f, n = frac(door, v, lambda x: x == 75)
    results.append((f >= 0.95, f'door cells Periodic (75): {n}/{len(door)} = {f:.3f} (need >= 0.95; '
                               f'{len(skipped)} door cells the open-door ray skips are not tested)'))
    f, n = frac(door, v, lambda x: x == 100)
    results.append((n == 0, f'door cells Static (100): {n}/{len(door)} (need 0)'))
    ever = sum(1 for c in mover if any(val(m)(c) == 100 for m in maps))
    results.append((ever == 0, f'mover cells Static in any of {len(maps)} maps: {ever}/{len(mover)} (need 0)'))
    seen = sum(1 for c in mover if any(val(m)(c) == 50 for m in maps))
    results.append((seen > 0, f'mover cells seen Transient (50) in some map: {seen}/{len(mover)} (need > 0)'))
    f, n = frac(mover, v, lambda x: x in (-1, 50))
    results.append((f == 1.0, f'mover cells Transient or pruned at the end: {n}/{len(mover)} (need all)'))
    # Not a gate: the documented encoding has no free value, so observed free space
    # reads Transient (50) until pruned and unknown (-1) after; report the split.
    free = set()
    for d in wall_bearings():
        free |= bresenham(*s, *grid_xy(final, *point(WALL_R, d)[:2]))
    free -= set(wall) | set(mover) | {s}
    counts = {}
    for m in maps:
        for c in free:
            x = val(m)(c)
            counts[x] = counts.get(x, 0) + 1
    total = sum(counts.values())
    share = ', '.join(f'{k}: {n / total:.2f}' for k, n in sorted(counts.items()))
    print(f'info  free-space cells on wall rays, share of (cell, map) pairs by value over '
          f'{len(maps)} maps: {share} (the encoding has no free value)')
    return results


def voxel_key(p, size):
    return tuple(math.floor(c / size) for c in p)


def march(origin, hit, size):
    """Voxels Voxel3DBackend::integrate marks free along one ray (half-voxel steps)."""
    d = [hit[i] - origin[i] for i in range(3)]
    length = math.sqrt(sum(c * c for c in d))
    steps = int(length / (size * 0.5))
    st = [c / max(steps, 1) for c in d]
    return {voxel_key([origin[j] + st[j] * i for j in range(3)], size) for i in range(1, steps)}


def cloud_points(msg):
    off = {f.name: f.offset for f in msg.fields}
    out = []
    for i in range(msg.width * msg.height):
        base = i * msg.point_step
        out.append(tuple(struct.unpack_from('<f', bytes(msg.data[base + off[a]:base + off[a] + 4]))[0]
                         for a in 'xyz'))
    return out


def check_voxel(maps, final, size):
    def keys(r, degrees):
        return {voxel_key(point(r, d, e), size) for d in degrees for e in ELEV_DEG}
    open_rays = set()
    for d in door_bearings():
        for e in ELEV_DEG:
            open_rays |= march(SENSOR, point(WALL_R, d, e), size)
    door, skipped = split_by_ray(keys(DOOR_R, door_bearings()), open_rays)
    door = set(door)
    wall = keys(WALL_R, wall_bearings())
    mover = set()
    for win in range(MOVER_WINDOWS):
        mover |= keys(MOVER_R, mover_bearings(win))
    static_final = {voxel_key(p, size) for p in cloud_points(final)}
    static_ever = set()
    for m in maps:
        static_ever |= {voxel_key(p, size) for p in cloud_points(m)}
    results = []
    n = len(wall & static_final)
    results.append((n / len(wall) >= 0.95,
                    f'wall voxels in the static map: {n}/{len(wall)} = {n / len(wall):.3f} (need >= 0.95)'))
    n = len(door & static_final)
    results.append((n == 0, f'door voxels in the final static map: {n}/{len(door)} (need 0; '
                            f'{len(skipped)} door voxels the open-door ray skips are not tested)'))
    n = len(mover & static_ever)
    results.append((n == 0, f'mover voxels in any of {len(maps)} static maps: {n}/{len(mover)} (need 0)'))
    return results


def save_map(node, timeout):
    client = node.create_client(Trigger, '/strata/save_map')
    if not client.wait_for_service(timeout_sec=timeout):
        sys.exit('FAIL: /strata/save_map not available')
    future = client.call_async(Trigger.Request())
    rclpy.spin_until_future_complete(node, future, timeout_sec=timeout)
    if future.result() is None:
        sys.exit('FAIL: /strata/save_map did not answer')
    return future.result()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('backend', choices=['grid2d', 'voxel3d'])
    ap.add_argument('params', help='the params YAML the node was launched with')
    ap.add_argument('--windows', type=int, default=72)
    ap.add_argument('--timeout', type=float, default=30.0)
    args = ap.parse_args()

    params = {}
    for section in (yaml.safe_load(open(args.params)) or {}).values():
        params.update((section or {}).get('ros__parameters', {}))

    rclpy.init()
    node = rclpy.create_node('strata_synthetic_e2e', parameter_overrides=[])
    drv = Driver(node, args.backend, params)
    drv.wait_for_subscriber(args.timeout)
    period = drv.run(args.windows)
    print(f'{args.backend}: sent {args.windows} windows x {params.get("layer_interval", 10)} scans, '
          f'door period {period} windows')
    final = drv.latest_map(len(drv.maps), args.timeout)

    if args.backend == 'grid2d':
        results = check_grid(drv.maps, final)
    else:
        results = check_voxel(drv.maps, final, float(params.get('voxel_size', 0.2)))

    res = save_map(node, args.timeout)
    saved = res.message.replace('saved ', '').split(' + ')[0]
    results.append((res.success and os.path.getsize(saved) > 0, f'save_map: {res.message}'))

    for ok, line in results:
        print(('ok    ' if ok else 'FAIL  ') + line)
    node.destroy_node()
    rclpy.shutdown()
    if not all(ok for ok, _ in results):
        sys.exit('FAIL: synthetic end-to-end')
    print('PASS: the node maps the synthetic scene as expected')


if __name__ == '__main__':
    main()
