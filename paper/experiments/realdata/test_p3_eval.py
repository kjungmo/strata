"""Tests of p3_eval.py on small synthetic data. No real data are needed.  python3 -I -m unittest test_p3_eval"""
import csv
import io
import json
import math
import os
import subprocess
import sys
import tempfile
import unittest

import numpy as np

import p3_eval as ev

HERE = os.path.dirname(os.path.abspath(__file__))


def brute_matched(q, t, tol):
    q = np.asarray(q, dtype=np.float64)
    t = np.asarray(t, dtype=np.float64)
    out = np.zeros(len(q), dtype=bool)
    for i in range(len(q)):
        d = t - q[i]
        out[i] = bool((((d * d).sum(axis=1)) <= tol * tol).any()) if len(t) else False
    return out


class TestPcd(unittest.TestCase):
    def test_roundtrip_with_and_without_intensity(self):
        rng = np.random.RandomState(1)
        xyz = (rng.rand(50, 3) * 100 - 50).astype(np.float32)
        inten = rng.randint(0, 2, 50).astype(np.float32)
        with tempfile.TemporaryDirectory() as d:
            ev.write_pcd(d + "/a.pcd", xyz, inten)
            ev.write_pcd(d + "/b.pcd", xyz)
            a, ia = ev.read_pcd(d + "/a.pcd")
            b, ib = ev.read_pcd(d + "/b.pcd")
        np.testing.assert_array_equal(a, xyz)
        np.testing.assert_array_equal(ia, inten)
        np.testing.assert_array_equal(b, xyz)
        self.assertIsNone(ib)

    def test_reads_the_header_written_by_the_cpp_streaming_writer(self):
        hdr = ("# .PCD v0.7 - Point Cloud Data file format" + " " * 38 + "\nVERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\n"
               "WIDTH 2\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS 2\nDATA binary\n")
        with tempfile.TemporaryDirectory() as d:
            with open(d + "/c.pcd", "wb") as f:
                f.write(hdr.encode())
                np.array([1, 2, 3, 4, 5, 6], dtype="<f4").tofile(f)
            xyz, _ = ev.read_pcd(d + "/c.pcd")
        np.testing.assert_array_equal(xyz, [[1, 2, 3], [4, 5, 6]])

    def test_malformed_files_fail_loudly(self):
        with tempfile.TemporaryDirectory() as d:
            ev.write_pcd(d + "/a.pcd", np.zeros((10, 3)))
            with open(d + "/a.pcd", "rb") as f:
                data = f.read()
            with open(d + "/trunc.pcd", "wb") as f:
                f.write(data[:-5])
            with self.assertRaises(ValueError):
                ev.read_pcd(d + "/trunc.pcd")
            with open(d + "/nodata.pcd", "wb") as f:
                f.write(b"FIELDS x y z\n")
            with self.assertRaises(ValueError):
                ev.read_pcd(d + "/nodata.pcd")
            with open(d + "/ascii.pcd", "wb") as f:
                f.write(data.replace(b"DATA binary", b"DATA ascii"))
            with self.assertRaises(ValueError):
                ev.read_pcd(d + "/ascii.pcd")
            with open(d + "/f8.pcd", "wb") as f:
                f.write(data.replace(b"SIZE 4 4 4", b"SIZE 8 8 8"))
            with self.assertRaises(ValueError):
                ev.read_pcd(d + "/f8.pcd")


class TestMatched(unittest.TestCase):
    def test_equals_brute_force_on_random_clouds(self):
        rng = np.random.RandomState(7)
        for tol in (0.05, 0.1):
            t = rng.rand(3000, 3) * 2.0
            q = np.concatenate([rng.rand(2000, 3) * 2.0, t[:500] + (rng.rand(500, 3) - 0.5) * tol * 1.4])
            np.testing.assert_array_equal(ev.matched(q, t, tol), brute_matched(q, t, tol))

    def test_boundary_is_inclusive_and_crosses_cells(self):
        t = np.array([[0.05, 0.0, 0.0]])
        self.assertTrue(ev.matched(np.array([[0.0, 0.0, 0.0]]), t, 0.05)[0])             # distance exactly tol
        self.assertFalse(ev.matched(np.array([[0.0, 0.0, 0.0]]), np.array([[0.0500001, 0, 0]]), 0.05)[0])
        # a pair on either side of a cell boundary, closer than tol
        self.assertTrue(ev.matched(np.array([[0.0499, 0, 0]]), np.array([[0.0501, 0, 0]]), 0.05)[0])
        # diagonal neighbour cell: distance sqrt(3) * 0.03 = 0.052 > 0.05 is not a match, 0.028 * sqrt(3) = 0.0485 is
        self.assertFalse(ev.matched(np.array([[0.0, 0, 0]]), np.array([[0.03, 0.03, 0.03]]), 0.05)[0])
        self.assertTrue(ev.matched(np.array([[0.0, 0, 0]]), np.array([[0.028, 0.028, 0.028]]), 0.05)[0])
        # negative coordinates
        self.assertTrue(ev.matched(np.array([[-0.001, -0.001, -0.001]]), np.array([[0.001, 0.001, 0.001]]), 0.05)[0])

    def test_many_points_in_one_cell(self):
        t = np.array([[0.001 * i, 0.0, 0.0] for i in range(40)])                          # 40 points in 2 cells
        q = np.array([[0.0395, 0.0, 0.049]])
        np.testing.assert_array_equal(ev.matched(q, t, 0.05), brute_matched(q, t, 0.05))
        self.assertTrue(ev.matched(q, t, 0.05)[0])

    def test_empty_and_invalid_inputs(self):
        self.assertEqual(ev.matched(np.zeros((0, 3)), np.zeros((3, 3)), 0.05).shape, (0,))
        self.assertFalse(ev.matched(np.zeros((2, 3)), np.zeros((0, 3)), 0.05).any())
        with self.assertRaises(ValueError):
            ev.matched(np.zeros((1, 3)), np.zeros((1, 3)), 0.0)
        with self.assertRaises(ValueError):
            ev.matched(np.array([[1e9, 0, 0]]), np.zeros((1, 3)), 0.05)


