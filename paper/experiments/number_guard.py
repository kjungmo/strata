#!/usr/bin/env python3
"""Number guard: every headline figure quoted in paper/strata_paper.md must be
recomputable from the committed CSVs in paper/experiments/results/, and retired
figures must not reappear. Run from anywhere: python3 paper/experiments/number_guard.py
Exits nonzero on any failure."""
import csv
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


TARGETS = [("strata_paper.md", PAPER.read_text(encoding="utf-8"))]
if (ARXIV / "main.tex").exists():
    TARGETS.append(("arxiv_ref", tex_text(ARXIV)))
failures = []
passes = []
text = ""
target = ""


def rows(name):
    with open(RESULTS / name, newline="") as f:
        return [r for r in csv.DictReader(f) if not r[list(r)[0]].startswith("#")]


def check(label, cond, detail=""):
    (passes if cond else failures).append(f"[{target}] {label}{' — ' + detail if detail else ''}")


def in_paper(label, *variants):
    ok = any(v in text for v in variants)
    check(label, ok, f"none of {variants} found in paper" if not ok else "")


def guard():
    # ---- E1: static-map quality ----
    e1 = rows("e1_static_quality.csv")
    gt = {r["gt_static"] for r in e1 if int(r["window"]) > 0}
    check("E1 GT wall cells constant", gt == {"161"}, f"gt_static values: {gt}")
    in_paper("E1 '161' quoted", "161")

    late_recall = [float(r["recall"]) for r in e1 if int(r["window"]) >= 3]
    check("E1 recall==1.0 from window 3 on (all runs)", all(v == 1.0 for v in late_recall))
    in_paper("E1 'window 3' quoted", "window 3")

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
    cost_ratios, cell_ratios = [], []
    for x in extents:
        g, v = by[("grid2d", x)], by[("voxel3d", x)]
        cost_ratios.append(float(v["us_per_integrate"]) / float(g["us_per_integrate"]))
        cell_ratios.append(float(v["final_cells"]) / float(g["final_cells"]))
    for r in cost_ratios:
        in_paper(f"E4 cost ratio {r:.1f}x quoted", f"{r:.1f}")
    lo, hi = min(cost_ratios), max(cost_ratios)
    in_paper(f"E4 cost-ratio range {lo:.1f}-{hi:.1f}", f"{lo:.1f}–{hi:.1f}", f"{lo:.1f}-{hi:.1f}")
    for r in cell_ratios:
        in_paper(f"E4 live-cell ratio {r:.1f}x quoted", f"{r:.1f}")
    check("E4 bytes_per_cell all 56", {r["bytes_per_cell"] for r in e4} == {"56"})
    in_paper("E4 '56 B' quoted", "56 B")
    g100 = float(by[("grid2d", extents[0])]["us_per_integrate"])
    in_paper(f"E4 grid2d {extents[0]}^2 us {g100:.1f}", f"{g100:.1f}")
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
        for sub in ("tables", "figures/data"):
            for f in sorted((tmp / sub).iterdir()):
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
    check("56 core gtest cases", ntests == 56, str(ntests))
    in_paper("56 tests quoted", "56 ")
    quoted = {int(n) for n in re.findall(r"(\d+)(?:~|\s)+(?:gtest cases|behaviou?r-level tests|unit tests)", text)}
    check("every quoted test count is the current one", quoted <= {ntests}, str(sorted(quoted)))
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
    ps = g("wall", "pred_S") / sum(g(k, "pred_S") for k in gts)
    pl, ph = wil(g("wall", "pred_S"), sum(g(k, "pred_S") for k in gts))
    in_paper("E5 Static precision", f"{ps:.3f} [{pl:.3f}, {ph:.3f}]")
    nP = sum(g("door", c) for c in ("pred_S", "pred_P", "pred_T", "pred_U"))
    rl, rh = wil(g("door", "pred_P"), nP)
    in_paper("E5 Periodic recall", f"{g('door', 'pred_P') / nP:.3f} [{rl:.3f}, {rh:.3f}]")
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


print(f"PASS {len(passes)}")
for f in failures:
    print(f"FAIL {f}")
sys.exit(1 if failures else 0)
