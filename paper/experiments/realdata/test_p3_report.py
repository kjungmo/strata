#!/usr/bin/env python3
"""Tests of make_report_p3.py on synthetic fixtures (no real data). Run: python3 -I -m unittest discover -s . -p test_p3_report.py (from this directory)."""
import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
import make_report_p3 as mr  # noqa: E402
import p3_eval  # noqa: E402


def dump(rows):
    keys = ("label", "ix", "iy", "iz", "n", "h", "first_static", "first_touch")
    d = {k: np.array([r[i] for r in rows]) for i, k in enumerate(keys)}
    d["verdict"] = np.array([r[8] for r in rows])
    return d


class Thickening(unittest.TestCase):
    def test_counts(self):
        S, U, R = p3_eval.VERDICTS["static"], p3_eval.VERDICTS["undecided"], p3_eval.VERDICTS["rejected"]
        near = p3_eval.REF_FREE_NEAR
        rows = [
            (near, 0, 0, 0, 10, 4, 3, 0, S),      # eligible, hit, Static at the end
            (near, 1, 0, 0, 10, 4, 2, 0, U),      # eligible, hit, ever Static, not Static at the end
            (near, 2, 0, 0, 10, 0, -1, 0, U),     # eligible, no hit, never Static
            (near, 3, 0, 0, 4, 4, 1, 0, S),       # not eligible (n < 5): excluded
            (p3_eval.REF_FREE, 4, 0, 0, 10, 4, 1, 0, S),      # other label: excluded
            (near, 5, 0, 0, 9, 2, -1, 0, R),      # eligible, hit, Rejected
        ]
        t = mr.near_wall_thickening(dump(rows))
        self.assertEqual((t["all_eligible_static_end"]["k"], t["all_eligible_static_end"]["n"]), (1, 4))
        self.assertEqual((t["all_eligible_ever_static"]["k"], t["all_eligible_ever_static"]["n"]), (2, 4))
        self.assertEqual((t["hit_at_least_once_static_end"]["k"], t["hit_at_least_once_static_end"]["n"]), (1, 3))
        self.assertEqual((t["hit_at_least_once_ever_static"]["k"], t["hit_at_least_once_ever_static"]["n"]), (2, 3))

    def test_empty_population_is_nan_not_an_error(self):
        t = mr.near_wall_thickening(dump([(p3_eval.REF_FREE, 0, 0, 0, 10, 4, 1, 0, p3_eval.VERDICTS["static"])]))
        self.assertEqual(t["all_eligible_static_end"]["n"], 0)
        self.assertTrue(t["all_eligible_static_end"]["rate"] != t["all_eligible_static_end"]["rate"])


class Report(unittest.TestCase):
    def test_missing_files_are_listed_not_hidden(self):
        with tempfile.TemporaryDirectory() as rd, tempfile.TemporaryDirectory() as od:
            os.makedirs(os.path.join(rd, "runs"))
            with open(os.path.join(rd, "failed_steps.tsv"), "w") as f:
                f.write("2026-10-19T00:00:00+09:00\tspires_03_sprt_r0.1_W10_p1\trc=1\n")
            with open(os.path.join(rd, "runs", "spires_03_sprt_r0.1_W10_p1.json"), "w") as f:
                json.dump({"returncode": 1}, f)
            out = os.path.join(rd, "r.md")
            self.assertEqual(mr.main(["--results-dir", rd, "--out-dir", od, "--out", out]), 0)
            text = open(out).read()
            self.assertIn("spires_03_sprt_r0.1_W10_p1", text)
            self.assertIn("failed, final", text)
            self.assertIn("missing", text)

    def test_repeated_failure_that_later_passed_is_marked(self):
        with tempfile.TemporaryDirectory() as rd, tempfile.TemporaryDirectory() as od:
            os.makedirs(os.path.join(rd, "runs"))
            with open(os.path.join(rd, "failed_steps.tsv"), "w") as f:
                f.write("t\tstepA\trc=1\n")
            with open(os.path.join(rd, "runs", "stepA.json"), "w") as f:
                json.dump({"returncode": 0}, f)
            out = os.path.join(rd, "r.md")
            mr.main(["--results-dir", rd, "--out-dir", od, "--out", out])
            self.assertIn("ok after repeat", open(out).read())


class Review4(unittest.TestCase):
    def test_da_ceiling_reads_json_with_trailing_text(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "sc.json")
            with open(p, "w") as f:
                f.write(json.dumps({"DA_oracle_ceiling": 96.2009}) + "\nSELFCHECK PASS\n")
            self.assertEqual(mr.da_ceiling(p), "96.20")
            self.assertEqual(mr.da_ceiling(os.path.join(d, "none.json")), "missing")

    def test_boreas_ground_sign_is_session_minus_reference(self):
        with tempfile.TemporaryDirectory() as d:
            rd, od = os.path.join(d, "r"), os.path.join(d, "o")
            os.makedirs(rd)
            os.makedirs(od)
            # stored zoffset JSON = reference minus session: -0.1 median means the session is 0.1 m higher
            with open(os.path.join(rd, "boreas_ground_diagnostic_boreas-2021-04-08-12-44.json"), "w") as f:
                json.dump({"tiles": 5, "median": -0.1, "p10": -0.2, "p90": 0.0}, f)
            out = mr.boreas_section(rd, od)
            self.assertIn("| boreas-2021-04-08-12-44 | 5 | 0.100 | 0.000 | 0.200 |", out)


if __name__ == "__main__":
    unittest.main()
