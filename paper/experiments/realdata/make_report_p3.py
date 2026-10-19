#!/usr/bin/env python3
"""Formats the phase 3 result tables into P3_REPORT.md (PREREG_P3.md, Amendment 15).

Every statistic is read from the JSON files written by p3_eval.py, from the meta files of the C++ probe, and from the run records of
reproduce_p3.sh. The only numbers computed here are (a) shares of counts that are already in a meta file and (b) the near-wall
thickening row of LILocBench (counts from the arm dump, with a Wilson interval from p3_eval.wilson; Amendment 15). Missing files are
printed as "missing" and listed; a failed or not-run step is never hidden.

usage: python3 -I make_report_p3.py --results-dir results_p3 --out-dir /mnt/d/wsl/strata/p3 --out results_p3/P3_REPORT.md
"""
import argparse
import csv
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
import p3_eval  # noqa: E402

ARMS = ["shipped_v0_1", "symmetric", "octomap", "elite_like", "sprt", "sprt_low_end", "sprt_high_end"]
ARM_LABEL = {
    "shipped_v0_1": "shipped v0.1",
    "symmetric": "symmetric",
    "octomap": "OctoMap parameters on window evidence",
    "elite_like": "ELite factors on window evidence",
    "sprt": "SPRT (0.20, 0.70)",
    "sprt_low_end": "SPRT low end (0.20, 0.65)",
    "sprt_high_end": "SPRT high end (0.30, 0.75)",
}
SPRT_ARMS = ("sprt", "sprt_low_end", "sprt_high_end")
ALPHA = 0.01


def jload(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def meta_load(path):
    d = {}
    try:
        with open(path) as f:
            for line in f:
                if "=" in line:
                    k, v = line.rstrip("\n").split("=", 1)
                    d[k] = v
    except OSError:
        pass
    return d


def tsv_rows(path):
    try:
        with open(path) as f:
            return [ln.rstrip("\n").split("\t") for ln in f if ln.strip()]
    except OSError:
        return []


def pc(x, nd=2):
    return "n/a" if x is None or x != x else ("%.*f%%" % (nd, 100.0 * x))


def pc_ci(blk, key="wilson95", nd=2):
    if not blk or blk.get("rate") is None or blk["rate"] != blk["rate"]:
        return "n/a"
    lo, hi = blk[key]
    return "%s [%s, %s]" % (pc(blk["rate"], nd), pc(lo, nd), pc(hi, nd))


def num(x, nd=1):
    return "n/a" if x is None or x != x else ("%.*f" % (nd, x))


def md(head, rows):
    if not rows:
        return "_no rows_\n"
    out = ["| " + " | ".join(head) + " |", "|" + "|".join("---" for _ in head) + "|"]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out) + "\n"


def near_wall_thickening(d, nmin=5, label_near=p3_eval.REF_FREE_NEAR):
    """Near-wall thickening of one LILocBench dump (Amendment 15): eligible (n >= nmin) cells that were free in the mapping session
    and lie within 0.10 m of an occupied cell (label 3). Returns Static at the end, ever Static, and the same on the subset hit at
    least once. Counts only; the Wilson interval is p3_eval.wilson."""
    n, h, lab, ver = d["n"], d["h"], d["label"], d["verdict"]
    near = (lab == label_near) & (n >= nmin)
    out = {}
    for name, m in (("all_eligible", near), ("hit_at_least_once", near & (h >= 1))):
        den = int(m.sum())
        for what, flag in (("static_end", ver == p3_eval.VERDICTS["static"]), ("ever_static", d["first_static"] >= 0)):
            k = int((m & flag).sum())
            lo, hi = p3_eval.wilson(k, den)
            out["%s_%s" % (name, what)] = {"k": k, "n": den, "rate": (k / den if den else float("nan")), "wilson95": [lo, hi]}
    return out


# ---------------------------------------------------------------------------------------------- run inventory ------

