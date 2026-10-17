#!/usr/bin/env python3
"""Phase 1b analysis (PREREG_P1B.md): W = 15 on LILocBench, Keble 02 in 3D with both miss definitions,
the KITTI 00 secondary check with the refined miss, the gate table and the grazing diagnostic.

The statistics are those of analyze_p1.py (imported, unchanged): percentiles, gap, 2000-replicate bootstrap
(seed 20261014, tiles of 10 m), touched windows, runs and lag-1, matched-rate bands and AUC, diversity,
survivorship. New here: block-bootstrap intervals of q_s and q_c next to the gap, and the rounded pair.

Run with: python3 -I analyze_p1b.py --data-dir <bulky p1b dir> --old-meta-dir <results_p1/meta> --meta-dir <results_p1b/meta>
                                    --out-dir <results_p1b> [--no-figures]
"""
import argparse
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze_p1 as a1  # noqa: E402

GAP_RULE = a1.GAP_RULE


# ------------------------------------------------------------------ rounding (PREREG_P1B.md section 1)
def ceil_005(x):
    return round(math.ceil(x / 0.05 - 1e-9) * 0.05, 2)


def floor_005(x):
    return round(math.floor(x / 0.05 + 1e-9) * 0.05, 2)


def round_pair(q_s, q_c):
    """m0 = ceil_0.05(q_c), m1 = floor_0.05(q_s), m0 >= 0.05, m1 <= 0.95. 'Outward' is toward each other."""
    m0 = max(0.05, ceil_005(q_c))
    m1 = min(0.95, floor_005(q_s))
    return m0, m1


# ------------------------------------------------------------------ bootstrap of q_s, q_c and the gap
def bootstrap_quantiles(rs, ts, rc, tc, joint, reps=a1.BOOT_REPS, seed=a1.BOOT_SEED):
    """Block (tile) bootstrap with the same draws as analyze_p1.bootstrap_gap, returning 95 % percentile
    intervals of q_s (p10 of rs), q_c (p90 of rc) and of the gap. Keys 'qs', 'qc', 'gap' -> (lo, hi)."""
    nan2 = (float("nan"), float("nan"))
    if len(rs) == 0 or len(rc) == 0:
        return {"qs": nan2, "qc": nan2, "gap": nan2}
    us, _ = a1.unique_counts(rs)
    uc, _ = a1.unique_counts(rc)
    ms = a1.tile_count_matrix(rs, ts, us)
    mc = a1.tile_count_matrix(rc, tc, uc)
    if joint:
        all_t = np.unique(np.vstack([ts, tc]), axis=0)

        def align(m, tiles):
            tl = np.unique(tiles, axis=0)
            full = np.zeros((len(all_t), m.shape[1]))
            key = {tuple(t): i for i, t in enumerate(all_t)}
            for i, t in enumerate(tl):
                full[key[tuple(t)]] = m[i]
            return full

        ms, mc = align(ms, ts), align(mc, tc)
    rng = np.random.RandomState(seed + 1)
    qs_b = np.empty(reps)
    qc_b = np.empty(reps)
    for b in range(reps):
        if joint:
            mult = rng.multinomial(len(ms), np.ones(len(ms)) / len(ms)).astype(np.float64)
            ws, wc = mult @ ms, mult @ mc
        else:
            ws = rng.multinomial(len(ms), np.ones(len(ms)) / len(ms)).astype(np.float64) @ ms
            wc = rng.multinomial(len(mc), np.ones(len(mc)) / len(mc)).astype(np.float64) @ mc
        qs_b[b] = a1.pct_weighted(us, ws, 10.0)
        qc_b[b] = a1.pct_weighted(uc, wc, 90.0)
    gap_b = qs_b - qc_b
    ci = lambda x: (float(np.nanpercentile(x, 2.5)), float(np.nanpercentile(x, 97.5)))  # noqa: E731
    return {"qs": ci(qs_b), "qc": ci(qc_b), "gap": ci(gap_b)}


