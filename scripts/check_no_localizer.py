#!/usr/bin/env python3
"""/diagnostics must say what the input is doing in every state, from the first status.

Walks a running node (wall clock, no /clock) through the input states and checks the
level, the message and the values of every `<node>: input rate` status on the way:

  A  nothing published             OK    "waiting for input on <topic>"
  B  scans, no TF (no localizer)   OK    "no usable TF <frame> -> sensor yet: N message(s)
                                          received on <topic>, 0 integrated, ..."
  S  scans, a TF that is published OK    the same message (it does not say the transform
     but 5 s behind the scans             is missing: it is there, only stale), until
                                          startup_timeout_s; then WARN "messages arrive on
                                          <topic> but the TF lookup ... has failed N times".
                                          last_tf_error is tf2's text: an extrapolation
  C  scans with TF                 OK    "ok", the failure streak back at 0, no stale text,
                                          last_tf_error empty again
  D  scans, TF stopped again       OK    "TF lookup <frame> -> sensor failing: last N
                                          message(s) ..." once nothing integrated for 1 s,
                                          ERROR "messages arrive ..." after input_timeout_s
  E  nothing published             ERROR "no input on <topic> for ..."

and that messages_received_total, messages_integrated_total, tf_failures_total,
tf_failures_since_integrate, seconds_since_last_integrate and last_tf_error are in every
status, after the values that were there before, whose names and order must not change
(the window values such as tf_failures_in_window only appear once a window has closed,
and no window closes while nothing integrates). last_tf_error is one line of at most
120 bytes, and is set exactly while tf_failures_since_integrate is above 0.

Usage: check_no_localizer.py <grid2d|voxel3d> <params.yaml>
Run against `ros2 launch strata <backend>.launch.py rviz:=false params_file:=<params.yaml>`,
started just before; give the YAML a short startup_timeout_s (CI uses 15) so phase B is short.
"""
import argparse
import math
import struct
import sys
import time

import rclpy
import rclpy.duration
import yaml
from diagnostic_msgs.msg import DiagnosticArray
from geometry_msgs.msg import TransformStamped
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import LaserScan, PointCloud2, PointField
from tf2_ros import TransformBroadcaster

ALWAYS = ['messages_received_total', 'messages_integrated_total', 'tf_failures_total',
          'tf_failures_since_integrate']
NEW = ALWAYS + ['seconds_since_last_integrate', 'last_tf_error']
# The values published before the input counts were added: names and order are fixed.
WINDOW = ['window_mode', 'sensors', 'scans_in_window', 'empty_windows', 'window_duration_s', 'input_rate_hz',
          'nominal_rate_hz', 'dropped_in_window', 'tf_failures_in_window', 'recent_drop_fraction',
          'mean_window_duration_s', 'window_duration_cv', 'effective_period_s']
TAIL = ['seconds_since_last_message', 'windows_closed_total']
EDGE = 0.05   # statuses this close to a threshold are not judged (values are printed to 0.01 s)
STALE_S = 5.0  # phase S: the transform is stamped this far behind the message


def level(status):
    return status.level if isinstance(status.level, int) else ord(status.level)