def inventory(rd):
    runs = {}
    for p in glob.glob(os.path.join(rd, "runs", "*.json")):
        name = os.path.basename(p)[:-5]
        if ".attempt-" in name:
            continue
        j = jload(p) or {}
        runs[name] = j.get("returncode")
    attempts = sorted(os.path.basename(p)[:-5] for p in glob.glob(os.path.join(rd, "runs", "*.attempt-*.json")))
    failed = tsv_rows(os.path.join(rd, "failed_steps.tsv"))
    not_run = tsv_rows(os.path.join(rd, "not_run_steps.tsv"))
    skipped = tsv_rows(os.path.join(rd, "skipped_repeat.tsv"))
    return runs, attempts, failed, not_run, skipped


def step_status(name, runs, failed_names):
    if name in runs:
        return "ok" if runs[name] == 0 else "FAILED rc=%s" % runs[name]
    if name in failed_names:
        return "FAILED"
    return "missing"


# ------------------------------------------------------------------------------------------------ LILocBench ------

def lil_section(rd, od, per, seqs=("lt_changes_dynamics_0", "lt_changes_0")):
    out = []
    for seq in seqs:
        rows, rows2, rows3, rows4, rows5 = [], [], [], [], []
        for arm in ARMS:
            j = jload(os.path.join(rd, "lil_%s_%s_p%d.json" % (seq, arm, per)))
            if j is None:
                rows.append([ARM_LABEL[arm], "missing"] + [""] * 5)
                continue
            nominal = "%.2f%% per history, independent windows" % (100 * ALPHA) if arm in SPRT_ARMS else "none stated"
            rows.append([ARM_LABEL[arm], nominal, pc_ci(j["ever_static_on_clutter"]), pc_ci(j["ever_static_on_clutter"], "tile_bootstrap95"),
                         pc_ci(j["ever_static_on_clutter_at_or_below_m0"]), pc_ci(j["false_static_on_clutter"]), "%d / %d" % (j["ever_static_on_clutter"]["k"], j["ever_static_on_clutter"]["n"])])
            q = j["qualitative"]
            rows2.append([ARM_LABEL[arm], pc_ci(j["clutter_undecided"]), pc_ci(j["clutter_rejected"]), pc_ci(j["false_reject_on_wall_runs"]),
                          pc_ci(j["wall_runs_not_static"]), "%d / %d" % (j["wall_runs_static"]["k"], j["wall_runs_static"]["n"]),
                          "%s / %s / %s" % (num(j["wall_run_windows_to_first_static"]["p50"]), num(j["wall_run_windows_to_first_static"]["p90"]),
                                            j["wall_run_windows_to_first_static"]["n"])])
            rows3.append([ARM_LABEL[arm], "%d / %d" % (q["ref_free_cells_static"], q["ref_free_cells_eligible"]),
                          "%d / %d" % (q["ref_occupied_cells_not_static"], q["ref_occupied_cells_eligible"]),
                          "%d / %d" % (q["ref_occupied_cells_rejected"], q["ref_occupied_cells_eligible"])])
            bands = " ; ".join("%s: %d/%d" % ("[%.2f,%.2f)" % tuple(b["band"]), b["k"], b["n"]) for b in j["false_static_by_hit_rate_band"])
            rows4.append([ARM_LABEL[arm], bands])
            dump = os.path.join(od, "lil", "dump_%s_%s_p%d.csv" % (seq, arm, per))
            try:
                t = near_wall_thickening(p3_eval.read_dump(dump))
                rows5.append([ARM_LABEL[arm], pc_ci(t["all_eligible_static_end"]), pc_ci(t["all_eligible_ever_static"]),
                              pc_ci(t["hit_at_least_once_static_end"]), pc_ci(t["hit_at_least_once_ever_static"]), str(t["all_eligible_static_end"]["n"])])
            except (OSError, ValueError) as e:
                rows5.append([ARM_LABEL[arm], "dump missing: %s" % type(e).__name__, "", "", "", ""])
        tag = "Periodic veto ON (primary)" if per == 1 else "Periodic veto OFF (secondary)"
        out.append("#### %s, %s\n" % (seq, tag))
        out.append("False-Static on clutter (reference-free cells hit at least once, N >= 5 windows; 0.05 m, W = 15). Primary quantity: ever Static (Amendment 5). "
                   "Intervals: Wilson 95 % (cells as independent) and the 95 % percentile interval of a block bootstrap over 10 m tiles (read the block interval).\n")
        out.append(md(["arm", "nominal bound", "ever Static, Wilson", "ever Static, tile bootstrap", "ever Static, H/N <= 0.20", "Static at the end", "k / n (ever Static)"], rows))
        out.append("Undecided and Rejected share of clutter, false-reject and windows to decision on wall runs (cell = windows from first touch to first Static window; p50 / p90 / n):\n")
        out.append(md(["arm", "clutter Undecided", "clutter Rejected", "wall runs Rejected", "wall runs not Static", "wall runs Static (k / n)", "windows to decision p50 / p90 / n"], rows2))
        out.append("Counts by hit-rate band of the clutter cell (Static at the end, k / n):\n")
        out.append(md(["arm", "bands"], rows4))
        out.append("Near-wall thickening: cells that were free in the mapping session and lie within 0.10 m of an occupied cell (label 3, N >= 5), Static share (Amendment 15, report-only):\n")
        out.append(md(["arm", "Static at the end", "ever Static", "Static at the end, hit >= 1", "ever Static, hit >= 1", "n eligible"], rows5))
        if seq == "lt_changes_0":
            out.append("Counts for the control (no people; rearranged furniture; no per-cell truth). Reference-free cells that end Static, reference-occupied cells not Static, and Rejected:\n")
            out.append(md(["arm", "ref-free Static / eligible", "ref-occupied not Static / eligible", "ref-occupied Rejected / eligible"], rows3))
        else:
            out.append("Confounder: moved furniture legitimately puts structure into reference-free cells; the false-Static rate is an upper bound for people alone (section 6.3 of the pre-registration). "
                       "Control counts of `lt_changes_0` follow below.\n")
    return "\n".join(out)