HEAD_GAP_B = ["dataset", "res_m", "W", "miss", "pair", "N_min", "n_structure", "n_clutter", "q_s_p10", "q_c_p90", "gap",
              "gap_ci95_iid_lo", "gap_ci95_iid_hi", "gap_ci95_block_lo", "gap_ci95_block_hi",
              "q_s_ci95_block_lo", "q_s_ci95_block_hi", "q_c_ci95_block_lo", "q_c_ci95_block_hi",
              "gap_ge_0.15", "auc_rate", "m0_rounded", "m1_rounded"]


def gap_rows(cond, miss, pname, gs, gc, joint, tables):
    for n_min in a1.N_MIN_SENSITIVITY:
        row = [cond["dataset"], cond["res"], cond["W"], miss, pname, n_min]
        if gs is None or gc is None or len(gs) == 0 or len(gc) == 0:
            tables["gap"].append(row + ["n/a"] * 17)
            continue
        rs_, ms_ = a1.rates(gs, n_min)
        rc_, mc_ = a1.rates(gc, n_min)
        if len(rs_) == 0 or len(rc_) == 0:
            tables["gap"].append(row + [len(rs_), len(rc_)] + ["n/a"] * 15)
            continue
        qs, qc = float(np.percentile(rs_, 10)), float(np.percentile(rc_, 90))
        gap = qs - qc
        ci_iid = a1.bootstrap_gap(rs_, gs.tile[ms_], rc_, gc.tile[mc_], joint=joint)["iid"]
        ci = bootstrap_quantiles(rs_, gs.tile[ms_], rc_, gc.tile[mc_], joint)
        m0, m1 = round_pair(qs, qc)
        tables["gap"].append(row + [len(rs_), len(rc_), a1.fmt(qs, 4), a1.fmt(qc, 4), a1.fmt(gap, 4),
                                    a1.fmt(ci_iid[0], 4), a1.fmt(ci_iid[1], 4), a1.fmt(ci["gap"][0], 4), a1.fmt(ci["gap"][1], 4),
                                    a1.fmt(ci["qs"][0], 4), a1.fmt(ci["qs"][1], 4), a1.fmt(ci["qc"][0], 4), a1.fmt(ci["qc"][1], 4),
                                    "yes" if gap >= GAP_RULE else "no", a1.fmt(a1.auc(rs_, rc_), 4),
                                    "%.2f" % m0, "%.2f" % m1])
    for b in a1.band_stats(gs, gc, a1.N_MIN_PRIMARY):
        tables["band"].append([cond["dataset"], cond["res"], cond["W"], pname + "|" + miss, a1.N_MIN_PRIMARY, b["band"],
                               b.get("s_cells", 0), a1.fmt(b.get("s_switch_ratio")), a1.fmt(b.get("s_lag1_mean")),
                               b.get("c_cells", 0), a1.fmt(b.get("c_switch_ratio")), a1.fmt(b.get("c_lag1_mean")),
                               a1.fmt(b["auc_lag1_structure_over_clutter"])])


def keble_classify(lab, h, n):
    return {1: "clutter", 2: "structure_ground", 3: "structure"}.get(lab & 15)


# ------------------------------------------------------------------ conditions
def do_lil(data_dir, tables, inventory):
    for res_label, res_file in (("0.05", "0.05"), ("0.1", "0.10")):
        W = 15
        gr, tc = {}, {}
        for seq in ("mapping", "static_0", "dynamics_0"):
            f = os.path.join(data_dir, "lil_cells_%s_r%s_W%d.csv" % (seq, res_file, W))
            if not os.path.exists(f):
                inventory.append(["lil_" + seq, res_label, W, "-", "missing"])
                continue
            survival = {}
            groups, touched = a1.read_cells(f, float(res_label), a1.lil_classify, min(a1.N_MIN_SENSITIVITY), survival)
            gr[seq], tc[seq] = groups, touched
            meta = a1.read_meta(f.replace(".csv", ".meta"))
            inventory.append(["lil_" + seq, res_label, W, "-", "ok", meta.get("windows_closed"), meta.get("seconds"), meta.get("peak_rss_kb")])
            cond = {"dataset": "lil", "res": res_label, "W": W, "source": seq, "key": "lil_%s_r%s_W%d" % (seq, res_label, W)}
            a1.analyse_condition(cond, groups, touched, a1.N_MIN_SENSITIVITY, [], False, None, tables)
            tables["surv"].extend(a1.survivorship_rows(cond, survival))
        if "static_0" in gr and "dynamics_0" in gr:
            cond = {"dataset": "lil", "res": res_label, "W": W}
            for pname, ssrc, csrc in (("structure@static_0_vs_clutter@dynamics_0", "static_0", "dynamics_0"),
                                      ("structure@dynamics_0_vs_clutter@dynamics_0", "dynamics_0", "dynamics_0"),
                                      ("structure@static_0_vs_hit_in_free@static_0", "static_0", "static_0")):
                gap_rows(cond, "-", pname, gr[ssrc].get("structure"), gr[csrc].get("hit_in_free"), ssrc == csrc, tables)


