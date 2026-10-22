#!/usr/bin/env python3
"""Number guard: every headline figure quoted in paper/strata_paper.md must be
recomputable from the committed CSVs in paper/experiments/results/, and retired
figures must not reappear. Run from anywhere: python3 paper/experiments/number_guard.py
Exits nonzero on any failure."""
import csv
import hashlib
import json
import math
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PAPER = HERE.parent / "strata_paper.md"
ARXIV = HERE.parent / "arxiv_ref"
RESULTS = HERE / "results"


def tex_text(root):
    """Concatenate the arXiv package's tex sources, normalised to plain text so the
    same quoted-number checks apply (thousands braces, ties, en-dashes)."""
    parts = [(root / "main.tex").read_text(encoding="utf-8")]
    for sub in ("sections", "tables", "figures"):
        parts += [f.read_text(encoding="utf-8") for f in sorted((root / sub).glob("*.tex"))]
    t = "\n".join(parts)
    for a, b in (("{,}", ","), ("~", " "), ("--", "\u2013"), ("\\times", "\u00d7"),
                 ("$", ""), ("{=}", "=")):
        t = t.replace(a, b)
    return t


def md_text(t):
    """The Markdown companion in the same plain form as tex_text(): math dollars and the
    brace around an equals sign are dropped, so that one phrase checks both files."""
    for a, b in (("{,}", ","), ("{=}", "="), ("$", "")):
        t = t.replace(a, b)
    return t


TARGETS = [("strata_paper.md", md_text(PAPER.read_text(encoding="utf-8")))]
if (ARXIV / "main.tex").exists():
    TARGETS.append(("arxiv_ref", tex_text(ARXIV)))
failures = []
passes = []
text = ""
target = ""


def rows(name):
    with open(RESULTS / name, newline="") as f:
        return [r for r in csv.DictReader(f) if not r[list(r)[0]].startswith("#")]


# The companions are generated from the manuscript (paper/arxiv_ref/tools/tex2md.py builds
# strata_paper.md from the LaTeX master; strata_paper_KR.md is its Korean translation), so
# every check below applies to strata_paper.md as it does to the manuscript, and
# companion_fidelity() compares the Korean file with the English one. Nothing is deferred.
COMPANION = "strata_paper.md"
DEFERRED_FOR_COMPANION = ()
deferred = []


def check(label, cond, detail=""):
    if target == COMPANION and not cond and any(label.startswith(d) or d in label for d in DEFERRED_FOR_COMPANION):
        deferred.append(f"[{target}] {label}")
        return
    (passes if cond else failures).append(f"[{target}] {label}{' — ' + detail if detail else ''}")


REG = {}   # target -> [(label, variants)]: every source-derived string the paper must contain


def in_paper(label, *variants):
    REG.setdefault(target, []).append((label, variants))
    ok = any(v in text for v in variants)
    check(label, ok, f"none of {variants} found in paper" if not ok else "")


def guard_sprt_arm():
    """The sequential-test arm of the harness (results/*_sprt.csv, phase P4a): structure of the
    files, the derived latency, the selected levels and the sentences of the text that quote them."""
    # derived quantities of the shipped defaults, summed as the engine sums them
    hpp = (HERE.parents[1] / "strata_core/include/strata_core/layered_map.hpp").read_text()
    dflt = {k: float(re.search(r"double %s\{([0-9.]+)\}" % k, hpp).group(1)) for k in ("m0", "m1", "alpha", "beta")}
    check("sprt arm: shipped defaults are m0 0.20, m1 0.70, alpha = beta = 0.01",
          (dflt["m0"], dflt["m1"], dflt["alpha"], dflt["beta"]) == (0.2, 0.7, 0.01, 0.01), str(dflt))
    l_hit = math.log(dflt["m1"] / dflt["m0"])
    A = math.log((1 - dflt["beta"]) / dflt["alpha"])
    ceiling = A + 2 * l_hit
    l, latency = 0.0, 0
    while l < A:
        l = min(l + l_hit, ceiling)
        latency += 1
    # E1: recall 1.0 from window index latency - 1 on, in all six runs of both files; no false Static cell
    for fn in ("e1_static_quality_sprt.csv", "e1_static_quality_periodic_sprt.csv"):
        rs = rows(fn)
        runs = {(r["backend"], r["density"]) for r in rs}
        first = {}
        for k in runs:
            rr = sorted((int(r["window"]), float(r["recall"]), float(r["precision"])) for r in rs if (r["backend"], r["density"]) == k)
            first[k] = next(w for w, rc, _ in rr if rc == 1.0)
            check(f"sprt arm {fn}: {k} recall stays 1.0 and precision stays 1.0 from window {first[k]}",
                  all(rc == 1.0 and pr == 1.0 for w, rc, pr in rr if w >= first[k]), str(rr[-1]))
        check(f"sprt arm {fn}: six runs, recall 1.0 first at the derived latency {latency} (index {latency - 1})",
              len(runs) == 6 and set(first.values()) == {latency - 1}, str(first))
    check("sprt arm: periodicity on equals periodicity off in E1",
          [(r["recall"], r["precision"], r["pred_static"]) for r in rows("e1_static_quality_sprt.csv")]
          == [(r["recall"], r["precision"], r["pred_static"]) for r in rows("e1_static_quality_periodic_sprt.csv")])
    in_paper("sprt arm: E1 derived fourth window quoted", "give recall 1.0 from the derived fourth window")
    in_paper("sprt arm: E1 no false Static cell quoted", "no false Static cell")
    check("sprt arm: the derived latency is four windows", latency == 4, str(latency))
    # levels: the periodic level of the sprt arm is chosen by the same pre-stated rules as in the legacy arm
    sel0 = [r for r in rows("e0_calibration_choice_sprt.csv") if r["selected"] == "1"]
    sel5 = [r for r in rows("e5_calibration_choice_sprt.csv") if r["selected"] == "1"]
    check("sprt arm: E0 selects delta 0.1 as in the legacy arm", len(sel0) == 1 and float(sel0[0]["alpha"]) == 0.1)
    pfa = float(re.search(r"double periodic_false_alarm\{([0-9.]+)\}", hpp).group(1))
    check("sprt arm: E5 selects the shipped spent level", len(sel5) == 1 and float(sel5[0]["delta"]) == pfa == 0.2, str(sel5))
    # E6 'e2' rows equal E5's spent rows (same code path and seeds), now with the Intermittent column
    c5 = {(r["rule"], r["readout"], r["gt"]): r for r in rows("e5_confusion_sprt.csv")}
    c6 = {(r["params"], r["readout"], r["gt"]): r for r in rows("e6_confusion_sprt.csv")}
    cols = ("pred_S", "pred_P", "pred_T", "pred_U", "pred_I")
    check("sprt arm: E6 'e2' rows equal E5's spent rows",
          all(tuple(c6[("e2", q, g)][k] for k in cols) == tuple(c5[("spending", q, g)][k] for k in cols)
              for q in ("64", "256") for g in ("wall", "door", "aperiodic", "dynamic")))
    for (pn, q, g), r in c6.items():
        check(f"sprt arm: E6 {pn} {q} {g} counts add up", sum(int(r[k]) for k in cols) == {"wall": 1000, "door": 1200, "aperiodic": 2000, "dynamic": 1000}[g])
    # E7 is a sprt-only file pair; E4 cell counts: one row per backend and extent
    e4 = rows("e4_cells_sprt.csv")
    check("sprt arm: E4 cell counts, six rows", len(e4) == 6 and all(r["bytes_per_cell"] == "56" for r in e4))
    sw = rows("e6_static_vs_m_sprt.csv")
    check("sprt arm: E6 sweep has 19 points for each of two parameter sets", len(sw) == 38 and {r["params"] for r in sw} == {"e2", "shipped"})
    ms = math.log((1 - dflt["m1"]) / (1 - dflt["m0"]))
    check("sprt arm: E6 zero-drift rate is -l_miss / (l_hit - l_miss)",
          all(abs(float(r["zero_drift_rate"]) - (-ms / (l_hit - ms))) < 1e-5 for r in sw), str(sw[0]["zero_drift_rate"]))


def guard():
    # ---- E1: static-map quality ----
    e1 = rows("e1_static_quality.csv")
    gt = {r["gt_static"] for r in e1 if int(r["window"]) > 0}
    check("E1 GT wall cells constant", gt == {"161"}, f"gt_static values: {gt}")
    in_paper("E1 '161' quoted", "161")

    late_recall = [float(r["recall"]) for r in e1 if int(r["window"]) >= 3]
    check("E1 recall==1.0 from window 3 on (all runs)", all(v == 1.0 for v in late_recall))
    in_paper("E1 'window 3' quoted", "window 3")
    if target == "arxiv_ref":
        guard_sprt_arm()

    last_w = max(int(r["window"]) for r in e1)
    final = {(r["backend"], r["density"]): r for r in e1 if int(r["window"]) == last_w}
    for b in ("grid2d", "voxel3d"):
        r = final[(b, "low")]
        check(f"E1 {b} low-density final precision==1", float(r["precision"]) == 1.0, r["precision"])
        check(f"E1 {b} low-density final F1==1", float(r["f1"]) == 1.0, r["f1"])
    for b, exp4 in (("grid2d", None), ("voxel3d", None)):
        p = float(final[(b, "high")]["precision"])
        in_paper(f"E1 {b} high-density precision {p:.4f}", f"{p:.4f}", f"{p:.3f}")
    f1g = float(final[("grid2d", "high")]["f1"])
    in_paper(f"E1 grid2d high-density F1 {f1g:.4f}", f"{f1g:.4f}", f"{f1g:.3f}")

    # ---- E2: periodicity ----
    e2 = {r["metric"]: r for r in rows("e2_summary.csv")}
    tpr, fpr = float(e2["periodic_TPR"]["value"]), float(e2["periodic_FPR"]["value"])
    check("E2 TPR is 2/3", abs(tpr - 2 / 3) < 1e-4, str(tpr))
    check("E2 FPR is 0/5", abs(fpr - 0.0) < 1e-9, str(fpr))
    in_paper("E2 TPR quoted as fraction", "2/3")
    in_paper("E2 FPR quoted as fraction", "0/5")
    pre64 = {r["metric"]: r for r in rows("pre_fix_2026-09-28/e2_summary.csv")}
    mid64 = {r["metric"]: r for r in rows("pre_calibration_2026-09-28/e2_summary.csv")}
    check("E2 n=64 FP v0.1.0 1/5, centred 2/5",
          (pre64["periodic_FPR"]["note"], mid64["periodic_FPR"]["note"]) == ("1", "2"))
    in_paper("E2 earlier n=64 FPRs quoted", "1/5")
    in_paper("E2 centred n=64 FPR quoted", "2/5")
    # read-out length sweep: current (calibrated), centred-only, and v0.1.0
    for sub, mean_q, fprange, tpset in (("", "0.000", (0, 0), {"0", "1", "2"}),
                                        ("pre_spending_2026-09-28/", "0.000", (0, 0), {"0", "1", "2"}),
                                        ("pre_calibration_2026-09-28/", "0.331", (0, 4), {"2"}),
                                        ("pre_fix_2026-09-28/", "0.363", (0, 4), {"2"})):
        sw = rows(sub + "e2_rates_vs_length.csv")
        fp = [int(r["fp"]) for r in sw]
        m = sum(fp) / (5 * len(fp))
        check(f"E2 sweep {sub or 'post'} lengths 8..100", [int(r["obs_length"]) for r in sw] == list(range(8, 101)))
        check(f"E2 sweep {sub or 'post'} mean FPR {mean_q}", f"{m:.3f}" == mean_q, f"{m:.3f}")
        check(f"E2 sweep {sub or 'post'} FP range {fprange}", (min(fp), max(fp)) == fprange)
        check(f"E2 sweep {sub or 'post'} TP set {sorted(tpset)}", {r["tp"] for r in sw} == tpset)
        if mean_q != "0.000":
            in_paper(f"E2 sweep mean FPR {mean_q} quoted", mean_q)
    in_paper("E2 sweep FP range quoted", "0 to 4 of 5", "0–4 of 5")
    in_paper("E2 calibrated: no FP at any length quoted", "no false positive at any", "0 at every read-out length")
    ONE = "pre_spending_2026-09-28/"   # calibrated test at a single read-out (delta 0.1)
    sw = rows(ONE + "e2_rates_vs_length.csv")
    tp_by = {int(r["obs_length"]): int(r["tp"]) for r in sw}
    check("E2 single-read-out TPR 0 at n=8-12, 1/3 at 13-14, 2/3 from 15",
          all(tp_by[n] == 0 for n in range(8, 13)) and all(tp_by[n] == 1 for n in (13, 14))
          and all(tp_by[n] == 2 for n in range(15, 101)))
    in_paper("E2 single-read-out detection from 15 quoted", "15 windows", "n=15", "against 15")
    cur = rows("e2_rates_vs_length.csv")
    tp_c = {int(r["obs_length"]): int(r["tp"]) for r in cur}
    check("E2 spent TPR 0 at n=8-19, 1/3 at 20-24, 2/3 from 25",
          all(tp_c[n] == 0 for n in range(8, 20)) and all(tp_c[n] == 1 for n in range(20, 25))
          and all(tp_c[n] == 2 for n in range(25, 101)))
    in_paper("E2 spent detection from 25 quoted", "25 windows", "n=25")
    check("E2 spent reference model: no FP at any length", all(r["ref_fp"] == "0" for r in cur))
    check("E2 spent reference detects the 25%-duty door at every n >= 57",
          min(n for n in range(8, 101) if all(int(r["ref_tp"]) == 3 for r in cur if int(r["obs_length"]) >= n)) == 57)
    in_paper("E2 spent reference door n=57 quoted", "n=57")
    mt = lambda sub: sum(int(r["tp"]) for r in rows(sub + "e2_rates_vs_length.csv")) / (3 * 93)
    check("E2 mean TPR 0.563 (spent), 0.624 (single), 0.667 (centred)",
          (f"{mt(''):.3f}", f"{mt(ONE):.3f}", f"{mt('pre_calibration_2026-09-28/'):.3f}") == ("0.563", "0.624", "0.667"))
    in_paper("E2 mean TPR 0.624 quoted", "0.624")
    in_paper("E2 mean TPR 0.563 quoted", "0.563")
    rfp = [int(r["ref_fp"]) for r in sw]
    check("E2 reference FP lengths 25, mean 0.054", (sum(v > 0 for v in rfp), f"{sum(rfp) / (5 * len(rfp)):.3f}") == (25, "0.054"))
    check("E2 reference FPs only aperiodic_1 at 29-31, 45-50, 52-67",
          {int(r["obs_length"]) for r in sw if r["ref_fp"] != "0"} == set(range(29, 32)) | set(range(45, 51)) | set(range(52, 68))
          and {r["ref_false_positives"] for r in sw if r["ref_fp"] != "0"} == {"aperiodic_1"})
    in_paper("E2 reference FP lengths quoted", "25 of 93")
    in_paper("E2 reference mean FPR quoted", "0.054")
    check("E2 reference detects 25%-duty door from n=26",
          min(n for n in range(8, 101) if all(int(r["ref_tp"]) == 3 for r in sw if int(r["obs_length"]) >= n)) == 26)
    in_paper("E2 reference door n=26 quoted", "n=26")

    amp_rows = rows("e2_amplitude_vs_length.csv")
    amp_cols = [c for c in amp_rows[0] if "amp" in c.lower()]
    all_amps = {round(float(r[c]), 3) for r in amp_rows for c in amp_cols if r[c]}
    for a in ("0.653", "0.707", "0.329"):
        check(f"E2 amplitude {a} traceable", float(a) in all_amps)
        in_paper(f"E2 amplitude {a} quoted", a)

    # ---- E3: hysteresis sensitivity ----
    e3 = rows("e3_sensitivity.csv")
    degen = [r for r in e3 if r["degenerate"] == "1"]
    worst = max(degen, key=lambda r: int(r["flicker_transitions"]))
    check("E3 worst degenerate flicker==574", worst["flicker_transitions"] == "574",
          worst["flicker_transitions"])
    check("E3 worst degenerate F1==0.810", f"{float(worst['final_f1']):.3f}" == "0.810",
          worst["final_f1"])
    in_paper("E3 '574' quoted", "574")
    in_paper("E3 '0.810' quoted", "0.810")
    matched = [r for r in e3 if r["graduate_prob"] == "0.9" and r["demote_prob"] == "0.3"
               and r["survival_decay"] == "0.9"]
    check("E3 matched wide-band row exists", len(matched) == 1)
    if matched:
        check("E3 matched wide-band flicker==54", matched[0]["flicker_transitions"] == "54",
              matched[0]["flicker_transitions"])
        check("E3 matched wide-band F1==1.0", float(matched[0]["final_f1"]) == 1.0)
        check("E3 '54' quoted (word-boundary)", re.search(r"\b54\b", text) is not None)

    # ---- E4: throughput / memory ----
    e4 = rows("e4_throughput.csv")
    by = {(r["backend"], r["extent"]): r for r in e4}
    extents = sorted({r["extent"] for r in e4}, key=int)
    cell_ratios = []
    for x in extents:
        g, v = by[("grid2d", x)], by[("voxel3d", x)]
        cell_ratios.append(float(v["final_cells"]) / float(g["final_cells"]))
    for r in cell_ratios:
        in_paper(f"E4 live-cell ratio {r:.1f}x quoted", f"{r:.1f}")
    in_paper("E4 live-cell ratio range", f"{min(cell_ratios):.1f}–{max(cell_ratios):.1f}")
    check("E4 bytes_per_cell all 56", {r["bytes_per_cell"] for r in e4} == {"56"})
    in_paper("E4 '56 B' quoted", "56 B")
    for r in e4:
        b = float(r["est_bytes"])
        if b < 1e6:  # paper uses KB below 1 MB
            in_paper(f"E4 memory {b/1e3:.0f} KB quoted ({r['backend']} {r['size_label']})",
                     f"{b/1e3:.0f} KB")
        else:
            mb = b / 1e6
            s = f"{mb:.2f}" if mb < 10 else f"{mb:.1f}"
            in_paper(f"E4 memory {s} MB quoted ({r['backend']} {r['size_label']})", f"{s} MB")

    # ---- E5: trajectory level ----
    e5 = rows("e5_trajectory.csv")
    bern = lambda rule: [r for r in e5 if r["rule"] == rule and r["kind"] == "bernoulli"]
    w1 = max(bern("single_readout"), key=lambda r: int(r["ever_1024"]))
    check("E5 single worst ever(1024) 953/2000 (E2, every, m=0.4)",
          (w1["ever_1024"], w1["cells"], w1["params"], w1["touch"], w1["m"]) == ("953", "2000", "e2", "every", "0.4"))
    in_paper("E5 953 of 2000 quoted", "953 of 2000")
    check("E5 single worst ever(64) 84", max(int(r["ever_64"]) for r in bern("single_readout")) == 84)
    in_paper("E5 84 of 2000 quoted", "84 of 2000")
    w2 = max(int(r["ever_1024"]) for r in bern("spending"))
    check("E5 spent worst ever(1024) 2 of 2000", w2 == 2 and all(r["cells"] == "2000" for r in bern("spending")))
    in_paper("E5 2 of 2000 quoted", "2 of 2000")
    check("E5 constant cells never Periodic",
          all(r["ever_1024"] == "0" for r in e5 if r["kind"].startswith("constant")))
    mk = {(r["rule"], r["params"], r["touch"]): r for r in e5 if r["kind"] == "markov"}
    check("E5 Markov shipped/every 1998 single, 1908 spent",
          (mk[("single_readout", "shipped", "every")]["ever_1024"], mk[("spending", "shipped", "every")]["ever_1024"]) == ("1998", "1908"))
    in_paper("E5 Markov 1908 quoted", "1908 of 2000")
    e5c = rows("e5_confusion.csv")
    C = {(r["rule"], r["readout"], r["gt"]): r for r in e5c}
    check("E5 aperiodic false Static 1221 of 2000 (spent, n=64)",
          C[("spending", "64", "aperiodic")]["pred_S"] == "1221" and
          sum(int(C[("spending", "64", "aperiodic")][k]) for k in ("pred_S", "pred_P", "pred_T", "pred_U")) == 2000)
    in_paper("E5 1221 of 2000 quoted", "1221 of 2000")
    sel5 = [r for r in rows("e5_calibration_choice.csv") if r["selected"] == "1"]
    check("E5 selects delta=0.2, the largest meeting the target",
          len(sel5) == 1 and float(sel5[0]["delta"]) == 0.2 and
          all((float(r["worst_wilson_hi_1024"]) <= 0.01) == (r["meets_target"] == "1") for r in rows("e5_calibration_choice.csv")))
    dd = {(r["rule"], r["door"]): r["median_first_n"] for r in rows("e5_doors.csv")}
    check("E5 door p8 4/4: 15 -> 25", (dd[("single_readout", "p8_4on4off")], dd[("spending", "p8_4on4off")]) == ("15", "25"))

    # ---- seed provenance ----
    in_paper("harness seed 12345 quoted", "12345")

    # ---- retired figures must NOT reappear ----
    for tok in ("4–8×", "4-8×", "6–10×", "6-10×", "1303", "1,303", "1{,}303"):
        check(f"retired token absent: {tok!r}", tok not in text, "found in paper")



