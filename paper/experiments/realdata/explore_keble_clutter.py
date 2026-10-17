#!/usr/bin/env python3
"""EXPLORATORY and post hoc (sets nothing): who are the Keble 02 'clutter' cells that are hit in almost every window?
For clutter cells with N >= 5, prints the quantiles of the hit rate, and for the cells with rate >= 0.9 the distance (in voxels,
Chebyshev) to the nearest TLS-present voxel, their height and their viewpoint diversity, next to the cells with rate < 0.5.

    python3 -I explore_keble_clutter.py --cells <keble02_r0.2_W10_today.csv> --tls <tls_r0.20.tbl> [--max-dist 8]
"""
import argparse
import struct
import sys

import numpy as np

KOFF, KBITS = 1 << 20, 21


def load_tls(path):
    with open(path, "rb") as f:
        magic, ver, res, n = struct.unpack("<4sIdQ", f.read(24))
        assert magic == b"VXTB" and ver == 1
        arr = np.frombuffer(f.read(16 * n), dtype=np.dtype([("k", "<u8"), ("c", "<u4"), ("z", "<f4")]))
    k = arr["k"].astype(np.int64)
    mask = (1 << KBITS) - 1
    z = (k & mask) - KOFF
    y = ((k >> KBITS) & mask) - KOFF
    x = ((k >> (2 * KBITS)) & mask) - KOFF
    return res, set(zip(x.tolist(), y.tolist(), z.tolist()))


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--cells", required=True)
    ap.add_argument("--tls", required=True)
    ap.add_argument("--max-dist", type=int, default=8)
    a = ap.parse_args(argv)
    res, tls = load_tls(a.tls)
    rows = []
    with open(a.cells) as f:
        f.readline()
        for line in f:
            p = line.split(",", 9)
            if int(p[0]) & 15 == 1 and int(p[4]) >= 5:
                rows.append((int(p[1]), int(p[2]), int(p[3]), int(p[4]), int(p[5]), int(p[8])))
    c = np.array(rows)
    r = c[:, 4] / c[:, 3]
    print("clutter cells with N >= 5: %d; hit-rate quantiles 10/25/50/75/90: %s" % (len(c), np.round(np.percentile(r, [10, 25, 50, 75, 90]), 3)))

    def dist(cell):
        x, y, z = int(cell[0]), int(cell[1]), int(cell[2])
        for d in range(1, a.max_dist + 1):
            for dx in range(-d, d + 1):
                for dy in range(-d, d + 1):
                    for dz in range(-d, d + 1):
                        if max(abs(dx), abs(dy), abs(dz)) == d and (x + dx, y + dy, z + dz) in tls:
                            return d
        return a.max_dist + 1

    for name, sel in (("rate >= 0.9", r >= 0.9), ("rate < 0.5", r < 0.5)):
        sub = c[sel]
        d = np.array([dist(row) for row in sub])
        print("%s: %d cells; distance to the nearest TLS-present voxel in voxels (%.2f m each): " % (name, len(sub), res) +
              " ".join("%d:%.3f" % (k, (d == k).mean()) for k in range(2, a.max_dist + 2)) +
              "; height (m) p5/p50/p95 %s; viewpoint diversity median %.1f; N median %.1f" %
              (np.round(np.percentile(sub[:, 2] * res, [5, 50, 95]), 2), np.median(sub[:, 5]), np.median(sub[:, 3])))
    # Sensitivity of q_c to a wider buffer around the TLS (post hoc; the pre-registered buffer is one voxel): clutter cells at least k voxels
    # from the nearest TLS-present voxel. q_s of the structure cells at N >= 5 is printed for the implied gap.
    dall = np.array([dist(row) for row in c])
    rs = []
    with open(a.cells) as f:
        f.readline()
        for line in f:
            p = line.split(",", 9)
            if int(p[0]) & 15 == 3 and int(p[4]) >= 5:
                rs.append(int(p[5]) / int(p[4]))
    qs = float(np.percentile(rs, 10)) if rs else float("nan")
    print("structure cells with N >= 5: %d, q_s (10th percentile) = %.4f" % (len(rs), qs))
    print("buffer k voxels (%.2f m) | clutter cells N>=5 | q_c (90th percentile) | implied gap q_s - q_c" % res)
    for k in range(2, 8):
        sel = dall >= k
        if sel.sum() < 100:
            break
        qc = float(np.percentile(r[sel], 90))
        print("  k=%d (%.1f m) | %6d | %.4f | %+.4f" % (k, k * res, int(sel.sum()), qc, qs - qc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
