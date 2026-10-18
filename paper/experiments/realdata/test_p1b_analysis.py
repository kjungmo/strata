"""Tests of the phase 1b Python code: rounding, derived defaults, the exact undecided probability, the bootstrap of
q_s and q_c, and the mechanical freeze rule. Synthetic inputs only."""
import math
import os
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze_p1 as a1  # noqa: E402
import analyze_p1b as ab  # noqa: E402
import derive_defaults as dd  # noqa: E402
import freeze_prereg as fp  # noqa: E402


class Rounding(unittest.TestCase):
    def test_toward_each_other_with_boundaries(self):
        self.assertEqual(ab.round_pair(0.6957, 0.1417), (0.15, 0.65))
        self.assertEqual(ab.round_pair(0.65, 0.15), (0.15, 0.65))           # exactly on the grid: unchanged
        self.assertEqual(ab.round_pair(0.65, 0.1500001), (0.20, 0.65))       # just above: next multiple up
        self.assertEqual(ab.round_pair(0.6499, 0.15), (0.15, 0.60))          # just below: next multiple down
        self.assertEqual(ab.round_pair(0.5770, 0.0697), (0.10, 0.55))
        self.assertEqual(ab.round_pair(0.5, 0.0), (0.05, 0.50))              # lower bound of m0
        self.assertEqual(ab.round_pair(0.99, 0.2), (0.20, 0.95))
        self.assertEqual(ab.round_pair(1.0, 0.2), (0.20, 0.95))              # upper bound of m1: floor(1.0) would be 1.0

    def test_float_noise_does_not_move_a_grid_value(self):
        for k in range(1, 19):
            x = k * 0.05
            self.assertEqual(ab.ceil_005(x + 1e-12), round(x, 2))
            self.assertEqual(ab.floor_005(x - 1e-12), round(x, 2))


class Derived(unittest.TestCase):
    def test_numbers_of_the_advice_table(self):
        d = dd.table(0.15, 0.65)
        self.assertAlmostEqual(d["l_hit"], 1.466, places=3)
        self.assertAlmostEqual(d["l_miss"], -0.887, places=3)
        self.assertAlmostEqual(d["A"], 4.595, places=3)
        self.assertAlmostEqual(d["mstar"], 0.377, places=3)
        self.assertAlmostEqual(d["EN"]["mstar"], 16.2, places=1)
        self.assertEqual(d["latency_all_hit"], 4)
        self.assertAlmostEqual(d["oc"]["0.30"], 0.209, places=3)
        self.assertAlmostEqual(d["oc"]["0.50"], 0.881, places=3)
        self.assertAlmostEqual(d["oc"]["0.15"], 0.01, places=9)
        self.assertAlmostEqual(d["oc"]["0.65"], 0.99, places=9)
        self.assertAlmostEqual(d["ceiling"], d["A"] + 2 * d["l_hit"], places=12)

    def test_second_pair_and_invalid_pairs(self):
        d = dd.derive(0.20, 0.65)
        self.assertAlmostEqual(d["l_hit"], 1.179, places=3)
        self.assertEqual(d["latency_all_hit"], 4)
        for bad in ((0.65, 0.15), (0.0, 0.5), (0.3, 1.0), (0.3, 0.3)):
            with self.assertRaises(ValueError):
                dd.derive(*bad)

    def test_dp_equals_enumeration(self):
        m0, m1, m, T, n = 0.15, 0.65, 0.4, 5, 11
        d = dd.derive(m0, m1)
        g = r = u = 0.0
        def walk(i, l, p):
            nonlocal g, r, u
            if i == n:
                u += p
                return
            for x, px in ((1, m), (0, 1 - m)):
                l2 = l + (d["l_hit"] if x else d["l_miss"])
                j = i + 1
                if l2 >= d["A"] - 1e-9:
                    g += p * px
                elif j >= T and l2 <= d["B"] + 1e-9:
                    r += p * px
                else:
                    walk(j, l2, p * px)
        walk(0, 0.0, 1.0)
        G, R, U = dd.outcome_probabilities(m0, m1, m, n, T=T)
        self.assertAlmostEqual(G[n], g, places=12)
        self.assertAlmostEqual(R[n], r, places=12)
        self.assertAlmostEqual(U[n], u, places=12)
        self.assertAlmostEqual(G[n] + R[n] + U[n], 1.0, places=12)

    def test_rejection_happens_exactly_at_window_t(self):
        # (0.5, 0.9): three misses give l = 3 * ln(0.2) = -4.83 <= B, so at T = 3 the all-miss history is rejected at window 3
        m = 0.2
        G, R, U = dd.outcome_probabilities(0.5, 0.9, m, 3, T=3)
        self.assertAlmostEqual(R[3], (1 - m) ** 3, places=12)
        G, R, U = dd.outcome_probabilities(0.5, 0.9, m, 3, T=4)
        self.assertEqual(R[3], 0.0)

    def test_reject_only_after_t_and_n_max_rule(self):
        # with T large no rejection can happen in the first T windows
        G, R, U = dd.outcome_probabilities(0.15, 0.65, 0.05, 20, T=24)
        self.assertEqual(R[20], 0.0)
        G0, R0, U0 = dd.outcome_probabilities(0.15, 0.65, 0.05, 20, T=0)
        self.assertGreater(R0[20], 0.5)
        n_max, u = dd.n_max_undecided(0.15, 0.65)
        self.assertEqual(n_max % 16, 0)
        self.assertLessEqual(u, 0.03)
        self.assertGreater(dd.undecided_probability(0.15, 0.65, dd.derive(0.15, 0.65)["mstar"], n_max - 16), 0.03)


