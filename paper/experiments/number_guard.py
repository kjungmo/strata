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
RESULTS = HERE / "results"

text = PAPER.read_text(encoding="utf-8")
failures = []
passes = []


def rows(name):
    with open(RESULTS / name, newline="") as f:
        return [r for r in csv.DictReader(f) if not r[list(r)[0]].startswith("#")]


def check(label, cond, detail=""):
    (passes if cond else failures).append(f"{label}{' — ' + detail if detail else ''}")


def in_paper(label, *variants):
    ok = any(v in text for v in variants)
    check(label, ok, f"none of {variants} found in paper" if not ok else "")


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
check("E2 FPR is 1/5", abs(fpr - 0.2) < 1e-9, str(fpr))
in_paper("E2 TPR quoted as fraction", "2/3")
in_paper("E2 FPR quoted as fraction", "1/5")

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

print(f"PASS {len(passes)}")
for f in failures:
    print(f"FAIL {f}")
sys.exit(1 if failures else 0)
