"""Unit tests of analyze_p1.py: known sequences give known run lengths, percentiles, transition
statistics and gaps. Synthetic tables only; no real data. Run: python3 -I -m unittest discover -s . -p "test_*.py"
"""
import itertools
import os
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze_p1 as A  # noqa: E402


def runs_of(seq):
    out = []
    for k, grp in itertools.groupby(seq):
        out.append(("h" if k else "m") + str(len(list(grp))))
    return "".join(out)


class Percentile(unittest.TestCase):
    def test_weighted_equals_numpy_linear(self):
        rng = np.random.RandomState(1)
        for n in (1, 2, 3, 10, 57):
            x = rng.randint(0, 6, size=n) / 5.0
            u, c = A.unique_counts(x)
            for q in (0, 1, 10, 25, 50, 90, 99, 100):
                self.assertAlmostEqual(A.pct_weighted(u, c, q), float(np.percentile(x, q)), places=12, msg="n=%d q=%s" % (n, q))

    def test_known_values_and_empty(self):
        u, c = np.array([0.0, 1.0]), np.array([9, 1])
        self.assertAlmostEqual(A.pct_weighted(u, c, 90), 0.1)            # position 8.1 between 0 and 1
        self.assertAlmostEqual(A.pct_weighted(u, c, 100), 1.0)
        self.assertTrue(np.isnan(A.pct_weighted(u, [0, 0], 50)))


class Runs(unittest.TestCase):
    def test_parse_and_features(self):
        r = A.parse_runs("h3m2h1")
        self.assertEqual(r, [(True, 3), (False, 2), (True, 1)])
        self.assertEqual(A.cell_features(r), (6, 4, 2, 1, 2))            # N, H, hh, mm, switches
        self.assertEqual(A.cell_features(A.parse_runs("m5")), (5, 0, 0, 4, 0))

    def test_parse_rejects_malformed(self):
        for bad in ("", "h", "x3", "h0m", "h3h2", "h3 m2", "h3m2x"):
            with self.assertRaises(ValueError, msg=bad):
                A.parse_runs(bad)

    def test_expected_switches_by_enumeration(self):
        for n, h in ((6, 3), (5, 1), (7, 2), (4, 4), (5, 0)):
            seqs = set(itertools.permutations([1] * h + [0] * (n - h)))
            sw = [sum(a != b for a, b in zip(s, s[1:])) for s in seqs]
            hh = [sum(a == 1 and b == 1 for a, b in zip(s, s[1:])) for s in seqs]
            self.assertAlmostEqual(A.expected_switches(n, h), np.mean(sw), places=12)
            self.assertAlmostEqual(A.expected_hh(n, h), np.mean(hh), places=12)

    def test_lag1_autocorr_matches_direct_formula(self):
        for seq in ([1, 1, 0, 0, 1, 0], [1, 0, 1, 0, 1, 0, 1, 0], [1, 1, 1, 0, 0, 0], [0, 1, 1, 1, 1, 0, 1]):
            x = np.array(seq, dtype=float)
            p = x.mean()
            direct = ((x[:-1] - p) * (x[1:] - p)).sum() / ((x - p) ** 2).sum()
            n, h, hh, mm, sw = A.cell_features(A.parse_runs(runs_of(seq)))
            self.assertAlmostEqual(A.lag1_autocorr(n, h, hh, mm, sw), direct, places=12, msg=str(seq))
        self.assertTrue(np.isnan(A.lag1_autocorr(5, 5, 4, 0, 0)))        # constant sequence
        self.assertTrue(np.isnan(A.lag1_autocorr(1, 1, 0, 0, 0)))

    def test_alternating_and_blocked_extremes(self):
        n, h, hh, mm, sw = A.cell_features(A.parse_runs("h1m1h1m1h1m1"))
        self.assertAlmostEqual(A.lag1_autocorr(n, h, hh, mm, sw), -5.0 / 6.0)   # -(N-1)/N for strict alternation
        self.assertAlmostEqual(sw / A.expected_switches(n, h), 5 / 3.0)
        n, h, hh, mm, sw = A.cell_features(A.parse_runs("h3m3"))
        self.assertAlmostEqual(sw / A.expected_switches(n, h), 1 / 3.0)         # 1 switch where 3 are expected