class Bootstrap(unittest.TestCase):
    def test_gap_interval_equals_the_phase1_block_interval(self):
        rng = np.random.RandomState(3)
        n1, n2 = 3000, 2000
        rs = np.round(rng.beta(6, 2, n1), 2)
        rc = np.round(rng.beta(1, 8, n2), 2)
        ts = rng.randint(0, 30, size=(n1, 2))
        tc = rng.randint(0, 30, size=(n2, 2))
        for joint in (True, False):
            old = a1.bootstrap_gap(rs, ts, rc, tc, reps=300, joint=joint)["block"]
            new = ab.bootstrap_quantiles(rs, ts, rc, tc, joint, reps=300)
            self.assertAlmostEqual(old[0], new["gap"][0], places=12)
            self.assertAlmostEqual(old[1], new["gap"][1], places=12)
            self.assertLess(new["qs"][0], new["qs"][1] + 1e-12)
            self.assertLessEqual(new["qc"][0], new["qc"][1])
            qs, qc = float(np.percentile(rs, 10)), float(np.percentile(rc, 90))
            self.assertTrue(new["qs"][0] - 0.05 <= qs <= new["qs"][1] + 0.05)
            self.assertTrue(new["qc"][0] - 0.05 <= qc <= new["qc"][1] + 0.05)

    def test_empty_groups(self):
        out = ab.bootstrap_quantiles(np.array([]), np.zeros((0, 2), dtype=int), np.array([0.1]), np.zeros((1, 2), dtype=int), True)
        self.assertTrue(math.isnan(out["gap"][0]))


def gap_row(dataset, res, W, miss, pair, gap, qs, qc, n_min=5):
    return {"dataset": dataset, "res_m": res, "W": str(W), "miss": miss, "pair": pair, "N_min": str(n_min), "n_structure": "1000", "n_clutter": "800",
            "q_s_p10": "%.4f" % qs, "q_c_p90": "%.4f" % qc, "gap": "%.4f" % gap,
            "gap_ci95_block_lo": "%.4f" % (gap - 0.05), "gap_ci95_block_hi": "%.4f" % (gap + 0.05),
            "q_s_ci95_block_lo": "%.4f" % (qs - 0.03), "q_s_ci95_block_hi": "%.4f" % (qs + 0.03),
            "q_c_ci95_block_lo": "%.4f" % (qc - 0.03), "q_c_ci95_block_hi": "%.4f" % (qc + 0.03)}


