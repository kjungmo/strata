#!/usr/bin/env python3
"""Check that a running STRATA node actually applied every value in a params YAML.

ROS 2 silently ignores YAML keys that a node never declares, so a typo or a key the
backend does not declare would leave the default in place without any warning.
This script reads the YAML, waits for the node's parameter services, and fails if
any YAML key is not declared by the node or holds a different value.

    python3 scripts/check_param_binding.py strata/params/grid2d.yaml --node strata
"""

import argparse
import math
import sys

import rclpy
import yaml
from rcl_interfaces.msg import ParameterType
from rcl_interfaces.srv import GetParameters, ListParameters


def yaml_params(path):
    with open(path, encoding="utf-8") as f:
        doc = yaml.safe_load(f)
    params = {}
    for section in doc.values():  # "/**" or a node name
        params.update(section.get("ros__parameters", {}))
    return params


def value_of(pv):
    return {
        ParameterType.PARAMETER_BOOL: lambda: pv.bool_value,
        ParameterType.PARAMETER_INTEGER: lambda: pv.integer_value,
        ParameterType.PARAMETER_DOUBLE: lambda: pv.double_value,
        ParameterType.PARAMETER_STRING: lambda: pv.string_value,
    }.get(pv.type, lambda: None)()


def same(expected, actual):
    if isinstance(expected, bool) or isinstance(actual, bool):
        return expected is actual
    if isinstance(expected, (int, float)) and isinstance(actual, (int, float)):
        return math.isclose(float(expected), float(actual), rel_tol=0.0, abs_tol=1e-12)
    return expected == actual


def call(node, client, request, timeout):
    if not client.wait_for_service(timeout_sec=timeout):
        sys.exit(f"FAIL: service {client.srv_name} not available after {timeout:.0f} s")
    future = client.call_async(request)
    rclpy.spin_until_future_complete(node, future, timeout_sec=timeout)
    if future.result() is None:
        sys.exit(f"FAIL: no response from {client.srv_name}")
    return future.result()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("yaml")
    ap.add_argument("--node", default="strata")
    ap.add_argument("--timeout", type=float, default=30.0)
    args = ap.parse_args()

    expected = yaml_params(args.yaml)
    rclpy.init()
    node = rclpy.create_node("strata_param_binding_check")
    target = "/" + args.node.lstrip("/")
    listed = call(node, node.create_client(ListParameters, f"{target}/list_parameters"),
                  ListParameters.Request(), args.timeout)
    declared = set(listed.result.names)

    undeclared = sorted(k for k in expected if k not in declared)
    keys = sorted(k for k in expected if k in declared)
    req = GetParameters.Request()
    req.names = keys
    got = call(node, node.create_client(GetParameters, f"{target}/get_parameters"), req, args.timeout)
    actual = {k: value_of(v) for k, v in zip(keys, got.values)}
    mismatched = [(k, expected[k], actual[k]) for k in keys if not same(expected[k], actual[k])]

    print(f"{args.yaml}: {len(expected)} YAML keys, {len(keys)} applied as written")
    for k in undeclared:
        print(f"  NOT DECLARED by {target}: {k} = {expected[k]!r} (the node ignores it)")
    for k, e, a in mismatched:
        print(f"  MISMATCH {k}: YAML {e!r}, node {a!r}")
    node.destroy_node()
    rclpy.shutdown()
    if undeclared or mismatched:
        sys.exit("FAIL: parameter binding")
    print("PASS: every YAML key is declared by the node and holds the YAML value")


if __name__ == "__main__":
    main()