def do_scene(data_dir, prefix, dataset, res_list, w_list, classify, pairs, tables, inventory, derived_static_all=False):
    """Conditions <prefix>_r<res>_W<W>_<today|refined>.csv. pairs: (name, structure group, clutter group)."""
    for res in res_list:
        for W in w_list:
            for miss in ("today", "refined"):
                f = os.path.join(data_dir, "%s_r%s_W%d_%s.csv" % (prefix, res, W, miss))
                if not os.path.exists(f):
                    inventory.append([dataset, res, W, miss, "missing"])
                    continue
                survival = {}
                groups, touched = a1.read_cells(f, float(res), classify, min(a1.N_MIN_SENSITIVITY), survival)
                if derived_static_all:
                    sa = a1.Group()
                    for nm in ("static_ground", "static_structure"):
                        g = groups.get(nm)
                        if g is None:
                            continue
                        for k in ("N", "H", "div", "span", "hh", "mm", "sw"):
                            setattr(sa, k, list(getattr(sa, k)) + list(getattr(g, k)))
                        sa.tile = list(sa.tile) + [tuple(t) for t in g.tile]
                        for att in ("interior_hit", "interior_miss", "all_hit", "all_miss"):
                            for kk, vv in getattr(g, att).items():
                                getattr(sa, att)[kk] = getattr(sa, att).get(kk, 0) + vv
                    if len(sa.N):
                        groups["static_all"] = sa.freeze()
                        touched["static_all"] = np.concatenate([touched.get("static_ground", np.array([], dtype=np.int64)),
                                                                touched.get("static_structure", np.array([], dtype=np.int64))])
                meta = a1.read_meta(f.replace(".csv", ".meta"))
                inventory.append([dataset, res, W, miss, "ok", meta.get("windows_closed"), meta.get("seconds"), meta.get("peak_rss_kb")])
                cond = {"dataset": dataset, "res": res, "W": W, "source": "%s_%s" % (dataset, miss), "key": "%s_r%s_W%d_%s" % (dataset, res, W, miss)}
                a1.analyse_condition(cond, groups, touched, a1.N_MIN_SENSITIVITY, [], True, None, tables)
                tables["surv"].extend(a1.survivorship_rows(cond, survival))
                for pname, sname, cname in pairs:
                    gap_rows(cond, miss, pname, groups.get(sname), groups.get(cname), True, tables)


def grazing_rows(data_dir, prefix, dataset, res_list, w_list, struct_group, rows):
    """Grazing diagnostic (PREREG_P1B.md section 5) from the meta files and the touched-window totals of the structure cells."""
    for res in res_list:
        for W in w_list:
            mt = os.path.join(data_dir, "%s_r%s_W%d_today.meta" % (prefix, res, W))
            if not os.path.exists(mt):
                continue
            m = a1.read_meta(mt)
            tot = {}
            for miss in ("today", "refined"):
                f = os.path.join(data_dir, "%s_r%s_W%d_%s.csv" % (prefix, res, W, miss))
                if not os.path.exists(f):
                    continue
                _, touched = a1.read_cells(f, float(res), (keble_classify if dataset == "keble02" else a1.kth_classify), 10 ** 9)
                tot[miss] = int(touched.get(struct_group, np.array([], dtype=np.int64)).sum())
            ev, evg = int(m["grazing_structure_miss_events"]), int(m["grazing_structure_miss_events_with_endpoint_within_1_voxel"])
            mw, mwg = int(m["grazing_structure_miss_windows"]), int(m["grazing_structure_miss_windows_with_graze_event"])
            rows.append([dataset, res, W, ev, evg, a1.fmt(evg / ev if ev else float("nan"), 4), mw, mwg,
                         a1.fmt(mwg / mw if mw else float("nan"), 4), tot.get("today", "n/a"), tot.get("refined", "n/a"),
                         a1.fmt((tot["today"] - tot["refined"]) / tot["today"] if "refined" in tot and tot.get("today") else float("nan"), 4)])


