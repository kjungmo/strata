#!/usr/bin/env python3
"""Summarise a running STRATA node's input-rate diagnostics, for calibrating a robot.

Listens to /diagnostics for --seconds, keeps the "strata: input rate" statuses the node
publishes at every window close, and prints min / median / max of each value, how
often the node warned, and a suggested window setting:

  * window_mode "time" with window_period_s = layer_interval / nominal sensor rate
    (nominal_rate_hz) keeps the window the shipped period_windows was meant for,
    whatever the load does;
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
    ap.add_argument('--params', help='the params YAML the node runs with (for layer_interval)')
    args = ap.parse_args()
    layer_interval, configured_rate = 10, 0.0
    if args.params:
        for section in (yaml.safe_load(open(args.params)) or {}).values():
            p = (section or {}).get('ros__parameters', {})
            layer_interval = int(p.get('layer_interval', layer_interval))
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

    rows = [{kv.key: kv.value for kv in s.values} for s in got]
    rows = [r for r in rows if 'input_rate_hz' in r]     # statuses after a window has closed
    if not rows:
        raise SystemExit('no window has closed yet: let the node run on live input for longer')
    print(f'{len(got)} window reports over {args.seconds:g} s; window_mode {rows[-1].get("window_mode")}')
    print(f'{"value":<22}{"min":>10}{"median":>10}{"max":>10}')
    for k in NUMERIC:
        v = [float(r[k]) for r in rows if k in r]
        if v:
            print(f'{k:<22}{min(v):>10.4g}{statistics.median(v):>10.4g}{max(v):>10.4g}')
    warns = [s.message for s in got if level(s) == 1]
    print(f'warnings: {len(warns)} of {len(got)}' + (f' (last: {warns[-1]})' if warns else ''))

    rates = [float(r['input_rate_hz']) for r in rows if float(r.get('input_rate_hz', 0)) > 0]
    nominals = [float(r['nominal_rate_hz']) for r in rows if float(r.get('nominal_rate_hz', 0)) > 0]
    if not rates or not nominals:
        raise SystemExit('no input or nominal rate measured yet: let the node run on live input for longer')
    rate, nominal = statistics.median(rates), statistics.median(nominals)
    drop = statistics.median(float(r['recent_drop_fraction']) for r in rows)
    print(f'\nsensor (nominal) rate ~{nominal:.3g} Hz, integrated ~{rate:.3g} Hz, loss ~{100 * drop:.1f} %'
          + (f' (expected_scan_rate_hz configured: {configured_rate:g})' if configured_rate > 0 else ''))
    print('The nominal rate is estimated from gaps between stamps and is unreliable above about 80 % loss\n'
          'or with several sensors per frame: confirm it with `ros2 topic hz` on the sensor host or the\n'
          'datasheet, and set expected_scan_rate_hz to it.')
    print('suggested settings:')
    print(f'  expected_scan_rate_hz: {nominal:.3g}')
    print(f'  window_mode: "time"\n  window_period_s: {layer_interval / nominal:.3g}'
          f'   # layer_interval {layer_interval} / {nominal:.3g} Hz: the window length the shipped'
          ' period_windows was meant for')


if __name__ == '__main__':
    main()