def lil_reference_rows():
    return md(["reference row (calibration sequences, P2 replay, shipped pair)", "ever Static"],
              [["at H/N <= 0.20 (n = 22,273)", "1.51 % (veto on) / 1.64 % (veto off)"], ["above 0.20 (n = 1,823)", "30.5 % (veto on) / 33.8 % (veto off)"], ["all clutter (n = 24,096)", "3.71 % (veto on) / 4.07 % (veto off)"],
               ["first-history unit at or below m0 (veto off)", "0.40 %"]])


# --------------------------------------------------------------------------------------------------- Spires ------


SPIRES_DIFFS = []


def spires_json(rd, od, s, arm, r, w, per):
    """The Spires metrics of one run. The JSON files written before Amendments 12 and 13 lack the bound and the population fields, so the
    metrics are recomputed from the counts and latency files by p3_eval.spires_metrics (the code the JSON came from); a recomputed value
    that differs from a field of the committed JSON is listed in the report."""
    committed = jload(os.path.join(rd, "spires_%s_%s_r%s_W%d_p%d.json" % (s, arm, r, w, per)))
    if committed is None:
        return None
    cnt = os.path.join(od, "spires_%s" % s, "counts_%s_r%s_W%d_p%d.csv" % (arm, r, w, per))
    if not os.path.exists(cnt):
        return committed
    lat = cnt + ".latency"
    j = p3_eval.spires_metrics(cnt, lat if os.path.exists(lat) else None)
    j = json.loads(json.dumps(j))                                    # same types as a committed file
    def strip(o):                                                    # the `population` and `unit` labels were added after some files were written
        if isinstance(o, dict):
            return {k: strip(v) for k, v in o.items() if k not in ("population", "unit")}
        return o
    for k, v in committed.items():
        if k in j and strip(j[k]) != strip(v):
            SPIRES_DIFFS.append("%s_%s_r%s_W%d_p%d: field %s differs" % (s, arm, r, w, per, k))
    return j