class TestKthScores(unittest.TestCase):
    def setUp(self):
        # 8 static and 2 dynamic points, far apart (1 m)
        self.gt = np.array([[float(i), 0.0, 0.0] for i in range(10)], dtype=np.float32)
        self.dyn = np.array([False] * 8 + [True] * 2)

    def test_hand_computed_scores(self):
        out = np.concatenate([self.gt[:6], self.gt[8:9]])                  # keeps 6 static and 1 of 2 dynamic
        r = ev.kth_scores(self.gt, self.dyn, out)
        self.assertAlmostEqual(r["SA"], 75.0)
        self.assertAlmostEqual(r["DA"], 50.0)
        self.assertAlmostEqual(r["AA"], math.sqrt(75.0 * 50.0))
        self.assertAlmostEqual(r["HA"], 2 * 75.0 * 50.0 / 125.0)
        self.assertEqual((r["gt_static"], r["gt_dynamic"], r["map_points"]), (8, 2, 7))

    def test_gt_static_only_gives_100_100(self):
        r = ev.kth_scores(self.gt, self.dyn, self.gt[~self.dyn])
        self.assertEqual((r["SA"], r["DA"], r["AA"], r["HA"]), (100.0, 100.0, 100.0, 100.0))

    def test_remove_nothing_gives_100_0(self):
        r = ev.kth_scores(self.gt, self.dyn, self.gt)
        self.assertEqual((r["SA"], r["DA"], r["AA"], r["HA"]), (100.0, 0.0, 0.0, 0.0))

    def test_remove_everything_gives_0_100(self):
        r = ev.kth_scores(self.gt, self.dyn, np.zeros((0, 3)))
        self.assertEqual((r["SA"], r["DA"]), (0.0, 100.0))

    def test_tolerance_decides_removal(self):
        out = self.gt + np.array([0.04, 0, 0], dtype=np.float32)               # every point 4 cm off
        self.assertEqual(ev.kth_scores(self.gt, self.dyn, out, 0.05)["SA"], 100.0)
        self.assertEqual(ev.kth_scores(self.gt, self.dyn, out, 0.03)["SA"], 0.0)

    def test_needs_both_classes(self):
        with self.assertRaises(ValueError):
            ev.kth_scores(self.gt, np.zeros(10, dtype=bool), self.gt)
        with self.assertRaises(ValueError):
            ev.kth_scores(self.gt, np.ones(10, dtype=bool), self.gt)

    def test_cli_selfcheck_and_kth(self):
        rng = np.random.RandomState(3)
        xyz = (rng.rand(400, 3) * 20).astype(np.float32)
        lab = (rng.rand(400) < 0.1).astype(np.float32)
        with tempfile.TemporaryDirectory() as d:
            ev.write_pcd(d + "/gt.pcd", xyz, lab)
            ev.write_pcd(d + "/map.pcd", xyz[lab == 0])
            run = subprocess.run([sys.executable, "-I", os.path.join(HERE, "p3_eval.py"), "kth-selfcheck", "--gt", d + "/gt.pcd"], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("SELFCHECK PASS", run.stdout)
            run = subprocess.run([sys.executable, "-I", os.path.join(HERE, "p3_eval.py"), "kth", "--gt", d + "/gt.pcd", "--map", d + "/map.pcd"], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            res = json.loads(run.stdout)
            self.assertEqual((res["SA"], res["DA"]), (100.0, 100.0))
            # a dynamic point 3 cm from a static point cannot be removed by any map: the oracle ceiling of DA drops below 100 and the check still passes
            near = xyz[lab == 0][:1] + np.array([[0.03, 0.0, 0.0]], dtype=np.float32)
            ev.write_pcd(d + "/gt2.pcd", np.concatenate([xyz, near]), np.concatenate([lab, [1.0]]).astype(np.float32))
            run = subprocess.run([sys.executable, "-I", os.path.join(HERE, "p3_eval.py"), "kth-selfcheck", "--gt", d + "/gt2.pcd"], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            res = json.loads(run.stdout.split("SELFCHECK")[0])
            self.assertEqual(res["dynamic_points_within_tol_of_a_static_point"], 1)
            n_dyn = int(lab.sum()) + 1
            self.assertAlmostEqual(res["DA_oracle_ceiling"], 100.0 * (n_dyn - 1) / n_dyn)
            self.assertAlmostEqual(res["gt_static_only"]["DA"], res["DA_oracle_ceiling"])
            # a GT cloud whose intensity is not a 0/1 flag is refused
            ev.write_pcd(d + "/bad.pcd", xyz, lab * 5)
            run = subprocess.run([sys.executable, "-I", os.path.join(HERE, "p3_eval.py"), "kth", "--gt", d + "/bad.pcd", "--map", d + "/map.pcd"], capture_output=True, text=True)
            self.assertNotEqual(run.returncode, 0)
            # a map that drops static points fails the self-check logic only through the numbers, not silently
            ev.write_pcd(d + "/half.pcd", xyz[lab == 0][::2])
            run = subprocess.run([sys.executable, "-I", os.path.join(HERE, "p3_eval.py"), "kth", "--gt", d + "/gt.pcd", "--map", d + "/half.pcd"], capture_output=True, text=True)
            self.assertAlmostEqual(json.loads(run.stdout)["SA"], 100.0 * np.ceil((lab == 0).sum() / 2) / (lab == 0).sum(), places=6)


class TestShadowed(unittest.TestCase):
    def test_independent_count_agrees_with_matched(self):
        rng = np.random.RandomState(11)
        st = rng.rand(3000, 3) * 3.0
        dy = np.concatenate([rng.rand(400, 3) * 3.0, st[:100] + (rng.rand(100, 3) - 0.5) * 0.08])
        for tol in (0.05, 0.1):
            self.assertEqual(ev.shadowed_independent(st, dy, tol), int(ev.matched(dy, st, tol).sum()))
        self.assertEqual(ev.shadowed_independent(np.zeros((0, 3)), dy, 0.05), 0)

    def test_boundary_is_inclusive(self):
        self.assertEqual(ev.shadowed_independent(np.array([[0.05, 0, 0]]), np.array([[0.0, 0, 0]]), 0.05), 1)
        self.assertEqual(ev.shadowed_independent(np.array([[0.0500001, 0, 0]]), np.array([[0.0, 0, 0]]), 0.05), 0)


class TestStatistics(unittest.TestCase):
    def test_wilson(self):
        lo, hi = ev.wilson(0, 10)
        self.assertAlmostEqual(lo, 0.0, places=6)
        self.assertAlmostEqual(hi, 0.27753, places=4)
        lo, hi = ev.wilson(5, 10)
        self.assertAlmostEqual(lo, 0.23659, places=4)
        self.assertAlmostEqual(hi, 0.76341, places=4)
        self.assertTrue(math.isnan(ev.wilson(0, 0)[0]))

    def test_block_bootstrap_is_deterministic_and_respects_blocks(self):
        tile = np.repeat(np.arange(20), 10)
        flag = np.zeros(200, dtype=bool)
        flag[tile < 4] = True                                       # whole tiles are positive: variance across tiles
        den = np.ones(200, dtype=bool)
        a = ev.block_bootstrap_rate(flag, den, tile, reps=500)
        b = ev.block_bootstrap_rate(flag, den, tile, reps=500)
        self.assertEqual(a, b)
        self.assertLess(a[0], 0.2)
        self.assertGreater(a[1], 0.2)
        self.assertLess(a[1] - a[0], 0.45)
        self.assertGreater(a[1] - a[0], 0.1)
        c = ev.block_bootstrap_rate(np.ones(200, dtype=bool), den, tile, reps=200)
        self.assertEqual(c, (1.0, 1.0))
        self.assertTrue(math.isnan(ev.block_bootstrap_rate(flag, np.zeros(200, dtype=bool), tile)[0]))
        # only cells of the denominator count
        half = np.arange(200) < 100
        d = ev.block_bootstrap_rate(flag, half, tile, reps=200)
        self.assertGreaterEqual(d[0], 0.0)

    def test_percentile_helper(self):
        self.assertTrue(math.isnan(ev.pct([], 50)))
        self.assertEqual(ev.pct([1, 2, 3, 4, 5], 50), 3.0)
        self.assertAlmostEqual(ev.pct([1, 2, 3, 4, 5], 90), 4.6)


def cells_of_wall(x0, y0, length, res, horizontal=True):
    n = int(round(length / res))
    return [(x0 + i, y0) if horizontal else (x0, y0 + i) for i in range(n)]


class TestWallRuns(unittest.TestCase):
    res = 0.05

    def mask(self, cells, **kw):
        ix = np.array([c[0] for c in cells]); iy = np.array([c[1] for c in cells])
        return ev.wall_run_mask(ix, iy, self.res, **kw)

    def test_straight_wall_middle_cells_are_runs(self):
        cells = cells_of_wall(0, 0, 4.0, self.res)                     # 80 cells, 4 m
        m = self.mask(cells)
        self.assertTrue(m[40])
        self.assertTrue(m[25])
        self.assertTrue(m[:].sum() > 60)
        # the two ends have patches of only ~1 m: extent 1.0 is the boundary; the very first cell has 21 cells (1 m) -> extent sqrt(12 * var)
        self.assertEqual(m.dtype, bool)

    def test_short_box_is_not_a_run(self):
        cells = cells_of_wall(0, 0, 0.5, self.res)                     # a 0.5 m piece of furniture
        self.assertFalse(self.mask(cells).any())

    def test_blob_is_not_a_run(self):
        cells = [(i, j) for i in range(20) for j in range(20)]         # 1 m x 1 m filled square
        self.assertFalse(self.mask(cells).any())

    def test_diagonal_wall_is_a_run(self):
        cells = [(i, i) for i in range(80)]
        self.assertTrue(self.mask(cells)[40])

    def test_corner_cell_is_excluded_but_the_arms_are_runs(self):
        cells = cells_of_wall(0, 0, 4.0, self.res) + cells_of_wall(0, 1, 4.0, self.res, horizontal=False)
        ix = np.array([c[0] for c in cells]); iy = np.array([c[1] for c in cells])
        m = ev.wall_run_mask(ix, iy, self.res)
        corner = [i for i, c in enumerate(cells) if c == (0, 0)][0]
        self.assertFalse(m[corner])
        far_h = [i for i, c in enumerate(cells) if c == (60, 0)][0]
        far_v = [i for i, c in enumerate(cells) if c == (0, 60)][0]
        self.assertTrue(m[far_h]); self.assertTrue(m[far_v])

    def test_thick_wall_noise_stays_a_run_but_double_wall_does_not(self):
        thick = [(i, j) for i in range(80) for j in range(3)]          # 0.15 m thick edge noise
        self.assertTrue(self.mask(thick)[120])
        double = cells_of_wall(0, 0, 4.0, self.res) + cells_of_wall(0, 12, 4.0, self.res)     # two walls 0.6 m apart in one patch
        self.assertFalse(self.mask(double)[40])

    def test_parameters_are_used(self):
        cells = cells_of_wall(0, 0, 4.0, self.res)
        self.assertFalse(self.mask(cells, min_cells=1000)[40])
        self.assertFalse(self.mask(cells, min_extent=5.0)[40])
        self.assertFalse(self.mask(cells, rho=0.2)[40])             # a 0.4 m patch has extent 0.4 < 1.0

    def test_empty_input(self):
        self.assertEqual(ev.wall_run_mask(np.array([], dtype=int), np.array([], dtype=int), 0.05).size, 0)


def make_dump(rows):
    """rows: (label, ix, iy, n, h, verdict, first_static[, first_touch = 1]) -> arrays as read_dump returns."""
    return {
        "label": np.array([r[0] for r in rows]), "ix": np.array([r[1] for r in rows]), "iy": np.array([r[2] for r in rows]),
        "iz": np.zeros(len(rows), dtype=int), "n": np.array([r[3] for r in rows]), "h": np.array([r[4] for r in rows]),
        "verdict": np.array([ev.VERDICTS[r[5]] for r in rows]), "first_static": np.array([r[6] for r in rows]),
        "first_touch": np.array([r[7] if len(r) > 7 else 1 for r in rows]),
    }


class TestLilMetrics(unittest.TestCase):
    def build(self):
        rows = []
        # a 4 m wall (80 cells) with n = 10: 60 static (30 decided 4 windows after the first touch, 30 decided 7 windows after it), 10 undecided, 10 rejected
        for i in range(80):
            v = "static" if i < 60 else ("undecided" if i < 70 else "rejected")
            fs = (4 if i < 30 else 9) if v == "static" else -1
            ft = 1 if i < 30 else 3                              # windows to decision: 4 for the first 30, 9 - 3 + 1 = 7 for the next 30
            rows.append((ev.REF_OCCUPIED, 100 + i, 200, 10, 10, v, fs, ft))
        # clutter: ref_free cells far from the wall; 40 with H >= 1 and n >= 5: 4 static, 6 undecided, 30 rejected; rates by band
        for i in range(40):
            v = "static" if i < 4 else ("undecided" if i < 10 else "rejected")
            h = 5 if i < 4 else 1                              # the static ones have hit rate 0.5, the others 0.1
            fs = 5 if i < 4 else (7 if i in (20, 21) else -1)  # two cells were Static at some window and rejected later
            rows.append((ev.REF_FREE, 100 + i, 400, 10, h, v, fs))
        # excluded: clutter with n < nmin, a free cell never hit (h = 0), a near-wall free cell that is static
        rows.append((ev.REF_FREE, 300, 400, 4, 2, "static", -1))
        rows.append((ev.REF_FREE, 301, 400, 10, 0, "static", -1))
        rows.append((ev.REF_FREE_NEAR, 302, 400, 10, 3, "static", -1))
        # a short piece of furniture (0.5 m) that is rejected: not part of the wall-run truth
        for i in range(10):
            rows.append((ev.REF_OCCUPIED, 300 + i, 600, 10, 10, "rejected", -1))
        return make_dump(rows)

    def test_rates_counts_and_exclusions(self):
        m = ev.lil_metrics(self.build(), 0.05)
        c = m["false_static_on_clutter"]
        self.assertEqual((c["k"], c["n"]), (4, 40))
        self.assertAlmostEqual(c["rate"], 0.1)
        # unit of the alpha test (Amendment 5): the cell, Static at any window: 4 Static at the end + 2 rejected after being Static
        e = m["ever_static_on_clutter"]
        self.assertEqual((e["k"], e["n"]), (6, 40))
        self.assertAlmostEqual(e["rate"], 0.15)
        self.assertEqual(m["clutter_undecided"]["k"], 6)
        self.assertEqual(m["clutter_rejected"]["k"], 30)
        w = m["false_reject_on_wall_runs"]
        # every cell of the 4 m wall has a patch of at least 21 cells with extent >= 1.0 m, so all 80 are wall-run cells;
        # the 0.5 m piece of furniture is none of them
        self.assertEqual((w["k"], w["n"]), (10, 80))
        self.assertEqual(m["wall_runs_not_static"]["k"], 20)
        self.assertEqual(m["wall_runs_static"]["k"], 60)
        self.assertEqual(m["wall_run_cells_total"], 80)
        lo, hi = m["false_static_on_clutter"]["wilson95"]
        self.assertLess(lo, 0.1); self.assertGreater(hi, 0.1)
        # band table: 4 static clutter cells have hit rate 0.5 (band [0.30, 0.65)), 36 have 0.1 (band [0, 0.15))
        bands = {tuple(b["band"]): b for b in m["false_static_by_hit_rate_band"]}
        self.assertEqual((bands[(0.0, 0.15)]["k"], bands[(0.0, 0.15)]["n"]), (0, 36))
        self.assertEqual((bands[(0.30, 0.65)]["k"], bands[(0.30, 0.65)]["n"]), (4, 4))
        # latency: windows to first static on wall-run cells
        lat = m["wall_run_windows_to_first_static"]
        self.assertEqual(lat["n"], 60)
        self.assertEqual((lat["p50"], lat["p90"], lat["min"]), (5.5, 7.0, 4.0))

    def test_nmin_and_h_filters(self):
        d = self.build()
        m = ev.lil_metrics(d, 0.05, nmin=11)                              # nobody has n >= 11
        self.assertEqual(m["false_static_on_clutter"]["n"], 0)
        self.assertTrue(math.isnan(m["false_static_on_clutter"]["rate"]))
        m = ev.lil_metrics(d, 0.05, nmin=1)                               # the n = 4 cell now counts
        self.assertEqual(m["false_static_on_clutter"]["n"], 41)
        self.assertEqual(m["false_static_on_clutter"]["k"], 5)

    def test_qualitative_counts(self):
        m = ev.lil_metrics(self.build(), 0.05)
        q = m["qualitative"]
        self.assertEqual(q["ref_free_cells_static"], 4 + 1)               # the h = 0 free cell with n = 10 counts here (no hit filter)
        self.assertEqual(q["ref_free_cells_eligible"], 40 + 1)
        self.assertEqual(q["ref_occupied_cells_eligible"], 80 + 10)
        self.assertEqual(q["ref_occupied_cells_rejected"], 10 + 10)
        self.assertEqual(q["ref_occupied_cells_not_static"], 10 + 10 + 10)

    def test_dump_reader_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            with open(d + "/x.csv", "w") as f:
                f.write("label,ix,iy,iz,n,h,verdict,first_static,first_touch\n1,60,50,0,10,10,static,3,1\n2,30,50,0,1,1,undecided,-1,4\n3,5,5,0,0,0,rejected,-1,-1\n")
            r = ev.read_dump(d + "/x.csv")
            self.assertEqual(r["verdict"].tolist(), [2, 1, 0])
            self.assertEqual(r["first_static"].tolist(), [3, -1, -1])
            self.assertEqual(r["first_touch"].tolist(), [1, 4, -1])
            with open(d + "/e.csv", "w") as f:
                f.write("label,ix,iy,iz,n,h,verdict,first_static,first_touch\n")
            with self.assertRaises(ValueError):
                ev.read_dump(d + "/e.csv")
            run = subprocess.run([sys.executable, "-I", os.path.join(HERE, "p3_eval.py"), "lil", "--dump", d + "/x.csv", "--res", "0.1"], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("false_static_on_clutter", json.loads(run.stdout))


class TestSpiresAndBoreas(unittest.TestCase):
    def test_spires_metrics(self):
        with tempfile.TemporaryDirectory() as d:
            with open(d + "/c.csv", "w") as f:
                f.write("label,eligible,verdict,count\n1,1,static,30\n1,1,undecided,10\n1,1,rejected,60\n1,0,static,999\n"
                        "3,1,static,700\n3,1,undecided,200\n3,1,rejected,100\n3,0,rejected,5\n"
                        "2,1,static,600\n2,1,undecided,50\n2,1,rejected,350\n2,0,static,7\n")
            with open(d + "/c.csv.latency", "w") as f:
                f.write("label,windows_to_static,count\n3,4,500\n3,6,200\n1,5,30\n2,3,100\n2,9,500\n")
            m = ev.spires_metrics(d + "/c.csv", d + "/c.csv.latency")
        c = m["false_static_on_clutter"]
        self.assertEqual((c["k"], c["n"]), (30, 100))                     # ineligible cells are excluded
        s = m["structure_label_3"]
        self.assertEqual((s["n"], s["rejected"], s["undecided"], s["static"]), (1000, 100, 200, 700))
        self.assertAlmostEqual(s["false_reject_rate"], 0.1)
        self.assertAlmostEqual(s["undecided_share"], 0.2)
        self.assertEqual(s["windows_to_first_static"], {"n": 700, "p50": 4.0, "p90": 6.0})
        # ground (label 2) is a block of its own, with its own rates and its own latency (PREREG_P3.md section 7.2)
        g = m["structure_label_2"]
        self.assertEqual((g["n"], g["rejected"], g["undecided"], g["static"]), (1000, 350, 50, 600))
        self.assertAlmostEqual(g["false_reject_rate"], 0.35)
        self.assertAlmostEqual(g["undecided_share"], 0.05)
        self.assertEqual(g["windows_to_first_static"], {"n": 600, "p50": 9.0, "p90": 9.0})
        self.assertNotIn("structure_windows_to_first_static", m)
        # a label that is asked for but absent gives an empty block, not a crash; a label not asked for gives no block
        with tempfile.TemporaryDirectory() as d:
            with open(d + "/c.csv", "w") as f:
                f.write("label,eligible,verdict,count\n1,1,static,1\n3,1,static,9\n")
            m2 = ev.spires_metrics(d + "/c.csv")
            self.assertEqual(m2["structure_label_2"]["n"], 0)
            self.assertTrue(math.isnan(m2["structure_label_2"]["false_reject_rate"]))
            self.assertNotIn("windows_to_first_static", m2["structure_label_3"])
            self.assertNotIn("structure_label_2", ev.spires_metrics(d + "/c.csv", structure_labels=(3,)))

    def test_voxel_id_decoding_matches_the_engine_packing(self):
        def pack(ix, iy, iz):
            off = 1 << 20
            return ((ix + off) << 42) | ((iy + off) << 21) | (iz + off)
        ids = np.array([pack(50, 0, 0), pack(-3, 7, -1), pack(1000, -1000, 5)], dtype=np.int64)
        ix, iy, iz = ev.decode_voxel_ids(ids)
        self.assertEqual(ix.tolist(), [50, -3, 1000]); self.assertEqual(iy.tolist(), [0, 7, -1000]); self.assertEqual(iz.tolist(), [0, -1, 5])

    def test_jaccard(self):
        r = ev.jaccard([1, 2, 3, 4], [3, 4, 5])
        self.assertEqual((r["intersection"], r["union"]), (2, 5))
        self.assertAlmostEqual(r["jaccard"], 0.4)
        self.assertEqual(ev.jaccard([1, 1, 2], [2, 2])["union"], 2)       # duplicates are sets
        self.assertTrue(math.isnan(ev.jaccard([], [])["jaccard"]))

    def test_jaccard_in_shared_tiles_ignores_unshared_areas(self):
        def pack(ix, iy, iz):
            off = 1 << 20
            return ((ix + off) << 42) | ((iy + off) << 21) | (iz + off)
        res = 0.2                                                          # 10 m tile = 50 voxels
        a = [pack(i, 0, 0) for i in range(0, 100, 10)]                    # tiles 0 and 1
        b = [pack(i, 0, 0) for i in range(0, 20, 10)] + [pack(i, 0, 0) for i in range(500, 600, 10)]      # tile 0 (shared), tiles 10, 11 (not)
        r = ev.jaccard_in_shared_tiles(np.array(a), np.array(b), res)
        self.assertEqual(r["shared_tiles"], 1)
        # inside tile 0: a has voxels 0,10,20,30,40; b has 0,10 -> Jaccard 2/5
        self.assertAlmostEqual(r["jaccard"], 2 / 5)
        # observed sets decide which tiles are shared: if b observed tile 1 as well (but no Static voxel there), tile 1 becomes shared
        obs_b = np.array(b + [pack(60, 0, 0)])
        r2 = ev.jaccard_in_shared_tiles(np.array(a), np.array(b), res, all_b=obs_b)
        self.assertEqual(r2["shared_tiles"], 2)
        self.assertLess(r2["jaccard"], r["jaccard"])

    def test_jaccard_over_sessions_uses_the_tiles_all_sessions_observed(self):
        def pack(ix, iy, iz):
            off = 1 << 20
            return ((ix + off) << 42) | ((iy + off) << 21) | (iz + off)
        res = 0.2                                                          # 10 m tile = 50 voxels; tile k holds ix in [50k, 50k+49]
        t = lambda k, n: [pack(50 * k + i, 0, 0) for i in range(n)]
        # tiles 0, 1, 2. Session A: Static in 0 (10 voxels), 1 (10), observes 2 without a Static voxel.
        # Session B: Static in 0 (5 of A's 10), observes 1 without Static voxels, Static in 2 (10).
        # Session C: Static in 0 (10), 1 (10), 2 (10).
        sa, sb, sc = t(0, 10) + t(1, 10), t(0, 5) + t(2, 10), t(0, 10) + t(1, 10) + t(2, 10)
        oa = sa + t(2, 20)
        ob = sb + t(1, 20)
        oc = list(sc)
        out = ev.jaccard_over_sessions([sa, sb, sc], [oa, ob, oc], res)
        self.assertEqual(len(out), 3)
        by = {(r["session_a"], r["session_b"]): r for r in out}
        for r in out:
            self.assertEqual(r["shared_tiles"], 3)                      # all three tiles are observed by all sessions
        # A vs B in the three tiles: A = 20 Static, B = 15 Static, common = 5 (tile 0), union = 30
        self.assertEqual((by[(0, 1)]["intersection"], by[(0, 1)]["union"]), (5, 30))
        # the old reading (tiles of the Static sets only): A and B share tile 0 only -> 5 / 10
        old = ev.jaccard_in_shared_tiles(np.array(sa), np.array(sb), res)
        self.assertEqual(old["shared_tiles"], 1)
        self.assertAlmostEqual(old["jaccard"], 5 / 10)
        self.assertLess(by[(0, 1)]["jaccard"], old["jaccard"])          # dropping tiles without a Static voxel biased the Jaccard upward
        # pairwise secondary block uses the tiles of the pair only
        self.assertEqual(by[(0, 1)]["pairwise_tiles"]["shared_tiles"], 3)
        # a tile that one session never observed is outside for every pair, also for the pair that both observed
        out2 = ev.jaccard_over_sessions([sa, sb, t(0, 10)], [oa, ob, t(0, 10)], res)
        by2 = {(r["session_a"], r["session_b"]): r for r in out2}
        self.assertEqual(by2[(0, 1)]["shared_tiles"], 1)
        self.assertEqual(by2[(0, 1)]["pairwise_tiles"]["shared_tiles"], 3)
        self.assertAlmostEqual(by2[(0, 1)]["jaccard"], 5 / 10)
        self.assertNotEqual(by2[(0, 1)]["jaccard"], by2[(0, 1)]["pairwise_tiles"]["jaccard"])
        # errors: counts differ, one session, a Static set outside its observed set
        with self.assertRaises(ValueError):
            ev.jaccard_over_sessions([sa, sb], [oa], res)
        with self.assertRaises(ValueError):
            ev.jaccard_over_sessions([sa], [oa], res)
        with self.assertRaises(ValueError):
            ev.jaccard_over_sessions([sa, sb], [oa, t(1, 20)], res)
        # nothing shared: empty sets give NaN, zero shared tiles
        nothing = ev.jaccard_over_sessions([t(0, 3), t(5, 3)], [t(0, 3), t(5, 3)], res)[0]
        self.assertEqual(nothing["shared_tiles"], 0)
        self.assertTrue(math.isnan(nothing["jaccard"]))

    def test_new_static_counts_cells_that_need_more_than_one_session(self):
        r = ev.new_static([1, 2, 3, 4, 5], [[1, 2], [2, 9], [4]])
        self.assertEqual((r["cumulative_static"], r["static_in_any_independent_session"], r["only_in_cumulative"]), (5, 4, 2))     # 3 and 5
        self.assertAlmostEqual(r["share_only_in_cumulative"], 0.4)
        self.assertEqual(ev.new_static([1, 2], [[1, 2]])["only_in_cumulative"], 0)
        self.assertTrue(math.isnan(ev.new_static([], [[1]])["share_only_in_cumulative"]))
        self.assertEqual(ev.new_static([7, 7, 8], [])["only_in_cumulative"], 2)                   # duplicates are sets, no independent session
        with tempfile.TemporaryDirectory() as d:
            np.array([1, 2, 3], dtype="<i8").tofile(d + "/c.ids"); np.array([1], dtype="<i8").tofile(d + "/a.ids"); np.array([2], dtype="<i8").tofile(d + "/b.ids")
            run = subprocess.run([sys.executable, "-I", os.path.join(HERE, "p3_eval.py"), "newstatic", "--cum", d + "/c.ids", "--ind", d + "/a.ids", d + "/b.ids"], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(json.loads(run.stdout)["only_in_cumulative"], 1)

    def test_zoffset_is_the_median_of_the_tile_differences(self):
        with tempfile.TemporaryDirectory() as d:
            def table(path, rows):
                with open(path, "w") as f:
                    f.write("tx,ty,count,ground_z\n")
                    for r in rows:
                        f.write("%d,%d,%d,%.3f\n" % r)
            table(d + "/a.csv", [(0, 0, 900, 1.0), (1, 0, 900, 2.0), (2, 0, 900, 3.0), (3, 0, 900, 5.0), (9, 9, 900, 7.0), (4, 0, 100, 9.0)])
            table(d + "/b.csv", [(0, 0, 900, 0.5), (1, 0, 900, 1.0), (2, 0, 900, 2.9), (3, 0, 900, 4.9), (8, 8, 900, 0.0), (4, 0, 900, 0.0)])
            r = ev.zoffset(d + "/a.csv", d + "/b.csv")
            self.assertEqual(r["tiles"], 4)                       # (9,9) and (8,8) are not shared; tile (4,0) has too few returns in a
            self.assertAlmostEqual(r["median"], 0.3)              # differences 0.5, 1.0, 0.1, 0.1 -> sorted 0.1 0.1 0.5 1.0 -> median 0.3
            self.assertAlmostEqual(ev.zoffset(d + "/a.csv", d + "/b.csv", min_count=50)["tiles"], 5)
            self.assertAlmostEqual(ev.zoffset(d + "/b.csv", d + "/a.csv")["median"], -0.3)     # reversed roles, reversed sign
            table(d + "/c.csv", [(50, 50, 900, 1.0)])
            self.assertTrue(math.isnan(ev.zoffset(d + "/a.csv", d + "/c.csv")["median"]))
            run = subprocess.run([sys.executable, "-I", os.path.join(HERE, "p3_eval.py"), "zoffset", "--a", d + "/a.csv", "--b", d + "/b.csv"], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertAlmostEqual(json.loads(run.stdout)["median"], 0.3)
            run = subprocess.run([sys.executable, "-I", os.path.join(HERE, "p3_eval.py"), "zoffset", "--a", d + "/a.csv", "--b", d + "/b.csv", "--field", "median"], capture_output=True, text=True)
            self.assertAlmostEqual(float(run.stdout), 0.3)

    def test_cli_boreas_and_spires(self):
        def pack(ix, iy, iz):
            off = 1 << 20
            return ((ix + off) << 42) | ((iy + off) << 21) | (iz + off)
        with tempfile.TemporaryDirectory() as d:
            np.array([pack(i, 0, 0) for i in range(0, 40)], dtype="<i8").tofile(d + "/a.ids")
            np.array([pack(i, 0, 0) for i in range(20, 60)], dtype="<i8").tofile(d + "/b.ids")
            np.array([pack(i, 0, 0) for i in range(0, 120)], dtype="<i8").tofile(d + "/a_obs.ids")           # tiles 0, 1, 2
            np.array([pack(i, 0, 0) for i in range(20, 120)], dtype="<i8").tofile(d + "/b_obs.ids")
            prog = [sys.executable, "-I", os.path.join(HERE, "p3_eval.py")]
            run = subprocess.run(prog + ["jaccard", "--ids", d + "/a.ids", d + "/b.ids", "--observed", d + "/a_obs.ids", d + "/b_obs.ids", "--res", "0.2"], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            r = json.loads(run.stdout)[0]
            self.assertEqual(r["shared_tiles"], 3)                                  # the observed sets decide, not the Static sets (which share one tile)
            self.assertAlmostEqual(r["jaccard"], 20 / 60)                           # 20 common of 60 in the union
            self.assertTrue(r["a"].endswith("a.ids") and r["b"].endswith("b.ids"))
            # --observed is required; a different number of files, or a Static voxel outside the observed set, is an error with a message
            run = subprocess.run(prog + ["jaccard", "--ids", d + "/a.ids", d + "/b.ids", "--res", "0.2"], capture_output=True, text=True)
            self.assertNotEqual(run.returncode, 0)
            run = subprocess.run(prog + ["jaccard", "--ids", d + "/a.ids", d + "/b.ids", "--observed", d + "/a_obs.ids", "--res", "0.2"], capture_output=True, text=True)
            self.assertEqual(run.returncode, 2); self.assertIn("one file per", run.stderr)
            run = subprocess.run(prog + ["jaccard", "--ids", d + "/b_obs.ids", d + "/a_obs.ids", "--observed", d + "/a.ids", d + "/b.ids", "--res", "0.2"], capture_output=True, text=True)
            self.assertEqual(run.returncode, 2); self.assertIn("not inside the observed set", run.stderr)
            with open(d + "/c.csv", "w") as f:
                f.write("label,eligible,verdict,count\n1,1,static,1\n3,1,static,9\n2,1,rejected,4\n2,1,static,6\n")
            with open(d + "/c.csv.latency", "w") as f:
                f.write("label,windows_to_static,count\n3,4,9\n2,7,6\n")
            run = subprocess.run(prog + ["spires", "--counts", d + "/c.csv", "--latency", d + "/c.csv.latency"], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            j = json.loads(run.stdout)
            self.assertEqual(j["false_static_on_clutter"]["k"], 1)
            self.assertEqual(j["structure_label_3"]["n"], 9)
            self.assertEqual(j["structure_label_2"]["n"], 10)                       # ground is reported by the default command line
            self.assertEqual(j["structure_label_2"]["windows_to_first_static"]["p50"], 7.0)
            run = subprocess.run(prog + ["spires", "--counts", d + "/c.csv", "--structure-labels", "3"], capture_output=True, text=True)
            self.assertNotIn("structure_label_2", json.loads(run.stdout))


if __name__ == "__main__":
    unittest.main()
