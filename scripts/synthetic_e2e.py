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
                                                           it Transient (50), then free (0)
  * free   -- cells the wall rays clear                -> grid2d: free (0), kept free after
                                                           the classifier prunes them

One window is `layer_interval` scans, or `window_period_s` of message time with
`window_mode: time` (both read from the params YAML), so the door state is switched
on window boundaries. --drop F withholds a deterministic share F of the scans while
time runs on, as a lossy best-effort link would; the node's /diagnostics must then
report about F lost. Scan-counted windows stretch under loss and detune the door
(--expect-detuned checks that negative control); time windows keep it periodic. Door cells are tested only where an open-door ray
actually walks through them (the backend's own Bresenham line or half-voxel march):
a door cell the ray skips is never observed free, so it rightly graduates to Static.
This is a ROS-path test (topics -> node -> map), not a field result: it shows the shipped parameters classify a clean scene as the
paper's E1-E3 harness does, on Humble, through the real node.

--check-map-server (grid2d) closes the loop to Nav2: after ~/save_map it moves the
saved pair to a fresh directory, loads the YAML there in nav2_map_server (own
namespace, lifecycle configure + activate), and
requires the map it serves to match the last published map in size, resolution and
origin, and cell by cell as 0->0, 100->100, 50/75/-1 -> -1 (map_server must serve
only -1, 0, 100). A row flip, origin or resolution error in the saved pair fails it.
--keep-map DIR copies that moved pair into DIR, for scripts/check_saved_map.py.

Usage: synthetic_e2e.py grid2d|voxel3d PARAMS_YAML [--windows N] [--check-map-server]
Run against `ros2 launch strata <backend>.launch.py rviz:=false` (use_sim_time).
"""
import argparse
import collections
import math
import os
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time

import rclpy
import yaml
from geometry_msgs.msg import TransformStamped
from nav_msgs.msg import OccupancyGrid
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
from diagnostic_msgs.msg import DiagnosticArray
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
    def __init__(self, node, backend, params, rate):
        self.node, self.backend, self.p, self.rate = node, backend, params, rate
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
        self.diags = []
        node.create_subscription(DiagnosticArray, '/diagnostics', self.on_diag, 50)
        self.frame = 'sensor'
        self.t = 0.0

    def on_diag(self, msg):
        self.diags += [(time.monotonic(), s) for s in msg.status if s.name.endswith(': input rate')]

    def send_tf(self):
        t = TransformStamped()
        t.header.frame_id = self.p.get('global_frame', 'map')
        t.child_frame_id = self.frame
        t.transform.translation.x, t.transform.translation.y, t.transform.translation.z = SENSOR
        t.transform.rotation.w = 1.0
        self.tf.sendTransform(t)

    def stamp(self):
        self.t += 1.0 / self.rate
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

    def run(self, windows, interval, drop):
        period = int(self.p.get('period_windows', 24))
        self.send_tf()
        rclpy.spin_once(self.node, timeout_sec=0.5)
        sent = 0
        for scan in range(windows * interval):
            window = scan // interval
            if int((scan + 1) * drop) != int(scan * drop):   # lost on the link: time runs on
                self.stamp()
                time.sleep(1.0 / self.rate)
                continue
            sent += 1
            if self.backend == 'grid2d':
                self.scan_pub.publish(self.scan_msg(window, period))
            else:
                self.cloud_pub.publish(self.cloud_msg(window, period))
            rclpy.spin_once(self.node, timeout_sec=0.0)
            time.sleep(1.0 / self.rate)
        return period, sent

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


def check_grid(maps, final, detuned=False):
    w = final.info.width
    def val(m):
        return lambda c: m.data[c[1] * w + c[0]]
    s_cell = grid_xy(final, *SENSOR[:2])
    open_rays = set()
    for d in door_bearings():
        open_rays |= bresenham(*s_cell, *grid_xy(final, *point(WALL_R, d)[:2]))
    door, skipped = split_by_ray({grid_xy(final, *point(DOOR_R, d)[:2]) for d in door_bearings()}, open_rays)
    wall = sorted({grid_xy(final, *point(WALL_R, d)[:2]) for d in wall_bearings()})
    mover = sorted({grid_xy(final, *point(MOVER_R, d)[:2])
                    for win in range(MOVER_WINDOWS) for d in mover_bearings(win)})
    v = val(final)
    results = []
    f, n = frac(wall, v, lambda x: x == 100)
    results.append((f >= 0.95, f'wall cells Static (100): {n}/{len(wall)} = {f:.3f} (need >= 0.95)'))
    f75, n75 = frac(door, v, lambda x: x == 75)
    f100, n100 = frac(door, v, lambda x: x == 100)
    if detuned:   # negative control: the stretched windows no longer match period_windows
        results.append((n75 == 0, f'negative control: door cells Periodic (75): {n75}/{len(door)} (need 0)'))
        results.append((f100 >= 0.95, f'negative control: door cells Static (100): {n100}/{len(door)} '
                                      f'= {f100:.3f} (need >= 0.95)'))
    else:
        results.append((f75 >= 0.95, f'door cells Periodic (75): {n75}/{len(door)} = {f75:.3f} (need >= 0.95; '
                                     f'{len(skipped)} door cells the open-door ray skips are not tested)'))
        results.append((n100 == 0, f'door cells Static (100): {n100}/{len(door)} (need 0)'))
    ever = sum(1 for c in mover if any(val(m)(c) == 100 for m in maps))
    results.append((ever == 0, f'mover cells Static in any of {len(maps)} maps: {ever}/{len(mover)} (need 0)'))
    seen = sum(1 for c in mover if any(val(m)(c) == 50 for m in maps))
    results.append((seen > 0, f'mover cells seen Transient (50) in some map: {seen}/{len(mover)} (need > 0)'))
    f, n = frac(mover, v, lambda x: x == 0)
    results.append((f >= 0.90, f'mover cells read free (0) at the end: {n}/{len(mover)} = {f:.3f} (need >= 0.90)'))
    f, n = frac(mover, v, lambda x: x in (75, 100))
    results.append((n == 0, f'mover cells Periodic or Static at the end: {n}/{len(mover)} (need 0)'))
    # Free space: cells on wall rays (not wall or mover cells) must read free (0) in
    # the final map and never read as an obstacle in any map.
    free = set()
    for d in wall_bearings():
        free |= bresenham(*s_cell, *grid_xy(final, *point(WALL_R, d)[:2]))
    free = sorted(free - set(wall) - set(mover) - {s_cell})
    f, n = frac(free, v, lambda x: x == 0)
    results.append((f >= 0.99, f'free cells on wall rays read free (0) at the end: {n}/{len(free)} = {f:.3f} (need >= 0.99)'))
    blocked = sum(1 for c in free if any(val(m)(c) in (50, 75, 100) for m in maps))
    results.append((blocked == 0, f'free cells drawn as an obstacle in any of {len(maps)} maps: {blocked}/{len(free)} (need 0)'))
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


def check_voxel(maps, final, size, detuned=False):
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
    if detuned:   # negative control: the door graduates into the static map
        results.append((n / len(door) >= 0.95, f'negative control: door voxels in the final static map: '
                                               f'{n}/{len(door)} (need >= 0.95)'))
    else:
        results.append((n == 0, f'door voxels in the final static map: {n}/{len(door)} (need 0; '
                                f'{len(skipped)} door voxels the open-door ray skips are not tested)'))
    n = len(mover & static_ever)
    results.append((n == 0, f'mover voxels in any of {len(maps)} static maps: {n}/{len(mover)} (need 0)'))
    return results


def level(status):
    """DiagnosticStatus.level is a byte; rclpy may hand it over as bytes or int."""
    return status.level if isinstance(status.level, int) else ord(status.level)


def save_map(node, timeout):
    client = node.create_client(Trigger, '/strata/save_map')
    if not client.wait_for_service(timeout_sec=timeout):
        sys.exit('FAIL: /strata/save_map not available')
    future = client.call_async(Trigger.Request())
    rclpy.spin_until_future_complete(node, future, timeout_sec=timeout)
    if future.result() is None:
        sys.exit('FAIL: /strata/save_map did not answer')
    return future.result()


ROUNDTRIP_NS = '/strata_e2e_roundtrip'   # map_server's namespace, so its map topic cannot collide
# What nav2_map_server must serve for each published value, given the PGM shades
# onSave writes (free 254, transient/periodic 100, static 0, unknown 205) and the
# thresholds beside them (occupied_thresh 0.65, free_thresh 0.196, trinary mode).
SERVED_FOR = {0: 0, 100: 100, 50: -1, 75: -1, -1: -1}


def wait_future(node, future, timeout, what):
    rclpy.spin_until_future_complete(node, future, timeout_sec=timeout)
    if future.result() is None:
        raise RuntimeError(f'{what} did not answer within {timeout:g} s')
    return future.result()


def stop_group(proc):
    """SIGTERM the subprocess's process group, SIGKILL if it lingers."""
    try:
        os.killpg(proc.pid, signal.SIGTERM)
        proc.wait(timeout=10)
    except ProcessLookupError:
        pass
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()


def serve_saved_map(node, yaml_path, timeout):
    """Load the saved YAML in nav2_map_server, bring it active, return the map it serves."""
    from lifecycle_msgs.msg import Transition
    from lifecycle_msgs.srv import ChangeState
    log = open('map_server_roundtrip.log', 'wb')
    proc = subprocess.Popen(
        ['ros2', 'run', 'nav2_map_server', 'map_server', '--ros-args',
         '-r', f'__ns:={ROUNDTRIP_NS}', '-r', '__node:=map_server',
         '-p', f'yaml_filename:={yaml_path}'],
        stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        served = []
        qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE,
                         durability=DurabilityPolicy.TRANSIENT_LOCAL)
        sub = node.create_subscription(OccupancyGrid, f'{ROUNDTRIP_NS}/map', served.append, qos)
        client = node.create_client(ChangeState, f'{ROUNDTRIP_NS}/map_server/change_state')
        if not client.wait_for_service(timeout_sec=timeout):
            raise RuntimeError(f'{ROUNDTRIP_NS}/map_server/change_state not available')
        for tid, name in ((Transition.TRANSITION_CONFIGURE, 'configure'),
                          (Transition.TRANSITION_ACTIVATE, 'activate')):
            req = ChangeState.Request()
            req.transition.id = tid
            if not wait_future(node, client.call_async(req), timeout, f'map_server {name}').success:
                raise RuntimeError(f'map_server failed to {name} with {yaml_path}')
        end = time.monotonic() + timeout
        while not served and time.monotonic() < end:
            rclpy.spin_once(node, timeout_sec=0.1)
        node.destroy_subscription(sub)
        node.destroy_client(client)
        if not served:
            raise RuntimeError(f'no {ROUNDTRIP_NS}/map from the active map_server')
        return served[-1]
    finally:
        stop_group(proc)
        log.close()


def check_roundtrip(published, served):
    """Compare the map the node published with the one map_server serves from the saved file."""
    pi, si = published.info, served.info
    results = []
    same_size = (pi.width, pi.height) == (si.width, si.height)
    geo = max(abs(pi.resolution - si.resolution), abs(pi.origin.position.x - si.origin.position.x),
              abs(pi.origin.position.y - si.origin.position.y))
    results.append((same_size and geo <= 1e-9,
                    f'map_server info: {si.width}x{si.height}, resolution {si.resolution:g}, origin '
                    f'({si.origin.position.x:.17g}, {si.origin.position.y:.17g}) vs published {pi.width}x{pi.height}, '
                    f'{pi.resolution:g}, ({pi.origin.position.x:.17g}, {pi.origin.position.y:.17g}) '
                    f'(need equal size; resolution and origin within 1e-9, max diff {geo:.3g})'))
    if same_size:
        bad = [(i, p, s) for i, (p, s) in enumerate(zip(published.data, served.data))
               if SERVED_FOR.get(p) != s]
        pub_hist = dict(sorted(collections.Counter(published.data).items()))
        results.append((not bad, f'map_server cell by cell over {len(published.data)} cells, published {pub_hist} '
                                 f'(0->0, 100->100, 50/75/-1 -> -1): {len(bad)} mismatches (need 0)'
                                 + (f'; first (index, published, served): {bad[:5]}' if bad else '')))
    else:
        results.append((False, 'map_server cell by cell: not compared, the sizes differ'))
    hist = dict(sorted(collections.Counter(served.data).items()))
    results.append((set(hist) <= {-1, 0, 100}, f'map_server served values {hist} (need a subset of -1, 0, 100)'))
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('backend', choices=['grid2d', 'voxel3d'])
    ap.add_argument('params', help='the params YAML the node was launched with')
    ap.add_argument('--windows', type=int, default=72)
    ap.add_argument('--timeout', type=float, default=30.0)
    ap.add_argument('--rate', type=float, default=None,
                    help='scans per second (default 50 for grid2d, 10 for voxel3d). The sensor '
                         'subscription is best effort, so a rate the node cannot keep up with drops '
                         'scans, stretches every window and detunes the door from period_windows')
    ap.add_argument('--drop', type=float, default=0.0,
                    help='share of scans withheld (deterministic pattern), as a lossy link would')
    ap.add_argument('--check-timeout', action='store_true',
                    help='after the stream, wait input_timeout_s and expect an ERROR status')
    ap.add_argument('--expect-detuned', action='store_true',
                    help='negative control: expect the door Static, not Periodic (scan windows under loss)')
    ap.add_argument('--check-map-server', action='store_true',
                    help='grid2d: load the saved YAML in nav2_map_server and compare the map it serves, '
                         'cell by cell, with the last map the node published')
    ap.add_argument('--keep-map', metavar='DIR',
                    help='with --check-map-server: copy the saved pair (as moved, before map_server '
                         'loads it) into DIR, e.g. to run scripts/check_saved_map.py on it')
    args = ap.parse_args()
    if args.check_map_server and args.backend != 'grid2d':
        ap.error('--check-map-server applies to grid2d only')
    if args.keep_map and not args.check_map_server:
        ap.error('--keep-map needs --check-map-server')

    params = {}
    for section in (yaml.safe_load(open(args.params)) or {}).values():
        params.update((section or {}).get('ros__parameters', {}))

    rclpy.init()
    node = rclpy.create_node('strata_synthetic_e2e', parameter_overrides=[])
    rate = args.rate or (50.0 if args.backend == 'grid2d' else 10.0)
    drv = Driver(node, args.backend, params, rate)
    drv.wait_for_subscriber(args.timeout)
    mode = params.get('window_mode', 'scans')
    if mode == 'time':
        spw = float(params.get('window_period_s', 1.0)) * rate
        if abs(spw - round(spw)) > 1e-6 or round(spw) < 1:
            sys.exit(f'FAIL: window_period_s x rate = {spw:g} scans per window; pick a whole number')
        interval = int(round(spw))
    else:
        interval = int(params.get('layer_interval', 10))
    period, sent = drv.run(args.windows, interval, args.drop)
    print(f'{args.backend}: window_mode {mode}, {args.windows} windows x {interval} scans at {rate:g} Hz, '
          f'{sent} sent ({args.drop:.0%} withheld), door period {period} windows')
    final = drv.latest_map(len(drv.maps), args.timeout)

    if args.backend == 'grid2d':
        results = check_grid(drv.maps, final, args.expect_detuned)
    else:
        results = check_voxel(drv.maps, final, float(params.get('voxel_size', 0.2)), args.expect_detuned)

    # /diagnostics: the node's own estimate of the loss and its warning.
    end = time.monotonic() + 3.0
    while time.monotonic() < end:
        rclpy.spin_once(node, timeout_sec=0.1)
    def vals(s):
        return {kv.key: kv.value for kv in s.values}
    # Statuses taken while the stream ran and after the first windows closed.
    live = [s for _, s in drv.diags if float(vals(s).get('seconds_since_last_message', 99)) < 1.0
            and int(vals(s).get('windows_closed_total', 0)) >= 3]
    if not live:
        results.append((False, 'no "<node>: input rate" status on /diagnostics during the stream'))
    else:
        last = vals(live[-1])
        est = float(last.get('recent_drop_fraction', 'nan'))
        warned = any(level(s) == 1 for s in live)
        results.append((last.get('window_mode') == mode,
                        f'diagnostics window_mode: {last.get("window_mode")} (expected {mode})'))
        results.append((abs(est - args.drop) <= 0.05,
                        f'diagnostics recent_drop_fraction {est:.3f} vs withheld {args.drop:.3f} (need within 0.05); '
                        f'input_rate_hz {last.get("input_rate_hz")}, effective_period_s {last.get("effective_period_s")}'))
        # Scan windows warn on loss (they detune); time windows keep the period and warn
        # only on starved or empty windows, which 40 % loss at 10 scans per window is not.
        should_warn = mode == 'scans' and args.drop > float(params.get('rate_warn_drop_fraction', 0.05))
        results.append((warned == should_warn,
                        f'diagnostics warned: {warned} (expected {should_warn}) over {len(live)} live statuses'
                        + (f'; e.g. "{next(s.message for s in live if level(s) == 1)}"' if warned else '')))
    if args.check_timeout:
        timeout_s = float(params.get('input_timeout_s', 5.0))
        end = time.monotonic() + timeout_s + 3.0
        while time.monotonic() < end:
            rclpy.spin_once(node, timeout_sec=0.1)
        tail = [s for _, s in drv.diags][-1:]
        ok = bool(tail) and level(tail[0]) == 2 and tail[0].message.startswith('no input')
        results.append((ok, f'input timeout: last status level {level(tail[0]) if tail else None}, '
                            f'"{tail[0].message if tail else ""}" (need ERROR "no input ...")'))

    before_save = drv.maps[-1]   # the stream has ended, so this is the state the file captures
    res = save_map(node, args.timeout)
    saved = res.message.replace('saved ', '').split(' + ')[0]
    results.append((res.success and os.path.getsize(saved) > 0, f'save_map: {res.message}'))
    if args.backend == 'grid2d' and res.success:
        with open(saved, 'rb') as fh:
            raw = fh.read()
        parts = raw.split(b'\n', 3)                          # P5, "W H", 255, pixels
        pixels = parts[3]
        free_px, occ_px = pixels.count(bytes([254])), pixels.count(bytes([0]))
        results.append((free_px > 0 and occ_px > 0,
                        f'saved PGM has free (254) and occupied (0) pixels: {free_px} free, {occ_px} occupied'))
    if args.check_map_server and res.success:
        # The node keeps publishing on its timer. With no input since the stream ended,
        # the map published after the save must equal the one before it; compare
        # map_server against that map, so it is the state the saved file holds.
        after_save = drv.latest_map(len(drv.maps), args.timeout)
        stable = (after_save.info == before_save.info and list(after_save.data) == list(before_save.data))
        results.append((stable, f'published map unchanged across save_map (needed to compare it with the file): {stable}'))
        # Move the pair to a fresh directory first: map_server must find the image
        # relative to the YAML, as it would after the map is copied to a robot.
        moved_dir = tempfile.mkdtemp(prefix='strata_moved_map_')
        try:
            base = os.path.splitext(saved)[0]
            for ext in ('.pgm', '.yaml'):
                shutil.move(base + ext, os.path.join(moved_dir, os.path.basename(base) + ext))
            yaml_path = os.path.join(moved_dir, os.path.basename(base) + '.yaml')
            results.append((not os.path.exists(base + '.pgm'), f'saved pair moved to {moved_dir} before loading'))
            if args.keep_map:
                os.makedirs(args.keep_map, exist_ok=True)
                for ext in ('.pgm', '.yaml'):
                    shutil.copy(os.path.join(moved_dir, os.path.basename(base) + ext), args.keep_map)
                print(f'kept a copy of the saved pair in {args.keep_map}')
            results += check_roundtrip(after_save, serve_saved_map(node, os.path.abspath(yaml_path), args.timeout))
        except RuntimeError as e:
            tail = open('map_server_roundtrip.log', errors='replace').read()[-1500:]
            results.append((False, f'map_server round trip: {e}\n{tail}'))
        finally:
            shutil.rmtree(moved_dir, ignore_errors=True)

    for ok, line in results:
        print(('ok    ' if ok else 'FAIL  ') + line)
    node.destroy_node()
    rclpy.shutdown()
    if not all(ok for ok, _ in results):
        sys.exit('FAIL: synthetic end-to-end')
    print('PASS: the node maps the synthetic scene as expected')


if __name__ == '__main__':
    main()