def gate(name, ok1=True, ok2=True):
    return {"sequence": name, "class": "2D", "g1_accepted": "900", "g1_p50_m": "0.08", "g1_p90_m": "0.15", "g1_limit_p50_m": "0.1", "g1_limit_p90_m": "0.2",
            "g1_pass": "PASS" if ok1 else "FAIL", "g2_queries": "10", "g2_matched_share": "0.9", "g2_p50_m": "0.01", "g2_p90_m": "0.04",
            "g2_limit_p50_m": "0.05", "g2_limit_p90_m": "0.1", "g2_pass": "PASS" if ok2 else "FAIL", "gates_pass": "PASS" if (ok1 and ok2) else "FAIL"}


GOOD_OFFSET = {"valid": "1", "alignment_ok": "1", "alignment_share": "0.8", "selected_dt_s": "-0.05", "reason": "ok"}
EQUIV_OK = "\n".join("r=%s W=%d IDENTICAL sha256_lean=aa sha256_engine=aa" % (r, w) for r in ("0.2", "0.1") for w in (10, 2))


class Freeze(unittest.TestCase):
    def rows2d(self, gap=0.55, qs=0.70, qc=0.15):
        return [gap_row("lil", "0.05", 15, "-", "structure@static_0_vs_clutter@dynamics_0", gap, qs, qc)]

    def gates_ok(self):
        return [gate("lil_mapping"), gate("lil_static_0"), gate("lil_dynamics_0"), gate("keble02")]

    def test_2d_frozen_by_rounding_and_derivation(self):
        d = fp.decide_2d(self.rows2d(0.5583, 0.6957, 0.1417), self.gates_ok())
        self.assertEqual(d["status"], "FROZEN")
        self.assertEqual((d["m0"], d["m1"]), (0.15, 0.65))
        self.assertAlmostEqual(d["l_hit"], math.log(0.65 / 0.15), places=12)
        self.assertEqual(d["n_max_undecided"] % 16, 0)

    def test_2d_not_frozen_when_a_gate_fails_or_the_gap_is_small(self):
        g = self.gates_ok()
        g[1] = gate("lil_static_0", ok1=False)
        d = fp.decide_2d(self.rows2d(), g)
        self.assertEqual(d["status"], "NOT FROZEN")
        self.assertTrue(any("lil_static_0 fails" in r and "G1" in r for r in d["reasons"]))
        self.assertIn("conditional", d)
        self.assertNotIn("m0", d)                                 # a failed gate freezes nothing
        g_map = self.gates_ok()
        g_map[0] = gate("lil_mapping", ok2=False)
        self.assertEqual(fp.decide_2d(self.rows2d(), g_map)["status"], "NOT FROZEN")          # the reference sequence must pass too
        self.assertEqual(fp.decide_2d(self.rows2d(0.15, 0.4, 0.25), self.gates_ok())["status"], "FROZEN")   # gap exactly 0.15 passes
        d2 = fp.decide_2d(self.rows2d(0.10, 0.4, 0.3), self.gates_ok())
        self.assertNotIn("conditional", d2)                       # a gap below 0.15 gives no conditional values either
        self.assertEqual(d2["status"], "NOT FROZEN")
        self.assertTrue(any("gap" in r for r in d2["reasons"]))
        d3 = fp.decide_2d([], self.gates_ok())
        self.assertEqual(d3["status"], "NOT FROZEN")
        d4 = fp.decide_2d(self.rows2d(0.5, 0.7, 0.2), [gate("lil_mapping")])
        self.assertEqual(d4["status"], "NOT FROZEN")

    def rows3d(self, gap_t, gap_r):
        pair = "structure_vs_clutter"
        return [gap_row("keble02", "0.2", 10, "today", pair, gap_t, 0.30 + gap_t / 2, 0.30 - gap_t / 2),
                gap_row("keble02", "0.2", 10, "refined", pair, gap_r, 0.40 + gap_r / 2, 0.40 - gap_r / 2)]

    def test_3d_branches(self):
        two = fp.decide_2d(self.rows2d(0.5583, 0.6957, 0.1417), self.gates_ok())
        t = fp.decide_3d(self.rows3d(0.20, 0.30), self.gates_ok(), GOOD_OFFSET, {}, EQUIV_OK, two)
        self.assertEqual((t["branch"], t["status"]), ("T", "FROZEN"))
        self.assertIn("today", t["miss_definition"])
        r = fp.decide_3d(self.rows3d(0.10, 0.30), self.gates_ok(), GOOD_OFFSET, {}, EQUIV_OK, two)
        self.assertEqual(r["branch"], "R")
        self.assertIn("refined", r["miss_definition"])
        f = fp.decide_3d(self.rows3d(0.10, 0.12), self.gates_ok(), GOOD_OFFSET, {}, EQUIV_OK, two)
        self.assertEqual(f["branch"], "F")
        self.assertEqual((f["m0"], f["m1"]), (two["m0"], two["m1"]))
        self.assertIn("not calibrated for 3D", f["flag"])
        rb = fp.decide_3d(self.rows3d(0.10, 0.15), self.gates_ok(), GOOD_OFFSET, {}, EQUIV_OK, two)
        self.assertEqual(rb["branch"], "R")                                       # refined gap exactly 0.15 passes
        # exactly at the threshold counts as passing (>= 0.15)
        e = fp.decide_3d(self.rows3d(0.15, 0.0), self.gates_ok(), GOOD_OFFSET, {}, EQUIV_OK, two)
        self.assertEqual(e["branch"], "T")
        just_below = fp.decide_3d(self.rows3d(0.1499, 0.1499), self.gates_ok(), GOOD_OFFSET, {}, EQUIV_OK, two)
        self.assertEqual(just_below["branch"], "F")

    def test_3d_fallback_when_a_check_fails_even_with_a_large_gap(self):
        two = fp.decide_2d(self.rows2d(0.5583, 0.6957, 0.1417), self.gates_ok())
        bad_gates = self.gates_ok()[:3] + [gate("keble02", ok2=False)]
        for gates, off, eq in ((bad_gates, GOOD_OFFSET, EQUIV_OK), (self.gates_ok(), dict(GOOD_OFFSET, valid="0"), EQUIV_OK),
                               (self.gates_ok(), dict(GOOD_OFFSET, alignment_ok="0"), EQUIV_OK),
                               (self.gates_ok(), GOOD_OFFSET, EQUIV_OK.replace("IDENTICAL", "DIFFERENT", 1)), (self.gates_ok(), GOOD_OFFSET, "")):
            f = fp.decide_3d(self.rows3d(0.4, 0.5), gates, off, {}, eq, two)
            self.assertEqual(f["branch"], "F")
            self.assertTrue(f["reasons"])
        f = fp.decide_3d(self.rows3d(0.4, 0.5), bad_gates, GOOD_OFFSET, {}, EQUIV_OK, two)
        self.assertEqual(f["conditional_branch"], "T")                 # the gap alone would have given branch T
        self.assertIn("conditional", f)
        f2 = fp.decide_3d(self.rows3d(0.1, 0.5), bad_gates, GOOD_OFFSET, {}, EQUIV_OK, two)
        self.assertEqual(f2["conditional_branch"], "R")
        f3 = fp.decide_3d(self.rows3d(0.1, 0.1), bad_gates, GOOD_OFFSET, {}, EQUIV_OK, two)
        self.assertNotIn("conditional", f3)
        not_frozen_2d = fp.decide_2d([], self.gates_ok())
        g = fp.decide_3d(self.rows3d(0.1, 0.1), self.gates_ok(), GOOD_OFFSET, {}, EQUIV_OK, not_frozen_2d)
        self.assertEqual(g["status"], "NOT FROZEN")

    def test_render_of_a_failed_gate_prints_conditional_values_as_not_a_freeze(self):
        gates = [gate("lil_mapping", ok1=False), gate("lil_static_0"), gate("lil_dynamics_0"), gate("keble02", ok1=False)]
        two = fp.decide_2d(self.rows2d(0.5583, 0.6957, 0.1417), gates)
        three = fp.decide_3d(self.rows3d(0.2, 0.3), gates, GOOD_OFFSET, {}, EQUIV_OK, two)
        text = fp.render(two, three, "abc", {})
        self.assertIn("NOT FROZEN", text)
        self.assertIn("NOT A FREEZE", text)
        self.assertNotIn("| m0 | 0.15 |", text)               # the quantity table of a freeze is absent

    def test_render_and_main(self):
        with tempfile.TemporaryDirectory() as d:
            rd = os.path.join(d, "res")
            os.makedirs(os.path.join(rd, "meta"))
            ab_rows = self.rows2d(0.5583, 0.6957, 0.1417) + self.rows3d(0.2, 0.3)
            with open(os.path.join(rd, "p1b_gap.csv"), "w") as f:
                keys = list(ab_rows[0].keys())
                f.write(",".join(keys) + "\n")
                for r in ab_rows:
                    f.write(",".join(r.get(k, "") for k in keys) + "\n")
            gates = self.gates_ok()
            with open(os.path.join(rd, "p1b_gates.csv"), "w") as f:
                keys = list(gates[0].keys())
                f.write(",".join(keys) + "\n")
                for g in gates:
                    f.write(",".join(g[k] for k in keys) + "\n")
            with open(os.path.join(rd, "meta", "spires_offset.meta"), "w") as f:
                f.write("".join("%s=%s\n" % kv for kv in GOOD_OFFSET.items()))
            with open(os.path.join(rd, "equivalence_kth.txt"), "w") as f:
                f.write("# x\n" + EQUIV_OK + "\n")
            self.assertEqual(fp.main(["--results-dir", rd, "--out-dir", d, "--commit", "abc"]), 0)
            with open(os.path.join(d, "PREREG.md")) as fh:
                text = fh.read()
            self.assertIn("**FROZEN**", text)
            self.assertIn("| m0 | 0.15 |", text)
            self.assertIn("| m1 | 0.65 |", text)
            self.assertIn("branch T", text)
            self.assertTrue(os.path.exists(os.path.join(d, "PREREG.json")))


