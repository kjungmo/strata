"""Tests of p3_runlog.py on a temporary data root. No real data are needed.  python3 -I -m unittest discover -s . -p test_p3_runlog.py"""
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest

import p3_runlog as rl

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "p3_runlog.py")


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


class TestRun(unittest.TestCase):
    def test_run_record_fields_and_return_code(self):
        with tempfile.TemporaryDirectory() as d:
            write(d + "/in.txt", b"input")
            write(d + "/out.txt", b"")
            cmd = [sys.executable, "-I", TOOL, "run", "--step", "unit", "--results", d + "/res", "--input", d + "/in.txt", "--hash-inputs",
                   "--output", d + "/out.txt", "--", sys.executable, "-I", "-c", "open(%r,'w').write('result')" % (d + "/out.txt")]
            r = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            rec = json.load(open(d + "/res/runs/unit.json"))
            for k in ("step", "argv", "host", "kernel", "cores", "before", "after", "git", "inputs", "outputs", "seconds", "returncode"):
                self.assertIn(k, rec)
            self.assertEqual(rec["returncode"], 0)
            self.assertTrue(rec["before"]["loadavg"] and rec["after"]["loadavg"])
            self.assertEqual(rec["inputs"][0]["sha256"], hashlib.sha256(b"input").hexdigest())
            self.assertEqual(rec["inputs"][0]["size"], 5)
            self.assertEqual(rec["outputs"][0]["sha256"], hashlib.sha256(b"result").hexdigest())
            inside = subprocess.run(["git", "-C", HERE, "rev-parse", "--is-inside-work-tree"], capture_output=True, text=True).returncode == 0
            if inside:                                       # a git archive copy (the build tree of reproduce_p3.sh) has no repository
                self.assertEqual(len(rec["git"]["commit"]), 40)
            else:
                self.assertEqual(rec["git"]["commit"], "")
            self.assertIn("uncommitted_in_experiment_dir", rec["git"])

    def test_failing_command_is_recorded_and_returned(self):
        with tempfile.TemporaryDirectory() as d:
            cmd = [sys.executable, "-I", TOOL, "run", "--step", "bad", "--results", d, "--", sys.executable, "-I", "-c", "raise SystemExit(7)"]
            r = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(r.returncode, 7)
            self.assertEqual(json.load(open(d + "/runs/bad.json"))["returncode"], 7)

    def test_inputs_are_not_hashed_without_the_flag_and_missing_files_are_recorded(self):
        with tempfile.TemporaryDirectory() as d:
            write(d + "/in.txt", b"x")
            cmd = [sys.executable, "-I", TOOL, "run", "--step", "nohash", "--results", d, "--input", d + "/in.txt", "--input", d + "/missing", "--output", d + "/nothing",
                   "--", sys.executable, "-I", "-c", "pass"]
            self.assertEqual(subprocess.run(cmd, capture_output=True, text=True).returncode, 0)
            rec = json.load(open(d + "/runs/nohash.json"))
            self.assertNotIn("sha256", rec["inputs"][0])
            self.assertIn("error", rec["inputs"][1])
            self.assertIn("error", rec["outputs"][0])

    def test_home_directory_is_scrubbed(self):
        self.assertEqual(rl.scrub(os.path.expanduser("~") + "/x/y"), "~/x/y")
        self.assertEqual(rl.scrub("/tmp/x"), "/tmp/x")


