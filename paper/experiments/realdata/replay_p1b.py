#!/usr/bin/env python3
"""Replays the derived SPRT over the per-cell hit/miss run sequences of the LILocBench CALIBRATION cell tables
(structure = ref_occupied cells on static_0, clutter = reference-free cells with H >= 1 on dynamics_0) for a pair (m0, m1).
Two rules:
  engine  the rule of PREREG_P1B.md section 8: clamp l at the ceiling A + 2 l_hit, graduate at l >= A, no lower clamp,
          reject at l <= B once n >= T (T = 24), a cell undecided after n_max windows is Intermittent
  pure    two absorbing barriers from window 1, no ceiling (the rule of the advisory replay scripts; used to check this script
          against the advisor's numbers)
Reports shares with Wilson 95 % intervals (cells treated as independent) and block-bootstrap 95 % intervals (10 m tiles).
All of it is calibration data: nothing here is a held-out estimate.

    python3 -I replay_p1b.py --cells-dir <dir with lil_cells_static_0_r0.05_W15.csv ...> --prefix lil_cells_ --window 15 --pair 0.20 0.70 [--rule engine] [--n-max 80]
"""
import argparse
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import derive_defaults as dd  # noqa: E402


def parse_runs(s):
    out, i = [], 0
    while i < len(s):
        st = s[i] == "h"
        j = i + 1
        while j < len(s) and s[j].isdigit():
            j += 1
        out.append((st, int(s[i + 1:j])))
        i = j
    return out


def replay_cell(runs, d, rule, n_max, T):
    a, b, A, B = d["l_hit"], d["l_miss"], d["A"], d["B"]
    ceil_ = A + 2 * a
    l, n = 0.0, 0
    for st, length in runs:
        x = a if st else b
        for _ in range(length):
            n += 1
            l += x
            if rule == "engine":
                if l > ceil_:
                    l = ceil_
                if l >= A - 1e-9:
                    return "S", n
                if n >= T and l <= B + 1e-9:
                    return "R", n
                if n_max and n >= n_max:
                    return "I", n
            else:
                if l >= A - 1e-9:
                    return "S", n
                if l <= B + 1e-9:
                    return "R", n
    return "U", n


def load_cells(path, keep, res, tile_m=10.0):
    cells = []
    with open(path) as fh:
        head = fh.readline().strip().split(",")
        assert head[:6] == ["label", "ix", "iy", "iz", "N", "H"], head
        for line in fh:
            p = line.rstrip("\n").split(",")
            lab, ix, iy, N, H = int(p[0]), int(p[1]), int(p[2]), int(p[4]), int(p[5])
            if not keep(lab, N, H):
                continue
            tile = (int(math.floor(ix * res / tile_m)), int(math.floor(iy * res / tile_m)))
            cells.append((N, H, tile, parse_runs(p[9])))
    return cells


def wilson(k, n, z=1.959964):
    if n == 0:
        return (float("nan"),) * 2
    p = k / n
    den = 1 + z * z / n
    c = p + z * z / (2 * n)
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))
    return ((c - h) / den, (c + h) / den)


def block_ci(items, fn, reps=2000, seed=20261018):
    """items: list of (tile, outcome flags). fn(list of per-tile (k, n)) -> ratio. Block bootstrap over tiles."""
    tiles = {}
    for t, k, n in items:
        a = tiles.setdefault(t, [0, 0])
        a[0] += k
        a[1] += n
    keys = sorted(tiles)
    rng = random.Random(seed)
    vals = []
    for _ in range(reps):
        k = n = 0
        for _ in keys:
            t = tiles[keys[rng.randrange(len(keys))]]
            k += t[0]
            n += t[1]
        vals.append(k / n if n else float("nan"))
    vals = sorted(v for v in vals if v == v)
    if not vals:
        return (float("nan"),) * 2
    return vals[int(0.025 * (len(vals) - 1))], vals[int(0.975 * (len(vals) - 1))]


def share_line(name, cells, d, rule, n_max, T, m0):
    res = [replay_cell(r, d, rule, n_max, T) for _, _, _, r in cells]
    n = len(cells)
    out = {}
    for o in ("S", "R", "U", "I"):
        k = sum(1 for x, _ in res if x == o)
        if k == 0 and o in ("U", "I") and not (o == "U" and rule == "pure") and not (o == "I" and rule == "engine"):
            continue
        items = [(c[2], 1 if x == o else 0, 1) for c, (x, _) in zip(cells, res)]
        w, bl = wilson(k, n), block_ci(items, None)
        out[o] = (k / n if n else float("nan"), w, bl)
    lat = sorted(nn for (x, nn) in res if x == "S")
    p50 = lat[len(lat) // 2] if lat else None
    p90 = lat[int(0.9 * (len(lat) - 1))] if lat else None
    parts = ["%s %.4f [W %.4f, %.4f] [B %.4f, %.4f]" % ((o,) + (out[o][0],) + out[o][1] + out[o][2]) for o in out]
    print("  %-34s cells=%6d  %s | windows-to-S p50=%s p90=%s" % (name, n, " | ".join(parts), p50, p90))
    return res


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--cells-dir", required=True)
    ap.add_argument("--window", type=int, required=True)
    ap.add_argument("--res", default="0.05")
    ap.add_argument("--prefix", default="cells_", help="cell table name prefix: cells_ (phase 1 tables) or lil_cells_ (phase 1b)")
    ap.add_argument("--pair", nargs=2, type=float, required=True)
    ap.add_argument("--alpha", type=float, default=0.01)
    ap.add_argument("--rule", default="engine", choices=("engine", "pure"))
    ap.add_argument("--n-max", type=int, default=0)
    ap.add_argument("--T", type=int, default=dd.PERIODICITY_T)
    a = ap.parse_args(argv)
    m0, m1 = a.pair
    d = dd.derive(m0, m1, a.alpha, a.alpha)
    if a.rule == "engine" and a.n_max == 0:
        a.n_max = dd.n_max_undecided(m0, m1, a.alpha, a.alpha)[0]
    print("replay: rule=%s (m0,m1)=(%.2f,%.2f) alpha=beta=%.2f l_hit=%.4f l_miss=%.4f A=%.4f ceiling=%.4f n_max=%s T=%d window=%d res=%s" %
          (a.rule, m0, m1, a.alpha, d["l_hit"], d["l_miss"], d["A"], d["ceiling"], a.n_max if a.rule == "engine" else "-", a.T, a.window, a.res))
    res_f = a.res if a.res != "0.1" else "0.10"
    st = load_cells(os.path.join(a.cells_dir, "%sstatic_0_r%s_W%d.csv" % (a.prefix, res_f, a.window)), lambda lab, N, H: lab == 1, float(a.res))
    cl = load_cells(os.path.join(a.cells_dir, "%sdynamics_0_r%s_W%d.csv" % (a.prefix, res_f, a.window)), lambda lab, N, H: lab == 2 and H >= 1, float(a.res))
    print("structure = ref_occupied on static_0, clutter = ref_free with H >= 1 on dynamics_0 (calibration data)")
    share_line("structure static_0 (all N>=1)", st, d, a.rule, a.n_max, a.T, m0)
    share_line("clutter dynamics_0 (all N>=1)", cl, d, a.rule, a.n_max, a.T, m0)
    le = [c for c in cl if c[1] / c[0] <= m0 + 1e-12]
    gt = [c for c in cl if c[1] / c[0] > m0 + 1e-12]
    share_line("clutter with r <= m0", le, d, a.rule, a.n_max, a.T, m0)
    share_line("clutter with r > m0", gt, d, a.rule, a.n_max, a.T, m0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
