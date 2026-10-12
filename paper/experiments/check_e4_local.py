#!/usr/bin/env python3
"""Compare a local E4 run with the committed reference results/e4_throughput.csv.

E4 is the only experiment that reads the wall clock. Its cell counts and byte
estimates follow from the seeded hit streams and must match the reference exactly;
its timings are this machine's and are only printed beside the reference ones.

    python3 paper/experiments/check_e4_local.py paper/experiments/build/e4_throughput_local.csv

Exit status 0 when every deterministic column matches, 1 otherwise.
"""
import csv
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REFERENCE = HERE / "results" / "e4_throughput.csv"
DETERMINISTIC = ("backend", "size_label", "extent", "hits_per_window", "windows", "final_cells",
                 "bytes_per_cell", "est_bytes", "repeats")


def rows(path):
    with open(path, newline="") as f:
        return [r for r in csv.DictReader(f) if not r["backend"].startswith("#")]


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    ref, loc = rows(REFERENCE), rows(argv[1])
    bad = []
    if len(ref) != len(loc):
        bad.append(f"row count {len(loc)} differs from the reference {len(ref)}")
    for a, b in zip(ref, loc):
        for col in DETERMINISTIC:
            if a[col] != b[col]:
                bad.append(f"{a['backend']} {a['size_label']}: {col} is {b[col]}, reference {a[col]}")
    print("E4 timings, median of the repeats (us per integrate() call): local | committed reference")
    for a, b in zip(ref, loc):
        print(f"  {a['backend']:8s} {a['size_label']:8s} {float(b['us_per_integrate']):10.1f} | "
              f"{float(a['us_per_integrate']):10.1f}")
    by = lambda rs: {(r["backend"], r["extent"]): float(r["us_per_integrate"]) for r in rs}
    r_, l_ = by(ref), by(loc)
    for ext in sorted({r["extent"] for r in ref}, key=int):
        print(f"  voxel3d / grid2d at {ext}: {l_[('voxel3d', ext)] / l_[('grid2d', ext)]:5.1f}x | "
              f"{r_[('voxel3d', ext)] / r_[('grid2d', ext)]:5.1f}x")
    for line in bad:
        print("FAIL " + line)
    if not bad:
        print(f"OK: cell counts and byte estimates of {len(loc)} rows equal the committed reference; "
              "timings are machine-dependent and are not compared")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
