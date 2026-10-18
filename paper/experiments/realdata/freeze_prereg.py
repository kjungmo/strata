#!/usr/bin/env python3
"""Applies the mechanical freeze rule of PREREG_P1B.md (sections 1, 6, 7, 8) to the committed result tables and writes
PREREG.md and PREREG.json. Nothing is chosen here: every value comes from a table, a gate verdict or derive_defaults.py.

    python3 -I freeze_prereg.py --results-dir results_p1b --out-dir .
"""
import argparse
import csv
import hashlib
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import derive_defaults as dd  # noqa: E402

ALPHA = BETA = 0.01
GAP_RULE = 0.15


def load_csv(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def num(x):
    try:
        v = float(x)
        return v
    except (TypeError, ValueError):
        return float("nan")


def read_meta(path):
    d = {}
    if not os.path.exists(path):
        return d
    with open(path) as f:
        for line in f:
            if "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                d[k] = v
    return d


def find_gap(rows, dataset, res, W, miss, pair, n_min=5):
    for r in rows:
        if (r["dataset"] == dataset and r["res_m"] == res and r["W"] == str(W) and r["miss"] == miss and r["pair"] == pair
                and r["N_min"] == str(n_min)):
            return r
    return None


def gate_pass(gates, name):
    for g in gates:
        if g["sequence"] == name:
            return g.get("gates_pass") == "PASS", g
    return False, None


def interval(r, k):
    return "[%s, %s]" % (r[k + "_lo"], r[k + "_hi"])


def pair_from_row(r):
    """m0, m1 from the point estimates of a gap row, by the rounding rule of section 1 (recomputed, not read)."""
    q_s, q_c = num(r["q_s_p10"]), num(r["q_c_p90"])
    m0 = max(0.05, round(math.ceil(q_c / 0.05 - 1e-9) * 0.05, 2))
    m1 = min(0.95, round(math.floor(q_s / 0.05 + 1e-9) * 0.05, 2))
    return m0, m1


def decide_2d(gap_rows, gates):
    out = {"backend": "grid2d", "condition": "LILocBench, resolution 0.05 m, W = 15 steps (1.0 s at 15 Hz), N_min = 5, structure@static_0 against hit_in_free@dynamics_0",
           "miss_definition": "today (pass-through: a beam without a return adds no observation)"}
    reasons = []
    for seq in ("lil_mapping", "lil_static_0", "lil_dynamics_0"):
        ok, g = gate_pass(gates, seq)
        if g is None:
            reasons.append("%s: no gate record" % seq)
        elif not ok:
            parts = []
            if g["g1_pass"] != "PASS":
                parts.append("G1 %s (accepted %s, median %s m, p90 %s m; limits %s, %s)" % (g["g1_pass"], g["g1_accepted"], g["g1_p50_m"], g["g1_p90_m"], g["g1_limit_p50_m"], g["g1_limit_p90_m"]))
            if g["g2_pass"] != "PASS":
                parts.append("G2 %s (matched %s, median %s m, p90 %s m)" % (g["g2_pass"], g["g2_matched_share"], g["g2_p50_m"], g["g2_p90_m"]))
            reasons.append("%s fails: %s" % (seq, "; ".join(parts)))
    r = find_gap(gap_rows, "lil", "0.05", 15, "-", "structure@static_0_vs_clutter@dynamics_0")
    out["gap_row"] = r
    if r is None or math.isnan(num(r["gap"])):
        reasons.append("no gap row for the shipped 2D condition")
    elif num(r["gap"]) < GAP_RULE:
        reasons.append("gap %s < 0.15 (the pass-through miss is already today's 2D miss; Option C is not implemented)" % r["gap"])
    if reasons:
        out["status"] = "NOT FROZEN"
        out["reasons"] = reasons
        if r is not None and not math.isnan(num(r["gap"])) and num(r["gap"]) >= GAP_RULE:
            out["conditional"] = freeze_values(*pair_from_row(r))      # what the rounding rule gives; NOT a freeze
        return out
    m0, m1 = pair_from_row(r)
    out.update(status="FROZEN", reasons=[])
    out.update(freeze_values(m0, m1))
    return out


def freeze_values(m0, m1):
    t = dd.table(m0, m1, ALPHA, BETA)
    return {"m0": m0, "m1": m1, "alpha": ALPHA, "beta": BETA, "l_hit": t["l_hit"], "l_miss": t["l_miss"], "A": t["A"], "B": t["B"],
            "ceiling": t["ceiling"], "n_max_undecided": t["n_max_undecided"], "undecided_at_n_max": t["undecided_at_n_max"],
            "latency_all_hit": t["latency_all_hit"], "mstar": t["mstar"], "oc": t["oc"], "EN": t["EN"]}


def decide_3d(gap_rows, gates, offset_meta, convert_meta, equivalence_text, two_d):
    out = {"backend": "voxel3d", "condition": "Spires Keble 02, resolution 0.2 m, W = 10 steps (1.0 s at 10 Hz), N_min = 5, structure (non-ground) against clutter"}
    checks = []
    ok, g = gate_pass(gates, "keble02")
    checks.append(("G1 and G2 on Keble 02", ok, "no gate record" if g is None else
                   "G1 %s (accepted %s, median %s m, p90 %s m), G2 %s (matched %s, median %s m, p90 %s m)" % (g["g1_pass"], g["g1_accepted"], g["g1_p50_m"], g["g1_p90_m"], g["g2_pass"], g["g2_matched_share"], g["g2_p50_m"], g["g2_p90_m"])))
    checks.append(("time offset search valid", offset_meta.get("valid") == "1", "valid=%s reason=%s selected_dt_s=%s" % (offset_meta.get("valid"), offset_meta.get("reason"), offset_meta.get("selected_dt_s"))))
    checks.append(("alignment check (share >= 0.5)", offset_meta.get("alignment_ok") == "1", "alignment_share=%s" % offset_meta.get("alignment_share")))
    lines = [l for l in equivalence_text.splitlines() if l.startswith("r=")]
    equiv_ok = len(lines) == 4 and all(" IDENTICAL " in l for l in lines)
    checks.append(("lean probe equals the engine on KITTI 00 (4 of 4 tables byte-identical)", equiv_ok, "%d of %d identical" % (sum(" IDENTICAL " in l for l in lines), len(lines))))
    out["checks"] = [{"check": c, "pass": p, "detail": d} for c, p, d in checks]
    valid = all(p for _, p, _ in checks)
    today = find_gap(gap_rows, "keble02", "0.2", 10, "today", "structure_vs_clutter")
    refined = find_gap(gap_rows, "keble02", "0.2", 10, "refined", "structure_vs_clutter")
    out["gap_today_row"], out["gap_refined_row"] = today, refined
    gt = num(today["gap"]) if today else float("nan")
    gr = num(refined["gap"]) if refined else float("nan")
    if valid and gt >= GAP_RULE:
        out.update(branch="T", status="FROZEN", miss_definition="today (the engine's half-voxel march, every sample is a miss)")
        out.update(freeze_values(*pair_from_row(today)))
        out["source_row"] = today
    elif valid and gr >= GAP_RULE:
        out.update(branch="R", status="FROZEN", miss_definition="refined (no pass-through miss within one voxel of the endpoint along the ray); a backend change")
        out.update(freeze_values(*pair_from_row(refined)))
        out["source_row"] = refined
    else:
        out.update(branch="F", miss_definition="today")
        if gt >= GAP_RULE:
            out["conditional_branch"] = "T"
            out["conditional"] = freeze_values(*pair_from_row(today))
            out["conditional_miss_definition"] = "today"
        elif gr >= GAP_RULE:
            out["conditional_branch"] = "R"
            out["conditional"] = freeze_values(*pair_from_row(refined))
            out["conditional_miss_definition"] = "refined"
        why = [c for c, p, _ in checks if not p]
        if not (gt >= GAP_RULE) and not (gr >= GAP_RULE):
            why.append("the gap is below 0.15 under both miss definitions (today %s, refined %s), whatever the checks say. "
                       "Caveat (review finding 1, deviations item 23): 64 %% of the Keble 02 clutter cells lie more than 0.5 m above the device, so the "
                       "clutter class is mostly not walking people and this gap is not evidence that hit rates do not separate in 3D" %
                       (today["gap"] if today else "n/a", refined["gap"] if refined else "n/a"))
        out["reasons"] = why
        if two_d.get("status") == "FROZEN":
            out["status"] = "FROZEN (fallback)"
            out.update(freeze_values(two_d["m0"], two_d["m1"]))
            out["flag"] = "not calibrated for 3D; on single-pass driving data most structure remains Undecided"
        else:
            out["status"] = "NOT FROZEN"
            out["reasons"].append("the fallback is the 2D pair, and the 2D pair is not frozen")
    return out


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def row_line(r):
    return ("q_s %s %s, q_c %s %s, gap %s %s; N_structure %s, N_clutter %s" %
            (r["q_s_p10"], interval(r, "q_s_ci95_block"), r["q_c_p90"], interval(r, "q_c_ci95_block"), r["gap"], interval(r, "gap_ci95_block"), r["n_structure"], r["n_clutter"]))


def render(two_d, three_d, commit, inputs):
    L = []
    L.append("# PREREG: frozen STRATA v2 evidence defaults (phase 1b)")
    L.append("")
    L.append("Written by `freeze_prereg.py` from the committed result tables of `results_p1b/` (commit `%s`, %s). Not edited by hand." % (commit, time.strftime("%Y-%m-%dT%H:%M:%S%z")))
    L.append("The rule is `PREREG_P1B.md` sections 1, 6, 7 and 8; the definitions are those of `PREREG_P1.md` with Amendments 1 and 2.")
    L.append("`m0 = ceil_0.05(q_c)`, `m1 = floor_0.05(q_s)`, `alpha = beta = 0.01`; `l_hit = ln(m1/m0)`, `l_miss = ln((1-m1)/(1-m0))`, `A = -B = ln(99)`,")
    L.append("ceiling `A + 2 l_hit`; engine floor rule: no lower clamp before `n >= 24` touched windows, reject at `l <= B` once `n >= 24`.")
    L.append("`n_max` is the smallest multiple of 16 with P(undecided after n windows) <= 0.03 for iid windows at the zero-drift rate `m*`, exact under that rule.")
    L.append("")
    for d in (two_d, three_d):
        L.append("## %s" % ("2D backend (`grid2d`)" if d["backend"] == "grid2d" else "3D backend (`voxel3d`)"))
        L.append("")
        L.append("Status: **%s**%s" % (d["status"], (" (branch %s)" % d["branch"]) if "branch" in d else ""))
        L.append("")
        L.append("Condition: %s." % d["condition"])
        L.append("")
        if d["backend"] == "voxel3d":
            L.append("Checks before the rule is read:")
            L.append("")
            for c in d["checks"]:
                L.append("- %s: **%s** (%s)" % (c["check"], "pass" if c["pass"] else "FAIL", c["detail"]))
            L.append("")
            if d.get("gap_today_row"):
                L.append("- gap under today's miss: %s" % row_line(d["gap_today_row"]))
            if d.get("gap_refined_row"):
                L.append("- gap under the refined miss: %s" % row_line(d["gap_refined_row"]))
            L.append("")
        else:
            if d.get("gap_row"):
                L.append("- %s" % row_line(d["gap_row"]))
                L.append("")
        if d.get("reasons"):
            L.append("Reasons:")
            L.append("")
            for r in d["reasons"]:
                L.append("- %s" % r)
            L.append("")
        if d.get("flag"):
            L.append("Flag: %s." % d["flag"])
            L.append("")
        if d.get("conditional"):
            c = d["conditional"]
            L.append("Conditional values, **NOT A FREEZE**: what the rounding rule gives from the measured gap if the failed gate or check were waived%s. They are printed so that the lead can decide; no engine default may take them from here." %
                     ((", branch %s, miss definition %s" % (d.get("conditional_branch"), d.get("conditional_miss_definition"))) if d.get("conditional_branch") else ""))
            L.append("")
            L.append("| m0 | m1 | alpha = beta | A = -B | l_hit | l_miss | ceiling | n_max | windows for an always-hit cell | m* |")
            L.append("|---|---|---|---|---|---|---|---|---|---|")
            L.append("| %.2f | %.2f | %.2f | %.4f | %.4f | %.4f | %.4f | %d | %d | %.4f |" % (c["m0"], c["m1"], c["alpha"], c["A"], c["l_hit"], c["l_miss"], c["ceiling"], c["n_max_undecided"], c["latency_all_hit"], c["mstar"]))
            L.append("")
        if "m0" in d:
            src = d.get("source_row") or d.get("gap_row")
            L.append("| quantity | value |")
            L.append("|---|---|")
            L.append("| m0 | %.2f |" % d["m0"])
            L.append("| m1 | %.2f |" % d["m1"])
            L.append("| alpha = beta | %.2f |" % d["alpha"])
            L.append("| A = -B | %.4f |" % d["A"])
            L.append("| l_hit | %.4f |" % d["l_hit"])
            L.append("| l_miss | %.4f |" % d["l_miss"])
            L.append("| ceiling (A + 2 l_hit) | %.4f |" % d["ceiling"])
            L.append("| n_max_undecided (windows) | %d (P(undecided) %.4f) |" % (d["n_max_undecided"], d["undecided_at_n_max"]))
            L.append("| windows for an always-hit cell | %d |" % d["latency_all_hit"])
            L.append("| zero-drift rate m* | %.4f |" % d["mstar"])
            L.append("| Wald P(graduate) at m0 / 0.30 / 0.50 / m1 | %s |" % " / ".join("%.3f" % v for v in d["oc"].values()))
            L.append("| Wald E[N] at m0 / m* / m1 | %s |" % " / ".join("%.1f" % d["EN"][k] for k in ("m0", "mstar", "m1")))
            L.append("| miss definition | %s |" % d["miss_definition"])
            if src:
                L.append("| source row | %s |" % row_line(src))
            L.append("")
    L.append("## Inputs (sha256)")
    L.append("")
    for k, v in sorted(inputs.items()):
        L.append("- `%s` %s" % (k, v))
    L.append("")
    return "\n".join(L)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--commit", default="")
    a = ap.parse_args(argv)
    rd = a.results_dir
    gap_rows = load_csv(os.path.join(rd, "p1b_gap.csv"))
    gates = load_csv(os.path.join(rd, "p1b_gates.csv"))
    offset_meta = read_meta(os.path.join(rd, "meta", "spires_offset.meta"))
    convert_meta = read_meta(os.path.join(rd, "meta", "spires_convert.meta"))
    eq_path = os.path.join(rd, "equivalence_kth.txt")
    equivalence = ""
    if os.path.exists(eq_path):
        with open(eq_path) as f:
            equivalence = f.read()
    two_d = decide_2d(gap_rows, gates)
    three_d = decide_3d(gap_rows, gates, offset_meta, convert_meta, equivalence, two_d)
    inputs = {}
    for n in ("p1b_gap.csv", "p1b_gates.csv", "equivalence_kth.txt", os.path.join("meta", "spires_offset.meta"), os.path.join("meta", "spires_convert.meta")):
        p = os.path.join(rd, n)
        if os.path.exists(p):
            inputs["results_p1b/" + n] = sha(p)
    text = render(two_d, three_d, a.commit, inputs)
    with open(os.path.join(a.out_dir, "PREREG.md"), "w") as f:
        f.write(text)
    keep = lambda d: {k: v for k, v in d.items() if k not in ("gap_row", "gap_today_row", "gap_refined_row", "source_row")}  # noqa: E731
    with open(os.path.join(a.out_dir, "PREREG.json"), "w") as f:
        json.dump({"grid2d": keep(two_d), "voxel3d": keep(three_d)}, f, indent=1, sort_keys=True)
        f.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
