#!/usr/bin/env python3
"""Compares the stat records of the test-split files before and after phase 1b (PREREG_P1B.md section 11).
Columns: path, size, mtime, atime, ctime. Reports every file whose size or mtime or ctime changed, files that appeared or
disappeared, and the files whose atime changed (the disk is mounted without access times, so atime is not evidence). The comparison shows non-modification only, never non-access.
Files named in --expected-opened (path prefixes) are the ones the amendment allowed to be read; a change in size, mtime or
ctime of any other file is an incident.

    python3 -I compare_stat.py --before a.tsv [b.tsv ...] --after a2.tsv [b2.tsv ...] --out compare.md
"""
import argparse
import sys


def load(paths):
    d, meta = {}, []
    for p in paths:
        with open(p) as f:
            for line in f:
                line = line.rstrip("\n")
                if not line:
                    continue
                if line.startswith("#"):
                    meta.append(line)
                    continue
                c = line.split("\t")
                if len(c) != 5:
                    raise ValueError("%s: bad line %r" % (p, line))
                d[c[0]] = {"size": c[1], "mtime": c[2], "atime": c[3], "ctime": c[4]}
    return d, meta


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--before", nargs="+", required=True)
    ap.add_argument("--after", nargs="+", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--expected-opened", nargs="*", default=[
        "spires/keble/sequences/2024-03-12-keble-college-02/", "spires/keble/ground_truth_map/keble-college/merged-cloud-1cm.pcd",
        "spires/keble/calibration/README.md"])
    a = ap.parse_args(argv)
    b, bm = load(a.before)
    f, fm = load(a.after)
    gone = sorted(set(b) - set(f))
    new = sorted(set(f) - set(b))
    changed = {k: [] for k in ("size", "mtime", "ctime", "atime")}
    for p in sorted(set(b) & set(f)):
        for k in changed:
            if b[p][k] != f[p][k]:
                changed[k].append(p)
    incidents = sorted(set(changed["size"]) | set(changed["mtime"]) | set(changed["ctime"]) | set(gone) | set(new))
    L = ["# Test-split stat comparison, phase 1b", "",
         "Before: %s" % "; ".join(bm), "", "After: %s" % "; ".join(fm), "",
         "Files in the before record: %d. Files in the after record: %d." % (len(b), len(f)), "",
         "- files that disappeared: %d" % len(gone), "- files that appeared: %d" % len(new),
         "- size changed: %d" % len(changed["size"]), "- mtime changed: %d" % len(changed["mtime"]), "- ctime changed: %d" % len(changed["ctime"]),
         "- atime changed: %d (not evidence: the disk is mounted without access times; see below)" % len(changed["atime"]), ""]
    for p in gone + new:
        L.append("- %s: %s" % ("gone" if p in gone else "new", p))
    L.append("")
    L.append("What this comparison proves: only that no file was modified. Reading a file changes neither size, mtime nor ctime, and the mount has no access "
             "times, so the comparison cannot show that a file was not opened. The evidence for that is the step logs and the committed scripts "
             "(and, from P3 on, an strace of each step).")
    L.append("")
    L.append("Files the amendment allowed to be read: " + ", ".join("`%s`" % e for e in a.expected_opened))
    L.append("")
    bad = [p for p in incidents if not any(p.startswith(e) for e in a.expected_opened)]
    if bad:
        L.append("## INCIDENT: files not named in the amendment changed (size, mtime, ctime, appeared or gone)")
        L.extend("- %s" % p for p in bad)
    else:
        L.append("No file outside the amendment's list was modified (size, mtime, ctime). **No modification incident.**")
    if changed["atime"]:
        L.append("")
        L.append("Files with a changed atime (first 30):")
        L.extend("- %s" % p for p in changed["atime"][:30])
    L.append("")
    with open(a.out, "w") as o:
        o.write("\n".join(L))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
