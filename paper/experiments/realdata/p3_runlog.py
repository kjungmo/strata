#!/usr/bin/env python3
"""Run logging and input checksums for phase 3 (PREREG_P3.md section 9). numpy-free, standard library only.

    run          run a command and write results_p3/runs/<step>.json: host, kernel, cores, load and the top CPU users before
                 and after, commit, uncommitted files, the command, return code, seconds, the inputs (size, mtime, and sha256
                 with --hash-inputs) and the outputs (size and sha256)
    make-list    write data_inputs_p3.sha256 from the verification tables of the data download (P0); no data file is read
    check-inputs compare sizes (and with --full sha256) of the files in data_inputs_p3.sha256 with the files on disk

    python3 -I p3_runlog.py run --step NAME [--results DIR] [--input PATH]... [--hash-inputs] [--output PATH]... -- cmd args...
"""
import argparse
import csv
import hashlib
import json
import os
import platform
import re
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DATA_ROOT = os.path.expanduser(os.environ.get("DATA_ROOT", "~/datasets/strata"))


def sha256_file(path, block=1 << 22):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(block)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def scrub(s):
    home = os.path.expanduser("~")
    return s.replace(home, "~")


def sh(cmd, cwd=None):
    try:
        return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=30).stdout.strip()
    except Exception as e:  # a missing tool is recorded, not fatal
        return f"unavailable: {e}"


def load_state():
    state = {"loadavg": open("/proc/loadavg").read().strip(), "time": time.strftime("%Y-%m-%dT%H:%M:%S%z")}
    ps = sh(["ps", "-eo", "pcpu,rss,comm", "--sort=-pcpu"]).splitlines()[:6]
    state["top_cpu"] = ps
    mem = {}
    for line in open("/proc/meminfo"):
        k, v = line.split(":", 1)
        if k in ("MemTotal", "MemAvailable", "SwapFree"):
            mem[k] = v.strip()
    state["meminfo"] = mem
    return state


def file_record(path, with_hash):
    try:
        st = os.stat(path)
    except OSError as e:
        return {"path": scrub(path), "error": str(e)}
    rec = {"path": scrub(path), "size": st.st_size, "mtime": st.st_mtime}
    if with_hash and os.path.isfile(path):
        rec["sha256"] = sha256_file(path)
    return rec


def tree_of(repo, rev, path="strata_core"):
    """Git tree hash of `path` at `rev` (the engine pin of PREREG_P3.md Amendment 3), or "" if there is none."""
    try:
        r = subprocess.run(["git", "-C", repo, "rev-parse", "--verify", "-q", f"{rev}:{path}"], capture_output=True, text=True, timeout=30)
    except Exception:
        return ""
    out = r.stdout.strip()
    return out if r.returncode == 0 and len(out) == 40 else ""


def git_state(repo, binary_commit=""):
    commit = sh(["git", "-C", repo, "rev-parse", "HEAD"])
    exp = sh(["git", "-C", repo, "status", "--porcelain", "--", ".", ":(exclude)results_p3"]).splitlines()
    whole = sh(["git", "-C", repo, "status", "--porcelain"]).splitlines()
    st = {"commit": commit, "uncommitted_in_experiment_dir": len(exp), "uncommitted_in_repository": len(whole),
          "strata_core_tree_head": tree_of(repo, "HEAD")}
    if binary_commit:                                  # the engine of the binary that the step runs: the tree of the commit it was built from
        st["binary_commit"] = binary_commit
        st["strata_core_tree_binary"] = tree_of(repo, binary_commit)
    return st


def cmd_run(args):
    if not args.argv:
        raise SystemExit("no command after --")
    results = os.path.abspath(args.results)
    os.makedirs(os.path.join(results, "runs"), exist_ok=True)
    rec = {
        "step": args.step,
        "argv": [scrub(a) for a in args.argv],
        "host": socket.gethostname(), "kernel": platform.release(), "cores": os.cpu_count(),
        "python": sys.version.split()[0],
        "before": load_state(),
        "git": git_state(HERE, args.binary_commit),
        "inputs": [file_record(p, args.hash_inputs) for p in args.input],
        "hash_inputs": bool(args.hash_inputs),
    }
    t0 = time.time()
    rc = subprocess.call(args.argv)
    rec["seconds"] = round(time.time() - t0, 3)
    rec["returncode"] = rc
    rec["after"] = load_state()
    rec["outputs"] = [file_record(p, True) for p in args.output]
    path = os.path.join(results, "runs", f"{args.step}.json")
    with open(path, "w") as f:
        json.dump(rec, f, indent=1, sort_keys=True)
        f.write("\n")
    print(f"run log {scrub(path)} rc={rc}")
    return rc


# ------------------------------------------------------------------------------ input list ------

SESSIONS = ("boreas-2020-11-26-13-58", "boreas-2021-04-08-12-44", "boreas-2021-06-17-17-52")
SPIRES_SEQ = ("03", "04", "05")


def read_tsv(path):
    with open(path) as f:
        return list(csv.DictReader(f, delimiter="\t"))