def spires_section(rd, od, per, failed_names, runs):
    out = []
    for r in ("0.2", "0.1"):
        for w in (10, 20):
            rows, rows3, rows2 = [], [], []
            for s in ("03", "04", "05"):
                for arm in ARMS:
                    step = "spires_%s_%s_r%s_W%d_p%d" % (s, arm, r, w, per)
                    j = spires_json(rd, od, s, arm, r, w, per)
                    if j is None:
                        rows.append(["Keble %s" % s, ARM_LABEL[arm], step_status(step, runs, failed_names)] + [""] * 4)
                        continue
                    b = j.get("ever_static_on_clutter_eligible_bound", {})
                    bound = "[%s, %s]" % (pc(b.get("lo")), pc(b.get("hi"))) if b else "n/a"
                    s3, s2 = j["structure_label_3"], j["structure_label_2"]
                    nominal = "%.2f%% per history" % (100 * ALPHA) if arm in SPRT_ARMS else "none stated"
                    rows.append(["Keble %s" % s, ARM_LABEL[arm], nominal, pc_ci(j["false_static_on_clutter"]), bound, "%d" % j["false_static_on_clutter"]["n"], ""])
                    rows2.append(["Keble %s" % s, ARM_LABEL[arm], pc_ci({"rate": s3["false_reject_rate"], "wilson95": s3["wilson95"]}, nd=2), pc(s3["undecided_share"]),
                                  "%s / %s" % (num(s3["windows_to_first_static"]["p50"], 0), num(s3["windows_to_first_static"]["p90"], 0)),
                                  pc_ci({"rate": s2["false_reject_rate"], "wilson95": s2["wilson95"]}, nd=2), pc(s2["undecided_share"]),
                                  "%s / %s" % (num(s2["windows_to_first_static"]["p50"], 0), num(s2["windows_to_first_static"]["p90"], 0)), str(s3["n"]), str(s2["n"])])
            if not any(row[2] not in ("missing",) and not str(row[2]).startswith(("FAILED", "missing")) for row in rows):
                pass
            tag = "veto ON (primary)" if per == 1 else "veto OFF (secondary)"
            out.append("#### Resolution %s m, W = %d, %s\n" % (r, w, tag))
            out.append("False-Static on TLS-absent clutter (label 1, end of run, eligible cells; not the unit of the alpha test) and the bound on the ever-Static rate over eligible cells (Amendment 13):\n")
            out.append(md(["sequence", "arm", "nominal", "Static at the end, Wilson", "ever Static, bound", "eligible n", ""], rows))
            out.append("False-reject, Undecided share and windows to first Static on non-ground structure (label 3) and ground (label 2). Latency population: all labelled cells ever Static, eligible or not (Amendment 12); it is not comparable with LILocBench latency.\n")
            out.append(md(["sequence", "arm", "label 3 Rejected", "label 3 Undecided", "label 3 windows p50 / p90", "label 2 Rejected", "label 2 Undecided", "label 2 windows p50 / p90", "n label 3", "n label 2"], rows2))
    return "\n".join(out)


def spires_agree_section(rd, per):
    rows = []
    for arm in ARMS:
        j = jload(os.path.join(rd, "spires_agree_%s_p%d.json" % (arm, per)))
        if not j:
            rows.append([ARM_LABEL[arm], "missing", "", ""])
            continue
        cells = []
        for p in j:
            cells.append("(%d,%d): %.3f [pairwise tiles %.3f]" % (p["session_a"], p["session_b"], p["jaccard"], p["pairwise_tiles"]["jaccard"]))
        rows.append([ARM_LABEL[arm], "; ".join(cells), str(j[0]["shared_tiles"]), "%d / %d" % (j[0]["intersection"], j[0]["union"])])
    return md(["arm", "Jaccard of Static sets by pair of sequences (03, 04, 05 = 0, 1, 2)", "shared 10 m tiles", "pair (0,1) intersection / union"], rows)


# -------------------------------------------------------------------------------------------------------- KTH ------