for target, text in TARGETS:
    guard()

def guard_arxiv():
    """Extra checks for the arXiv package: generated tables must be current, and
    prose numbers not covered above must recompute from CSVs / code constants."""
    import math
    import subprocess
    import tempfile
    import filecmp
    import shutil
    # 1. tables/ and figures/data/ must equal a fresh regeneration from the CSVs
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td) / "pkg"
        shutil.copytree(ARXIV / "tools", tmp / "tools")
        (tmp / "tables").mkdir()
        (tmp / "figures" / "data").mkdir(parents=True)
        # make_artifacts resolves results as PKG.parent/experiments/results
        link = Path(td) / "experiments"
        link.symlink_to(HERE)
        subprocess.run(["python3", str(tmp / "tools" / "make_artifacts.py")], check=True,
                       stdout=subprocess.DEVNULL)
        for sub in ("tables", "figures/data", "figures"):
            for f in sorted(x for x in (tmp / sub).iterdir() if x.is_file()):
                ok = filecmp.cmp(f, ARXIV / sub / f.name, shallow=False)
                check(f"generated {sub}/{f.name} is current", ok, "stale; rerun make_artifacts.py")
    # 2. prose numbers
    e1 = rows("e1_static_quality.csv")
    med = [r for r in e1 if r["density"] == "med" and int(r["window"]) == 39][0]
    in_paper("E1 med precision", f"{float(med['precision']):.4f}")
    for b, fin in (("grid2d", "174"), ("voxel3d", "165")):
        hi = [r for r in e1 if r["backend"] == b and r["density"] == "high"]
        first = min(int(r["window"]) for r in hi if int(r["pred_static"]) > 161)
        last = [r for r in hi if int(r["window"]) == 39][0]["pred_static"]
        check(f"E1 {b} final pred {fin}", last == fin, last)
        in_paper(f"E1 {b} first false static t={first}", f"t={first}")
        in_paper(f"E1 {b} final pred {last}", last)
    first_full = {min(int(r["window"]) for r in e1 if r["backend"] == b and r["density"] == d
                      and float(r["recall"]) == 1.0) for b in ("grid2d", "voxel3d")
                  for d in ("low", "med", "high")}
    check("E1 recall reaches 1 at t=2 everywhere", first_full == {2}, str(first_full))
    in_paper("E1 't=2' quoted", "t=2")
    cls = {r["cell"]: r for r in rows("e2_classification.csv")}
    ref = float(cls["door_p8_2on6off"]["ref_amplitude"])
    in_paper("E2 low-duty ref amplitude", f"{ref:.3f}")
    fa1 = float(cls["aperiodic_1"]["ref_false_alarm"])
    check("aperiodic_1 reference bound 0.056 <= delta", f"{fa1:.3f}" == "0.056" and fa1 <= 0.1)
    in_paper("aperiodic_1 reference bound quoted", "0.056")
    z = float(cls["aperiodic_1"]["ref_amplitude"]) / math.sqrt(0.5 / 64)
    check("aperiodic_1 ~3.7 null sd", f"{z:.1f}" == "3.7", str(z))
    in_paper("3.7 sd quoted", "3.7")
    e3 = rows("e3_sensitivity.csv")
    idx = {(r["graduate_prob"], r["demote_prob"], r["survival_decay"]): r for r in e3}
    in_paper("E3 worst recall", f"{float(idx[('0.9','0.9','0.9')]['final_recall']):.2f}")
    for k in (("0.7", "0.7", "0.9"), ("0.8", "0.8", "0.9"), ("0.9", "0.9", "0.97"), ("0.9", "0.9", "1")):
        in_paper(f"E3 flicker {k}", idx[k]["flicker_transitions"])
    band = [int(idx[("0.9", "0.3", l)]["flicker_transitions"]) for l in ("0.9", "0.97", "1")]
    in_paper("E3 wide-band flicker range", f"{min(band)}\u2013{max(band)}")
    check("E3 36 configurations", len(e3) == 36, str(len(e3)))
    e4 = {(r["backend"], r["extent"]): r for r in rows("e4_throughput.csv")}
    big = e4[("voxel3d", "500")]
    in_paper("E4 largest cell count", f"{int(big['final_cells']):,}")
    in_paper("E4 largest memory", f"{float(big['est_bytes'])/1e6:.1f} MB")
    # 3. constants derived from code defaults (layered_map.hpp)
    hdr = (HERE.parents[1] / "strata_core/include/strata_core/layered_map.hpp").read_text()
    for tok in ("l_hit{0.85}", "survival_decay{0.97}", "graduate_prob{0.8}", "l_max{5.0}",
                "prune_prob{0.05}", "demote_prob{0.45}", "min_observations{3}"):
        check(f"code default {tok}", tok in hdr)
    in_paper("fixed point 27.5", f"{0.97*0.85/0.03:.1f}")
    in_paper("sigma(5)", f"{1/(1+math.exp(-5)):.4f}")
    in_paper("ln 4", f"{math.log(4):.3f}")
    in_paper("ln(1/19)", f"{math.log(1/19):.3f}")
    # 4. partial-period leakage (Remark rem:leak) and Prop. race graduation guard
    def leak(n, T, H):
        return max(2 * abs(math.sin(n * m * math.pi / T)) / (n * abs(math.sin(m * math.pi / T)))
                   for m in range(1, H + 1))
    for T, H, rng, bound in ((8, 3, (10, 13), 18), (24, 2, (29, 40), 52)):
        above = [n for n in range(T, 4000) if leak(n, T, H) >= 0.3]
        check(f"leak T={T} above a_min exactly n={rng}", above == list(range(rng[0], rng[1] + 1)), str(above))
        check(f"leak bound T={T} n>={bound}", math.ceil(2 / (0.3 * math.sin(math.pi / T))) == bound)
        in_paper(f"leak range T={T} quoted", f"n={rng[0]}\u2013{rng[1]}")
        in_paper(f"leak bound T={T} quoted", f"n={bound}", f"n\\ge{bound}", f"n\\ge {bound}")
    in_paper("leak peak 0.439", f"{leak(11, 8, 3):.3f}")
    ydef = (HERE.parents[1] / "strata/params/grid2d.yaml").read_text()
    for tok in ("period_windows: 24", "n_harmonics: 2", "layer_interval: 10"):
        check(f"yaml default {tok}", tok in ydef)
    in_paper("leak 120 ticks", "120 integration ticks")
    pre = "pre_fix_2026-09-28/"
    amp_pre = {(r["cell"], r["obs_length"]): float(r["amplitude"]) for r in rows(pre + "e2_amplitude_vs_length.csv")}
    amp = {(r["cell"], r["obs_length"]): float(r["amplitude"]) for r in rows("e2_amplitude_vs_length.csv")}
    check("pre-fix door n=12 amplitude 0.871", f"{amp_pre[('door_p8_4on4off', '12')]:.3f}" == "0.871")
    in_paper("pre-fix door n=12 amplitude quoted", "0.871")
    check("pre-fix wall n=12 leak matches CSV", abs(amp_pre[("wall_constant", "12")] - leak(12, 8, 3)) < 1e-4)
    check("post-fix door n=12 amplitude 0.581", f"{amp[('door_p8_4on4off', '12')]:.3f}" == "0.581")
    in_paper("post-fix door n=12 amplitude quoted", "0.581")
    check("post-fix wall amplitude exactly 0 at every length",
          all(v == 0.0 for (c, _), v in amp.items() if c == "wall_constant"))
    in_paper("aperiodic_0 ref amplitude quoted", f"{float(cls['aperiodic_0']['ref_amplitude']):.3f}")
    check("aperiodic_0 ends Static (calibrated), was Periodic (centred)",
          cls["aperiodic_0"]["final_class"] == "Static" and
          {r["cell"]: r for r in rows("pre_calibration_2026-09-28/e2_classification.csv")}["aperiodic_0"]["final_class"] == "Periodic")
    in_paper("noise sd at n=T quoted", f"{math.sqrt(0.5 / 8):.2f}")
    # centred bound a <= 4 m (1-m): a_min=0.3 needs 0.081 < m < 0.919 (necessary condition)
    lo = (1 - math.sqrt(1 - 0.3)) / 2
    check("centred bound interval", 0.081 < lo < 0.082 and 0.918 < 1 - lo < 0.919, str(lo))
    in_paper("centred bound quoted", "0.081", "0.919")
    wall_pre = sum("wall_constant" in r["false_positives"] for r in rows(pre + "e2_rates_vs_length.csv"))
    wall_post = sum("wall_constant" in r["false_positives"] for r in rows("e2_rates_vs_length.csv"))
    check("constant wall Periodic at 4 lengths pre-fix, 0 post", (wall_pre, wall_post) == (4, 0))
    in_paper("wall FP lengths quoted", "4 of 93")
    # E3 with periodicity on
    tot = lambda n: sum(int(r["flicker_transitions"]) for r in rows(n))
    mid = "pre_calibration_2026-09-28/"
    one = "pre_spending_2026-09-28/"
    for n, v in (("e3_sensitivity.csv", 3672), ("e3_sensitivity_periodic.csv", 3672),
                 (one + "e3_sensitivity_periodic.csv", 3792),
                 (mid + "e3_sensitivity_periodic.csv", 5472), (pre + "e3_sensitivity_periodic.csv", 7132)):
        check(f"E3 total flicker {n} == {v}", tot(n) == v, str(tot(n)))
        in_paper(f"E3 total flicker {v} quoted", str(v))
    e3p = {(r["graduate_prob"], r["demote_prob"], r["survival_decay"]): r for r in rows(one + "e3_sensitivity_periodic.csv")}
    e3s = {(r["graduate_prob"], r["demote_prob"], r["survival_decay"]): r for r in rows("e3_sensitivity_periodic.csv")}
    check("E3 spent: flicker and F1 identical to periodicity off in every row",
          all(e3s[k]["flicker_transitions"] == idx[k]["flicker_transitions"] and e3s[k]["final_f1"] == idx[k]["final_f1"] for k in idx))
    in_paper("E3 spent identical quoted", "identical to that with periodicity off")
    e3pp = {(r["graduate_prob"], r["demote_prob"], r["survival_decay"]): r for r in rows(pre + "e3_sensitivity_periodic.csv")}
    e3pm = {(r["graduate_prob"], r["demote_prob"], r["survival_decay"]): r for r in rows(mid + "e3_sensitivity_periodic.csv")}
    check("E3 P-on wide band 58 (single read-out) / centred 102 / pre 138", (e3p[("0.9", "0.3", "0.9")]["flicker_transitions"],
          e3pm[("0.9", "0.3", "0.9")]["flicker_transitions"],
          e3pp[("0.9", "0.3", "0.9")]["flicker_transitions"]) == ("58", "102", "138"))
    in_paper("E3 P-on 58 quoted", "58")
    in_paper("E3 P-on 102 quoted", "102")
    dif = [int(e3p[k]["flicker_transitions"]) - int(idx[k]["flicker_transitions"]) for k in idx]
    check("E3 single-read-out excess flicker 2..4 per row", (min(dif), max(dif)) == (2, 4), str((min(dif), max(dif))))
    in_paper("E3 P-on pre 138 quoted", "138")
    check("E3 P-on F1 equals P-off F1 in all rows", all(idx[k]["final_f1"] == e3p[k]["final_f1"] for k in idx))
    check("E3 P-on flicker higher in every row",
          all(int(e3p[k]["flicker_transitions"]) > int(idx[k]["flicker_transitions"]) for k in idx))
    in_paper("sigma(2) < p_grad", f"{1/(1+math.exp(-2)):.3f}")
    # uneven phase coverage: wall visible 8 of every 48 windows, T=24 -> 2 sin(8pi/24)/(8 sin(pi/24))
    loop = 2 * math.sin(8 * math.pi / 24) / (8 * math.sin(math.pi / 24))
    check("revisit-loop amplitude 1.66", f"{loop:.2f}" == "1.66", str(loop))
    in_paper("revisit-loop amplitude quoted", "1.66")
    # E1 with periodicity on at T=24: wall periodic in windows 29-40, i.e. t=28-39 (t = window-1)
    e1_last = max(int(r["window"]) for r in e1)
    check("E1 horizon ends at t=39", e1_last == 39, str(e1_last))
    check("E1 final window 40 inside leak", leak(e1_last + 1, 24, 2) >= 0.3)
    in_paper("E1 leak windows quoted", "windows 29\u201340")
    in_paper("E1 leak t range quoted", "t=28\u201339")
    e1p_pre = rows(pre + "e1_static_quality_periodic.csv")
    bad = {int(r["window"]) for r in e1p_pre if int(r["window"]) >= 3 and float(r["recall"]) < 1}
    check("pre-fix E1 P-on recall<1 exactly at t=28..39", bad == set(range(28, 40)), str(sorted(bad)))
    check("pre-fix E1 P-on final recall 0 in all runs",
          all(float(r["recall"]) == 0 for r in e1p_pre if int(r["window"]) == 39))
    e1p = rows("e1_static_quality_periodic.csv")
    check("post-fix E1 P-on identical to P-off", [(r["recall"], r["precision"], r["pred_static"]) for r in e1p]
          == [(r["recall"], r["precision"], r["pred_static"]) for r in e1])
    ntests = sum(len(re.findall(r"^TEST(?:_F)?\(", f.read_text(), re.M))
                 for f in (HERE.parents[1] / "strata_core/test").glob("*.cpp"))
    check("114 core gtest cases", ntests == 114, str(ntests))
    in_paper("114 tests quoted", "114 ")
    quoted = {int(n) for n in re.findall(r"(\d+)(?:~|\s)+(?:gtest cases|behaviou?r-level tests|unit tests)", text)}
    nnode = sum(len(re.findall(r"^TEST(?:_F)?\(", f.read_text(), re.M))
                for f in (HERE.parents[1] / "strata/test").glob("*.cpp"))
    check("every quoted test count is a current one", quoted <= {ntests, nnode}, str(sorted(quoted)))
    # ---- E0 calibration and the calibrated test (Proposition prop:chernoff) ----
    e0 = rows("e0_calibration_choice.csv")
    sel = [r for r in e0 if r["selected"] == "1"]
    check("E0 selects exactly delta=0.1", len(sel) == 1 and float(sel[0]["alpha"]) == 0.1)
    by0 = {float(r["alpha"]): r for r in e0}
    check("E0 rule: selected is the largest meeting 0.01",
          all((float(r["worst_null_rate"]) <= 0.01 and float(r["worst_pipeline_rate"]) <= 0.01) == (r["meets_target"] == "1") for r in e0)
          and max(a for a, r in by0.items() if r["meets_target"] == "1") == 0.1)
    for a, nq, pq in ((0.1, "0.0050", "0.007"), (0.2, "0.0149", "0.014")):
        check(f"E0 delta={a} rates", (f"{float(by0[a]['worst_null_rate']):.4f}", f"{float(by0[a]['worst_pipeline_rate']):.3f}") == (nq, pq))
        in_paper(f"E0 delta={a} null rate quoted", nq)
        in_paper(f"E0 delta={a} pipeline rate quoted", pq)
    pipe0 = [r for r in rows("e0_calibration_pipeline.csv") if r["alpha"] in ("0.1",)]
    worst = max(pipe0, key=lambda r: int(r["periodic"]))
    kw, nw = int(worst["periodic"]), int(worst["cells"])
    zq = 1.959964
    ph = kw / nw
    wil = (ph + zq * zq / (2 * nw) + zq * math.sqrt(ph * (1 - ph) / nw + zq * zq / (4 * nw * nw))) / (1 + zq * zq / nw)
    check("E0 worst pipeline 7 of 1000, Wilson upper 0.0144", (kw, nw, f"{wil:.4f}") == (7, 1000, "0.0144"), f"{kw}/{nw} {wil:.4f}")
    in_paper("E0 Wilson quoted", "7 of 1000 cells, has a 95\\% Wilson upper limit of 0.0144")
    check("E0 door median 22 -> 15", (by0[0.01]["door_p8_median_first_n"], by0[0.1]["door_p8_median_first_n"]) == ("22", "15"))
    in_paper("E0 door 22 to 15 quoted", "from 22 to 15")
    beta = [r for r in csv.DictReader(open(RESULTS / "e0_calibration_choice.csv")) if r["alpha"].startswith("#beta")][0]
    check("E0 Beta approximation worst 0.037", f"{float(beta['worst_null_rate']):.3f}" == "0.037")
    in_paper("E0 Beta 0.037 quoted", "0.037")
    src0 = (HERE / "src/e0_calibration.cpp").read_text()
    check("E0 seed base 20260928 (disjoint from 12345+[0,1500])", "kCalSeed = 20260928u" in src0 and 20260928 > 12345 + 1500)
    in_paper("E0 seed quoted", "20260928")
    check("code default periodic_false_alarm{0.2}", "periodic_false_alarm{0.2}" in hdr)
    check("code default periodic_alpha_spending{true}", "periodic_alpha_spending{true}" in hdr)
    for yf in ("grid2d.yaml", "voxel3d.yaml"):
        yt = (HERE.parents[1] / "strata/params" / yf).read_text()
        check(f"{yf} periodic_false_alarm: 0.2 + spending", "periodic_false_alarm: 0.2" in yt and "periodic_alpha_spending: true" in yt)
    node = (HERE.parents[1] / "strata/src/mapping_node.cpp").read_text()
    check("node default 0.2 + spending", '"periodic_false_alarm", 0.2' in node and '"periodic_alpha_spending", true' in node)
    # ---- E5 prose numbers (arXiv) ----
    src5 = (HERE / "src/e5_trajectory.cpp").read_text()
    check("E5 seeds 30260928 / 40260928 disjoint from E0, E1-E4, tests",
          "kCalSeed = 30260928u" in src5 and "kEvalSeed = 40260928u" in src5 and 30260928 > 20260928 + 100000)
    in_paper("E5 seeds quoted", "30260928")
    in_paper("E5 eval seeds quoted", "40260928")
    e5 = rows("e5_trajectory.csv")
    def wil(k, n, z=1.959964):
        p_ = k / n
        c = (p_ + z * z / (2 * n)) / (1 + z * z / n)
        h = z * math.sqrt(p_ * (1 - p_) / n + z * z / (4 * n * n)) / (1 + z * z / n)
        return max(0.0, c - h), c + h
    lo_, hi_ = wil(953, 2000)
    in_paper("E5 953 interval quoted", f"[{lo_:.4f}, {hi_:.4f}]")
    in_paper("E5 spent Wilson upper 0.0036", f"{wil(2, 2000)[1]:.4f}")
    sb = [r for r in e5 if r["rule"] == "single_readout" and r["kind"] == "bernoulli"]
    pw1 = max(float(r["per_window_rate"]) for r in sb)
    in_paper("E5 single per-window max", f"{pw1:.4f}")
    pw2 = max(float(r["per_window_rate"]) for r in e5 if r["rule"] == "spending" and r["kind"] == "bernoulli")
    check("E5 spent per-window max 3.9e-6", f"{pw2*1e6:.1f}" == "3.9")
    sh = max((r for r in sb if r["params"] == "shipped" and r["touch"] == "every"), key=lambda r: int(r["ever_1024"]))
    check("E5 shipped single 202 at m=0.3", (sh["ever_1024"], sh["m"]) == ("202", "0.3"))
    in_paper("E5 202 quoted", "202 of 2000")
    lv = [r for r in e5 if r["rule"] == "spending" and r["params"] == "e2" and r["touch"] == "every" and r["kind"] == "bernoulli" and r["m"] == "0.4"][0]
    check("E5 about 62 histories", round(float(lv["mean_lives"])) == 62)
    in_paper("E5 62 histories quoted", "about 62 histories")
    C = {(r["rule"], r["readout"], r["gt"]): r for r in rows("e5_confusion.csv")}
    check("E5 ever-P within 256: single 219, spent 1",
          (C[("single_readout", "256", "aperiodic")]["ever_periodic"], C[("spending", "256", "aperiodic")]["ever_periodic"]) == ("219", "1"))
    in_paper("E5 219 quoted", "219 of 2000")
    g = lambda k, col: int(C[("spending", "64", k)][col])
    gts = ("wall", "door", "aperiodic", "dynamic")
    nP = sum(g("door", c) for c in ("pred_S", "pred_P", "pred_T", "pred_U"))
    check("E5 Periodic precision 1 (no P outside doors)", sum(g(k, "pred_P") for k in gts) == g("door", "pred_P"))
    check("E5 misses = the 400 25%-duty doors", (g("door", "pred_P"), nP) == (800, 1200))
    dd = {(r["rule"], r["params"], r["door"]): r["median_first_n"] for r in rows("e5_doors.csv")}
    for door, ps_, a, b in (("p8_4on4off", "e2", "15", "25"), ("p4_2on2off", "e2", "13", "20"), ("p8_3on5off", "e2", "21", "31"),
                            ("p8_4on4off_noisy", "e2", "26", "48"), ("p24_12on12off_noisy", "shipped", "25", "40")):
        check(f"E5 door {door} {a}->{b}", (dd[("single_readout", ps_, door)], dd[("spending", ps_, door)]) == (a, b))
        in_paper(f"E5 door {door} quoted", f"from {a} to {b}")
    check("E5 shipped 12/12 door stays 24", (dd[("single_readout", "shipped", "p24_12on12off")], dd[("spending", "shipped", "p24_12on12off")]) == ("24", "24"))
    check("E5 no door run fails", all(r["never"] == "0" for r in rows("e5_doors.csv")))
    check("delta_64 = 3.8e-4", f"{0.2 * 8 / (64 * 65) * 1e4:.1f}" == "3.8")
    Bf = lambda r: 2 * r * math.exp(1 - 2 * r)
    lo_, hi_ = 0.5, 50.0
    for _ in range(200):
        mid_ = 0.5 * (lo_ + hi_)
        lo_, hi_ = (mid_, hi_) if Bf(mid_) > 0.1 / 3 else (lo_, mid_)
    rstar = hi_
    check("r* = 3.115", f"{rstar:.3f}" == "3.115", str(rstar))
    in_paper("r* quoted", "3.115")
    for n, q in ((8, "0.883"), (64, "0.312")):
        check(f"a*({n}) = {q}", f"{math.sqrt(2 * rstar / n):.3f}" == q)
        in_paper(f"a*({n}) quoted", q)
    check("Chernoff/Gaussian ratio 2e r* ~ 17", round(2 * math.e * rstar) == 17)
    in_paper("ratio 17 quoted", "about 17")


