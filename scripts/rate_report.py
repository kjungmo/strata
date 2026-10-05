#!/usr/bin/env python3
"""Summarise a running STRATA node's input-rate diagnostics, for calibrating a robot.

Listens to /diagnostics for --seconds, keeps the "<node>: input rate" statuses the node
publishes once a second (one per window close is kept: the values repeat between
closes), and prints min / median / max of each value, how often the node warned, and a
suggested setting:

  * window_mode "time": the periodic period is period_windows x window_period_s
    seconds whatever the load does; keep window_period_s long enough that a window
    holds several messages, and set period_windows from the period you want to detect;
  * window_mode "scans" is only safe when the loss is near zero and the window
    duration is steady (low window_duration_cv).

Usage (with the node running on the robot, real sensor input):
  python3 scripts/rate_report.py --seconds 600 [--params path/to/params.yaml]
"""
import argparse
import statistics
import time

import rclpy
import yaml
from diagnostic_msgs.msg import DiagnosticArray

NUMERIC = ['input_rate_hz', 'nominal_rate_hz', 'scans_in_window', 'window_duration_s',
           'dropped_in_window', 'recent_drop_fraction', 'window_duration_cv', 'effective_period_s']


def level(status):
    return status.level if isinstance(status.level, int) else ord(status.level)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--seconds', type=float, default=300.0)
    ap.add_argument('--params', help='the params YAML the node runs with')
    ap.add_argument('--target-period-s', type=float, default=None,
                    help='period of the cycles to detect (e.g. a door that opens every 60 s)')
    args = ap.parse_args()
    configured_rate, window_period, period_windows = 0.0, 1.0, 24
    if args.params:
        for section in (yaml.safe_load(open(args.params)) or {}).values():
            p = (section or {}).get('ros__parameters', {})
            window_period = float(p.get('window_period_s', window_period))
            period_windows = int(p.get('period_windows', period_windows))
            configured_rate = float(p.get('expected_scan_rate_hz', configured_rate))

    rclpy.init()
    node = rclpy.create_node('strata_rate_report')
    got = []
    node.create_subscription(DiagnosticArray, '/diagnostics',
                             lambda m: got.extend(s for s in m.status if s.name.endswith(': input rate')), 50)
    end = time.monotonic() + args.seconds
    while time.monotonic() < end:
        rclpy.spin_once(node, timeout_sec=0.2)
    node.destroy_node()
    rclpy.shutdown()
    if not got:
        raise SystemExit('no "<node>: input rate" status on /diagnostics: is the node running?')

    warns = [s.message for s in got if level(s) == 1]
    errors = [s.message for s in got if level(s) == 2]
    rows, seen = [], set()
    for s in got:                                        # one row per window close
        r = {kv.key: kv.value for kv in s.values}
        if 'input_rate_hz' in r and r.get('windows_closed_total') not in seen:
            seen.add(r.get('windows_closed_total'))
            rows.append(r)
    if not rows:
        raise SystemExit('no window has closed yet: let the node run on live input for longer')
    print(f'{len(rows)} window closes ({len(got)} status samples) over {args.seconds:g} s; '
          f'window_mode {rows[-1].get("window_mode")}, sensors {rows[-1].get("sensors")}')
    print(f'{"value":<22}{"min":>10}{"median":>10}{"max":>10}')
    for k in NUMERIC:
        v = [float(r[k]) for r in rows if k in r]
        if v:
            print(f'{k:<22}{min(v):>10.4g}{statistics.median(v):>10.4g}{max(v):>10.4g}')
    print(f'status samples at WARN: {len(warns)}, at ERROR: {len(errors)} of {len(got)}'
          + (f' (last warning: {warns[-1]})' if warns else '') + (f' (last error: {errors[-1]})' if errors else ''))

    rates = [float(r['input_rate_hz']) for r in rows if float(r.get('input_rate_hz', 0)) > 0]
    nominals = [float(r['nominal_rate_hz']) for r in rows if float(r.get('nominal_rate_hz', 0)) > 0]
    if not rates or not nominals:
        raise SystemExit('no input or nominal rate measured yet: let the node run on live input for longer')
    rate, nominal = statistics.median(rates), statistics.median(nominals)
    drop = statistics.median(float(r['recent_drop_fraction']) for r in rows)
    print(f'\nsensor (nominal) rate ~{nominal:.3g} Hz, integrated ~{rate:.3g} Hz, loss ~{100 * drop:.1f} %'
          + (f' (expected_scan_rate_hz configured: {configured_rate:g})' if configured_rate > 0 else ''))
    sensors = max(1, int(float(rows[-1].get('sensors', 1))))
    per_sensor = nominal / sensors
    print('The nominal rate is estimated from gaps between stamps and is unreliable above about 80 % loss:\n'
          'confirm it with `ros2 topic hz` on the sensor host or the datasheet.')
    # Time windows: the period is period_windows x window_period_s seconds whatever the load;
    # a window should hold a few messages from each sensor so it is not starved.
    window = max(window_period, 5.0 / per_sensor)
    windows = round(args.target_period_s / window) if args.target_period_s else period_windows
    print('suggested settings:')
    print(f'  expected_scan_rate_hz: {per_sensor:.3g}   # per sensor ({sensors} sensor frame(s) on the topic)')
    print(f'  window_mode: "time"')
    print(f'  window_period_s: {window:.3g}   # holds ~{window * per_sensor:.0f} messages per sensor')
    print(f'  period_windows: {windows}   # periodic period {windows * window:.3g} s'
          + ('' if args.target_period_s else ' (pass --target-period-s to choose it)'))

if __name__ == '__main__':
    main()
