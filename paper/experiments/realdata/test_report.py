"""Unit tests of the formatting helpers of make_report.py (gate verdicts and table filters). Synthetic input only."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_report as M  # noqa: E402


def convert_meta(**kw):
    m = {"counts_match": "1", "gt_max_gap_s": "0.1", "gt_max_step_m": "0.05", "steps": "1000", "scans_dropped_no_pose": "0",
         "beams_dropped_no_pose": "0", "beams_total": "1000000", "tf_front_spread": "0", "tf_rear_spread": "0",
         "max_front_rear_stamp_skew_s": "0.04"}
    m.update(kw)
    return m


class Verdicts(unittest.TestCase):
    def test_thickness_gate_boundaries(self):
        ok = {"thickness_m_p50": "0.20", "thickness_m_p90": "0.40", "used": "500"}
        self.assertEqual(M.thickness_verdict(ok), "PASS")                                  # all three at the limit pass
        self.assertEqual(M.thickness_verdict(dict(ok, thickness_m_p50="0.2001")), "FAIL")
        self.assertEqual(M.thickness_verdict(dict(ok, thickness_m_p90="0.4001")), "FAIL")
        self.assertEqual(M.thickness_verdict(dict(ok, used="499")), "FAIL")
        self.assertEqual(M.thickness_verdict({}), "n/a")

    def test_pose_verdict_under_both_skew_bounds(self):
        m = convert_meta()                                                                   # skew 0.04 s
        self.assertEqual(M.lil_pose_verdict(m, 0.05), "PASS")
        self.assertTrue(M.lil_pose_verdict(m, 0.03).startswith("FAIL"))
        self.assertIn("pair skew", M.lil_pose_verdict(m, 0.03))
        self.assertEqual(M.lil_pose_verdict(convert_meta(max_front_rear_stamp_skew_s="0.05"), 0.05), "PASS")   # at the bound
        self.assertTrue(M.lil_pose_verdict(convert_meta(max_front_rear_stamp_skew_s="0.0501"), 0.05).startswith("FAIL"))
        self.assertEqual(M.lil_pose_verdict({}, 0.05), "n/a")

    def test_pose_verdict_other_limits(self):
        self.assertEqual(M.lil_pose_verdict(convert_meta(gt_max_gap_s="0.2", gt_max_step_m="0.1"), 0.05), "PASS")
        self.assertIn("GT gap", M.lil_pose_verdict(convert_meta(gt_max_gap_s="0.21"), 0.05))
        self.assertIn("GT step", M.lil_pose_verdict(convert_meta(gt_max_step_m="0.11"), 0.05))
        self.assertIn("counts", M.lil_pose_verdict(convert_meta(counts_match="0"), 0.05))
        self.assertIn("extrinsic", M.lil_pose_verdict(convert_meta(tf_rear_spread="1e-5"), 0.05))
        # 1 % of 2 x 1000 scans = 20 scans; 1 % of 1e6 beams = 10000 beams
        self.assertEqual(M.lil_pose_verdict(convert_meta(scans_dropped_no_pose="20"), 0.05), "PASS")
        self.assertIn("scans dropped", M.lil_pose_verdict(convert_meta(scans_dropped_no_pose="21"), 0.05))
        self.assertEqual(M.lil_pose_verdict(convert_meta(beams_dropped_no_pose="10000"), 0.05), "PASS")
        self.assertIn("beams dropped", M.lil_pose_verdict(convert_meta(beams_dropped_no_pose="10001"), 0.05))


class Tables(unittest.TestCase):
    def test_md_table_and_filters(self):
        t = M.md_table(["a", "b"], [[1, "x"], [2, "y"]])
        self.assertEqual(t.splitlines()[0], "| a | b |")
        self.assertEqual(t.splitlines()[1], "|---|---|")
        self.assertEqual(t.splitlines()[3], "| 2 | y |")
        self.assertEqual(M.md_table(["a"], []), "_no rows_\n")
        d = [{"a": "1", "b": "x"}, {"a": "2", "b": "y"}]
        self.assertEqual(M.cols_of(d, ["b"], lambda r: r["a"] == "2"), [["y"]])
        self.assertEqual(M.cols_of(d, ["zzz"]), [["n/a"], ["n/a"]])

    def test_curve_and_score_lookup_from_a_search_file(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "o.csv")
            with open(p, "w") as f:
                f.write("anchor,dt_s,c_front,c_rear,score,dropped_share,hits\n")
                for a in ("start", "end", "single"):
                    for k in range(-30, 31):
                        f.write("%s,%s,1,1,%d,0,5\n" % (a, repr(k * 0.01), 100 + abs(k) + (5 if a == "single" else 0)))
            self.assertEqual(M.score_at(p, "start", -0.13 + 0.13), "100")
            self.assertEqual(M.score_at(p, "start", -0.1), "110")
            self.assertEqual(M.score_at(p, "end", 0.3), "130")
            self.assertEqual(M.score_at(p, "start", 0.55), "n/a")
            self.assertEqual(M.best_single(p), "105 at dt 0.0")
            rows = M.curve_rows(p)
            self.assertEqual(rows[0], ["-0.30", "130", "130", "135"])
            self.assertEqual(rows[15], ["+0.00", "100", "100", "105"])
            self.assertEqual(len(rows), 31)

    def test_gap_pick(self):
        rows = [{"dataset": "kth00", "pair": "p", "N_min": "5"}, {"dataset": "kth00", "pair": "p", "N_min": "3"}, {"dataset": "lil", "pair": "p", "N_min": "5"}]
        self.assertEqual(len(M.gap_pick(rows, "kth00", "p")), 2)
        self.assertEqual(len(M.gap_pick(rows, "kth00", "p", 5)), 1)


if __name__ == "__main__":
    unittest.main()
