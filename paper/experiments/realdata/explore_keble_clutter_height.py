#!/usr/bin/env python3
"""EXPLORATORY and post hoc (sets nothing; review finding 1 of phase 1b): at which height, relative to the device, do the
Keble 02 'clutter' cells (N >= 5) lie? Height = voxel centre z minus the z of the ground-truth track position nearest in xy.
Bands: below -1.8 m, person band (-1.8 to +0.5 m), above +0.5 m (above head height: not walking people).
Prints the share of cells and the 90th percentile of the hit rate (the q_c rule) per band. Keble 02 is a calibration sequence.

    python3 -I explore_keble_clutter_height.py --cells <keble02_r0.2_W10_today.csv> --gt <gt-tum.txt> --res 0.2
"""
import argparse

import numpy as np


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--cells", required=True)
    ap.add_argument("--gt", required=True)
    ap.add_argument("--res", type=float, required=True)
    a = ap.parse_args(argv)
    gt = np.loadtxt(a.gt)[:, 1:4]
    rows = []
    with open(a.cells) as f:
        f.readline()
        for line in f:
            p = line.split(",", 9)
            if int(p[0]) & 15 == 1 and int(p[4]) >= 5:
                rows.append((int(p[1]), int(p[2]), int(p[3]), int(p[4]), int(p[5])))
    c = np.array(rows, dtype=np.int64)
    ctr = (c[:, :3] + 0.5) * a.res
    nz = np.empty(len(c))
    for i in range(0, len(c), 2000):   # nearest track position in xy, in chunks (no scipy needed)
        d = ((ctr[i:i + 2000, None, :2] - gt[None, :, :2]) ** 2).sum(-1)
        nz[i:i + 2000] = gt[d.argmin(1), 2]
    rel = ctr[:, 2] - nz
    rate = c[:, 4] / c[:, 3]
    print("clutter cells with N >= 5: %d; q90 overall %.4f" % (len(c), np.percentile(rate, 90)))
    for name, sel in (("below -1.8 m", rel < -1.8), ("person band -1.8..+0.5 m", (rel >= -1.8) & (rel <= 0.5)), ("above +0.5 m", rel > 0.5)):
        print("%-26s %6d cells (%5.1f %%)  q90 %.4f" % (name, sel.sum(), 100.0 * sel.mean(), np.percentile(rate[sel], 90) if sel.any() else float("nan")))


if __name__ == "__main__":
    main()