class TestInputList(unittest.TestCase):
    def build_root(self, d):
        root = d + "/root"
        h = lambda b: hashlib.sha256(b).hexdigest()
        os.makedirs(root + "/_scripts/test_split")
        sp = ["key\texpected_bytes\tlocal_bytes\tcontent_length\tsha256\tserver_sha256\turl\tstatus"]
        for s in ("02", "03"):
            for f, data in (("raw/lidar-clouds.zip", b"zip" + s.encode()), ("processed/trajectory/gt-tum.txt", b"tum" + s.encode())):
                key = f"sequences/2024-03-12-keble-college-{s}/{f}"
                write(f"{root}/spires/keble/{key}", data)
                sp.append(f"{key}\t{len(data)}\t{len(data)}\t{len(data)}\t{h(data)}\t-\turl\tOK")
        write(root + "/spires/keble/calibration/README.md", b"readme")
        sp.append(f"calibration/README.md\t6\t6\t6\t{h(b'readme')}\t-\turl\tOK")
        sp.append(f"calibration/other.yaml\t1\t1\t1\t{h(b'o')}\t-\turl\tOK")
        sp.append(f"sequences/2024-03-12-keble-college-04/raw/lidar-clouds.zip\t1\t1\t1\t{h(b'z')}\t-\turl\tFAILED")
        open(root + "/_scripts/test_split/spires_verify.tsv", "w").write("\n".join(sp) + "\n")
        bo = ["key\texpected_bytes\tlocal_bytes\tcontent_length\tsha256\tserver_sha256\turl\tstatus"]
        for sess, ok in (("boreas-2020-11-26-13-58", True), ("boreas-2099-01-01-00-00", True)):
            for rel in ("lidar/1606417543533737.bin", "applanix/gps_post_process.csv", "applanix/imu.csv", "calib/T_applanix_lidar.txt"):
                data = (sess + rel).encode()
                write(f"{root}/boreas/{sess}/{rel}", data)
                bo.append(f"{sess}/{rel}\t{len(data)}\t{len(data)}\t{len(data)}\t{h(data)}\t-\turl\tOK")
        open(root + "/_scripts/test_split/boreas_verify_full.tsv", "w").write("\n".join(bo) + "\n")
        write(root + "/kth/05.zip", b"k5"); write(root + "/kth/00.zip", b"k0"); write(root + "/kth/semindoor.zip", b"ks")
        open(root + "/kth/SHA256SUMS.txt", "w").write(f"{h(b'k0')}  00.zip\n{h(b'k5')}  05.zip\n{h(b'ks')}  semindoor.zip\n")
        write(root + "/lilocbench/bags/lt_changes_0.bag", b"b1"); write(root + "/lilocbench/bags/mapping.bag", b"b2")
        write(root + "/lilocbench/gt_poses_public.zip", b"gt")
        open(root + "/lilocbench/bags_sha256.txt", "w").write(f"{h(b'b1')}  bags/lt_changes_0.bag\n{h(b'b2')}  bags/mapping.bag\n")
        open(root + "/lilocbench/sha256.txt", "w").write(f"{h(b'gt')}  gt_poses_public.zip\n")
        return root

    def test_make_list_selects_test_inputs_only(self):
        with tempfile.TemporaryDirectory() as d:
            root = self.build_root(d)
            out = d + "/list.sha256"
            r = subprocess.run([sys.executable, "-I", TOOL, "make-list", "--data-root", root, "--out", out], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            paths = [l.split()[2] for l in open(out) if not l.startswith("#")]
            self.assertIn("spires/keble/sequences/2024-03-12-keble-college-03/raw/lidar-clouds.zip", paths)
            self.assertIn("spires/keble/calibration/README.md", paths)
            self.assertNotIn("spires/keble/sequences/2024-03-12-keble-college-02/raw/lidar-clouds.zip", paths)    # Keble 02 is calibration data
            self.assertNotIn("spires/keble/calibration/other.yaml", paths)
            self.assertNotIn("spires/keble/sequences/2024-03-12-keble-college-04/raw/lidar-clouds.zip", paths)    # status FAILED
            self.assertIn("boreas/boreas-2020-11-26-13-58/lidar/1606417543533737.bin", paths)
            self.assertIn("boreas/boreas-2020-11-26-13-58/applanix/gps_post_process.csv", paths)
            self.assertNotIn("boreas/boreas-2020-11-26-13-58/applanix/imu.csv", paths)
            self.assertFalse(any("2099" in p for p in paths))                                                      # a session that is not one of the three
            self.assertIn("kth/05.zip", paths); self.assertIn("kth/semindoor.zip", paths); self.assertNotIn("kth/00.zip", paths)
            self.assertIn("lilocbench/bags/lt_changes_0.bag", paths); self.assertNotIn("lilocbench/bags/mapping.bag", paths)
            self.assertIn("lilocbench/gt_poses_public.zip", paths)
            self.assertTrue(os.path.exists(out + ".unrecorded"))
            ok = subprocess.run([sys.executable, "-I", TOOL, "check-inputs", "--data-root", root, "--list", out, "--full"], capture_output=True, text=True)
            self.assertEqual(ok.returncode, 0, ok.stdout)

    def test_check_inputs_finds_each_kind_of_problem(self):
        with tempfile.TemporaryDirectory() as d:
            root = d + "/root"
            write(root + "/a.bin", b"hello"); write(root + "/b.bin", b"world")
            lst = d + "/l.sha256"
            h = lambda b: hashlib.sha256(b).hexdigest()
            open(lst, "w").write(f"# c\n{h(b'hello')}  5  a.bin\n{h(b'world')}  5  b.bin\n")
            run = lambda *extra: subprocess.run([sys.executable, "-I", TOOL, "check-inputs", "--data-root", root, "--list", lst, *extra], capture_output=True, text=True)
            self.assertEqual(run().returncode, 0)
            self.assertEqual(run("--full").returncode, 0)
            write(root + "/b.bin", b"WORLD")                                     # same size, other content: only --full sees it
            self.assertEqual(run().returncode, 0)
            r = run("--full"); self.assertEqual(r.returncode, 1); self.assertIn("SHA256 b.bin", r.stdout)
            write(root + "/b.bin", b"worl")
            r = run(); self.assertEqual(r.returncode, 1); self.assertIn("SIZE b.bin", r.stdout)
            os.remove(root + "/a.bin")
            r = run(); self.assertEqual(r.returncode, 1); self.assertIn("MISSING a.bin", r.stdout)
            # no data root: skipped, not failed
            r = subprocess.run([sys.executable, "-I", TOOL, "check-inputs", "--data-root", d + "/none", "--list", lst], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0); self.assertIn("SKIP", r.stdout)


if __name__ == "__main__":
    unittest.main()