class Report(unittest.TestCase):
    def test_report_smoke(self):
        import make_report_p1b as mr
        with tempfile.TemporaryDirectory() as d:
            rd = os.path.join(d, "res")
            os.makedirs(os.path.join(rd, "meta"))
            os.makedirs(os.path.join(rd, "logs"))
            rows = [gap_row("lil", "0.05", 15, "-", "structure@static_0_vs_clutter@dynamics_0", 0.5, 0.7, 0.2)]
            for r in rows:
                r.update({"m0_rounded": "0.20", "m1_rounded": "0.70", "gap_ge_0.15": "yes"})
            with open(os.path.join(rd, "p1b_gap.csv"), "w") as f:
                keys = list(rows[0].keys())
                f.write(",".join(keys) + "\n")
                for r in rows:
                    f.write(",".join(r[k] for k in keys) + "\n")
            with open(os.path.join(rd, "logs", "x.log"), "w") as f:
                f.write("STEP x\nSTART 2026-10-18T07:30:00+09:00 host=h load=1 cores=2\n")
            out = os.path.join(d, "R.md")
            here = os.path.dirname(os.path.abspath(__file__))
            self.assertEqual(mr.main(["--results-dir", rd, "--prereg-dir", here, "--out", out]), 0)
            with open(out) as f:
                text = f.read()
            # the chronology lists real commits (finding 1: the root was one level too deep and the list was empty)
            chron = text.split("## 1. Chronology")[1].split("Step logs")[0]
            self.assertRegex(chron, r"- [0-9a-f]{7,} 20\d\d-")
            self.assertIn("2db4f2c", chron)
            self.assertIn("no test-split file was modified", text)
            self.assertIn("## 13. Open items", text)
            self.assertIn("## 2. Gates G1 and G2", text)
            self.assertIn("0.20", text)
            self.assertIn("The first measurement step (`x`) started 2026-10-18T07:30:00+09:00", text)