def kth_section(rd, od, per):
    out = []
    for seq, cfgs in (("05", (("0.2", 10), ("0.2", 2))), ("semindoor", (("0.2", 10), ("0.2", 2), ("0.1", 10)))):
        sc = jload(os.path.join(rd, "kth_selfcheck_%s.json" % seq))
        for r, w in cfgs:
            rows = []
            for arm in ARMS:
                m = meta_load(os.path.join(od, "kth_%s" % seq, "%s_r%s_W%d_p%d.meta" % (arm, r, w, per)))
                for pol in ("static_only", "not_rejected"):
                    j = jload(os.path.join(rd, "kth_%s_%s_r%s_W%d_p%d_%s.json" % (seq, arm, r, w, per, pol)))
                    if j is None:
                        rows.append([ARM_LABEL[arm], pol, "missing", "", "", "", "", ""])
                        continue
                    pts = int(m.get("points_in_range", 0)) or 1
                    rows.append([ARM_LABEL[arm], pol, num(j["SA"], 2), num(j["DA"], 2), num(j["AA"], 2), num(j["HA"], 2),
                                 "%.1f / %.1f / %.1f" % tuple(100.0 * int(m.get("points_voxel_" + k, 0)) / pts for k in ("static", "undecided", "rejected")),
                                 m.get("windows_closed", "n/a")])
            tag = "veto ON (primary)" if per == 1 else "veto OFF (secondary)"
            out.append("#### KITTI %s, %s m, W = %d, %s\n" % (seq, r, w, tag))
            ident = jload(os.path.join(rd, "kth_%s_shipped_v0_1_r%s_W%d_p%d_all.json" % (seq, r, w, per)))
            if ident:
                out.append("Identity check (policy all, every frame point exported): SA = %s, DA = %s.\n" % (num(ident["SA"], 4), num(ident["DA"], 4)))
            if sc:
                out.append("Self-check of the ground-truth cloud: %s\n" % json.dumps({k: sc[k] for k in sc if k not in ("gt",)}, sort_keys=True)[:400])
            out.append(md(["arm", "export policy", "SA", "DA", "AA (geometric mean)", "HA (harmonic mean)", "points in Static / Undecided / Rejected voxel, %", "windows"], rows))
    return "\n".join(out)


# ----------------------------------------------------------------------------------------------------- Boreas ------