class Auc(unittest.TestCase):
    def test_known(self):
        self.assertEqual(A.auc([3, 4], [1, 2]), 1.0)
        self.assertEqual(A.auc([1, 2], [3, 4]), 0.0)
        self.assertEqual(A.auc([1, 1], [1, 1]), 0.5)
        self.assertAlmostEqual(A.auc([1, 3], [2]), 0.5)
        self.assertTrue(np.isnan(A.auc([], [1])))
        self.assertTrue(np.isnan(A.auc([float("nan")], [1])))


class Hist(unittest.TestCase):
    def test_edges(self):
        h = A.hist_counts(np.array([0.0, 0.049, 0.05, 0.5, 0.95, 0.9999, 1.0]))
        self.assertEqual(h.sum(), 7)
        self.assertEqual(h[0], 2)         # 0.0 and 0.049
        self.assertEqual(h[1], 1)         # 0.05 opens the second bin
        self.assertEqual(h[10], 1)
        self.assertEqual(h[19], 3)        # 0.95, 0.9999, 1.0
        self.assertEqual(len(h), 20)


def write_cells(path, rows):
    """rows: (label, ix, iy, seq) ; windows numbered from 1 so that first=1, last=len(seq)."""
    with open(path, "w") as f:
        f.write("label,ix,iy,iz,N,H,first,last,div,runs\n")
        for lab, ix, iy, seq, div in rows:
            f.write("%d,%d,%d,0,%d,%d,1,%d,%d,%s\n" % (lab, ix, iy, len(seq), sum(seq), len(seq), div, runs_of(seq)))


class Loading(unittest.TestCase):
    def test_read_cells_groups_runs_and_touched(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "c.csv")
            write_cells(p, [(3, 0, 0, [1, 1, 1, 1, 1, 0], 2),       # structure, N=6
                            (3, 1, 0, [1, 1, 0, 0, 1, 1, 1], 1),    # structure, N=7
                            (3, 2, 0, [1, 0], 1),                   # structure, N=2: touched but below n_min_keep
                            (1, 60, 60, [1, 0, 1, 0, 1, 0], 3),     # moving
                            (4, 5, 5, [1, 1, 1, 1, 1, 1], 1)])      # ego: no group
            g, t = A.read_cells(p, 0.2, A.kth_classify, 5)
            self.assertEqual(sorted(g), ["moving", "static_structure"])
            s = g["static_structure"]
            self.assertEqual(list(s.N), [6, 7]); self.assertEqual(list(s.H), [5, 5])
            self.assertEqual(list(t["static_structure"]), [6, 7, 2])        # the short cell is counted as touched
            self.assertEqual(s.interior_hit, {})                            # 'h5m1' and 'h2m2h3': no hit run has runs on both sides
            self.assertEqual(s.interior_miss, {2: 1})                       # the m2 of 'h2m2h3'
            self.assertEqual(s.all_miss, {1: 1, 2: 1})
            self.assertEqual(s.all_hit, {5: 1, 2: 1, 3: 1})
            self.assertEqual(list(s.tile[0]), [0, 0]); self.assertEqual(list(g["moving"].tile[0]), [1, 1])   # 60 * 0.2 / 10 = 1.2

    def test_read_cells_rejects_inconsistent_rows(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "c.csv")
            with open(p, "w") as f:
                f.write("label,ix,iy,iz,N,H,first,last,div,runs\n1,0,0,0,4,2,1,4,1,h3m1\n")     # runs say H=3
            with self.assertRaises(ValueError):
                A.read_cells(p, 0.2, A.kth_classify, 1)
            with open(p, "w") as f:
                f.write("wrong header\n")
            with self.assertRaises(ValueError):
                A.read_cells(p, 0.2, A.kth_classify, 1)
            with open(p, "w") as f:
                f.write("label,ix,iy,iz,N,H,first,last,div,runs\n1,0,0,0,4,2,1,2,1,h2m2\n")     # span shorter than N
            with self.assertRaises(ValueError):
                A.read_cells(p, 0.2, A.kth_classify, 1)

    def test_lil_classify(self):
        self.assertEqual(A.lil_classify(1, 0, 10), "structure")
        self.assertEqual(A.lil_classify(2, 0, 10), None)               # free and never hit: free space
        self.assertEqual(A.lil_classify(2, 1, 10), "hit_in_free")
        self.assertEqual(A.lil_classify(3, 1, 10), "hit_near_wall")
        self.assertEqual(A.lil_classify(3, 0, 10), None)
        self.assertEqual(A.lil_classify(9, 5, 10), None)


