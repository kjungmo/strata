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
    check("E2 FPR is 2/5", abs(fpr - 0.4) < 1e-9, str(fpr))
    in_paper("E2 TPR quoted as fraction", "2/3")
    in_paper("E2 FPR quoted as fraction", "2/5")
    # read-out length sweep (post-fix) and its pre-fix counterpart
    for sub, mean_q in (("", "0.331"), ("pre_fix_2026-09-28/", "0.363")):
        sw = rows(sub + "e2_rates_vs_length.csv")
        fp = [int(r["fp"]) for r in sw]
        m = sum(fp) / (5 * len(fp))
        check(f"E2 sweep {sub or 'post'} lengths 8..100", [int(r["obs_length"]) for r in sw] == list(range(8, 101)))
        check(f"E2 sweep {sub or 'post'} mean FPR {mean_q}", f"{m:.3f}" == mean_q, f"{m:.3f}")
        check(f"E2 sweep {sub or 'post'} FP range 0-4", (min(fp), max(fp)) == (0, 4))
        check(f"E2 sweep {sub or 'post'} TPR 2/3 at every length", {r["tp"] for r in sw} == {"2"})
        in_paper(f"E2 sweep mean FPR {mean_q} quoted", mean_q)
    in_paper("E2 sweep FP range quoted", "0 to 4/5", "0–4 of 5")

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
    fp = float(cls["aperiodic_1"]["ref_amplitude"])
    in_paper("E2 FP margin over a_min", f"{fp - 0.3:.3f}")
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
    check("aperiodic_0 is a pipeline FP at n=64", cls["aperiodic_0"]["final_class"] == "Periodic")
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
    for n, v in (("e3_sensitivity.csv", 3672), ("e3_sensitivity_periodic.csv", 5472),
                 (pre + "e3_sensitivity_periodic.csv", 7132)):
        check(f"E3 total flicker {n} == {v}", tot(n) == v, str(tot(n)))
        in_paper(f"E3 total flicker {v} quoted", str(v))
    e3p = {(r["graduate_prob"], r["demote_prob"], r["survival_decay"]): r for r in rows("e3_sensitivity_periodic.csv")}
    e3pp = {(r["graduate_prob"], r["demote_prob"], r["survival_decay"]): r for r in rows(pre + "e3_sensitivity_periodic.csv")}
    check("E3 P-on wide band 102 / pre 138", (e3p[("0.9", "0.3", "0.9")]["flicker_transitions"],
          e3pp[("0.9", "0.3", "0.9")]["flicker_transitions"]) == ("102", "138"))
    in_paper("E3 P-on 102 quoted", "102")
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
    check("32 core gtest cases", ntests == 32, str(ntests))
    in_paper("32 tests quoted", "32 ")


if len(TARGETS) > 1:
    target, text = TARGETS[1]
    guard_arxiv()


print(f"PASS {len(passes)}")
for f in failures:
    print(f"FAIL {f}")
sys.exit(1 if failures else 0)
