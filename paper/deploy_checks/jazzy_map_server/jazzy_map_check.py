#!/usr/bin/env python3
"""Load a map pair saved by strata (on Humble) in another distribution's nav2_map_server.

strata's ~/save_map writes two extra YAML keys (strata_image_bytes, strata_image_sha256).
This starts the sourced distribution's map_server on that YAML (own namespace, lifecycle
configure + activate), reads the map it serves and compares it with the PGM, decoded here
with map_server's documented trinary rule:

    occ = (255 - pixel) / 255;  occ > occupied_thresh -> 100;  occ < free_thresh -> 0;  else -1
    PGM row 0 is the TOP of the map (largest y)

Pass: configure and activate succeed; size, resolution (as float32, the type of
MapMetaData.resolution) and origin equal the YAML's; 0 cell mismatches; the map_server
log has no ERROR line and does not name the two extra keys.

Usage: jazzy_map_check.py MAP_YAML        (run inside a sourced ROS 2 environment)
It is not part of CI. One recorded run is in recorded_run.txt (see README.md).
"""
import collections
import struct
import os
import signal
import subprocess
import sys
import time

import rclpy
import yaml
from lifecycle_msgs.msg import Transition
from lifecycle_msgs.srv import ChangeState
from nav_msgs.msg import OccupancyGrid
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy

NS = '/strata_saved_map_check'


def read_pgm(path):
    raw = open(path, 'rb').read()
    tok, pos = [], 0
    while len(tok) < 4:                      # P5, width, height, maxval (comments allowed)
        while raw[pos:pos + 1].isspace():
            pos += 1
        if raw[pos:pos + 1] == b'#':
            pos = raw.index(b'\n', pos) + 1
            continue
        end = pos
        while not raw[end:end + 1].isspace():
            end += 1
        tok.append(raw[pos:end])
        pos = end
    assert tok[0] == b'P5', tok[0]
    w, h, maxval = int(tok[1]), int(tok[2]), int(tok[3])
    assert maxval == 255
    pix = raw[pos + 1:]
    assert len(pix) == w * h, (len(pix), w, h)
    return w, h, pix


def main():
    ypath = os.path.abspath(sys.argv[1])
    y = yaml.safe_load(open(ypath))
    extra = sorted(k for k in y if k.startswith('strata_'))
    print(f'ROS_DISTRO={os.environ.get("ROS_DISTRO")}; YAML keys: {sorted(y)}; extra keys: {extra}')
    ver = subprocess.run(['ros2', 'pkg', 'xml', 'nav2_map_server', '-t', 'version'], capture_output=True, text=True)
    print(f'nav2_map_server {ver.stdout.strip()}')
    w, h, pix = read_pgm(os.path.join(os.path.dirname(ypath), y['image']))
    assert y.get('mode', 'trinary') == 'trinary' and int(y['negate']) == 0
    expect = [0] * (w * h)
    for r in range(h):
        for c in range(w):
            occ = (255 - pix[r * w + c]) / 255.0
            v = 100 if occ > y['occupied_thresh'] else (0 if occ < y['free_thresh'] else -1)
            expect[(h - 1 - r) * w + c] = v

    log = open('jazzy_map_server.log', 'w')
    proc = subprocess.Popen(['ros2', 'run', 'nav2_map_server', 'map_server', '--ros-args', '-r', f'__ns:={NS}',
                             '-p', f'yaml_filename:={ypath}'], stdout=log, stderr=subprocess.STDOUT,
                            start_new_session=True)
    rclpy.init()
    node = rclpy.create_node('strata_saved_map_check')
    served = []
    qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE, durability=DurabilityPolicy.TRANSIENT_LOCAL)
    node.create_subscription(OccupancyGrid, f'{NS}/map', served.append, qos)
    results = []
    try:
        cli = node.create_client(ChangeState, f'{NS}/map_server/change_state')
        ready = cli.wait_for_service(timeout_sec=30.0)
        results.append((ready, f'{NS}/map_server/change_state available: {ready}'))
        for tid, name in ((Transition.TRANSITION_CONFIGURE, 'configure'), (Transition.TRANSITION_ACTIVATE, 'activate')):
            ok = False
            if ready:
                req = ChangeState.Request()
                req.transition.id = tid
                fut = cli.call_async(req)
                end = time.monotonic() + 30.0
                while not fut.done() and time.monotonic() < end:
                    rclpy.spin_once(node, timeout_sec=0.1)
                ok = fut.done() and fut.result() is not None and fut.result().success
            results.append((ok, f'map_server {name}: {ok}'))
        end = time.monotonic() + 15.0
        while not served and time.monotonic() < end:
            rclpy.spin_once(node, timeout_sec=0.1)
        results.append((bool(served), f'{NS}/map received: {bool(served)}'))
        if served:
            m = served[-1]
            i = m.info
            res32 = struct.unpack('f', struct.pack('f', y['resolution']))[0]
            geo = max(abs(i.origin.position.x - y['origin'][0]), abs(i.origin.position.y - y['origin'][1]))
            results.append(((i.width, i.height) == (w, h) and geo <= 1e-12 and i.resolution == res32,
                            f'served {i.width}x{i.height}, resolution {i.resolution!r}, origin '
                            f'({i.origin.position.x!r}, {i.origin.position.y!r}) vs PGM {w}x{h}, YAML resolution '
                            f'{y["resolution"]!r} (as float32 {res32!r}), origin {y["origin"][:2]} '
                            f'(origin max diff {geo:.3g}, need <= 1e-12; resolution equal as float32)'))
            if (i.width, i.height) == (w, h):
                bad = [(k, e, s) for k, (e, s) in enumerate(zip(expect, m.data)) if e != s]
                hist = dict(sorted(collections.Counter(m.data).items()))
                results.append((not bad, f'cell by cell over {len(expect)} cells, served values {hist}: '
                                         f'{len(bad)} mismatches against the PGM (need 0)'
                                         + (f'; first {bad[:5]}' if bad else '')))
    finally:
        node.destroy_node()
        rclpy.shutdown()
        try:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=10)
        except (ProcessLookupError, subprocess.TimeoutExpired):
            os.killpg(proc.pid, signal.SIGKILL)
        log.close()
    text = open('jazzy_map_server.log', errors='replace').read()
    errs = [ln for ln in text.splitlines() if '[ERROR]' in ln or '[FATAL]' in ln]
    mention = [ln for ln in text.splitlines() if 'strata_image' in ln]
    results.append((not errs, f'map_server log ERROR lines: {errs[:3] if errs else "none"}'))
    results.append((not mention, f'map_server log lines naming the extra keys: {mention[:3] if mention else "none"}'))
    for ok, line in results:
        print(('PASS: ' if ok else 'FAIL: ') + line)
    if not all(ok for ok, _ in results):
        sys.exit('FAIL: map_server on the strata-saved pair')
    print('PASS: map_server loads the strata-saved pair, 0 cell mismatches')


if __name__ == '__main__':
    main()