def vals(status):
    return {kv.key: kv.value for kv in status.values}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('backend', choices=['grid2d', 'voxel3d'])
    ap.add_argument('params', help='the params YAML the node was launched with')
    ap.add_argument('--node', default='/strata')
    ap.add_argument('--rate', type=float, default=10.0, help='messages per second')
    args = ap.parse_args()
    params = {}
    for section in (yaml.safe_load(open(args.params)) or {}).values():
        params.update((section or {}).get('ros__parameters', {}))
    startup = float(params.get('startup_timeout_s', 30.0))
    timeout = float(params.get('input_timeout_s', 5.0))
    frame = params.get('global_frame', 'map')
    grid = args.backend == 'grid2d'
    topic = params.get('scan_topic', '/scan') if grid else params.get('points_topic', '/points')

    rclpy.init()
    node = rclpy.create_node('strata_no_localizer_check')
    pub = node.create_publisher(LaserScan if grid else PointCloud2, topic, qos_profile_sensor_data)
    tf = TransformBroadcaster(node)
    name = args.node.rsplit('/', 1)[-1] + ': input rate'
    phase = ['A']
    diags = []   # (phase, seconds into the phase, status)
    phase_start = [time.monotonic()]
    node.create_subscription(
        DiagnosticArray, '/diagnostics',
        lambda m: diags.extend((phase[0], time.monotonic() - phase_start[0], s) for s in m.status if s.name == name), 50)
    end = time.monotonic() + 30.0
    while pub.get_subscription_count() == 0:
        if time.monotonic() > end:
            sys.exit(f'FAIL: the node never subscribed to {topic}')
        rclpy.spin_once(node, timeout_sec=0.1)

    if grid:
        msg = LaserScan()
        msg.angle_min, msg.angle_increment = 0.0, math.radians(1.0)
        msg.angle_max = math.radians(359.0)
        msg.range_min, msg.range_max = 0.1, 20.0
        msg.ranges = [5.0] * 360
    else:
        pts = [(5.0 * math.cos(a), 5.0 * math.sin(a), 0.5) for a in (0.0, 1.0, 2.0, 3.0)]
        msg = PointCloud2()
        msg.height, msg.width = 1, len(pts)
        msg.fields = [PointField(name=n, offset=4 * i, datatype=PointField.FLOAT32, count=1)
                      for i, n in enumerate('xyz')]
        msg.point_step, msg.row_step = 12, 12 * len(pts)
        msg.is_dense = True
        msg.data = b''.join(struct.pack('<fff', *p) for p in pts)
    msg.header.frame_id = 'sensor'

    def enter(p):
        phase[0] = p
        phase_start[0] = time.monotonic()

    def run(seconds, scans, with_tf, until=None):
        """Spin for `seconds` (or until `until()`), publishing at --rate if `scans`."""
        nxt = time.monotonic()
        end = nxt + seconds
        while time.monotonic() < end and not (until and until()):
            if scans:
                stamp = node.get_clock().now().to_msg()
                if with_tf:   # the transform at the message stamp (or STALE_S behind it), sent first
                    t = TransformStamped()
                    t.header.stamp = stamp if with_tf != 'stale' else (
                        node.get_clock().now() - rclpy.duration.Duration(seconds=STALE_S)).to_msg()
                    t.header.frame_id, t.child_frame_id = frame, 'sensor'
                    t.transform.translation.x, t.transform.translation.y = 0.013, 0.021
                    t.transform.rotation.w = 1.0
                    tf.sendTransform(t)
                msg.header.stamp = stamp
                pub.publish(msg)
            nxt += 1.0 / args.rate
            while time.monotonic() < nxt:
                rclpy.spin_once(node, timeout_sec=max(0.0, min(0.05, nxt - time.monotonic())))

    def seen(p, after=0.0):
        return [s for q, t, s in diags if q == p and t >= after]

    def past_startup_twice():   # two statuses clear of the threshold, whatever they say
        return sum(1 for s in seen('S')
                   if float(vals(s).get('seconds_since_last_integrate', 0.0)) > startup + EDGE) >= 2

    enter('A'); run(2.5, False, False)
    enter('B'); run(5.0, True, False)
    enter('S'); run(startup + 20.0, True, 'stale', until=past_startup_twice)
    enter('C'); run(6.0, True, True)
    enter('D'); run(timeout + 4.0, True, False)
    enter('E'); run(timeout + 3.0, False, False)

    results = []

    def check(ok, line):
        results.append((bool(ok), line))

    def show(s):
        v = vals(s)
        return (f'level {level(s)} "{s.message}" ' + ' '.join(f'{k}={v.get(k)}' for k in ALWAYS)
                + f' seconds_since_last_integrate={v.get("seconds_since_last_integrate")}'
                + f' last_tf_error="{v.get("last_tf_error")}"')

    # Every status, whatever the state, carries the five values as plain numbers.
    everything = [s for _, _, s in diags]
    bad = []
    for s in everything:
        v = vals(s)
        try:
            ok = all(int(v[k]) >= 0 for k in ALWAYS) and float(v['seconds_since_last_integrate']) >= 0.0
            ok = ok and int(v['windows_closed_total']) >= 0 and float(v['seconds_since_last_message']) >= 0.0
            # The names and their order: the earlier values first, unchanged, then the new ones.
            names = [kv.key for kv in s.values]
            ok = ok and names in (['window_mode'] + TAIL + NEW, WINDOW + TAIL + NEW)
            # The cause of the running streak: one bounded line, set exactly while it runs.
            err = v['last_tf_error']
            ok = ok and (err != '') == (int(v['tf_failures_since_integrate']) > 0)
            ok = ok and len(err.encode()) <= 120 and err == ' '.join(err.split()) and err.isprintable()
        except (KeyError, ValueError):
            ok = False
        if not ok:
            bad.append(s)
    closed = sum(1 for s in everything if [kv.key for kv in s.values] == WINDOW + TAIL + NEW)
    check(everything and not bad and closed >= 2,
          f'{len(everything)} statuses, all with the {len(TAIL) + 1} or (after a window close, {closed} of them) '
          f'{len(WINDOW + TAIL)} earlier values in their order, then {", ".join(NEW)}; last_tf_error one line of at '
          'most 120 bytes, set exactly while the streak is above 0'
          + (f'; first that is not: {[(kv.key, kv.value) for kv in bad[0].values]}' if bad else ''))
    if bad or not everything:
        for ok, line in results:
            print(('ok    ' if ok else 'FAIL  ') + line)
        sys.exit('FAIL: /diagnostics does not carry the input counts in every status')

    def n(s, k):
        return int(vals(s)[k])

    def since_integrate(s):
        return float(vals(s)['seconds_since_last_integrate'])

    # A: no input yet.
    a = seen('A')
    wrong = [s for s in a if not (level(s) == 0 and s.message == f'waiting for input on {topic}'
                                  and all(n(s, k) == 0 for k in ALWAYS) and n(s, 'windows_closed_total') == 0
                                  and 'tf_failures_in_window' not in vals(s) and vals(s)['last_tf_error'] == '')]
    check(a and not wrong, f'A no input: {len(a)} statuses, all OK "waiting for input on {topic}" with every '
          'count 0, no window values and an empty last_tf_error' + (f'; got {show(wrong[0])}' if wrong else ''))

    # B and S: messages arrive and cannot be placed, first without any transform, then with one
    # that is 5 s too old. OK with a truthful message inside startup_timeout_s, WARN after.
    b = seen('B', 1.5) + seen('S')
    waiting = [s for s in b if since_integrate(s) <= startup - EDGE]
    late = [s for s in b if since_integrate(s) > startup + EDGE]
    wrong = []
    for s in waiting:
        r, f = n(s, 'messages_received_total'), n(s, 'tf_failures_since_integrate')
        want = (f'no usable TF {frame} -> sensor yet: {r} message(s) received on {topic}, 0 integrated, '
                f'{f} TF lookup(s) failed (WARN after startup_timeout_s = {startup:.0f} s)')
        if not (level(s) == 0 and s.message == want):
            wrong.append(s)
    check(len(waiting) >= 2 and not wrong,
          f'B+S scans that cannot be placed, first {startup:g} s: {len(waiting)} statuses (need >= 2), all OK "no usable '
          f'TF {frame} -> sensor yet: N message(s) received on {topic}, 0 integrated, N TF lookup(s) failed ..."'
          + (f'; got {show(wrong[0])}' if wrong else (f'; e.g. {show(waiting[-1])}' if waiting else '')))
    wrong = [s for s in late if not (level(s) == 1 and s.message.startswith(
        f'messages arrive on {topic} but the TF lookup {frame} -> sensor has failed '
        f'{n(s, "tf_failures_since_integrate")} times; nothing integrated for '))]
    check(len(late) >= 2 and not wrong,
          f'S after startup_timeout_s: {len(late)} statuses (need >= 2), all WARN "messages arrive on {topic} but '
          f'the TF lookup ..."' + (f'; got {show(wrong[0])}' if wrong else (f'; e.g. {show(late[-1])}' if late else '')))
    wrong = [s for s in b if not (n(s, 'messages_integrated_total') == 0 and n(s, 'messages_received_total') > 0
                                  and n(s, 'tf_failures_total') == n(s, 'messages_received_total')
                                  and n(s, 'tf_failures_since_integrate') == n(s, 'tf_failures_total')
                                  and n(s, 'windows_closed_total') == 0 and 'tf_failures_in_window' not in vals(s))]
    grows = len(b) >= 2 and n(b[-1], 'tf_failures_since_integrate') > n(b[0], 'tf_failures_since_integrate')
    check(b and not wrong and grows,
          'B+S values: received > 0, integrated 0, tf_failures_total = tf_failures_since_integrate = received, '
          'growing, and no window has closed'
          + (f'; got {show(wrong[0])}' if wrong else (f'; {show(b[0])} ... {show(b[-1])}' if b else '')))
    failed_in_b = n(b[-1], 'tf_failures_total') if b else 0
    # The cause is a value, not a claim in the message: no transform at all in B, a stale one in S.
    none = [vals(s)['last_tf_error'] for s in seen('B', 1.5)]
    stale = [s for s in seen('S', 2.0)]
    wrong = [s for s in stale if 'extrapolation' not in vals(s)['last_tf_error'].lower()
             or any(w in s.message.lower() for w in ('waiting', 'missing', 'does not exist', 'not published'))]
    check(len(none) >= 2 and all(none) and not any('extrapolation' in e.lower() for e in none)
          and len(stale) >= 2 and not wrong,
          f'last_tf_error: {len(none)} statuses without any TF (need >= 2) give a cause that is not an extrapolation '
          f'("{none[-1] if none else ""}"); {len(stale)} with a TF {STALE_S:g} s behind (need >= 2) give an '
          'extrapolation, and the message does not say the transform is missing'
          + (f'; got {show(wrong[0])}' if wrong else (f'; e.g. {show(stale[-1])}' if stale else '')))

    # C: the localizer is up. Back to "ok", the streak at 0, the total kept.
    c = seen('C', 2.5)
    wrong = [s for s in c if not (n(s, 'messages_integrated_total') > 0 and n(s, 'tf_failures_since_integrate') == 0
                                  and n(s, 'tf_failures_total') >= failed_in_b
                                  and n(s, 'tf_failures_total') == n(c[0], 'tf_failures_total')
                                  and since_integrate(s) < 1.0 and 'TF' not in s.message
                                  and vals(s)['last_tf_error'] == '')]
    plain = [s for s in c if level(s) == 0 and s.message == 'ok']
    check(len(c) >= 2 and not wrong and plain and n(c[-1], 'messages_integrated_total') > n(c[0], 'messages_integrated_total'),
          f'C scans with TF: {len(c)} statuses (need >= 2), integrating, tf_failures_since_integrate 0, '
          f'tf_failures_total kept (>= {failed_in_b}), no TF text in the message, last_tf_error empty, '
          f'{len(plain)} of them OK "ok"'
          + (f'; got {show(wrong[0])}' if wrong else (f'; e.g. {show(c[-1])}' if c else '')))

    # D: TF lost in mid-run. Named in the message after 1 s, ERROR after input_timeout_s.
    d = seen('D')
    note = [s for s in d if 1.0 + EDGE < since_integrate(s) <= timeout - EDGE and n(s, 'tf_failures_since_integrate') > 0]
    lost = [s for s in d if since_integrate(s) > timeout + EDGE]
    wrong = [s for s in note if not (level(s) <= 1 and (
        f'TF lookup {frame} -> sensor failing: last {n(s, "tf_failures_since_integrate")} message(s) on {topic} '
        f'not integrated, nothing integrated for ') in s.message
        and s.message.endswith(f'(ERROR after input_timeout_s = {timeout:.0f} s)')
        and 'extrapolation' in vals(s)['last_tf_error'].lower())]
    check(len(note) >= 2 and not wrong and any(level(s) == 0 for s in note),
          f'D TF stopped, 1 s to {timeout:g} s: {len(note)} statuses (need >= 2) saying "TF lookup {frame} -> sensor '
          f'failing: last N message(s) on {topic} not integrated ...", level OK, last_tf_error an extrapolation'
          + (f'; got {show(wrong[0])}' if wrong else (f'; e.g. {show(note[-1])}' if note else '')))
    wrong = [s for s in lost if not (level(s) == 2 and s.message.startswith(
        f'messages arrive on {topic} but the TF lookup {frame} -> sensor has failed '
        f'{n(s, "tf_failures_since_integrate")} times; nothing integrated for ')
        and n(s, 'tf_failures_since_integrate') > 0
        and n(s, 'messages_integrated_total') == n(lost[0], 'messages_integrated_total'))]
    check(len(lost) >= 2 and not wrong,
          f'D after input_timeout_s: {len(lost)} statuses (need >= 2), all ERROR "messages arrive on {topic} but the '
          f'TF lookup ..."' + (f'; got {show(wrong[0])}' if wrong else (f'; e.g. {show(lost[-1])}' if lost else '')))

    # E: the sensor stops too.
    e = seen('E')[-1:]
    check(e and level(e[0]) == 2 and e[0].message.startswith(f'no input on {topic} for ')
          and n(e[0], 'tf_failures_since_integrate') > 0,
          'E no input: last status ERROR "no input on ...", the streak still reported'
          + (f'; got {show(e[0])}' if e else ''))

    for ok, line in results:
        print(('ok    ' if ok else 'FAIL  ') + line)
    node.destroy_node()
    rclpy.shutdown()
    if not all(ok for ok, _ in results):
        sys.exit('FAIL: /diagnostics does not describe the input in every state')
    print('PASS: /diagnostics names the input state, with the counts, in every status')


if __name__ == '__main__':
    main()