if len(TARGETS) > 1:
    target, text = TARGETS[1]
    guard_arxiv()


# Code that the paper pins: the tree ids of strata_core/, strata/ and scripts/ at
# commit 708d62f (strata_core 0.2.0: evidence_model legacy | sprt). A change to any of them
# must come with a new pin in the paper. The pin moved twice on purpose: from 312b09c
# (v0.1 engine, 11 October 2026) to a7559d4 (19 October 2026, phase P4a of the v2 plan; the evidence model sprt was added in 0f5befc, 18 October 2026),
# and, in two further steps on 22 October 2026 (5c20bba, 708d62f), only the node status text and
# the two parameter-file comments changed; the strata_core and scripts trees are the same as at a7559d4.
PIN = "708d62f"
PIN_TREES = {"strata_core": "c780639aec9402da556bd33b91ff9acbc06c0cd0",
             "strata": "bf95ea57f234a84f82d42f342e5a62947d940be2",
             "scripts": "3a7ef6330965552ccf5a72f7f7ad7c9c4cfd1b2a"}
# Core regression tests by the defect or rule they cover (the paper's 10 / 9 / 8).
CENTRING_TESTS = ("AlwaysOccupiedHasZeroAmplitudeAtEveryLengthT8", "AlwaysOccupiedHasZeroAmplitudeAtEveryLengthT24",
                  "ConstantCellUnderUnevenPhaseCoverageHasZeroAmplitude", "SquareWaveDetectedAtNonMultiplesOfPeriod",
                  "AlwaysHitWallNeverPeriodicT8", "AlwaysHitWallNeverPeriodicShippedDefaultsT24",
                  "RevisitLoopWall8of48StaysStatic", "GenuineDoorStillPeriodicE2Params",
                  "PeriodicDemotionNeedsContradictingFreeEvidence", "GraduatedDoorIsDemotedOncePeriodic")
SIGNIFICANCE_TESTS = ("ChernoffBoundIsAValidTailBound", "BernoulliNoiseRarelyPeriodicT8",
                      "BernoulliNoiseRarelyPeriodicShippedT24", "UncalibratedAmplitudeAloneWouldFail",
                      "SquareWaveDoorsStillDetected", "ConstantCellNeverPeriodicUnderAnySampling",
                      "BelowPeriodGateNeverPeriodic", "BernoulliClutterRarelyPeriodicWithPruning",
                      "CalibrationCanBeDisabledForLegacyAmplitudeRule")
SPENDING_TESTS = ("LevelsAreZeroBelowGateAndSumToAlpha", "PerReadOutRuleIsNotTrajectoryValidButSpendingIs",
                  "DoorsStillDetectedLater", "AmplitudeOnlyRuleUnaffected", "AlphaSpendingIsTheShippedRule",
                  "GraduatedDoorIsDemotedOncePeriodicWithSpending",
                  "NoisyWallRarelyEverDemotedByPeriodicPathWithSpending", "PrunedClutterRarelyEverPeriodicWithSpending")


