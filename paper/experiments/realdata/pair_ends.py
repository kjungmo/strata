"""Pair (m0, m1) at both ends of the block-bootstrap intervals (PREREG_P1B.md section 1): 'If an interval crosses a
rounding boundary, the report states the pair at both ends; the frozen pair does not change.' No dependencies, so that
freeze_prereg.py and make_report_p1b.py share one definition.

Low end:  m0 = ceil_0.05(q_c lower end), m1 = floor_0.05(q_s lower end).
High end: m0 = ceil_0.05(q_c upper end), m1 = floor_0.05(q_s upper end).
"""
import math


def _ceil(x):
    return round(math.ceil(x / 0.05 - 1e-9) * 0.05, 2)


def _floor(x):
    return round(math.floor(x / 0.05 + 1e-9) * 0.05, 2)


def pair(q_s, q_c):
    return max(0.05, _ceil(q_c)), min(0.95, _floor(q_s))


def _f(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return float("nan")


def ends(row):
    """Returns None when the row lacks a number; else dict(point, low, high, crosses)."""
    keys = ("q_s_p10", "q_c_p90", "q_s_ci95_block_lo", "q_s_ci95_block_hi", "q_c_ci95_block_lo", "q_c_ci95_block_hi")
    v = [_f(row.get(k)) for k in keys]
    if any(math.isnan(x) for x in v):
        return None
    qs, qc, qs_lo, qs_hi, qc_lo, qc_hi = v
    point, low, high = pair(qs, qc), pair(qs_lo, qc_lo), pair(qs_hi, qc_hi)
    return {"point": point, "low": low, "high": high, "crosses": low != point or high != point}


def sentence(row):
    """One line for a report, or '' when the interval stays inside one rounding cell."""
    e = ends(row)
    if e is None or not e["crosses"]:
        return ""
    return "pair at the low interval end (%.2f, %.2f), at the high end (%.2f, %.2f); point pair (%.2f, %.2f)" % (e["low"] + e["high"] + e["point"])


def cell(row):
    """Table cell: the two end pairs, or 'same' when the interval crosses no boundary, or '-' without numbers."""
    e = ends(row)
    if e is None:
        return "-"
    if not e["crosses"]:
        return "same"
    return "(%.2f, %.2f) / (%.2f, %.2f)" % (e["low"] + e["high"])