def make_list(args):
    root = os.path.expanduser(args.data_root)
    tdir = os.path.join(root, "_scripts", "test_split")
    lines, unrecorded = [], []
    for r in read_tsv(os.path.join(tdir, "spires_verify.tsv")):
        k = r["key"]
        keep = k.startswith("ground_truth_map/keble-college/merged-cloud-1cm.pcd") or k == "calibration/README.md" or any(
            f"2024-03-12-keble-college-{s}/" in k and (k.endswith("lidar-clouds.zip") or k.endswith("gt-tum.txt")) for s in SPIRES_SEQ)
        if keep and r["status"] == "OK":
            lines.append((r["sha256"], int(r["local_bytes"]), "spires/keble/" + k))
    for r in read_tsv(os.path.join(tdir, "boreas_verify_full.tsv")):
        k = r["key"]
        sess = k.split("/")[0]
        if sess not in SESSIONS or r["status"] != "OK":
            continue
        rel = k[len(sess) + 1:]
        if rel.startswith("lidar/") or rel in ("applanix/gps_post_process.csv", "applanix/lidar_poses.csv", "applanix/lidar_times.txt", "calib/T_applanix_lidar.txt"):
            lines.append((r["sha256"], int(r["local_bytes"]), "boreas/" + k))
    for line in open(os.path.join(root, "kth", "SHA256SUMS.txt")):
        parts = line.split()
        if len(parts) == 2 and os.path.basename(parts[1]) in ("05.zip", "semindoor.zip"):
            lines.append((parts[0], os.path.getsize(os.path.join(root, "kth", os.path.basename(parts[1]))), "kth/" + os.path.basename(parts[1])))
    for line in open(os.path.join(root, "lilocbench", "bags_sha256.txt")):
        parts = line.split()
        if len(parts) == 2 and re.search(r"lt_changes", parts[1]):
            lines.append((parts[0], os.path.getsize(os.path.join(root, "lilocbench", "bags", os.path.basename(parts[1]))), "lilocbench/bags/" + os.path.basename(parts[1])))
    for line in open(os.path.join(root, "lilocbench", "sha256.txt")):
        parts = line.split()
        if len(parts) == 2 and parts[1].endswith("gt_poses_public.zip"):
            lines.append((parts[0], os.path.getsize(os.path.join(root, "lilocbench", "gt_poses_public.zip")), "lilocbench/gt_poses_public.zip"))
    lines.sort(key=lambda x: x[2])
    with open(args.out, "w") as f:
        f.write("# sha256  size  path (relative to the data root); from the P0 download verification, no data file read when this was written\n")
        for h, n, p in lines:
            f.write(f"{h}  {n}  {p}\n")
    unrecorded = ["kth/05/gt_cloud.pcd", "kth/05/pcd/*.pcd", "kth/semindoor/gt_cloud.pcd", "kth/semindoor/pcd/*.pcd",
                  "lilocbench/gt_x/lt_changes_0/gt_poses.txt", "lilocbench/gt_x/lt_changes_dynamics_0/gt_poses.txt"]
    with open(args.out + ".unrecorded", "w") as f:
        f.write("# files read by phase 3 whose sha256 was not recorded at download; the run record (p3_runlog.py run --hash-inputs) hashes them when they are read\n")
        for p in unrecorded:
            f.write(p + "\n")
    print(f"{len(lines)} files listed in {args.out}")
    return 0


def check_inputs(args):
    root = os.path.expanduser(args.data_root)
    if not os.path.isdir(root):
        print(f"SKIP: no data under {root}")
        return 0
    bad, n = 0, 0
    for line in open(args.list):
        if line.startswith("#") or not line.strip():
            continue
        h, size, rel = line.split()
        p = os.path.join(root, rel)
        n += 1
        if not os.path.exists(p):
            print(f"MISSING {rel}")
            bad += 1
        elif os.path.getsize(p) != int(size):
            print(f"SIZE {rel} {os.path.getsize(p)} != {size}")
            bad += 1
        elif args.full and sha256_file(p) != h:
            print(f"SHA256 {rel}")
            bad += 1
    print(("FAIL" if bad else "OK") + f": {n} files checked ({'sha256' if args.full else 'size'}), {bad} problems")
    return 1 if bad else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("run")
    p.add_argument("--step", required=True)
    p.add_argument("--results", default=os.path.join(HERE, "results_p3"))
    p.add_argument("--input", action="append", default=[])
    p.add_argument("--output", action="append", default=[])
    p.add_argument("--hash-inputs", action="store_true")
    p.add_argument("--binary-commit", default="")
    p.add_argument("argv", nargs=argparse.REMAINDER)
    p.set_defaults(fn=cmd_run)
    p = sub.add_parser("make-list")
    p.add_argument("--data-root", default=DATA_ROOT)
    p.add_argument("--out", default=os.path.join(HERE, "data_inputs_p3.sha256"))
    p.set_defaults(fn=make_list)
    p = sub.add_parser("check-inputs")
    p.add_argument("--data-root", default=DATA_ROOT)
    p.add_argument("--list", default=os.path.join(HERE, "data_inputs_p3.sha256"))
    p.add_argument("--full", action="store_true")
    p.set_defaults(fn=check_inputs)
    args = ap.parse_args(argv)
    if args.cmd == "run" and args.argv and args.argv[0] == "--":
        args.argv = args.argv[1:]
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