class Gap(unittest.TestCase):
    def make(self, rates_n, n_each, tile_of):
        N, H, T = [], [], []
        for k, (r, n) in enumerate(rates_n):
            for i in range(n_each):
                N.append(n); H.append(int(round(r * n))); T.append(tile_of(k, i))
        return np.array(N), np.array(H), np.array(T)

    def test_gap_point_estimate_and_interval_on_separated_groups(self):
        rng = np.random.RandomState(0)
        rs = np.concatenate([rng.uniform(0.7, 1.0, 100), [0.8] * 0])      # structure
        rc = rng.uniform(0.0, 0.3, 100)                                    # clutter
        ts = np.array([(i % 5, 0) for i in range(100)])
        tc = np.array([(i % 5, 1) for i in range(100)])
        gap = float(np.percentile(rs, 10)) - float(np.percentile(rc, 90))
        ci = A.bootstrap_gap(rs, ts, rc, tc, reps=300, seed=7)
        for k in ("iid", "block"):
            self.assertLess(ci[k][0], gap); self.assertGreater(ci[k][1], gap)
            self.assertGreater(ci[k][0], 0.0); self.assertLess(ci[k][1] - ci[k][0], 0.5)
        self.assertEqual(A.bootstrap_gap(rs, ts, rc, tc, reps=300, seed=7), ci)          # same seed, same interval
        self.assertNotEqual(A.bootstrap_gap(rs, ts, rc, tc, reps=300, seed=8), ci)

    def test_constant_groups_give_zero_width_interval(self):
        rs, rc = np.full(20, 0.9), np.full(20, 0.1)
        t = np.array([(i % 4, 0) for i in range(20)])
        for joint in (False, True):
            ci = A.bootstrap_gap(rs, t, rc, t, reps=100, joint=joint)
            self.assertEqual(ci["iid"], (0.8, 0.8)); self.assertEqual(ci["block"], (0.8, 0.8))

    def test_empty_group_gives_nan(self):
        ci = A.bootstrap_gap(np.array([]), np.zeros((0, 2)), np.array([0.1]), np.zeros((1, 2)), reps=10)
        self.assertTrue(np.isnan(ci["iid"][0]))

    def test_block_interval_is_wider_when_cells_share_a_tile_value(self):
        # Rate depends on the tile: the cell-level bootstrap sees 400 independent cells, the tile bootstrap only 4 tiles.
        tiles = np.array([(i // 100, 0) for i in range(400)])
        rs = np.array([[0.9, 0.7, 0.5, 0.95][i // 100] for i in range(400)])
        rc = np.full(400, 0.1)
        ci = A.bootstrap_gap(rs, tiles, rc, tiles, reps=400, seed=3, joint=True)
        self.assertGreater(ci["block"][1] - ci["block"][0], ci["iid"][1] - ci["iid"][0])


class BandsAndRuns(unittest.TestCase):
    def group(self, seqs):
        g = A.Group()
        for k, seq in enumerate(seqs):
            runs = A.parse_runs(runs_of(seq))
            g.add(len(seq), sum(seq), 1, len(seq), (0, 0), runs)
            for i, (s, l) in enumerate(runs):
                (g.all_hit if s else g.all_miss)[l] = (g.all_hit if s else g.all_miss).get(l, 0) + 1
                if 0 < i < len(runs) - 1:
                    (g.interior_hit if s else g.interior_miss)[l] = (g.interior_hit if s else g.interior_miss).get(l, 0) + 1
        return g.freeze()

    def test_run_stats_blocky_vs_alternating(self):
        blocky = self.group([[1, 1, 1, 1, 0, 0, 0, 0]] * 3)
        alt = self.group([[1, 0, 1, 0, 1, 0, 1, 0]] * 3)
        rb, ra = A.run_stats(blocky, 5), A.run_stats(alt, 5)
        self.assertAlmostEqual(rb["switch_ratio"], 1 / (2 * 4 * 4 / 8.0))     # 1 observed vs 4 expected
        self.assertAlmostEqual(ra["switch_ratio"], 7 / 4.0)
        self.assertLess(rb["lag1_mean"], 1.0); self.assertGreater(rb["lag1_mean"], ra["lag1_mean"])
        self.assertEqual(rb["mixed_cells"], 3)
        c = self.group([[1] * 6, [0] * 6])
        rc = A.run_stats(c, 5)
        self.assertEqual(rc["mixed_cells"], 0); self.assertEqual(rc["frac_constant"], 1.0)
        self.assertEqual(A.run_stats(blocky, 9)["cells"], 0)                  # N_min above every N

    def test_interior_run_lengths(self):
        g = self.group([[1, 1, 0, 0, 0, 1, 0, 0, 1, 1, 1]])
        self.assertEqual(g.interior_hit, {1: 1}); self.assertEqual(g.interior_miss, {3: 1, 2: 1})
        self.assertAlmostEqual(A.dict_mean(g.interior_miss), 2.5)
        self.assertAlmostEqual(A.weighted_percentile_from_dict(g.interior_miss, 50), 2.5)
        self.assertTrue(np.isnan(A.dict_mean({})))

    def test_band_stats_selects_by_rate_and_counts_cells(self):
        s = self.group([[1, 1, 1, 0, 0, 0, 0, 1, 1, 0]] * 2 + [[1] * 10])      # r = 0.5 (x2) and 1.0
        c = self.group([[1, 0, 1, 0, 0, 0, 1, 0, 0, 1]] * 3)                   # r = 0.4 (x3)
        rows = A.band_stats(s, c, 5)
        by = {r["band"]: r for r in rows}
        self.assertEqual(by["[0.4,0.6)"]["s_cells"], 2); self.assertEqual(by["[0.4,0.6)"]["c_cells"], 3)
        self.assertEqual(by["[0.2,0.4)"]["c_cells"], 0)                        # 0.4 belongs to the next band (half-open)
        self.assertEqual(by["[0.6,0.8)"]["s_cells"], 0)
        self.assertLess(by["[0.4,0.6)"]["s_switch_ratio"], by["[0.4,0.6)"]["c_switch_ratio"])


class EndToEnd(unittest.TestCase):
    def test_run_all_on_synthetic_kth_tables(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "kth00"))
            rows = []
            for i in range(40):
                rows.append((3, i, 0, [1] * 8 + [0] * (i % 2), 1))             # structure: rate 1 or 8/9
                rows.append((1, i, 400, [1, 0, 0, 0, 0, 0, 1, 0], 1))          # moving: rate 0.25
                rows.append((2, i, 800, [1] * 8, 1))                           # ground
            write_cells(os.path.join(d, "kth00", "cells_r0.2_W10.csv"), rows)
            with open(os.path.join(d, "kth00", "cells_r0.2_W10.meta"), "w") as f:
                f.write("windows_closed=8\nseconds=1\npeak_rss_kb=1\n")
            out = os.path.join(d, "out")
            A.run_all(d, out, figures=False)
            gap = open(os.path.join(out, "p1_gap.csv")).read().splitlines()
            head = gap[0].split(",")
            row = [r.split(",") for r in gap[1:] if r.startswith("kth00,0.2,10,structure_vs_moving,5")]
            self.assertEqual(len(row), 1)
            r = dict(zip(head, row[0]))
            self.assertEqual(r["n_structure"], "40"); self.assertEqual(r["n_clutter"], "40")
            self.assertAlmostEqual(float(r["q_s_p10"]), 8 / 9.0, places=3)
            self.assertAlmostEqual(float(r["q_c_p90"]), 0.25, places=3)
            self.assertAlmostEqual(float(r["gap"]), 8 / 9.0 - 0.25, places=3)
            self.assertEqual(r["gap_ge_0.15"], "yes")
            for f in ("p1_percentiles.csv", "p1_runs.csv", "p1_touched_windows.csv", "p1_hist.csv", "p1_diversity.csv", "p1_matched_band.csv", "p1_inventory.csv"):
                self.assertTrue(os.path.getsize(os.path.join(out, f)) > 0, f)
            inv = open(os.path.join(out, "p1_inventory.csv")).read()
            self.assertIn("kth00,0.1,10,missing", inv)                        # absent conditions are listed, not skipped silently
            touch = [x.split(",") for x in open(os.path.join(out, "p1_touched_windows.csv")).read().splitlines()[1:]]
            st = [x for x in touch if x[3] == "kitti00" and x[4] == "static_structure" and x[1] == "0.2" and x[2] == "10"][0]
            self.assertEqual(st[5], "40"); self.assertEqual(st[10], "9")      # cells, N_max


if __name__ == "__main__":
    unittest.main()