HEAD_GRAZE = ["dataset", "res_m", "W", "structure_miss_events", "events_endpoint_within_1_voxel", "share_events",
              "structure_miss_windows", "miss_windows_with_such_event", "share_windows",
              "structure_touched_windows_today", "structure_touched_windows_refined", "share_touched_windows_removed"]
HEAD_GATE = ["sequence", "class", "g1_accepted", "g1_p50_m", "g1_p90_m", "g1_limit_p50_m", "g1_limit_p90_m", "g1_pass",
             "g2_queries", "g2_matched_share", "g2_p50_m", "g2_p90_m", "g2_limit_p50_m", "g2_limit_p90_m", "g2_pass", "gates_pass",
             "old_gate_p50_m", "old_gate_p90_m"]


def gate_rows(meta_dir, p1_meta_dir, rows):
    items = [("lil_mapping", "2D", "gate2d_mapping.meta", "lil_gate_mapping.meta"), ("lil_static_0", "2D", "gate2d_static_0.meta", "lil_gate_static_0.meta"),
             ("lil_dynamics_0", "2D", "gate2d_dynamics_0.meta", "lil_gate_dynamics_0.meta"), ("kitti00", "3D", "gate_kth.meta", "kth_gate.meta"),
             ("keble02", "3D", "gate_spires.meta", None)]
    for name, cls, fn, old in items:
        path = os.path.join(meta_dir, fn)
        if not os.path.exists(path):
            rows.append([name, cls, "missing"] + [""] * 15)
            continue
        m = a1.read_meta(path)
        o = a1.read_meta(os.path.join(p1_meta_dir, old)) if old and os.path.exists(os.path.join(p1_meta_dir, old)) else {}
        f = lambda k, nd=4: a1.fmt(float(m[k]), nd)  # noqa: E731
        rows.append([name, cls, m["g1_accepted"], f("g1_thickness_m_p50"), f("g1_thickness_m_p90"), m["g1_limit_p50_m"], m["g1_limit_p90_m"],
                     "PASS" if m["g1_pass"] == "1" else "FAIL", m["g2_queries"], f("g2_matched_share"), f("g2_d_m_p50"), f("g2_d_m_p90"),
                     m["g2_limit_p50_m"], m["g2_limit_p90_m"], "PASS" if m["g2_pass"] == "1" else "FAIL",
                     "PASS" if m["gates_pass"] == "1" else "FAIL",
                     a1.fmt(float(o["thickness_m_p50"]), 4) if "thickness_m_p50" in o else "n/a",
                     a1.fmt(float(o["thickness_m_p90"]), 4) if "thickness_m_p90" in o else "n/a"])


def make_figures(tables, out_dir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    conds = {}
    for c, g, h in tables["hist"]:
        conds.setdefault((c["dataset"], c["res"], c["W"], c["source"]), []).append((c["source"], g, h))
    nb = int(round(1 / a1.HIST_BIN))
    xs = (np.arange(nb) + 0.5) * a1.HIST_BIN
    show = {"structure": "-", "static_structure": "-", "hit_in_free": ":", "clutter": ":", "moving": ":"}
    for (ds, res, W, src), items in sorted(conds.items()):
        if ds == "lil" and src != "static_0" and src != "dynamics_0":
            continue
        fig, ax = plt.subplots(figsize=(6.4, 3.4))
        n_drawn = 0
        for (_, g, h) in items:
            if g not in show or h.sum() == 0:
                continue
            if ds == "lil" and not ((g == "structure" and src == "static_0") or (g == "hit_in_free" and src == "dynamics_0")):
                continue
            ax.plot(xs, h / h.sum(), show[g], marker="o", ms=3, color="k", label="%s (n=%d)" % (g, h.sum()))
            n_drawn += 1
        if n_drawn == 0:
            plt.close(fig)
            continue
        ax.set_xlabel("per-cell hit rate, cells with N >= %d" % a1.N_MIN_PRIMARY, fontsize=8)
        ax.set_ylabel("fraction of cells", fontsize=8)
        ax.set_title("%s  res %s m  W %d  %s" % (ds, res, W, src), fontsize=9)
        ax.legend(fontsize=7)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, "fig_p1b_hist_%s_r%s_W%d_%s.png" % (ds, res, W, src)), dpi=130)
        plt.close(fig)