def git(*args):
    import subprocess
    try:
        r = subprocess.run(["git", "-C", str(HERE), *args], capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else None
    except OSError:
        return None


STDLIB = {}


def wilson95(k, n):
    z = 1.959964
    p_ = k / n
    c = (p_ + z * z / (2 * n)) / (1 + z * z / n)
    hw = z * math.sqrt(p_ * (1 - p_) / n + z * z / (4 * n * n)) / (1 + z * z / n)
    return max(0.0, c - hw), c + hw


def guard_code(full):
    """Facts about the pinned code, the node and the reproduction that both paper
    copies state. full=True adds the checks of text that only the arXiv package has."""
    import hashlib
    import math
    root = HERE.parents[1]

    # ---- the pin ----
    in_paper("pin commit quoted", PIN)
    pin_date = git("show", "-s", "--format=%ad", "--date=short", PIN)
    if pin_date is not None:
        check("pin date quoted in the text is the commit date", pin_date == "2026-10-22", str(pin_date))
    if git("rev-parse", "--is-inside-work-tree") == "true":
        for d, want in PIN_TREES.items():
            got = git("rev-parse", f"HEAD:{d}")
            check(f"{d}/ is the tree of {PIN}", got == want, f"HEAD:{d} is {got}")
        check("working tree: pinned directories unmodified",
              git("status", "--porcelain", "--", *[":/" + d for d in PIN_TREES]) == "",
              "uncommitted changes under a pinned directory")
    else:
        passes.append(f"[{target}] pin trees not checked (not a git checkout)")

    # ---- tests ----
    def cases(p):
        return re.findall(r"^TEST(?:_F)?\(\s*\w+\s*,\s*(\w+)\s*\)", p.read_text(), re.M)
    core = [n for f in sorted((root / "strata_core/test").glob("*.cpp")) for n in cases(f)]
    for label, group, word in (("centring", CENTRING_TESTS, "ten"), ("significance", SIGNIFICANCE_TESTS, "nine"),
                               ("spending", SPENDING_TESTS, "eight")):
        words = {10: "ten", 9: "nine", 8: "eight"}
        check(f"{len(group)} {label} regression tests exist in strata_core/test",
              all(core.count(n) == 1 for n in group) and words[len(group)] == word,
              str([n for n in group if core.count(n) != 1]))
    check("the three regression groups do not overlap",
          len(set(CENTRING_TESTS) | set(SIGNIFICANCE_TESTS) | set(SPENDING_TESTS)) == 27)
    for w in ("ten regression tests", "nine for", "eight for the spent level"):
        in_paper(f"regression count quoted: {w!r}", w, w.replace("ten", "Ten"))
    node = {f.stem: len(cases(f)) for f in sorted((root / "strata/test").glob("*.cpp"))}
    check("71 node gtest cases", sum(node.values()) == 71, str(node))
    in_paper("71 node gtest cases quoted", "71 gtest cases")
    for stem in ("test_window_clock", "test_tf_wait", "test_input_status", "test_rate_monitor", "test_map_writer"):
        m = re.search(stem.replace("_", r"\\?_") + r"\}?`? \((\d+) cases\)", text)
        check(f"{stem} case count quoted is {node[stem]}", bool(m) and int(m.group(1)) == node[stem],
              m.group(1) if m else "not quoted")

    # ---- node constants behind Table tab:node ----
    hpp = (root / "strata/include/strata/mapping_node.hpp").read_text()
    cpp = (root / "strata/src/mapping_node.cpp").read_text()
    tfw = (root / "strata/include/strata/tf_wait.hpp").read_text()
    wck = (root / "strata/include/strata/window_clock.hpp").read_text()
    ist = (root / "strata/include/strata/input_status.hpp").read_text()
    rmo = (root / "strata/include/strata/rate_monitor.hpp").read_text()
    mwr = (root / "strata/include/strata/map_writer.hpp").read_text()
    for label, tok, src in (
            ("TF wait 0.1 s of ROS time", "kTfWaitS = 0.1;", hpp), ("TF wait cap 1 s", "kTfWaitCapS = 1.0;", hpp),
            ("clock frozen after 1 s", "kClockFrozenS = 1.0;", hpp), ("clock stall WARN after 3 s", "kClockStallS = 3.0;", hpp),
            ("16 sensor frames", "kMaxSensors = 16;", hpp), ("frame forgotten after 10 closes", "kIdleWindowsToForget = 10;", hpp),
            ("step grace 20 ms", "step_grace_s{0.02}", tfw), ("re-anchor half a window back", "newest_ns_ - period_ns_ / 2", wck),
            ("re-anchor 10^6 windows ahead", "kMaxGapWindows = 1000000;", wck),
            ("TF failure named after 1 s", "kTfNoteAfterS = 1.0;", ist),
            ("no-window-closing WARN: 10 periods and 2 s", "since_close > 10.0 * window_period_s_ && since_close > 2.0", cpp),
            ("zero stamp dropped with time windows", 'if (window_mode_ == "time") return false;', cpp),
            ("diagnostics: wall timer, 1 s", "create_wall_timer(std::chrono::seconds(1)", cpp),
            ("20th-percentile interval", "v.size() / 5;", rmo),
            ("PGM shades 205 / 0 / 100 / 254", "return 205;", mwr), ("PGM static 0", "if (v >= 100) return 0;", mwr),
            ("PGM periodic/transient 100", "if (v >= 50) return 100;", mwr), ("PGM free 254", "return 254;", mwr),
            ("map thresholds 0.65 / 0.196", "occupied_thresh: 0.65\\nfree_thresh: 0.196", mwr),
            ("checksum keys", "strata_image_sha256", mwr),
            ("backend string not validated", "} else {  // voxel3d", cpp),
            ("MapBackend::closeWindows", "virtual void closeWindows(int k) = 0;",
             (root / "strata_core/include/strata_core/map_backend.hpp").read_text())):
        check(f"code: {label}", tok in src)
    mb_hpp = (root / "strata_core/include/strata_core/map_backend.hpp").read_text()
    check("MapBackend has five pure virtual methods and one non-pure counter (intermittentCellCount, returns 0)",
          mb_hpp.count(") = 0;") + mb_hpp.count(") const = 0;") == 5
          and "virtual std::size_t intermittentCellCount() const { return 0; }" in mb_hpp)
    in_paper("six MapBackend methods quoted", "six methods")

    # ---- shipped YAML files against the code defaults and the parameter table ----
    def yaml_params(name):
        out = {}
        for line in (root / "strata/params" / name).read_text().splitlines():
            m = re.match(r"\s{4}(\w+):\s*([^#]*?)\s*(#.*)?$", line)
            if m:
                out[m.group(1)] = m.group(2).strip('"')
        return out
    y2, y3 = yaml_params("grid2d.yaml"), yaml_params("voxel3d.yaml")
    engine = ("layer_interval", "l_hit", "l_miss", "l_min", "l_max", "survival_decay", "graduate_prob", "demote_prob",
              "min_observations", "prune_prob", "enable_periodicity", "periodic_amplitude_min", "periodic_false_alarm",
              "periodic_alpha_spending", "period_windows", "n_harmonics")
    lm = (root / "strata_core/include/strata_core/layered_map.hpp").read_text()
    pm = (root / "strata_core/include/strata_core/periodicity.hpp").read_text()
    for k in engine:
        check(f"YAML files agree on {k}", y2[k] == y3[k], f"{y2[k]} vs {y3[k]}")
        m = re.search(r"\b%s\{([^}]*)\}" % k, lm + pm)
        check(f"struct default of {k} equals the YAML value", bool(m) and float(m.group(1).replace("true", "1").replace("false", "0"))
              == float(y2[k].replace("true", "1").replace("false", "0")), f"{m.group(1) if m else None} vs {y2[k]}")
        m = re.search(r'"%s",\s*([-\w.]+)\)' % k, cpp)
        check(f"node default of {k} equals the YAML value", bool(m) and float(m.group(1).replace("true", "1").replace("false", "0"))
              == float(y2[k].replace("true", "1").replace("false", "0")), f"{m.group(1) if m else None} vs {y2[k]}")
    for k in ("window_mode", "window_period_s", "expected_scan_rate_hz", "rate_warn_drop_fraction", "input_timeout_s",
              "startup_timeout_s", "publish_period"):
        check(f"YAML files agree on node parameter {k}", y2[k] == y3[k])
    check("shipped window_mode time, code default scans",
          y2["window_mode"] == "time" and 'declare_parameter<std::string>("window_mode", "scans")' in cpp)
    check("node parameter values quoted in the paper are the YAML ones",
          (y2["window_period_s"], y2["expected_scan_rate_hz"], y2["rate_warn_drop_fraction"], y2["input_timeout_s"],
           y2["startup_timeout_s"], y2["grid_width"], y2["grid_height"], y2["grid_resolution"], y2["grid_origin_x"],
           y3["voxel_size"], y2["publish_period"]) == ("1.0", "0.0", "0.05", "5.0", "30.0", "400", "400", "0.05", "-10.0", "0.2", "1.0"))
    cells = int(y2["grid_width"]) * int(y2["grid_height"])
    in_paper("cells of the shipped grid quoted", f"{cells:,}")
    if full:
        app = (ARXIV / "sections" / "appendix.tex").read_text()
        rows_ = dict(re.findall(r"^\\code\{([^}]*)\} & [^&]* & ([^&]*) & ", app, re.M))
        rows_ = {k.replace("\\_", "_"): v.strip().replace("$", "") for k, v in rows_.items()}
        for k in engine:
            if k in ("l_min", "l_max"):
                continue
            check(f"parameter table default of {k}", k in rows_ and float(rows_[k].replace("true", "1").replace("false", "0"))
                  == float(y2[k].replace("true", "1").replace("false", "0")), f"{rows_.get(k)} vs {y2[k]}")
        check("parameter table: clamp bounds", "$-5.0$, $5.0$" in app and (y2["l_min"], y2["l_max"]) == ("-5.0", "5.0"))

    # ---- E4: deterministic columns, and every timing file that exists ----
    e4 = rows("e4_throughput.csv")
    check("E4 repeats 15 in every row", {r["repeats"] for r in e4} == {"15"})
    in_paper("E4 15 runs quoted", "15 times")
    check("E4 harness: 15 repeats, one warm-up",
          "kRepeats = 15;" in (HERE / "src/e4_throughput.cpp").read_text()
          and "// warm-up, discarded" in (HERE / "src/e4_throughput.cpp").read_text())

    def ratios(path):
        with open(path, newline="") as fh:
            m = {(r["backend"], r["extent"]): r for r in csv.DictReader(fh) if not r["backend"].startswith("#")}
        return ([float(m[("voxel3d", x)]["us_per_integrate"]) / float(m[("grid2d", x)]["us_per_integrate"]) for x in ("100", "250", "500")],
                {k: v["final_cells"] for k, v in m.items()})
    with open(RESULTS / "e4_exploratory" / "index.csv", newline="") as fh:
        idx = list(csv.DictReader(fh))
    files = [RESULTS / "e4_exploratory" / r["file"] for r in idx]
    on_disk = sorted(p.name for p in (RESULTS / "e4_exploratory").glob("*.csv") if p.name != "index.csv")
    check("E4 exploratory index lists every file", sorted(r["file"] for r in idx) == on_disk)
    recs = sorted(p for p in (RESULTS / "e4_recordings").glob("*/summary.csv")) if (RESULTS / "e4_recordings").is_dir() else []
    cells = {(r["backend"], r["extent"]): r["final_cells"] for r in e4}
    allr = []
    for p in files + recs:
        rr, cc = ratios(p)
        allr.append(rr)
        check(f"E4 {p.parent.name}/{p.name}: grid2d cheaper at every extent", min(rr) > 1.0, str(rr))
        check(f"E4 {p.parent.name}/{p.name}: cell counts equal the reference", cc == cells)
    valid = []
    for p in recs:
        env = dict(l.split(": ", 1) for l in (p.parent / "env.txt").read_text().splitlines() if ": " in l)
        la, lb = float(env["loadavg_before"].split()[0]), float(env["loadavg_after"].split()[0])
        ok = la < float(env["max_load_before"]) and lb < float(env["max_load_after"])
        check(f"E4 recording {p.parent.name}: valid flag follows the load rule", (env["valid"] == "yes") == ok, f"{la} {lb} {env['valid']}")
        check(f"E4 recording {p.parent.name}: thresholds 0.50 / 1.50", (env["max_load_before"], env["max_load_after"]) == ("0.50", "1.50"))
        check(f"E4 recording {p.parent.name}: pinned strata_core, 15 repeats per configuration",
              env["strata_core_tree"] == PIN_TREES["strata_core"]
              and len((p.parent / "timings.csv").read_text().splitlines()) == 1 + 6 * 15)
        if env["valid"] == "yes":
            valid.append((p, env))
    msh = (HERE / "measure_e4.sh").read_text()
    check("measure_e4.sh thresholds 0.50 / 1.50", "MAX_LOAD_BEFORE=0.50" in msh and "MAX_LOAD_AFTER=1.50" in msh)
    if full:   # the E4 sentences are generated for the manuscript; the companion gives the direction only
        in_paper("E4 admission threshold quoted", "load average is below 0.50")
        in_paper("E4 after-run threshold quoted", "afterwards is 1.50 or more")
        in_paper("E4 number of timing files quoted", f"In all {len(allr)} timing files")
        if len(valid) < 3:
            in_paper("E4 exploratory ratio range at 100^2", f"({min(r[0] for r in allr):.1f}–{max(r[0] for r in allr):.1f}× at 100^2)")
            check("E4: no latency in the paper without three valid recordings",
                  "integrate} [min, max]" not in (ARXIV / "tables" / "e4.tex").read_text())
        else:
            in_paper("E4 number of valid recordings quoted", f"{len(valid)} quiet-host recordings")

    # ---- E6: shipped-default confusion and the Static share against m ----
    c5 = {(r["rule"], r["readout"], r["gt"]): r for r in rows("e5_confusion.csv")}
    c6 = {(r["params"], r["readout"], r["gt"]): r for r in rows("e6_confusion.csv")}
    cols = ("pred_S", "pred_P", "pred_T", "pred_U")
    check("E6 'e2' rows equal E5's spent rows (same code path and seeds)",
          all(tuple(c6[("e2", q, g)][k] for k in cols) == tuple(c5[("spending", q, g)][k] for k in cols)
              for q in ("64", "256") for g in ("wall", "door", "aperiodic", "dynamic")))
    S = lambda p, q, g: int(c6[(p, q, g)]["pred_S"])
    for p_, lab in (("e2", "E2"), ("e2_decay097", "E2 decay 0.97"), ("shipped", "shipped")):
        k_ = S(p_, "64", "aperiodic")
        lo_, hi_ = wilson95(k_, 2000)
        check(f"E6 {lab}: 2000 aperiodic cells", sum(int(c6[(p_, '64', 'aperiodic')][k]) for k in cols) == 2000)
        in_paper(f"E6 {lab} false Static {k_} of 2000", f"{k_} of 2000")
        if p_ != "e2_decay097":
            in_paper(f"E6 {lab} false Static share with interval", f"{k_/2000:.3f} [{lo_:.3f}, {hi_:.3f}]")
    in_paper("E6 shipped false Static after 256", f"{S('shipped', '256', 'aperiodic')} after 256 windows")
    in_paper("E6 ever-Static within 256 windows quoted (macro SqEsixEverTw)", "at some window within 256 windows")
    src7 = (HERE / "src/e7_sprt_oc.cpp").read_text()
    check("E7 seed and generator", "kSeed = 70261018ull" in src7 and "std::mt19937_64" in src7)
    in_paper("E7 seed quoted", "E7 draws from 70261018 upward with")
    in_paper("E6 shipped dynamic Static", f"{S('shipped', '64', 'dynamic')} of 1000 dynamic cells")
    in_paper("E6 shipped doors Periodic at 64", f"{c6[('shipped', '64', 'door')]['pred_P']} are Periodic after 64 windows")
    check("E6 shipped doors all Periodic at 256", c6[("shipped", "256", "door")]["pred_P"] == "1200")
    check("E6 E2 decay 0.97: all 1200 doors Periodic", c6[("e2_decay097", "64", "door")]["pred_P"] == "1200")
    check("E5 E2: 800 of 1200 doors Periodic, 1 dynamic cell Static", (c6[("e2", "64", "door")]["pred_P"], S("e2", "64", "dynamic")) == ("800", 1))
    sw = {(r["params"], f"{float(r['m']):.2f}"): r for r in rows("e6_static_vs_m.csv")}
    check("E6 sweep: 2000 cells per point, 19 rates, 4 sets",
          len(sw) == 76 and all(r["cells"] == "2000" for r in sw.values()))
    lmp = {"lhit": 0.85, "lmiss": -0.4, "lam": 0.97, "pg": 0.8}
    mdag = lambda lh, lm_, lam, pg: (math.log(pg / (1 - pg)) * (1 - lam) / lam - lm_) / (lh - lm_)
    for p_, args in (("shipped", (0.85, -0.4, 0.97, 0.8)), ("shipped_sym", (0.85, -0.85, 0.97, 0.8)),
                     ("e2", (1.0, -1.0, 1.0, 0.9)), ("e2_decay097", (1.0, -1.0, 0.97, 0.9))):
        check(f"E6 mean-field threshold of {p_} recomputed", abs(mdag(*args) - float(sw[(p_, '0.50')]["mean_field_threshold"])) < 1e-5)
    in_paper("m-dagger shipped", f"m^\\dagger={mdag(0.85, -0.4, 0.97, 0.8):.3f}")
    in_paper("m-dagger shipped symmetric", f"m^\\dagger to {mdag(0.85, -0.85, 0.97, 0.8):.3f}")
    in_paper("m-dagger E2", f"m^\\dagger={mdag(1.0, -1.0, 1.0, 0.9):.1f}")
    in_paper("m-dagger shipped in limitations", f"{mdag(0.85, -0.4, 0.97, 0.8):.3f} with the v0.1 defaults", f"{mdag(0.85, -0.4, 0.97, 0.8):.3f} with the shipped defaults")
    st = lambda p, m: sw[(p, m)]["static_256"]
    in_paper("E6 shipped 0.25", f"{st('shipped', '0.25')} of 2000 Bernoulli(0.25) cells")
    in_paper("E6 shipped 0.30-0.50",
             f"{st('shipped', '0.30')} at m=0.30, {st('shipped', '0.35')} at 0.35, {st('shipped', '0.40')} at 0.40 and {st('shipped', '0.50')} at 0.50")
    in_paper("E6 shipped ever Static at 0.10", f"{sw[('shipped', '0.10')]['ever_static_256']} of 2000 Bernoulli(0.10) cells")
    in_paper("E6 symmetric at 0.50", f"the count at m=0.50 to {st('shipped_sym', '0.50')}")
    in_paper("E6 walls: symmetric against shipped", f"{st('shipped_sym', '0.60')} of 2000 are then Static, against {st('shipped', '0.60')}")
    in_paper("E6 walls and clutter under shipped", f"{st('shipped', '0.60')} and {st('shipped', '0.50')} of 2000 such cells")
    def width(p_):
        ms = [f"{0.05 * j:.2f}" for j in range(1, 20)]
        lo_ = max(m for m in ms if int(st(p_, m)) < 200)
        hi_ = min(m for m in ms if int(st(p_, m)) > 1900)
        return f"{float(hi_) - float(lo_):.2f}"
    check("E6 transition widths 0.30 (shipped) and 0.35 (others)",
          (width("shipped"), width("e2"), width("e2_decay097"), width("shipped_sym")) == ("0.30", "0.35", "0.35", "0.35"),
          str([width(x) for x in ("shipped", "e2", "e2_decay097", "shipped_sym")]))
    in_paper("E6 widths quoted", "0.30 wide with the shipped defaults and 0.35 wide with the other three sets")
    src6 = (HERE / "src/e6_default_confusion.cpp").read_text()
    check("E6 seeds", "kSweepSeed = 50260928u" in src6 and "kEvalSeed = 40260928u" in src6)
    in_paper("E6 sweep seed quoted", "50260928")
    e5t = [r for r in rows("e5_trajectory.csv") if r["rule"] == "spending" and r["params"] == "e2" and r["touch"] == "every" and r["m"] == "0.6" and r["kind"] == "bernoulli"][0]
    check("E5: Bernoulli(0.6) cells begin 5 histories", round(float(e5t["mean_lives"])) == 5)
    in_paper("E5 5 histories quoted", "begin 5 histories")

    # ---- standard-library sensitivity record ----
    sl = (RESULTS / "stdlib_libcxx" / "summary.txt").read_text().splitlines()
    diff = sorted(l.split(": ")[1] for l in sl if l.startswith("different: "))
    same = sorted(l.split(": ")[1] for l in sl if l.startswith("identical: "))
    check("stdlib record: made with the pinned strata_core", f"strata_core_tree: {PIN_TREES['strata_core']}" in sl)
    check("stdlib record: differing files are kept", all((RESULTS / "stdlib_libcxx" / f).exists() for f in diff))
    STDLIB.update(diff=diff, same=same, lines=sl)
    # The legacy arm (the 19 files of the first release's harness) is what the pinned text
    # reports; the sprt-arm files and E7 are new and counted beside it.
    is_new = lambda n: n.endswith("_sprt.csv") or n.startswith("e7_")
    same_l, diff_l = [n for n in same if not is_new(n)], [n for n in diff if not is_new(n)]
    same_n, diff_n = [n for n in same if is_new(n)], [n for n in diff if is_new(n)]
    STDLIB.update(same_new=same_n, diff_new=diff_n)
    in_paper("stdlib: identical / total quoted (legacy arm)", f"{len(same_l)} of the {len(same_l) + len(diff_l)} seeded result files")
    in_paper("stdlib: identical / total quoted (sprt arm and E7)", f"{len(same_n)} of the {len(same_n) + len(diff_n)} result files of the sequential-test arm and E7")
    check("stdlib: the four differing files of the legacy arm", diff_l == ["e0_calibration_doors.csv", "e1_static_quality.csv",
                                                     "e1_static_quality_periodic.csv", "e6_confusion.csv"], str(diff_l))
    check("stdlib: E7 does not depend on the standard library", all(n.startswith("e7_") for n in same_n if n.startswith("e7_")) and
          not any(n.startswith("e7_") for n in diff_n), str(diff_n))
    check("stdlib: every seeded result file was compared",
          sorted(same + diff) == sorted(f.name for f in RESULTS.glob("*.csv") if f.name != "e4_throughput.csv"))
    clang = re.search(r"clang version (\d+\.\d+\.\d+)", sl[0]).group(1)
    in_paper("stdlib: clang version quoted", f"clang {clang}")
    lc = lambda n: [r for r in csv.DictReader(open(RESULTS / "stdlib_libcxx" / n)) if not r[list(r)[0]].startswith("#")]
    fin = lambda rs: {(r["backend"], r["density"]): r for r in rs if r["window"] == "39"}
    a_, b_ = fin(lc("e1_static_quality.csv")), fin(rows("e1_static_quality.csv"))
    ga, gb = a_[("grid2d", "high")], b_[("grid2d", "high")]
    in_paper("stdlib: E1 grid2d high under libc++", f"{float(ga['precision']):.4f} with {ga['pred_static']} predicted cells instead of "
                                                   f"{float(gb['precision']):.4f} with {gb['pred_static']}")
    check("stdlib: every other final E1 value unchanged",
          all((a_[k]["precision"], a_[k]["recall"], a_[k]["pred_static"]) == (b_[k]["precision"], b_[k]["recall"], b_[k]["pred_static"])
              for k in a_ if k != ("grid2d", "high")) and ga["recall"] == gb["recall"])
    d_a = {(r["alpha"], r["door"]): r["median_first_n"] for r in lc("e0_calibration_doors.csv")}
    d_b = {(r["alpha"], r["door"]): r["median_first_n"] for r in rows("e0_calibration_doors.csv")}
    moved = sorted((k, d_b[k], d_a[k]) for k in d_a if d_a[k] != d_b[k])
    check("stdlib: E0 door medians move by one window", all(abs(int(a) - int(b)) == 1 for _, b, a in moved), str(moved))
    in_paper("stdlib: number of E0 door medians that move", f"{('zero', 'one', 'two', 'three', 'four', 'five', 'six')[len(moved)]} of the "
                                                             f"{len(d_a)} E0 door medians")
    check("stdlib: the quoted E0 examples", (("0.1", "p8_3on5off"), "20", "21") in moved and (("0.1", "p8_4on4off_noisy"), "23", "24") in moved)
    in_paper("stdlib: E0 door medians quoted", "20 to 21 and 23 to 24")
    ch_a = open(RESULTS / "e0_calibration_choice.csv").read()
    check("stdlib: E0 choice file identical under libc++", "e0_calibration_choice.csv" in same and "e5_calibration_choice.csv" in same)
    check("stdlib: the sprt-arm choice files identical under libc++",
          "e0_calibration_choice_sprt.csv" in same and "e5_calibration_choice_sprt.csv" in same, str(diff_n))
    x6 = {(r["params"], r["readout"], r["gt"]): r for r in lc("e6_confusion.csv")}
    in_paper("stdlib: E6 shipped false Static under libc++",
             f"{x6[('shipped', '64', 'aperiodic')]['pred_S']} instead of {c6[('shipped', '64', 'aperiodic')]['pred_S']} of the 2000")

    # ---- byte-for-byte reproduction log (keyed on the seeded result files) ----
    h = hashlib.sha256()
    for f in sorted(RESULTS.glob("*.csv")):
        if f.name != "e4_throughput.csv":
            h.update(f.read_bytes())
    log = [l for l in (HERE / "reproduction_log.txt").read_text().splitlines() if l.startswith("REPRODUCED")]
    cur = [l for l in log if f"seeded results sha256 {h.hexdigest()}" in l]
    check("reproduction log: at least one line for the committed seeded results", len(cur) >= 1, h.hexdigest())
    for l in cur:
        check("reproduction log line: pinned strata_core tree", f"strata_core tree {PIN_TREES['strata_core']}" in l, l[:80])
        m = re.search(r"\| [^|]*?(\d+\.\d+\.\d+)\s*\|", l)
        in_paper(f"reproduction compiler GCC {m.group(1)} quoted", f"GCC {m.group(1)}")


for target, text in TARGETS:
    text = " ".join(text.split())   # the Markdown copy wraps lines inside phrases
    guard_code(full=(target == "arxiv_ref"))



# ------------------------------------------------------------------ numeral coverage
# Every numeral of the hand-written text of the arXiv package must be, at its own
# location, (a) inside a string that a check derived from the result files or the code
# ("source": an in_paper() string above or a RULE below), or (b) matched by a structural
# rule, which says why the numeral is not a measured value. Anything else fails.
# Generated files (tables/, figures/data/, figures/e4_cost.tex) are compared with a
# fresh regeneration instead. Source always wins over structural.
MASK = re.compile(r"(?<!\\)%.*|\\(?:ref|label|cite[tp]?|eqref|input|url|nolinkurl|includegraphics)\*?(?:\[[^\]]*\])*\{[^}]*\}")
NUMERAL = re.compile(r"(?<![A-Za-z_\\.\d])\d+(?:,\d{3})*(?:\.\d+)?(?!\d)")
MATH = "inline or displayed mathematics: symbol, index, exponent, design constant written as symbol = value, or a constant derived in the text"
STRUCT = [
    (r"sections/", r"\\begin\{(equation|algorithm|proof|proposition|remark)\}.*?\\end\{\1\}", MATH),
    (r"sections/4_experiments", r"\\noindent\\textbf\{Scenarios and hardware\.\}.*?(?=\\subsection)",
     "scenario size or grid restated by hand from the harness source named in the sentence (not parsed)"),
    (r"sections/", r"\d+\\?%[ -]*(?:duty|flipped|flips|Wilson|intervals?|of (?:its|their|them)|upper)|Wilson 95|95\\?% (?:interval|upper)|"
                   r"\d+\\,\\?% (?:loss|of the scans)|below \d+\\?% to above \d+\\?%|at least \d+\\,\\?%",
     "duty cycle, share or confidence level that defines a scenario, a threshold of a check or an interval"),
    (r"sections/", r"\d+-on/\d+-off|\d+-bit|\d+-byte|x86-64|SHA.256|\b[23]D\b|6-DoF|ROS 2|C\+\+17|Bernoulli\(\d?\.\d+\)|"
                   r"\d+ seeds of \d+|\d+th-percentile|O\(1\)|depth \d+|p\{\d?\.\d+\\linewidth\}|\d-on|\d+-window run",
     "name of a scenario, data type, standard or layout width"),
    (r"sections/", r"\(20\d\d\)|Two 2026 works|(?:18|19|22) October 2026|\\code\{0f5befc\}|v0\.1\.0|GCC \d+\.\d+\.\d+|clang \d+\.\d+\.\d+|Apache-2\.0",
     "year, date, version or licence identifier (versions are also checked against the records)"),
    (r"sections/", r"\\sname\\ 0\.2|\\sname\} 0\.2|[Vv]ersion 0\.2",
     "version of the engine (checked against strata_core/package.xml and CMakeLists.txt)"),
    (r"sections/", r"(?:KITTI|Keble(?: sequences?)?|pairs) \d+(?:/\d+)?(?:(?:,| and| to) \d+(?:/\d+)?)*",
     "identifier of a dataset sequence or of a pair of sequences"),
    (r"sections/(?:0_abs|3b_|4b_|4c_|5_disc|appendix)",
     r"ranges \d+ to \d+ m|\d+(?:\.\d+)?\s?(?:m|Hz|GB)(?![A-Za-z_])|\d+(?:\.\d+)?\s?s(?![A-Za-z_])|\d+(?: and \d+)?\s(?:scans|frames|beams)(?![A-Za-z])",
     "dataset configuration (resolution, window length, rate, range, beams, memory limit) restated by hand from PREREG_P1B.md and PREREG_P3.md; the files are checked for the strings"),
    (r"sections/(?:3b_|4b_|4c_)",
     r"\d+th percentile|at least 100 cells|at least 0\.65 or at most 0\.35|multiple of 16|at most 3\\% of cells",
     "threshold or definition of a pre-registered rule, restated by hand from PREREG_P1.md, PREREG_P1B.md and PREREG.md; the files are checked for the strings"),
    (r"sections/", r"lag-1", "name of a statistic"),
    (r"sections/appendix", r"items 25 and 30|\(Amendment 3\)|, section 5\)",
     "number of an item, amendment or section of a committed record (results_p1b/deviations.md, PREREG_P3.md, results_p3/P3_REPORT.md; the files are checked for the strings)"),
    (r"sections/", r"95\\% percentile interval", "confidence level that defines an interval"),
    (r"sections/", r"Eqs?\. \(\d+\)(?: and \(\d+\))?|Lemma \d+", "number of an equation or lemma of a cited source"),
    (r"sections/appendix", r"\\begin\{table\}\[H\]\s*\\centering\s*\\caption\{Symbols used.*?\\end\{table\}",
     "notation table"),
    (r"sections/appendix", r"Unknown & Transient & .*?\\bottomrule", "transition table: conditions written as formulas"),
]
RULES = []   # (file pattern, regex with groups, expected values): filled by guard_rules()


def guard_rules():
    """Sentences whose numbers no in_paper() string covers: (file, regex, values from source).
    A group must equal the source value as a string, or as a number when both parse."""
    R = lambda f, rx, *vals: RULES.append((f, rx, vals))
    root = HERE.parents[1]
    # ---- facts that the structural categories of the v2 sections restate (checked by string) ----
    pr = lambda n: (HERE / "realdata" / n).read_text()
    check("engine version 0.2.0 (package.xml and CMakeLists.txt)", "<version>0.2.0</version>" in (root / "strata_core/package.xml").read_text()
          and "VERSION 0.2.0" in (root / "strata_core/CMakeLists.txt").read_text())
    for fn, needles in (
            ("PREREG_P1B.md", ("W = 15", "1.0 s at 15 Hz", "0.05", "ceil_0.05", "smallest multiple of 16", "0.10 m", "0.20 m", "0.40 m")),
            ("PREREG_P1.md", ("at least 0.65 or at most 0.35", "at least 100 cells", "10th percentile", "90th percentile", "q_s - q_c >= 0.15")),
            ("PREREG.md", ("smallest multiple of 16", "<= 0.03", "(0.20, 0.65)", "(0.30, 0.75)")),
            ("PREREG_P3.md", ("W = 15", "1.0 s at 15 Hz", "0.05 m", "0.2 m", "0.1 m", "10 and 20 steps", "1.0 s and 2.0 s at 10 Hz", "321 frames", "64 beams", "960 frames",
                              "W = 10", "W = 2", "N_min = 5", "3 to 25 m")),
            ("results_p3/deviations.md", ("9 GB", "Amendment 3")),
            ("results_p1b/deviations.md", ("\n25. **Build provenance", "\n30. **Clean-tree check")),
            ("results_p3/P3_REPORT.md", ("\n## 5. Provenance",))):
        txt = pr(fn)
        for needle in needles:
            check(f"{fn} holds the restated fact {needle!r}", needle in txt)
    rho = {r["rho"] for r in rows("e7_autocorr.csv")}
    check("E7 autocorrelations include 0.23 (the value quoted in the abstract, the introduction and Section 4)", "0.23" in rho, str(rho))
    src = lambda n: (HERE / "src" / n).read_text()

    def code(path, rx):
        m = re.search(rx, (root / path).read_text())
        assert m, (path, rx)
        return m.group(1)
    e3 = {(r["graduate_prob"], r["demote_prob"], r["survival_decay"]): r for r in rows("e3_sensitivity.csv")}
    fl = lambda g, d, l: e3[(g, d, l)]["flicker_transitions"]
    wide, worst = fl("0.9", "0.3", "0.9"), fl("0.9", "0.9", "0.9")
    for f in ("4_experiments",):    # the summary of the legacy arm (moved from the introduction in the v2 rewrite)
        R(f, r"flicker from (\d+) to (\d+) toggles", wide, worst)
    R("4_experiments", r"flickers (\d+) times and final F1 drops to ([\d.]+) \(recall ([\d.]+)\), whereas .*? band widened to ([\d.]+) .*? flickers (\d+) times at F1 ([\d.]+)",
      worst, e3[("0.9", "0.9", "0.9")]["final_f1"], e3[("0.9", "0.9", "0.9")]["final_recall"], "0.6", wide, e3[("0.9", "0.3", "0.9")]["final_f1"])
    R("4_experiments", r"\((\d+) at ([\d.]+), (\d+) at ([\d.]+), (\d+) at ([\d.]+)\), even though",
      fl("0.7", "0.7", "0.9"), "0.7", fl("0.8", "0.8", "0.9"), "0.8", worst, "0.9")
    R("4_experiments", r"\((\d+) at \\lambda=0.90, (\d+) at ([\d.]+), (\d+) at ([\d.]+)\)",
      worst, fl("0.9", "0.9", "0.97"), "0.97", fl("0.9", "0.9", "1"), "1.0")
    R("4_experiments", r"total over the (\d+) configurations is (\d+) in both", str(len(e3)), str(sum(int(r["flicker_transitions"]) for r in e3.values())))
    R("4_experiments", r"\\lambda=0.90 has (\d+) toggles", wide)
    R("5_discussion", r"already yields F1 ([\d.]+) at both", e3[("0.9", "0.3", "0.9")]["final_f1"])
    R("5_discussion", r"not adjusted for the (\d+) held-out", "36")
    e5 = rows("e5_trajectory.csv")
    check("E5: 36 held-out Bernoulli configurations per rule",
          len([r for r in e5 if r["rule"] == "spending" and r["kind"] == "bernoulli"]) == 36)
    sp = max(int(r["ever_1024"]) for r in e5 if r["rule"] == "spending" and r["kind"] == "bernoulli")
    s5 = src("e5_trajectory.cpp")
    hor = re.search(r"kHorizon = (\d+);", s5).group(1)
    rep_ = re.search(r"kReport = \{(\d+), (\d+), (\d+)\};", s5).groups()
    for f in ("0_abstract", "5_discussion", "6_conclusion", "3_method", "4_experiments", "1_introduction"):
        for m_ in (r"(?:within|over|in) (\d+) windows", r"a (\d+)-window run"):
            RULES.append((f, m_, (hor,), "optional", ("64", "256")))
    for f in ("5_discussion",):
        R(f, r"(?:at most|no more than) (\d+) of 2000 (?:Bernoulli )?(?:clutter )?cells", str(sp))
    R("4_experiments", r"horizons of (\d+), (\d+) and (\d+) windows", *rep_)
    R("4_experiments", r"Periodic within (\d+) windows and 953 of 2000 within (\d+)", rep_[0], rep_[2])
    mk = {(r["rule"], r["params"], r["touch"]): r["ever_1024"] for r in e5 if r["kind"] == "markov"}
    R("4_experiments", r"\((\d+) at a single read-out\); at T=8 it labels (\d+)\.", mk[("single_readout", "shipped", "every")], mk[("spending", "e2", "every")])
    pw = max(float(r["per_window_rate"]) for r in e5 if r["rule"] == "spending" and r["kind"] == "bernoulli")
    R("4_experiments", r"per-window rate is at most ([\d.]+)\{\u00d7\}10\^\{-(\d)\}", f"{pw * 1e6:.1f}", "6")
    R("4_experiments", r"never-pruned case at ([\d.]+)\.", "0.2")
    dd = {(r["rule"], r["params"], r["door"]): r["median_first_n"] for r in rows("e5_doors.csv")}
    R("4_experiments", r"12-on/12-off door stays at (\d+) windows", dd[("spending", "shipped", "p24_12on12off")])
    R("4_experiments", r"period of (\d+) windows; this is", "24")
    R("5_discussion", r"needs (\d+) windows instead of (\d+), and one with 10\\% flipped windows (\d+) instead of (\d+)",
      dd[("spending", "e2", "p8_4on4off")], dd[("single_readout", "e2", "p8_4on4off")],
      dd[("spending", "e2", "p8_4on4off_noisy")], dd[("single_readout", "e2", "p8_4on4off_noisy")])
    cur = rows("e2_rates_vs_length.csv")
    tp = {int(r["obs_length"]): int(r["tp"]) for r in cur}
    first = str(min(n for n in tp if all(tp[k] == 2 for k in tp if k >= n)))
    one = {int(r["obs_length"]): int(r["tp"]) for r in rows("pre_spending_2026-09-28/e2_rates_vs_length.csv")}
    first1 = str(min(n for n in one if all(one[k] == 2 for k in one if k >= n)))
    mid = {int(r["obs_length"]): int(r["tp"]) for r in rows("pre_calibration_2026-09-28/e2_rates_vs_length.csv")}
    first0 = str(min(n for n in mid if all(mid[k] == 2 for k in mid if k >= n)))
    for f in ("4_experiments",):
        R(f, r"from (\d+) windows", first)
    R("4_experiments", r"from \d+ windows instead of (\d+),", first0)
    R("4_experiments", r"every read-out length from (\d+) to (\d+) windows", str(min(tp)), str(max(tp)))
    R("3_method", r"detected from (\d+) windows, against (\d+) with a single-read-out level of ([\d.]+) and (\d+) without the test",
      first, first1, "0.1", first0)
    t1 = sorted(n for n in tp if tp[n] == 1)
    R("4_experiments", r"rate is (\d) for n=(\d+)\u2013(\d+) and (\d)/(\d) at n=(\d+)\u2013(\d+), and it reaches 2/3 at n=(\d+)",
      "0", str(min(tp)), str(t1[0] - 1), "1", "3", str(t1[0]), str(t1[-1]), first)
    R("4_experiments", r"falls from ([\d.]+) to ([\d.]+) \(single read-out\) and ([\d.]+) \(spent\)",
      f"{sum(mid.values()) / (3 * len(mid)):.3f}", f"{sum(one.values()) / (3 * len(one)):.3f}", f"{sum(tp.values()) / (3 * len(tp)):.3f}")
    ref_fp = [int(r["obs_length"]) for r in rows("pre_spending_2026-09-28/e2_rates_vs_length.csv") if r["ref_fp"] != "0"]
    runs, a = [], None
    for n in sorted(ref_fp) + [10 ** 9]:
        if a is None:
            a = b = n
        elif n == b + 1:
            b = n
        else:
            runs += [str(a), str(b)]
            a = b = n
    R("4_experiments", r"at (\d+) of (\d+) lengths \(n=(\d+)\u2013(\d+), (\d+)\u2013(\d+) and (\d+)\u2013(\d+)\)", str(len(ref_fp)), str(len(tp)), *runs)
    R("4_experiments", r"H\\,\\Theta=0.056<([\d.]+),", "0.1")
    s64 = lambda sub_: {r["metric"]: r for r in rows(sub_ + "e2_summary.csv")}["periodic_FPR"]["note"]
    R("4_experiments", r"false-positive rate of (\d)/(\d); the two earliest engines scored (\d)/(\d) \(v0\.1\.0\) and (\d)/(\d) \(centred only\)",
      s64(""), "5", s64("pre_fix_2026-09-28/"), "5", s64("pre_calibration_2026-09-28/"), "5")
    R("4_experiments", r"\\delta_\{64\}=([\d.]+)\\cdot(\d+)/\((\d+)\\cdot(\d+)\)\\approx([\d.]+)\u00d710\^\{-(\d)\}", "0.2", "8", "64", "65", f"{0.2 * 8 / (64 * 65) * 1e4:.1f}", "4")
    R("4_experiments", r"bounds below 10\^\{-(\d)\}", "3")
    amp = {(r["cell"], r["obs_length"]): float(r["amplitude"]) for r in rows("e2_amplitude_vs_length.csv")} if "cell" in rows("e2_amplitude_vs_length.csv")[0] else {}
    e1 = rows("e1_static_quality.csv")
    lastw = str(max(int(r["window"]) for r in e1))
    s1 = src("e1_static_quality.cpp")
    mov = re.search(r"return d == 0 \? (\d+) : \(d == 1 \? (\d+) : (\d+)\);", s1).groups()
    R("4_experiments", r"recall at ([\d.]+) through window (\d+)", "1.0", lastw)
    R("4_experiments", r"under (\d+) movers per window", mov[2])
    R("4_experiments", r"recall jumps from (\d) to ([\d.]+) exactly", "0", "1.0")
    R("4_experiments", r"then holds at ([\d.]+) through", "1.0")
    R("4_experiments", r"At (\d+) movers per window this never happens and precision stays at ([\d.]+); at (\d+) it saturates .*? at (\d+) false statics",
      mov[0], "1.0", mov[1], mov[2])
    R("4_experiments", r"centred amplitude is (\d) \(Remark", "0")
    R("4_experiments", r"ended with Static recall (\d)", "0")
    R("4_experiments", r"amplitude exactly (\d) at every length", "0")
    R("4_experiments", r"amplitude is exactly (\d) at n=6", "0")
    R("4_experiments", r"\(n=8,16,\\dots,(\d+)\)", "64")
    R("4_experiments", r"offsets (\d+)\u2013(\d+)\)", "0", "1500")
    e0 = {float(r["alpha"]): r for r in rows("e0_calibration_choice.csv") if not r["alpha"].startswith("#")}
    R("4_experiments", r"both within the ([\d.]+) target", "0.01")
    R("4_experiments", r"Relaxing \\delta from ([\d.]+) to ([\d.]+) shortens", "0.01", "0.1")
    R("4_experiments", r"at a level of ([\d.]+) it reaches", "0.01")
    for f in ("3_method", "5_discussion"):
        RULES.append((f, r"[Tt]he ([\d.]+) target", ("0.01",), "optional", ()))
    R("3_method", r"at or below ([\d.]+), both over", "0.01")
    R("3_method", r"upper limit of at most ([\d.]+) in every", re.search(r"kTarget = ([\d.]+);", s5).group(1))
    R("3_method", r"single-read-out level of ([\d.]+), up to", "0.1")
    R("3_method", r"per-history bound ([\d.]+);", "0.2")
    R("5_discussion", r"probability at most ([\d.]+)\. The", "0.2")
    c6 = {(r["params"], r["readout"], r["gt"]): r for r in rows("e6_confusion.csv")}
    tot = lambda g: str(sum(int(c6[("e2", "64", g)][k]) for k in ("pred_S", "pred_P", "pred_T", "pred_U")))
    R("4_experiments", r"over (\d+) held-out scenes", re.search(r"for \(int s = 0; s < (\d+); \+\+s\) \{\n      std::mt19937 rng\(kEvalSeed \+ 97000", s5).group(1))
    R("4_experiments", r"E5 does, over (\d+) scenes", "20")
    R("4_experiments", r"Over (\d+) held-out scenes", "20")
    # sprt arm of the synthetic suite (Section 5.4) and the readouts it quotes
    ro6 = sorted({r["readout"] for r in rows("e6_confusion_sprt.csv")}, key=int)
    R("4_experiments", r"recall reaches ([\d.]+) at the window index", "1.0")
    R("4_experiments", r"the ([\d.]+) target is met only", re.search(r"kTarget = ([\d.]+);", s5).group(1))
    R("4_experiments", r"clutter cells of (\d+) that are ever labelled", "2000")
    R("4_experiments", r"SqEfiveMarkov\{\} of (\d+) cells", "2000")
    R("4_experiments", r"Static at window (\d+),", ro6[0])
    R("4_experiments", r"Intermittent after (\d+) windows", ro6[1])
    R("4_experiments", r"After (\d+) windows, \\SqEsixAtTw", ro6[1])
    R("4_experiments", r"Periodic after (\d+) windows and all \\SqEsixDoorsLate\{\} after (\d+)", ro6[0], ro6[1])
    R("4_experiments", r"one exception in (\d+)\.", tot("dynamic"))
    R("4_experiments", r"All (\d+) 50\\%-duty doors are Periodic and none of the (\d+) 25", c6[("e2", "64", "door")]["pred_P"], c6[("e2", "64", "door")]["pred_T"])
    R("4_experiments", r"Within (\d+) windows the single-read-out level", "256")
    c5 = {(r["rule"], r["readout"], r["gt"]): r for r in rows("e5_confusion.csv")}
    R("4_experiments", r"the spent level (\d)\. The dominant", c5[("spending", "256", "aperiodic")]["ever_periodic"])
    R("4_experiments", r"of the (\d+) doors of that scene \d+ are Periodic after (\d+) windows and all (\d+) after (\d+)",
      tot("door"), "64", c6[("shipped", "256", "door")]["pred_P"], "256")
    R("4_experiments", r"all (\d+) doors of the E2 scenes", c6[("e2_decay097", "64", "door")]["pred_P"])
    R("4_experiments", r"cells are Static after (\d+) windows, \d+ at", "256")
    R("5_discussion", r"are Static after (\d+) windows \(E6\)", "256")
    R("4_experiments", r"for (\d+) windows, under four parameter sets", "256")
    R("4_experiments", r"On the ([\d.]+) grid", "0.05")
    R("4_experiments", r"level, (\d) or (\d) such demotions", "1", "2")
    # parameter table (appendix): defaults against the shipped YAML
    y = {}
    for line in (root / "strata/params/grid2d.yaml").read_text().splitlines():
        m = re.match(r"\s{4}(\w+):\s*\"?([^#\"]*?)\"?\s*(#.*)?$", line)
        if m:
            y[m.group(1)] = m.group(2)
    y3 = re.search(r"voxel_size: ([\d.]+)", (root / "strata/params/voxel3d.yaml").read_text()).group(1)
    for k in ("layer_interval", "l_hit", "l_miss", "survival_decay", "graduate_prob", "demote_prob", "min_observations", "prune_prob",
              "periodic_amplitude_min", "periodic_false_alarm", "period_windows", "n_harmonics", "m0", "m1", "alpha", "beta", "n_max_undecided"):
        R("appendix", r"\\code\{%s\} & [^&]* & -?([\d.]+) &" % k.replace("_", r"\\_"), y[k].lstrip("-"))
    R("appendix", r"l_\{\\min\},l_\{\\max\} & -([\d.]+), ([\d.]+) &", y["l_min"].lstrip("-"), y["l_max"])
    R("appendix", r"grid\\_height\} (\d+), \\code\{grid\\_resolution\} ([\d.]+) m and origin \(-(\d+),-(\d+)\) m", y["grid_width"], y["grid_resolution"],
      y["grid_origin_x"].lstrip("-"), y["grid_origin_y"].lstrip("-"))
    R("appendix", r"voxel\\_size\} ([\d.]+) m", y3)
    R("appendix", r"publish\\_period\} ([\d.]+) s; \\code\{window", y["publish_period"])
    R("appendix", r"window\\_period\\_s\} ([\d.]+) s; \\code\{expected\\_scan\\_rate\\_hz\} (\d+) \(estimated from stamps\), \\code\{rate\\_warn\\_drop\\_fraction\} ([\d.]+), "
                  r"\\code\{input\\_timeout\\_s\} (\d+) s and \\code\{startup\\_timeout\\_s\} (\d+) s",
      y["window_period_s"], y["expected_scan_rate_hz"], y["rate_warn_drop_fraction"], y["input_timeout_s"], y["startup_timeout_s"])
    R("appendix", r"publish\\_period\} \(([\d.]+) s\)", y["publish_period"])
    # per-experiment deviations (appendix): each cell against the harness source
    hs = {n: src(f"e{n}_{t}.cpp") for n, t in ((1, "static_quality"), (2, "periodicity"), (3, "sensitivity"), (4, "throughput"))}
    hv = lambda n, k: re.search(r"p\.%s = (-?[\d.]+);" % k, hs[n]).group(1).lstrip("-")
    for n_, ks in ((1, ("l_hit", "l_miss", "survival_decay", "graduate_prob", "demote_prob", "min_observations", "prune_prob")),
                   (3, ("l_hit", "l_miss", "min_observations")), (4, ("l_hit", "l_miss", "graduate_prob", "demote_prob", "min_observations", "prune_prob")),
                   (2, ("prune_prob",))):
        for k_ in ks:   # cells shown as "unchanged" in the deviations table
            check(f"E{n_} harness leaves {k_} at the shipped default", float(hv(n_, k_)) == float(y[k_].lstrip("-")), hv(n_, k_))
    dflt = lambda k: y[k].lstrip("-")
    R("appendix", r"L \((\d+)\) & (\d) & (\d) & (\d) & (\d) \\\\", dflt("layer_interval"), *[hv(n, "layer_interval") for n in (1, 2, 3, 4)])
    R("appendix", r"\\lhit \(([\d.]+)\) & \u2013- & ([\d.]+) &", dflt("l_hit"), hv(2, "l_hit"))
    R("appendix", r"\\lmiss \(-([\d.]+)\) & \u2013- & -([\d.]+) &", dflt("l_miss"), hv(2, "l_miss"))
    R("appendix", r"\\lambda \(([\d.]+)\) & \u2013- & ([\d.]+) & swept & ([\d.]+) \\\\", dflt("survival_decay"), hv(2, "survival_decay"), hv(4, "survival_decay"))
    R("appendix", r"\\pgrad \(([\d.]+)\) & \u2013- & ([\d.]+) & swept", dflt("graduate_prob"), hv(2, "graduate_prob"))
    R("appendix", r"\\pdem \(([\d.]+)\) & \u2013- & ([\d.]+) & swept", dflt("demote_prob"), hv(2, "demote_prob"))
    R("appendix", r"\\Nmin \((\d+)\) & \u2013- & (\d+) &", dflt("min_observations"), hv(2, "min_observations"))
    R("appendix", r"\\pprune \(([\d.]+)\) & \u2013- & \u2013- & ([\d.]+) &", dflt("prune_prob"), hv(3, "prune_prob"))
    R("appendix", r"T \((\d+)\) & \u2013- & (\d+) &", dflt("period_windows"), re.search(r"kPeriod = (\d+);", hs[2]).group(1))
    R("appendix", r"H \((\d+)\) & \u2013- & (\d+) &", dflt("n_harmonics"), re.search(r"kHarmonics = (\d+);", hs[2]).group(1))
    R("appendix", r"\\delta \(([\d.]+), spent\)", dflt("periodic_false_alarm"))
    R("4c_realdata", r"\\lhit=([\d.]+), \\lmiss=-([\d.]+), leak ([\d.]+), graduate at ([\d.]+) after three touched windows, demote at ([\d.]+)",
      dflt("l_hit"), dflt("l_miss"), dflt("survival_decay"), dflt("graduate_prob"), dflt("demote_prob"))
    # node behaviour table (appendix): constants against the node headers
    H = "strata/include/strata/"
    R("appendix", r"more than 10\^(\d) windows ahead", str(len(code(H + "window_clock.hpp", r"kMaxGapWindows = (\d+);")) - 1))
    R("3_method", r"more than 10\^(\d) windows ahead", str(len(code(H + "window_clock.hpp", r"kMaxGapWindows = (\d+);")) - 1))
    R("appendix", r"blocking for ([\d.]+) s of ROS time, at most (\d+) s of steady time\. A ROS clock unchanged for (\d+) s",
      code(H + "mapping_node.hpp", r"kTfWaitS = ([\d.]+);"), code(H + "mapping_node.hpp", r"kTfWaitCapS = ([\d.]+);"), code(H + "mapping_node.hpp", r"kClockFrozenS = ([\d.]+);"))
    R("appendix", r"budget gets (\d+) ms more", str(round(1000 * float(code(H + "tf_wait.hpp", r"step_grace_s\{([\d.]+)\}")))))
    R("appendix", r"nothing was integrated for (\d+) s, then ERROR", code(H + "input_status.hpp", r"kTfNoteAfterS = ([\d.]+);"))
    R("appendix", r"stands still for (\d+) s while", code(H + "mapping_node.hpp", r"kClockStallS = ([\d.]+);"))
    R("appendix", r"closes for (\d+) window periods \(and more than (\d+) s\)", code("strata/src/mapping_node.cpp", r"since_close > ([\d.]+) \* window_period_s_"),
      code("strata/src/mapping_node.cpp", r"since_close > ([\d.]+)\) \{\n"))
    R("appendix", r"At most (\d+) frames are tracked; a frame silent for (\d+) window closes", code(H + "mapping_node.hpp", r"kMaxSensors = (\d+);"),
      code(H + "mapping_node.hpp", r"kIdleWindowsToForget = (\d+);"))
    R("appendix", r"with (\d+)\\,\\% of the scans withheld", str(round(100 * float(re.search(r"--drop ([\d.]+)", (root / ".github/workflows/ci.yml").read_text()).group(1)))))
    R("5_discussion", r"withholding (\d+)\\,\\% of the scans", str(round(100 * float(re.search(r"--drop ([\d.]+)", (root / ".github/workflows/ci.yml").read_text()).group(1)))))
    R("5_discussion", r"at least (\d+)\\,\\%, none periodic", str(round(100 * float(re.search(r"f100 >= ([\d.]+)", (root / "scripts/synthetic_e2e.py").read_text()).group(1)))))
    mw = (root / H / "map_writer.hpp").read_text()
    R("appendix", r"Free (\d+), Transient and Periodic (\d+), Static (\d+), Unknown (\d+) with thresholds ([\d.]+) and ([\d.]+)",
      re.search(r"return (\d+);\s+// free", mw).group(1), re.search(r"if \(v >= 50\) return (\d+);", mw).group(1),
      re.search(r"if \(v >= 100\) return (\d+);", mw).group(1), re.search(r"if \(v < 0\) return (\d+);", mw).group(1),
      re.search(r"occupied_thresh: ([\d.]+)", mw).group(1), re.search(r"free_thresh: ([\d.]+)", mw).group(1))
    nt = {f.stem: len(re.findall(r"^TEST(?:_F)?\(", f.read_text(), re.M)) for f in (root / "strata/test").glob("*.cpp")}
    for stem in ("test_window_clock", "test_tf_wait", "test_input_status", "test_rate_monitor", "test_map_writer"):
        R("appendix", stem.replace("_", r"\\_") + r"\} \((\d+) cases\)", str(nt[stem]))
    # rendering convention and footprint (method), free value (discussion)
    g2 = (root / "strata_core/src/grid2d_backend.cpp").read_text()
    R("3_method", r"Unknown -(\d), free (\d), Transient (\d+), Periodic (\d+), Static (\d+),", re.search(r"assign\([^;]*, -(\d)\);", g2).group(1),
      re.search(r"else if\(f&kFree\) g\.data\[id\]=(\d);", g2).group(1), re.search(r"g\.data\[id\]=(\d+);\n    else if", g2).group(1),
      re.search(r"periodicCells\(\)\)\s*put\(id,(\d+)\)", g2).group(1), re.search(r"staticCells\(\)\)\s*put\(id,(\d+)\)", g2).group(1))
    R("3_method", r"it draws (\d+) for a cell hit .*?\(p_i>([\d.]+)\), and (\d) for a cleared", "50", re.search(r"occupancyProb\(id\)>([\d.]+)", g2).group(1), "0")
    R("5_discussion", r"from its first hit it reads (\d+),", "50")
    R("3_method", r"plus roughly (\d+) bytes of hash-node", "32")
    R("3_method", r"two phase sums, (\d+) bytes per harmonic", "16")
    R("3_method", r"seen in (\d+) of every (\d+) windows", "8", "48")
    check("test RevisitLoopWall8of48StaysStatic exists", "RevisitLoopWall8of48StaysStatic" in (root / "strata_core/test/test_layered_map.cpp").read_text())
    R("5_discussion", r"above about (\d+)\\,\\% loss", "80")
    R("4_experiments", r"12-on/12-off door stays at \d+ windows, and with (\d+)\\% flips", "10")


def coverage():
    files = sorted((ARXIV / "sections").glob("*.tex")) + [ARXIV / "figures" / n for n in
            ("e1_f1.tex", "e2_amplitude.tex", "e2_fpr.tex", "e3_flicker.tex", "overview.tex")]
    variants = sorted({" ".join(v.split()) for t, reg in REG.items() if t == "arxiv_ref" for _, vs in reg for v in vs if v.strip()},
                      key=len, reverse=True)
    stats = {}
    LAYOUT = "structural: plot layout (axis limits, sizes, tick positions, styles)"
    for path in files:
        rel = str(path.relative_to(ARXIV))
        raw = MASK.sub(lambda m: " " * len(m.group(0)), path.read_text(encoding="utf-8"))
        if rel.startswith("figures/"):      # only captions are prose; the rest is plot layout
            keep = "".join(m.group(0) for m in re.finditer(r"\\caption\{.*?\}\n\\label", raw, re.S))
            stats[LAYOUT] = stats.get(LAYOUT, 0) + len(NUMERAL.findall(raw)) - len(NUMERAL.findall(keep))
            layout = raw
            for m in re.finditer(r"\\caption\{.*?\}\n\\label", raw, re.S):
                layout = layout.replace(m.group(0), "")
            LOCK.append((rel, "sha256:" + hashlib.sha256(" ".join(layout.split()).encode()).hexdigest()[:16],
                         "(everything outside the caption)", LAYOUT[12:]))
            raw = keep
        for a, b in (("{,}", ","), ("~", " "), ("--", "\u2013"), ("\\times", "\u00d7"), ("{=}", "=")):
            raw = raw.replace(a, b)
        # drop "$" while remembering which characters are inside inline mathematics; collapse blanks
        chars, math, inm, prev = [], [], False, ""
        for ch in raw:
            if ch == "$" and prev != "\\":
                inm = not inm
            elif ch.isspace():
                if chars and chars[-1] != " ":
                    chars.append(" "); math.append(inm)
            else:
                chars.append(ch); math.append(inm)
            prev = ch
        nf = "".join(chars)
        cover = [None] * len(nf)

        def mark(a, b, what, rank):
            for i in range(a, b):
                if cover[i] is None or cover[i][1] < rank:
                    cover[i] = (what, rank)
        for rule in RULES:
            fpat, rx, vals = rule[:3]
            optional = len(rule) > 3
            if fpat in rel:
                ms = list(re.finditer(rx, nf))
                if not optional:
                    check(f"rule matches in {rel}: {rx[:60]}", bool(ms))
                for m in ms:
                    for gi, want in enumerate(vals, 1):
                        got = m.group(gi)
                        try:     # compare at the precision the paper prints
                            dec = len(got.split(".")[1]) if "." in got else 0
                            ok = abs(round(float(want), dec) - float(got.replace(",", ""))) < 1e-12
                        except ValueError:
                            ok = got == want
                        if optional and not ok and got in rule[4]:
                            continue     # another documented horizon; left to the other rules
                        check(f"{rel}: '...{m.group(0)[:70]}' group {gi} is {want}", ok, got)
                        mark(m.start(gi), m.end(gi), "source: tied at this sentence to a value recomputed from the results or the code", 3)
        for v in variants:
            ctx = bool(re.search(r"[A-Za-z]{2}", v))
            kind = ("source: inside a phrase recomputed from the results or the code" if ctx
                    else "source: equal to a recomputed value (bare number, any location in the file)")
            start = nf.find(v)
            while start >= 0:
                end = start + len(v)     # a variant must not begin or end inside a longer number
                inside = ((v[0].isdigit() and start > 0 and (nf[start - 1].isdigit() or nf[start - 1] == "."))
                          or (v[-1].isdigit() and end < len(nf) and (nf[end].isdigit() or (nf[end] == "." and nf[end + 1:end + 2].isdigit()))))
                if not inside:
                    mark(start, end, kind, 2 if ctx else 1)
                start = nf.find(v, start + 1)
        for fpat, rx, reason in STRUCT:
            if re.search(fpat, rel):
                for m in re.finditer(rx, nf, re.S):
                    mark(m.start(), m.end(), "structural: " + reason, 0)
        for i, im in enumerate(math):
            if im and cover[i] is None:
                cover[i] = ("structural: " + MATH, 0)
        for m in NUMERAL.finditer(nf):
            kinds = {cover[i] for i in range(m.start(), m.end())}
            if None in kinds or len(kinds) != 1:
                check(f"numeral '{m.group(0)}' in {rel} is tied to a source or listed as structural", False,
                      "..." + nf[max(0, m.start() - 45):m.end() + 25] + "...")
            else:
                k = kinds.pop()[0]
                stats[k] = stats.get(k, 0) + 1
                if k.startswith("structural"):
                    LOCK.append((rel, m.group(0), nf[max(0, m.start() - 28):m.start()].replace("\t", " "), k[12:]))
                if k.startswith("structural: inline") and "." in m.group(0):
                    MATHDEC.append(f"{rel}: {nf[max(0, m.start() - 30):m.end() + 10]}")
    return stats


def guard_v2():
    """Cross-checks of the sequential-test and real-data parts. Their numbers enter the text as the macros of
    tables/numbers_v2.tex, which make_v2_artifacts.py writes from the committed result files; here every macro is checked against a
    second source (the generated P3 and P1b reports, the pre-registration, an independent recomputation) and the wording rails are enforced."""
    P3D = HERE / "realdata" / "results_p3"
    lines = (ARXIV / "tables" / "numbers_v2.tex").read_text().splitlines()
    mac = {}
    for ln in lines:
        if ln.startswith("\\newcommand{\\"):
            name = ln[len("\\newcommand{\\"):ln.index("}")]
            rest = ln[ln.index("}{") + 2:]
            i = rest.rindex("}% ")
            mac[name] = (rest[:i].replace("{,}", ""), rest[i + 3:])
    check("numbers_v2.tex defines each macro once", len(mac) == sum(l.startswith("\\newcommand{\\") for l in lines), str(len(mac)))
    used = set()
    for f in sorted((ARXIV / "sections").glob("*.tex")) + sorted((ARXIV / "tables").glob("*.tex")) + sorted((ARXIV / "figures").glob("*.tex")):
        if f.name != "numbers_v2.tex":
            used |= set(re.findall(r"\\((?:Sq|Pt|Cal)[A-Za-z]+)", f.read_text()))
    check("every number macro used in the text is defined", used <= set(mac), str(sorted(used - set(mac))))
    unused = sorted(set(mac) - used)
    passes.append(f"[arxiv_ref] number macros: {len(mac)} defined, {len(used & set(mac))} used, {len(unused)} unused")
    # P3: each Pt macro is a number of the generated report or of the hand-written summary
    summ = (P3D / "P3_REPORT_summary.md").read_text()
    rep = (P3D / "P3_REPORT.md").read_text()
    tok = set(re.findall(r"\d+(?:\.\d+)?", summ))
    derived = {"PtRatioLe": "quotient of two printed rates", "PtRatioAll": "quotient of two printed rates",
               "PtVetoSpLo": "quotient of printed veto-on and veto-off rates", "PtVetoSpHi": "quotient of printed veto-on and veto-off rates",
               "PtLilLeN": "number of clutter cells at or below m0 (field n of the JSON; the report prints the rate)", "PtKthIdCount": "count of the finished policy-all conditions (summary: 5 + 4)", "PtFailAlloc": "count of failed runs whose log holds std::bad_alloc",
               "PtFailAll": "count of failed runs (summary: 7 + 7)",
               "PtProbeCells": "cells of the Amendment 1 memory probe of PREREG_P3.md (checked against that table below)",
               "PtProbeOff": "peak memory of that probe with the veto off (checked below)", "PtProbeOn": "peak memory of that probe with the veto on (checked below)",
               "PtWallEverSprt": "count from the JSON of the SPRT arm (wall-run cells with a first Static window; checked below)",
               "PtWallEndSprt": "count from the JSON of the SPRT arm (wall-run cells Static at the end; checked below)",
               "PtWallLostSprt": "difference of two JSON counts (checked below)", "PtWallNotKSprt": "count from the JSON of the SPRT arm (checked below)",
               "PtWallNeverSprt": "difference of two JSON counts (checked below)"}
    for k, (v, src) in sorted(mac.items()):
        if not k.startswith("Pt") or k in derived:
            continue
        ok = v in tok or re.search(r"(?<![\d.])" + re.escape(v) + r"(?![\d])", rep) is not None
        check(f"P3 macro {k} = {v} is a number of P3_REPORT.md or of its summary", ok, src)
    for k, why in derived.items():
        check(f"P3 macro {k} is {why}", k in mac)
    # memory probe of Amendment 1 of PREREG_P3.md: the table row is the source of the three macros
    pre3 = (HERE / "realdata" / "PREREG_P3.md").read_text()
    for on_off, name in (("off", "PtProbeOff"), ("on", "PtProbeOn")):
        mrow = re.search(r"\| 3 to 50 m \| 0\.2 m \| " + on_off + r" \| ([\d,]+) \| ([\d,]+) MB \|", pre3)
        check(f"memory probe: {name} and PtProbeCells are the 3 to 50 m, 0.2 m, veto {on_off} row of PREREG_P3.md Amendment 1",
              mrow is not None and mrow.group(2).replace(",", "") == mac[name][0] and mrow.group(1).replace(",", "") == mac["PtProbeCells"][0])
    check("memory probe: veto on needs more than five times the memory of veto off (the text says 'more than five times')",
          float(mac["PtProbeOn"][0]) / float(mac["PtProbeOff"][0]) > 5)
    # wall-run counts of the SPRT arm, recomputed from the JSON: ever Static, Static at the end, lost, never, not Static at the end
    jw = json.loads((P3D / "lil_lt_changes_dynamics_0_sprt_p1.json").read_text())
    ever, endk = jw["wall_run_windows_to_first_static"]["n"], jw["wall_runs_static"]["k"]
    nn, kn = jw["wall_runs_not_static"]["n"], jw["wall_runs_not_static"]["k"]
    check("wall-run counts of SPRT (0.20, 0.70): ever Static, Static at the end, lost = ever - end, never = n - ever, not Static = lost + never",
          (str(ever), str(endk), str(ever - endk), str(kn), str(nn - ever)) == (mac["PtWallEverSprt"][0], mac["PtWallEndSprt"][0], mac["PtWallLostSprt"][0], mac["PtWallNotKSprt"][0], mac["PtWallNeverSprt"][0])
          and kn == (ever - endk) + (nn - ever) and jw["false_reject_on_wall_runs"]["k"] == 0)
    check("PtFailAll = 7 + 7 failed runs, all std::bad_alloc", (mac["PtFailAll"][0], mac["PtFailAlloc"][0], mac["PtFailSpires"][0], mac["PtFailKth"][0]) == ("14", "14", "7", "7"))
    # the headline statements of the summary, in words
    for needle in ("The nominal bound `alpha = 0.01` is per history under independent windows", "No claim of the form \"the realised rate is below the bound\" is made",
                   "The pair is the 2D pair and is not calibrated for 3D", "What these runs cannot show"):
        check(f"P3_REPORT_summary.md states: {needle[:60]}", needle in summ)
    # calibration macros: numbers of the P1b report, PREREG.md, the P2 replay or the P1 report
    srcs = "\n".join((HERE / "realdata" / fn).read_text() for fn in ("results_p1b/P1B_REPORT.md", "PREREG.md", "results_p2/replay_shipped_pair_W15.txt", "results_p1/P1_REPORT.md",
                                                                "results_p1b/exploratory/keble_clutter_height_r0.2_W10_today.txt"))
    for k, (v, src) in sorted(mac.items()):
        if k.startswith("Cal") and k not in ("CalRepAll", "CalRepLe", "CalRepGt", "CalGateSeq", "CalGateTwoD", "CalGateGoneFail", "CalGateGtwoFail", "CalKittiMed", "CalKittiWindows",
                                              "CalOptcLil", "CalOptcKebLo", "CalOptcKebHi", "CalOptcKth"):
            vv = v.replace("-", "-")
            check(f"calibration macro {k} = {v} is a number of the P1b report, PREREG.md, the P2 replay or the keble height record",
                  re.search(r"(?<![\d.])" + re.escape(vv) + r"(?![\d])", srcs) is not None, src)
    check("CalRepAll / CalRepLe / CalRepGt are the percentages of the P2 replay (whole sequence, periodicity on)",
          all(re.search(r"ever-Static 0\.0371 .* at H/N <= m0 0\.0151 .* above m0 0\.3050", (HERE / "realdata/results_p2/replay_shipped_pair_W15.txt").read_text()) for _ in [0])
          and (mac["CalRepAll"][0], mac["CalRepLe"][0], mac["CalRepGt"][0]) == ("3.71", "1.51", "30.5"))
    check("G1 fails on all five calibration sequences, G2 passes on all five (macros)", (mac["CalGateGoneFail"][0], mac["CalGateGtwoFail"][0], mac["CalGateSeq"][0]) == ("5", "0", "5"))
    check("Option C: AUCs of the three datasets have the signs the text states (LIL, Keble above 0.65; KITTI 00 below 0.35)",
          float(mac["CalOptcLil"][0]) >= 0.65 and float(mac["CalOptcKebLo"][0]) >= 0.65 and float(mac["CalOptcKth"][0]) <= 0.35)
    pr = (HERE / "realdata" / "PREREG.md").read_text()
    check("PREREG.md: the 2D and the 3D pair are NOT FROZEN", pr.count("Status: **NOT FROZEN**") == 2)
    # sequential test: independent recomputation
    def f_(n):
        return float(mac[n][0])
    m0_, m1_ = f_("SqLegMzero"), f_("SqLegMone")
    check("v0.1 increments are the SPRT between SqLegMzero and SqLegMone", abs(math.log(m1_ / m0_) - 0.85) < 2e-3 and abs(math.log((1 - m1_) / (1 - m0_)) + 0.4) < 2e-3, f"{m0_} {m1_}")
    check("Ville bound exp(-A) = alpha / (1 - beta) = SqVille", abs(math.exp(-f_("SqA")) - f_("SqVille")) < 1e-4 and abs(0.01 / 0.99 - f_("SqVille")) < 1e-4)
    check("Proposition ville: E_p[lambda] = 1 + (p - m0)(m1 - m0) / (m0 (1 - m0)) and E_p[1/lambda] = 1 + (p - m1)(m0 - m1) / (m1 (1 - m1))",
          all(abs((p_ * 0.7 / 0.2 + (1 - p_) * 0.3 / 0.8) - (1 + (p_ - 0.2) * 0.5 / (0.2 * 0.8))) < 1e-12
              and abs((p_ * 0.2 / 0.7 + (1 - p_) * 0.8 / 0.3) - (1 + (p_ - 0.7) * (-0.5) / (0.7 * 0.3))) < 1e-12 for p_ in (0.0, 0.1, 0.2, 0.45, 0.7, 1.0)))
    lam = 0.97
    ez = m0_ * math.exp(lam * 0.85) + (1 - m0_) * math.exp(lam * -0.4)
    check("Proposition leak: E exp(l')/exp(l) at l = -4 is SqLeakRatio, and exceeds 1", abs(math.exp(0.03 * 4) * ez - f_("SqLeakRatio")) < 6e-3 and f_("SqLeakRatio") > 1)
    check("Proposition leak: E_{m0} exp(Z) = 1", abs(m0_ * (m1_ / m0_) + (1 - m0_) * ((1 - m1_) / (1 - m0_)) - 1) < 1e-12)
    # Wald's closed form and the exact lattice rule, recomputed independently of the harness
    M0, M1, AL, BE = 0.2, 0.7, 0.01, 0.01
    lh, lm = math.log(M1 / M0), math.log((1 - M1) / (1 - M0))
    A_, B_ = math.log((1 - BE) / AL), math.log(BE / (1 - AL))
    mstar = -lm / (lh - lm)

    def wald(m):
        if abs(m - mstar) < 1e-5:
            return A_ / (A_ - B_)
        g = lambda h: m * (M1 / M0) ** h + (1 - m) * ((1 - M1) / (1 - M0)) ** h - 1
        lo, hi = (1e-9, 60.0) if m < mstar else (-60.0, -1e-9)
        for _ in range(200):
            mid = 0.5 * (lo + hi)
            if (g(lo) > 0) == (g(mid) > 0):
                lo = mid
            else:
                hi = mid
        h = 0.5 * (lo + hi)
        a, b = math.exp(A_), math.exp(B_)
        return (1 - b ** h) / (a ** h - b ** h)

    def exact(m, gate, nmax=4000):
        cap = A_ + 2 * lh
        cur, grad = {0: 1.0}, 0.0          # hits -> probability; misses = n - hits
        for n in range(1, nmax + 1):
            nxt = {}
            for h, pr_ in cur.items():
                for hit, q in ((1, m), (0, 1 - m)):
                    if q == 0:
                        continue
                    hh = h + hit
                    l_ = min(hh * lh + (n - hh) * lm, cap)
                    if l_ >= A_:
                        grad += pr_ * q
                    elif n >= gate and l_ <= B_:
                        pass
                    else:
                        nxt[hh] = nxt.get(hh, 0.0) + pr_ * q
            cur = nxt
            if not cur:
                break
        return grad

    oc_rows = rows("e7_oc.csv")
    worst_w = max(abs(wald(float(r["m"])) - float(r["p_graduate_wald"])) for r in oc_rows)
    check("E7: Wald column of e7_oc.csv equals the closed form recomputed here", worst_w < 2e-6, f"{worst_w:.2e}")
    worst_e = max(abs(exact(float(r["m"]), 1 if r["rule"] == "pure" else 24) - float(r["p_graduate_exact"])) for r in oc_rows if float(r["m"]) in (0.05, 0.2, 0.439126, 0.5, 0.7, 0.9))
    check("E7: exact column of e7_oc.csv equals an independent lattice computation (rates 0.05, 0.2, m*, 0.5, 0.7, 0.9; both rules; the m* row is printed at six digits)", worst_e < 3e-6, f"{worst_e:.2e}")
    check("E7: Wald gives alpha at m0 and 1 - beta at m1", abs(wald(M0) - AL) < 1e-9 and abs(wald(M1) - (1 - BE)) < 1e-9)
    # wording rails
    for label, needle in (("provisional pair", "provisional"), ("nothing froze", "Nothing froze"), ("3D not calibrated", "not calibrated for 3D"),
                          ("Periodic validated on synthetic streams only", "validated on synthetic streams only"),
                          ("realised above nominal", "do not claim that the realised rate is within the nominal level"),
                          ("independent windows", "independent windows"), ("ELite cited", "ELite"), ("MTD-Map cited", "MTD-Map"),
                          ("Periodic not validated on real data", "Periodic is not validated on real data"),
                          ("memory failures at 0.1 m with the veto on", "ran out of memory"), ("single pass KTH", "single pass"),
                          ("Boreas has no truth", "no truth for structure or clutter"), ("no winner", "picks no winner"),
                          ("demotion is CUSUM-like", "CUSUM-like"), ("bound is for the first test of a history", "first test"),
                          ("zero-drift limit is -B/(A-B)", "-B/(A-B)"), ("pre-registration amended after the first calibration measurement", "amended after"),
                          ("memory is unbounded over a lifelong run", "memory is unbounded"), ("offline harness, node not in the loop", "The ROS node was not in the loop")):
        in_paper(f"wording rail: {label}", needle)
    for label, needle in (("no 'below the bound' claim", "is below the bound"), ("no 'within the bound' claim", "is within the bound"),
                          ("no frozen claim", "was frozen"), ("no winner word", "the best rule"), ("no 'outperform' claim", "outperform"),
                          ("no SPRT-with-same-error-rates claim for demotion", "with the same error rates"), ("no 'ever graduates' claim for a history", " ever graduates"),
                          ("no 'did not measure' memory claim", "we did not measure it"), ("no 'calibrated periodic' label", "calibrated periodic"),
                          ("no 'times the bound' ratio", "times the bound")):
        check(f"forbidden wording absent: {label}", needle not in text, needle)


def companion_fidelity():
    """The Korean companion carries the numbers of the English one and no others: the
    numerals with a decimal point or at least three digits (section, table and equation
    numbers are not compared) must form the same set in both files, and every one of them
    must occur in the manuscript sources, the bibliography or a committed result file."""
    global target
    target = "companions"
    kr_path = HERE.parent / "strata_paper_KR.md"
    check("Korean companion exists", kr_path.exists())
    if not kr_path.exists():
        return

    def big(t):
        t = t[t.index("# Abstract"):]
        t = re.sub(r"\{#[^}]*\}", "", t)
        t = re.sub(r"\\tag\{[^}]*\}", "", t)
        t = re.sub(r"(?m)^#+ .*$", "", t)                                   # headings carry section numbers
        t = re.sub(r"(§|Sections?\s)\s*\d+(?:\.\d+)*|\d+(?:\.\d+)*(?:\s*[-–]\s*\d+(?:\.\d+)*)?절", "", t)   # section references
        return {x for x in re.findall(r"(?<![A-Za-z_\d.])\d+(?:\.\d+)?", t) if "." in x or len(x) >= 3}

    en, kr = big(PAPER.read_text(encoding="utf-8")), big(kr_path.read_text(encoding="utf-8"))
    check("Korean companion: numerals equal those of the English companion", en == kr,
          f"only English: {sorted(en - kr)[:8]}; only Korean: {sorted(kr - en)[:8]}")
    src = "\n".join([tex_text(ARXIV), (ARXIV / "main.bbl").read_text(encoding="utf-8"),
                     *(f.read_text(encoding="utf-8", errors="ignore") for f in sorted(RESULTS.glob("*.csv")))])
    known = set(re.findall(r"\d+(?:\.\d+)?", src))
    unknown = sorted(x for x in en if x not in known)
    check("companions: every numeral occurs in the manuscript sources or the result files", not unknown, str(unknown[:12]))


README = HERE.parents[1] / "README.md"
# numerals of the README that are not results: versions, dates, commit ids, interface values
README_ALLOW = {"0.2.0", "0.1.0", "2026", "2025", "100", "75", "50", "254", "205", "0.196", "0.65", "400", "71", "114", "-10"}


def readme_numbers():
    """The README quotes numbers by hand. Those in its News, Overview, Evaluation and
    Deployment-cautions sections must occur in the manuscript sources or the result files
    (versions, dates and interface values are listed in README_ALLOW)."""
    global target
    target = "README.md"
    sections = re.split(r"(?m)^## ", README.read_text(encoding="utf-8"))
    body = "\n".join(sec for sec in sections if sec.split("\n", 1)[0].strip().lstrip("\U0001F4E2\U0001F4CA\u26A0\uFE0F ").startswith(
        ("News", "Overview", "Evaluation", "Deployment cautions")))
    body = re.sub(r"`[0-9a-f]{7}`|\]\([^)]*\)|results/[\w./-]+|\bE\d\b|\bE\d[\u2013-]E\d\b|\b\d{4}-\d{2}\b", "", body)
    src = "\n".join([tex_text(ARXIV), md_text(PAPER.read_text(encoding="utf-8"))])
    bad = []
    for m in re.finditer(r"(?<![\w.])-?\d[\d,]*(?:\.\d+)*(?![\w])", body):
        x = m.group(0).rstrip(",")
        if x in README_ALLOW or x.lstrip("-") in README_ALLOW or len(x.lstrip("-")) == 1:
            continue
        if not re.search(r"(?<![\d.,])" + re.escape(x.lstrip("-")) + r"(?!\d|\.\d)", src):
            bad.append(x)
    check("README.md: numerals of its News, Overview, Evaluation and Deployment-cautions sections occur in the manuscript", not bad,
          str(sorted(set(bad))[:20]))


COVER, MATHDEC, LOCK = {}, [], []
LOCKFILE = ARXIV / "structural_numerals.tsv"
if len(TARGETS) > 1:
    target, text = TARGETS[1]
    guard_rules()
    guard_v2()
    COVER = coverage()
    passes.append(f"[arxiv_ref] numeral coverage: {sum(COVER.values())} numerals located")
    # Structural numerals are not recomputed, so they are locked instead: the committed
    # list names every one of them (file, value, preceding text, reason) in order. A
    # changed, added or removed structural numeral fails until the list is rewritten
    # with --update-lock, which shows up in the diff for review.
    body = "".join(f"{f}\t{tok}\t{ctx}\t{why}\n" for f, tok, ctx, why in LOCK)
    if "--update-lock" in sys.argv:
        LOCKFILE.write_text("file\tnumeral\tpreceding text\treason\n" + body, encoding="utf-8")
    want = LOCKFILE.read_text(encoding="utf-8").split("\n", 1)[1] if LOCKFILE.exists() else ""
    old = [(l.split("\t")[0], l.split("\t")[1]) for l in want.splitlines()]
    new = [(f, tok) for f, tok, _, _ in LOCK]
    if old != new:
        import difflib
        d = [x for x in difflib.unified_diff([" ".join(o) for o in old], [" ".join(n) for n in new], lineterm="", n=0)][2:8]
        check("structural numerals equal the committed list (structural_numerals.tsv)", False, "; ".join(d))
    else:
        check("structural numerals equal the committed list (structural_numerals.tsv)", True)

companion_fidelity()
readme_numbers()
print(f"PASS {len(passes)}")
if deferred:
    print(f"DEFERRED {len(deferred)} code-state checks of the companion {COMPANION} (it is re-synced at the end of P5):")
    for d_ in deferred:
        print("  " + d_)
for k_, v_ in sorted(COVER.items()):
    print(f"COVERAGE {v_:4d}  {k_}")
if "--math" in sys.argv:
    for l_ in MATHDEC:
        print("MATHDEC", l_)
for f in failures:
    print(f"FAIL {f}")
sys.exit(1 if failures else 0)