def boreas_section(rd, od):
    rows, rows2 = [], []
    for arm in ARMS:
        ind = [meta_load(os.path.join(od, "boreas", "%s_independent.meta" % arm))]
        j = jload(os.path.join(rd, "boreas_%s_independent.json" % arm))
        ns = jload(os.path.join(rd, "boreas_%s_newstatic.json" % arm))
        if not j:
            rows.append([ARM_LABEL[arm], "missing", "", ""])
        else:
            rows.append([ARM_LABEL[arm], "; ".join("(%d,%d): %.3f [pairwise %.3f]" % (p["session_a"], p["session_b"], p["jaccard"], p["pairwise_tiles"]["jaccard"]) for p in j),
                         str(j[0]["shared_tiles"]), "; ".join("(%d,%d): %d / %d" % (p["session_a"], p["session_b"], p["intersection"], p["union"]) for p in j)])
        if not ns:
            rows2.append([ARM_LABEL[arm], "missing", "", "", ""])
        else:
            rows2.append([ARM_LABEL[arm], str(ns["cumulative_static"]), str(ns["static_in_any_independent_session"]), str(ns["only_in_cumulative"]), pc(ns["share_only_in_cumulative"])])
    out = ["Sessions 0, 1, 2 = boreas-2020-11-26-13-58 (late autumn), boreas-2021-04-08-12-44 (spring), boreas-2021-06-17-17-52 (summer). 0.2 m, W = 10, range 3 to 25 m, Periodic veto off (Amendment 1).\n"]
    out.append("Jaccard of the independent Static sets inside the 10 m tiles observed by all three sessions (and, in brackets, inside the tiles observed by the pair):\n")
    out.append(md(["arm", "Jaccard by pair", "shared tiles", "intersection / union"], rows))
    out.append("Cells that need more than one session: Static voxels of the cumulative map (three sessions in turn) that are Static in no independent session:\n")
    out.append(md(["arm", "cumulative Static", "Static in any independent session", "only in cumulative", "share only in cumulative"], rows2))
    srows = []
    for arm in ARMS:
        for mode in ("independent", "cumulative"):
            m = meta_load(os.path.join(od, "boreas", "%s_%s.meta" % (arm, mode)))
            if m:
                srows.append([ARM_LABEL[arm], mode, m.get("windows_closed", ""), m.get("map_cells_end", ""), m.get("peak_rss_kb", ""), m.get("seconds", "")])
    out.append("Run facts (map cells at the end, peak memory in kB, seconds; timing is stated, not measured under controlled load):\n")
    out.append(md(["arm", "mode", "windows closed", "map cells end", "peak RSS kB", "seconds"], srows))
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    rd, od = a.results_dir, a.out_dir
    runs, attempts, failed, not_run, skipped = inventory(rd)
    failed_names = {r[1] for r in failed if len(r) > 1}
    doc = ["# STRATA v2 phase 3: test-split evaluation report\n"]
    doc.append("Generated by `make_report_p3.py` from `results_p3/` (Amendment 15). Every number is copied from the JSON, meta and run-record files; the only computed rows are the near-wall thickening rows.\n")
    doc.append("## 0. Runs\n")
    ok = sum(1 for v in runs.values() if v == 0)
    bad = sorted(k for k, v in runs.items() if v not in (0, None))
    doc.append("Run records: %d with return code 0, %d with a non-zero return code, %d earlier attempts kept (repeats after a crash)." % (ok, len(bad), len(attempts)))
    doc.append("\n### Failed steps (`failed_steps.tsv`, append-only)\n")
    frows = [r + ["ok after repeat (identical command)" if runs.get(r[1]) == 0 else ("failed, final" if r[1] in runs else "no run record")] for r in failed]
    doc.append(md(["time", "step", "result", "final state"], frows) if failed else "none\n")
    doc.append("### Steps never started (`not_run_steps.tsv`)\n")
    seen, nr = set(), []
    for r in not_run:                                        # the resume passes append the same line again; the first line of a step stands
        if len(r) > 1 and r[1] not in seen:
            seen.add(r[1])
            nr.append(r)
    doc.append(md(["time (first listing)", "step", "reason"], nr) if nr else "none\n")
    doc.append("### Repeats skipped because the step had failed (`skipped_repeat.tsv`)\n")
    sk, seen = [], set()
    for r in skipped:
        if len(r) > 1 and r[1] not in seen:
            seen.add(r[1])
            sk.append(r)
    doc.append(md(["time (first listing)", "step", "reason"], sk) if sk else "none\n")
    doc.append("## 1. LILocBench\n")
    doc.append("Reference rows from the calibration sequences (same code and pair), quoted with the primary quantity (Amendment 5):\n")
    doc.append(lil_reference_rows())
    for per in (1, 0):
        doc.append(lil_section(rd, od, per))
    doc.append("## 2. Oxford Spires Keble 03 to 05\n")
    for per in (1, 0):
        doc.append(spires_section(rd, od, per, failed_names, runs))
        doc.append("#### Agreement of the Static sets of the three same-day sequences (0.2 m, W = 10, independent maps), %s\n" % ("veto ON" if per == 1 else "veto OFF"))
        doc.append(spires_agree_section(rd, per))
    doc.append("Spires JSON files written before Amendments 12 and 13 lack the bound and population fields; the tables above are recomputed from the counts and latency files by `p3_eval.spires_metrics`. Fields of the committed JSON that differ from the recomputed value: %s.\n" % ("none" if not SPIRES_DIFFS else "; ".join(SPIRES_DIFFS)))
    doc.append("## 3. KTH KITTI 05 and semi-indoor\n")
    doc.append("Single pass, one tick per frame, SuMa poses (KITTI 05) and NDT SLAM poses (semi-indoor, 16-beam sensor), AA geometric mean, HA harmonic mean. "
               "Every arm applies its increment once per touched window, so the rows labelled OctoMap parameters on window evidence and ELite factors on window evidence are not the published OctoMap and ELite methods. "
               "The `sprt` pair is the 2D pair, not calibrated for 3D. No arm is a competitive removal method.\n")
    for per in (1, 0):
        doc.append(kth_section(rd, od, per))
    doc.append("## 4. Boreas\n")
    doc.append(boreas_section(rd, od))
    with open(a.out, "w") as f:
        f.write("\n".join(doc) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