def run_all(data_dir, old_meta_dir, meta_dir, out_dir, parts, figures=True):
    tables = {k: [] for k in ("pct", "gap", "runs", "touch", "div", "band", "hist", "surv")}
    inventory, graze = [], []
    if "lil" in parts:
        do_lil(data_dir, tables, inventory)
    if "keble" in parts:
        do_scene(data_dir, "keble02", "keble02", ("0.2", "0.1"), (10, 20), keble_classify,
                 [("structure_vs_clutter", "structure", "clutter"), ("ground_vs_clutter", "structure_ground", "clutter")], tables, inventory)
        grazing_rows(data_dir, "keble02", "keble02", ("0.2", "0.1"), (10, 20), "structure", graze)
    if "kth" in parts:
        do_scene(data_dir, "kth", "kth00", ("0.2", "0.1"), (10, 2), a1.kth_classify,
                 [("structure_vs_moving", "static_structure", "moving"), ("static_all_vs_moving", "static_all", "moving"),
                  ("ground_vs_moving", "static_ground", "moving"), ("solid_vs_moving", "struct_solid", "moving")],
                 tables, inventory, derived_static_all=True)
        grazing_rows(data_dir, "kth", "kth00", ("0.2", "0.1"), (10, 2), "static_structure", graze)
    os.makedirs(out_dir, exist_ok=True)
    w = a1.write_csv
    w(os.path.join(out_dir, "p1b_percentiles.csv"), a1.HEAD_PCT, tables["pct"])
    w(os.path.join(out_dir, "p1b_gap.csv"), HEAD_GAP_B, tables["gap"])
    w(os.path.join(out_dir, "p1b_runs.csv"), a1.HEAD_RUNS, tables["runs"])
    w(os.path.join(out_dir, "p1b_touched_windows.csv"), a1.HEAD_TOUCH, tables["touch"])
    w(os.path.join(out_dir, "p1b_diversity.csv"), a1.HEAD_DIV, tables["div"])
    w(os.path.join(out_dir, "p1b_matched_band.csv"), a1.HEAD_BAND, tables["band"])
    w(os.path.join(out_dir, "p1b_survivorship.csv"), a1.HEAD_SURV, tables["surv"])
    nb = int(round(1 / a1.HIST_BIN))
    w(os.path.join(out_dir, "p1b_hist.csv"), ["dataset", "res_m", "W", "source", "group", "N_min"] + ["bin_%.2f" % (i * a1.HIST_BIN) for i in range(nb)],
      [[c["dataset"], c["res"], c["W"], c["source"], g, a1.N_MIN_PRIMARY] + [int(v) for v in h] for c, g, h in tables["hist"]])
    w(os.path.join(out_dir, "p1b_inventory.csv"), ["dataset", "res_m", "W", "miss", "status", "windows_closed", "seconds", "peak_rss_kb"], inventory)
    w(os.path.join(out_dir, "p1b_grazing.csv"), HEAD_GRAZE, graze)
    gates = []
    gate_rows(meta_dir, old_meta_dir, gates)
    w(os.path.join(out_dir, "p1b_gates.csv"), HEAD_GATE, gates)
    if figures:
        make_figures(tables, out_dir)
    return tables


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-dir", required=True, help="directory with the bulky phase 1b outputs (cell tables and metas)")
    ap.add_argument("--old-meta-dir", default="", help="results_p1/meta (the phase 1 gate values, shown next to the new gates)")
    ap.add_argument("--meta-dir", required=True, help="results_p1b/meta (gate metas)")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--parts", default="lil,keble,kth")
    ap.add_argument("--no-figures", action="store_true")
    a = ap.parse_args(argv)
    run_all(a.data_dir, a.old_meta_dir, a.meta_dir, a.out_dir, set(a.parts.split(",")), figures=not a.no_figures)
    return 0


if __name__ == "__main__":
    sys.exit(main())