class ReportGit(unittest.TestCase):
    def test_git_fails_loudly_outside_a_repository(self):
        import make_report_p1b as mr
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(RuntimeError):
                mr.git(["log", "-1"], d)
            with self.assertRaises(RuntimeError):
                mr.repo_root(d)

    def test_repo_root_is_the_top_level_not_paper(self):
        import make_report_p1b as mr
        here = os.path.dirname(os.path.abspath(__file__))
        root = mr.repo_root(here)
        self.assertTrue(os.path.isdir(os.path.join(root, "paper", "experiments", "realdata")))

    def test_laser_optima(self):
        import make_report_p1b as mr
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "o.csv")
            with open(p, "w") as f:
                f.write("anchor,dt_s,c_front,c_rear,score\nstart,-0.13,5,9,14\nstart,-0.06,8,7,15\nstart,0.0,9,8,17\n")
            o = mr.laser_optima(p)
            self.assertEqual(o["c_front"], ("-0.13", "5"))
            self.assertEqual(o["c_rear"], ("-0.06", "7"))
            self.assertEqual(o["score"], ("-0.13", "14"))

    def test_n_max_numbers_quoted_in_the_report(self):
        import derive_defaults as dd
        for m0, m1, u64, u80 in ((0.15, 0.65, 0.0314, 0.0113), (0.20, 0.70, 0.0388, 0.0150)):
            t = dd.table(m0, m1)
            self.assertEqual(t["n_max_undecided"], 80)
            self.assertAlmostEqual(dd.undecided_probability(m0, m1, t["mstar"], 64), u64, places=4)
            self.assertAlmostEqual(dd.undecided_probability(m0, m1, t["mstar"], 80), u80, places=4)


class GapRows(unittest.TestCase):
    def test_gap_rows_have_all_columns_and_the_rounded_pair(self):
        rng = np.random.RandomState(1)

        def grp(rates, n=10):
            g = a1.Group()
            for r in rates:
                h = int(round(r * n))
                g.N.append(n); g.H.append(h); g.div.append(1); g.span.append(n)
                g.tile.append((int(rng.randint(0, 10)), int(rng.randint(0, 10))))
                g.hh.append(max(h - 1, 0)); g.mm.append(max(n - h - 1, 0)); g.sw.append(1)
            return g.freeze()

        gs = grp(np.clip(rng.normal(0.8, 0.1, 500), 0.3, 1.0))
        gc = grp(np.clip(rng.normal(0.1, 0.05, 500), 0.0, 0.5))
        tables = {"gap": [], "band": []}
        ab.gap_rows({"dataset": "x", "res": "0.2", "W": 10}, "today", "p", gs, gc, True, tables)
        self.assertEqual(len(tables["gap"]), len(a1.N_MIN_SENSITIVITY))
        self.assertTrue(all(len(r) == len(ab.HEAD_GAP_B) for r in tables["gap"]))
        prim = [r for r in tables["gap"] if r[5] == 5][0]
        qs, qc = float(prim[8]), float(prim[9])
        self.assertEqual((float(prim[-2]), float(prim[-1])), ab.round_pair(qs, qc))
        # below the rule the rounded pair is n/a
        tables_small = {"gap": [], "band": []}
        ab.gap_rows({"dataset": "x", "res": "0.2", "W": 10}, "today", "p", gc, gs, True, tables_small)
        small = [r for r in tables_small["gap"] if r[5] == 5][0]
        self.assertEqual((small[-2], small[-1]), ("n/a", "n/a"))
        self.assertEqual(prim[19], "yes")
        # a gap of exactly 0.15 (0.25 - 0.10) counts as reaching the rule
        gs2, gc2 = grp([0.25] * 40, n=8), grp([0.10] * 40, n=10)
        tables3 = {"gap": [], "band": []}
        ab.gap_rows({"dataset": "x", "res": "0.2", "W": 10}, "today", "p", gs2, gc2, True, tables3)
        prim3 = [r for r in tables3["gap"] if r[5] == 5][0]
        self.assertEqual(float(prim3[10]), 0.15)
        self.assertEqual(prim3[19], "yes")
        # an empty group gives n/a rows of the right width
        tables2 = {"gap": [], "band": []}
        ab.gap_rows({"dataset": "x", "res": "0.2", "W": 10}, "today", "p", None, gc, True, tables2)
        self.assertTrue(all(len(r) == len(ab.HEAD_GAP_B) for r in tables2["gap"]))


if __name__ == "__main__":
    unittest.main()
