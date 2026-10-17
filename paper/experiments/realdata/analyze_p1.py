#!/usr/bin/env python3
"""Phase 1 analysis: per-cell hit-rate statistics from the cell tables written by hitrate_probe.

Every number in P1_REPORT.md comes from this file applied to the cell tables
(cells_*.csv + .meta) that hitrate_probe wrote. Definitions are fixed in PREREG_P1.md.
Run with: python3 -I analyze_p1.py --data-dir <p1 data dir> --out-dir <results dir>
Needs numpy; matplotlib only for figures (--no-figures skips them).
"""
import argparse
import csv
import json
import math
import os
import re
import sys

import numpy as np

# ------------------------------------------------------------------ constants (PREREG_P1.md section 6)
N_MIN_PRIMARY = 5
N_MIN_SENSITIVITY = (2, 3, 5, 7, 10)      # PREREG_P1.md section 11.3 (was 3, 5, 10)
PERCENTILES = (1, 5, 10, 25, 50, 75, 90, 95, 99)
HIST_BIN = 0.05
TILE_M = 10.0
BOOT_REPS = 2000
BOOT_SEED = 20261014
GAP_RULE = 0.15            # DESIGN_AND_PLAN section 3 C, first branch: mechanical read-out only
WINDOW_COUNTS = (1, 2, 3, 5, 7, 10, 14, 20, 30, 50)
BANDS = ((0.2, 0.4), (0.4, 0.6), (0.6, 0.8))
RUN_RE = re.compile(r"([hm])(\d+)")


# ------------------------------------------------------------------ weighted percentile
def pct_weighted(values, counts, q):
    """numpy's default (linear) percentile of the multiset {values[i] repeated counts[i] times}.
    values ascending. Returns nan for an empty multiset."""
    counts = np.asarray(counts, dtype=np.float64)
    n = counts.sum()
    if n <= 0:
        return float("nan")
    cum = np.cumsum(counts)
    pos = (q / 100.0) * (n - 1)
    lo = math.floor(pos)
    i_lo = min(int(np.searchsorted(cum, lo, side="right")), len(values) - 1)
    i_hi = min(int(np.searchsorted(cum, lo + 1, side="right")), len(values) - 1)
    return float(values[i_lo] + (pos - lo) * (values[i_hi] - values[i_lo]))


def unique_counts(x):
    u, c = np.unique(np.asarray(x, dtype=np.float64), return_counts=True)
    return u, c


# ------------------------------------------------------------------ per-cell run features
def parse_runs(s):
    """'h3m2h1' -> [(True,3),(False,2),(True,1)]. Raises ValueError on a malformed string."""
    out = []
    pos = 0
    for m in RUN_RE.finditer(s):
        if m.start() != pos:
            raise ValueError("bad runs string %r" % s)
        pos = m.end()
        out.append((m.group(1) == "h", int(m.group(2))))
    if pos != len(s) or not out:
        raise ValueError("bad runs string %r" % s)
    for (a, _), (b, _) in zip(out, out[1:]):
        if a == b:
            raise ValueError("adjacent runs of the same state in %r" % s)
    return out


def cell_features(runs):
    """N, H, hit-hit adjacencies, miss-miss adjacencies, switches of one hit/miss sequence."""
    n = sum(l for _, l in runs)
    h = sum(l for s, l in runs if s)
    hh = sum(l - 1 for s, l in runs if s)
    mm = sum(l - 1 for s, l in runs if not s)
    return n, h, hh, mm, len(runs) - 1


def expected_switches(n, h):
    """Expected number of hit/miss switches when the h hits are placed at random among n windows."""
    return 2.0 * h * (n - h) / n if n > 0 else 0.0


def expected_hh(n, h):
    return h * (h - 1.0) / n if n > 0 else 0.0


def lag1_autocorr(n, h, hh, mm, sw):
    """Lag-1 autocorrelation of the 0/1 sequence about its own mean (numerator over the n-1 pairs,
    denominator n p (1-p)). nan if the sequence is constant or shorter than 2."""
    if n < 2 or h == 0 or h == n:
        return float("nan")
    p = h / n
    num = hh * (1 - p) ** 2 + mm * p ** 2 - sw * p * (1 - p)
    return num / (n * p * (1 - p))


def auc(pos, neg):
    """P(pos > neg) + 0.5 P(pos == neg) (Mann-Whitney). nan if either is empty."""
    pos = np.asarray(pos, dtype=np.float64)
    neg = np.asarray(neg, dtype=np.float64)
    pos = pos[~np.isnan(pos)]
    neg = neg[~np.isnan(neg)]
    if len(pos) == 0 or len(neg) == 0:
        return float("nan")
    allv = np.concatenate([pos, neg])
    u, inv, cnt = np.unique(allv, return_inverse=True, return_counts=True)
    first = np.cumsum(cnt) - cnt + 1                    # 1-based rank of the first tie
    avg = first + (cnt - 1) / 2.0                       # average rank per unique value
    ranks = avg[inv]
    r_pos = ranks[: len(pos)].sum()
    return float((r_pos - len(pos) * (len(pos) + 1) / 2.0) / (len(pos) * len(neg)))


# ------------------------------------------------------------------ cell table loading
class Group:
    """Accumulates everything the report needs for one label group of one condition."""

    def __init__(self):
        self.N = []
        self.H = []
        self.div = []
        self.span = []
        self.tile = []
        self.hh = []
        self.mm = []
        self.sw = []
        self.interior_hit = {}
        self.interior_miss = {}
        self.all_hit = {}
        self.all_miss = {}

    def add(self, n, h, div, span, tile, runs):
        _, _, hh, mm, sw = cell_features(runs)
        self.N.append(n)
        self.H.append(h)
        self.div.append(div)
        self.span.append(span)
        self.tile.append(tile)
        self.hh.append(hh)
        self.mm.append(mm)
        self.sw.append(sw)

    def freeze(self):
        for k in ("N", "H", "div", "span", "hh", "mm", "sw"):
            setattr(self, k, np.asarray(getattr(self, k), dtype=np.int64))
        self.tile = np.asarray(self.tile, dtype=np.int64).reshape(-1, 2)
        return self

    def __len__(self):
        return len(self.N)


def read_cells(path, res, classify, n_min_keep, survival=None):
    """Streams a cells CSV. classify(label, H, N) -> group name, a tuple of group names (a cell may belong
    to several nested groups), or None. Cells with N < n_min_keep are counted (touched) but their runs are
    not parsed. Returns (groups, touched_counts). If survival is a dict it receives, per group, the lists
    (N, H) of every cell of the group, kept or not (PREREG_P1.md section 11.3)."""
    groups = {}
    touched = {}
    with open(path) as f:
        head = f.readline().strip()
        if head != "label,ix,iy,iz,N,H,first,last,div,runs":
            raise ValueError("%s: unexpected header %r" % (path, head))
        for ln, line in enumerate(f, start=2):
            p = line.rstrip("\n").split(",")
            if len(p) != 10:
                raise ValueError("%s:%d: need 10 fields" % (path, ln))
            lab, ix, iy = int(p[0]), int(p[1]), int(p[2])
            n, h, first, last, div = int(p[4]), int(p[5]), int(p[6]), int(p[7]), int(p[8])
            if n < 1 or h < 0 or h > n or last < first or last - first + 1 < n:
                raise ValueError("%s:%d: inconsistent counts N=%d H=%d first=%d last=%d" % (path, ln, n, h, first, last))
            g = classify(lab, h, n)
            if g is None:
                continue
            names = (g,) if isinstance(g, str) else tuple(g)
            for nm in names:
                touched.setdefault(nm, []).append(n)
                if survival is not None:
                    sv = survival.setdefault(nm, ([], []))
                    sv[0].append(n)
                    sv[1].append(h)
            if n < n_min_keep:
                continue
            runs = parse_runs(p[9])
            nn = sum(l for _, l in runs)
            if nn != n or sum(l for s, l in runs if s) != h:
                raise ValueError("%s:%d: runs %r disagree with N=%d H=%d" % (path, ln, p[9], n, h))
            tile = (int(math.floor(ix * res / TILE_M)), int(math.floor(iy * res / TILE_M)))
            k = len(runs)
            for nm in names:
                grp = groups.setdefault(nm, Group())
                grp.add(n, h, div, last - first + 1, tile, runs)
                for i, (s, l) in enumerate(runs):
                    d_all = grp.all_hit if s else grp.all_miss
                    d_all[l] = d_all.get(l, 0) + 1
                    if 0 < i < k - 1:
                        d_in = grp.interior_hit if s else grp.interior_miss
                        d_in[l] = d_in.get(l, 0) + 1
    for g in groups.values():
        g.freeze()
    return groups, {k: np.asarray(v, dtype=np.int64) for k, v in touched.items()}


# ------------------------------------------------------------------ statistics on a group
def rates(g, n_min):
    m = g.N >= n_min
    return g.H[m] / g.N[m], m


def hist_counts(r):
    """Counts in [0,.05),...,[.90,.95),[.95,1.0]; r == 1 goes to the last bin."""
    nb = int(round(1.0 / HIST_BIN))
    idx = np.minimum(np.floor(np.asarray(r) / HIST_BIN + 1e-9).astype(np.int64), nb - 1)
    return np.bincount(idx, minlength=nb)


def weighted_percentile_from_dict(d, q):
    if not d:
        return float("nan")
    keys = np.array(sorted(d))
    return pct_weighted(keys, [d[k] for k in keys], q)


def dict_mean(d):
    n = sum(d.values())
    return sum(k * v for k, v in d.items()) / n if n else float("nan")


def tile_count_matrix(r, tiles, u):
    """T x len(u) matrix: number of cells of each tile with rate u[j]."""
    tl, tinv = np.unique(tiles, axis=0, return_inverse=True)
    col = np.searchsorted(u, r)
    m = np.zeros((len(tl), len(u)), dtype=np.float64)
    np.add.at(m, (tinv.reshape(-1), col), 1.0)
    return m


def bootstrap_gap(rs, ts, rc, tc, q_s=10.0, q_c=90.0, reps=BOOT_REPS, seed=BOOT_SEED, joint=False):
    """Bootstrap of gap = pct(rs, q_s) - pct(rc, q_c).
    Returns {'iid': (lo, hi), 'block': (lo, hi)}; 95 % percentile intervals.
    iid resamples cells; block resamples ground tiles of TILE_M metres (jointly for both groups
    if joint, which is right when both come from one scene, else independently)."""
    out = {}
    if len(rs) == 0 or len(rc) == 0:
        return {"iid": (float("nan"),) * 2, "block": (float("nan"),) * 2}
    rng = np.random.RandomState(seed)
    us, cs = unique_counts(rs)
    uc, cc = unique_counts(rc)
    gaps = np.empty(reps)
    for b in range(reps):
        ws = rng.multinomial(len(rs), cs / cs.sum())
        wc = rng.multinomial(len(rc), cc / cc.sum())
        gaps[b] = pct_weighted(us, ws, q_s) - pct_weighted(uc, wc, q_c)
    out["iid"] = (float(np.percentile(gaps, 2.5)), float(np.percentile(gaps, 97.5)))
    ms = tile_count_matrix(rs, ts, us)
    mc = tile_count_matrix(rc, tc, uc)
    if joint:
        # one common tile index set: align both matrices on the union of tiles
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
    for b in range(reps):
        if joint:
            mult = rng.multinomial(len(ms), np.ones(len(ms)) / len(ms)).astype(np.float64)
            ws, wc = mult @ ms, mult @ mc
        else:
            ws = rng.multinomial(len(ms), np.ones(len(ms)) / len(ms)).astype(np.float64) @ ms
            wc = rng.multinomial(len(mc), np.ones(len(mc)) / len(mc)).astype(np.float64) @ mc
        gaps[b] = pct_weighted(us, ws, q_s) - pct_weighted(uc, wc, q_c)
    out["block"] = (float(np.nanpercentile(gaps, 2.5)), float(np.nanpercentile(gaps, 97.5)))
    return out


def run_stats(g, n_min):
    """Transition and run-length statistics of the cells with N >= n_min, split into constant
    cells and mixed cells (0 < H < N)."""
    m = g.N >= n_min
    N, H, hh, mm, sw = g.N[m], g.H[m], g.hh[m], g.mm[m], g.sw[m]
    mixed = (H > 0) & (H < N)
    res = {"cells": int(m.sum()), "mixed_cells": int(mixed.sum()),
           "frac_constant": float(1 - mixed.sum() / m.sum()) if m.sum() else float("nan")}
    if mixed.sum() == 0:
        return res
    Nm, Hm, hhm, mmm, swm = N[mixed], H[mixed], hh[mixed], mm[mixed], sw[mixed]
    e_sw = np.array([expected_switches(a, b) for a, b in zip(Nm, Hm)])
    e_hh = np.array([expected_hh(a, b) for a, b in zip(Nm, Hm)])
    res["switch_ratio"] = float(swm.sum() / e_sw.sum())          # observed / expected switches, 1 = iid
    res["hh_ratio"] = float(hhm.sum() / e_hh.sum())
    ac = np.array([lag1_autocorr(a, b, c, d, e) for a, b, c, d, e in zip(Nm, Hm, hhm, mmm, swm)])
    res["lag1_mean"] = float(np.nanmean(ac))
    res["lag1_median"] = float(np.nanmedian(ac))
    res["lag1_iid_reference"] = float(np.mean(-1.0 / (Nm - 1.0)))
    return res


def band_stats(g_s, g_c, n_min):
    """At matched hit rate: run features of the two groups in the same rate band."""
    rows = []
    for lo, hi in BANDS:
        row = {"band": "[%.1f,%.1f)" % (lo, hi)}
        acs = {}
        for name, g in (("s", g_s), ("c", g_c)):
            if g is None or len(g) == 0:
                row.update({name + "_cells": 0})
                acs[name] = np.array([])
                continue
            m = (g.N >= n_min)
            r = np.where(m, g.H / np.maximum(g.N, 1), -1.0)
            b = m & (r >= lo) & (r < hi) & (g.H > 0) & (g.H < g.N)
            row[name + "_cells"] = int(b.sum())
            if b.sum():
                e_sw = np.array([expected_switches(a, c) for a, c in zip(g.N[b], g.H[b])])
                row[name + "_switch_ratio"] = float(g.sw[b].sum() / e_sw.sum())
                ac = np.array([lag1_autocorr(a, c, d, e, f) for a, c, d, e, f in
                               zip(g.N[b], g.H[b], g.hh[b], g.mm[b], g.sw[b])])
                acs[name] = ac
                row[name + "_lag1_mean"] = float(np.nanmean(ac))
            else:
                acs[name] = np.array([])
        row["auc_lag1_structure_over_clutter"] = auc(acs["s"], acs["c"])
        rows.append(row)
    return rows


# ------------------------------------------------------------------ conditions
KTH_STRUCT_GROUPS = {1: "struct_other", 2: "struct_solid", 3: "struct_vehicle", 4: "struct_vegetation"}


def kth_classify(lab, h, n):
    """Label byte = base | (class group << 4) (PREREG_P1.md section 11.2). Group codes 1 to 4 are
    other, solid, vehicle, vegetation; group 0 means the labels were built without classes. A
    structure cell belongs to static_structure and, if it has a class group, to that group too."""
    base, grp = lab & 15, lab >> 4
    if base == 3:
        if lab >= 16:
            return ("static_structure", KTH_STRUCT_GROUPS[grp])
        return "static_structure"
    return {1: "moving", 2: "static_ground"}.get(base)


def lil_classify(lab, h, n):
    if lab == 1:
        return "structure"
    if lab == 2 and h >= 1:
        return "hit_in_free"          # a reference-free cell that was hit at least once
    if lab == 3 and h >= 1:
        return "hit_near_wall"        # a free cell next to an occupied one that was hit at least once
    return None


def read_meta(path):
    d = {}
    with open(path) as f:
        for line in f:
            if "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                d[k] = v
    return d


def fmt(x, nd=3):
    if x is None or (isinstance(x, float) and math.isnan(x)):
        return "n/a"
    return ("%." + str(nd) + "f") % x


def write_csv(path, header, rows):
    with open(path, "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(header)
        for r in rows:
            w.writerow(r)


def pct_row(r):
    return [fmt(float(np.percentile(r, q)), 4) if len(r) else "n/a" for q in PERCENTILES]


def analyse_condition(cond, groups, touched, n_mins, pairs, joint, out_dir, tables):
    """pairs: list of (name, structure_group, clutter_group) to compute gaps for."""
    key = cond["key"]
    for gname, g in sorted(groups.items()):
        for n_min in n_mins:
            r, m = rates(g, n_min)
            tables["pct"].append([cond["dataset"], cond["res"], cond["W"], cond["source"], gname, n_min,
                                  len(touched.get(gname, [])), len(r), fmt(float(r.mean()) if len(r) else float("nan"), 4)] + pct_row(r))
        r, _ = rates(g, N_MIN_PRIMARY)
        hc = hist_counts(r) if len(r) else np.zeros(int(round(1 / HIST_BIN)), dtype=np.int64)
        tables["hist"].append((cond, gname, hc))
        rs = run_stats(g, N_MIN_PRIMARY)
        tables["runs"].append([cond["dataset"], cond["res"], cond["W"], cond["source"], gname, N_MIN_PRIMARY,
                               rs["cells"], rs["mixed_cells"], fmt(rs.get("frac_constant")), fmt(rs.get("switch_ratio")),
                               fmt(rs.get("hh_ratio")), fmt(rs.get("lag1_mean")), fmt(rs.get("lag1_median")),
                               fmt(rs.get("lag1_iid_reference")),
                               fmt(weighted_percentile_from_dict(g.interior_hit, 50), 1), fmt(dict_mean(g.interior_hit), 2),
                               fmt(weighted_percentile_from_dict(g.interior_hit, 90), 1), sum(g.interior_hit.values()),
                               fmt(weighted_percentile_from_dict(g.interior_miss, 50), 1), fmt(dict_mean(g.interior_miss), 2),
                               fmt(weighted_percentile_from_dict(g.interior_miss, 90), 1), sum(g.interior_miss.values()),
                               fmt(dict_mean(g.all_hit), 2), fmt(dict_mean(g.all_miss), 2)])
        t = touched.get(gname, np.array([], dtype=np.int64))
        tw = [cond["dataset"], cond["res"], cond["W"], cond["source"], gname, len(t)]
        tw += [fmt(float(np.percentile(t, q)), 1) if len(t) else "n/a" for q in (10, 50, 90, 99)] + [int(t.max()) if len(t) else "n/a"]
        tw += [fmt(float((t >= k).mean()), 4) if len(t) else "n/a" for k in WINDOW_COUNTS]
        tables["touch"].append(tw)
        dv = g.div[g.N >= N_MIN_PRIMARY]
        tables["div"].append([cond["dataset"], cond["res"], cond["W"], cond["source"], gname, len(dv)] +
                             [fmt(float(np.percentile(dv, q)), 1) if len(dv) else "n/a" for q in (10, 50, 90)] +
                             [fmt(float((dv >= k).mean()), 4) if len(dv) else "n/a" for k in (2, 3, 5)] +
                             [fmt(float(np.median(g.span[g.N >= N_MIN_PRIMARY] / g.N[g.N >= N_MIN_PRIMARY])), 2) if len(dv) else "n/a"])
    for pname, sname, cname, sgroups, cgroups, stouched in pairs:
        gs, gc = sgroups.get(sname), cgroups.get(cname)
        for n_min in n_mins:
            row = [cond["dataset"], cond["res"], cond["W"], pname, n_min]
            if gs is None or gc is None or len(gs) == 0 or len(gc) == 0:
                row += ["n/a"] * 11
                tables["gap"].append(row)
                continue
            rs_, ms_ = rates(gs, n_min)
            rc_, mc_ = rates(gc, n_min)
            if len(rs_) == 0 or len(rc_) == 0:
                row += [len(rs_), len(rc_)] + ["n/a"] * 9
                tables["gap"].append(row)
                continue
            qs, qc = float(np.percentile(rs_, 10)), float(np.percentile(rc_, 90))
            ci = bootstrap_gap(rs_, gs.tile[ms_], rc_, gc.tile[mc_], joint=(joint or sgroups is cgroups))
            gap = qs - qc
            row += [len(rs_), len(rc_), fmt(qs, 4), fmt(qc, 4), fmt(gap, 4),
                    fmt(ci["iid"][0], 4), fmt(ci["iid"][1], 4), fmt(ci["block"][0], 4), fmt(ci["block"][1], 4),
                    "yes" if gap >= GAP_RULE else "no", fmt(auc(rs_, rc_), 4)]
            tables["gap"].append(row)
        # matched-rate run features at the primary N_min
        for b in band_stats(gs, gc, N_MIN_PRIMARY):
            tables["band"].append([cond["dataset"], cond["res"], cond["W"], pname, N_MIN_PRIMARY, b["band"],
                                   b.get("s_cells", 0), fmt(b.get("s_switch_ratio")), fmt(b.get("s_lag1_mean")),
                                   b.get("c_cells", 0), fmt(b.get("c_switch_ratio")), fmt(b.get("c_lag1_mean")),
                                   fmt(b["auc_lag1_structure_over_clutter"])])


def survivorship_rows(cond, survival, n_min=N_MIN_PRIMARY):
    """Who survives the N_min filter: share of cells dropped by N < n_min and the mean per-cell hit rate of
    the dropped and of the kept cells (PREREG_P1.md section 11.3). survival: group -> (N list, H list)."""
    rows = []
    for gname in sorted(survival):
        n = np.asarray(survival[gname][0], dtype=np.float64)
        h = np.asarray(survival[gname][1], dtype=np.float64)
        keep = n >= n_min
        rate = h / n
        drop = ~keep
        rows.append([cond["dataset"], cond["res"], cond["W"], cond["source"], gname, len(n), int(keep.sum()), int(drop.sum()),
                     fmt(float(drop.mean()), 4) if len(n) else "n/a",
                     fmt(float(rate[drop].mean()), 4) if drop.any() else "n/a",
                     fmt(float(rate[keep].mean()), 4) if keep.any() else "n/a",
                     fmt(float(rate.mean()), 4) if len(n) else "n/a",
                     fmt(float(np.median(n[drop])), 1) if drop.any() else "n/a",
                     fmt(float(np.median(n[keep])), 1) if keep.any() else "n/a"])
    return rows


HEAD_PCT = ["dataset", "res_m", "W", "source", "group", "N_min", "cells_touched_any", "cells_eligible", "mean"] + ["p%d" % q for q in PERCENTILES]
HEAD_GAP = ["dataset", "res_m", "W", "pair", "N_min", "n_structure", "n_clutter", "q_s_p10", "q_c_p90", "gap",
            "gap_ci95_iid_lo", "gap_ci95_iid_hi", "gap_ci95_block_lo", "gap_ci95_block_hi", "gap_ge_0.15", "auc_rate"]
HEAD_RUNS = ["dataset", "res_m", "W", "source", "group", "N_min", "cells", "mixed_cells", "frac_constant", "switch_ratio",
             "hh_ratio", "lag1_mean", "lag1_median", "lag1_iid_ref", "hit_run_interior_median", "hit_run_interior_mean",
             "hit_run_interior_p90", "hit_runs_interior_n", "miss_run_interior_median", "miss_run_interior_mean",
             "miss_run_interior_p90", "miss_runs_interior_n", "hit_run_all_mean", "miss_run_all_mean"]
HEAD_TOUCH = ["dataset", "res_m", "W", "source", "group", "cells", "N_p10", "N_p50", "N_p90", "N_p99", "N_max"] + ["frac_N_ge_%d" % k for k in WINDOW_COUNTS]
HEAD_DIV = ["dataset", "res_m", "W", "source", "group", "cells", "div_p10", "div_p50", "div_p90", "frac_div_ge_2", "frac_div_ge_3", "frac_div_ge_5", "median_span_over_N"]
HEAD_SURV = ["dataset", "res_m", "W", "source", "group", "cells_N_ge_1", "cells_N_ge_5", "cells_dropped_N_lt_5", "share_dropped",
             "mean_rate_dropped", "mean_rate_kept", "mean_rate_all", "median_N_dropped", "median_N_kept"]
HEAD_BAND = ["dataset", "res_m", "W", "pair", "N_min", "band", "n_structure", "structure_switch_ratio", "structure_lag1_mean",
             "n_clutter", "clutter_switch_ratio", "clutter_lag1_mean", "auc_lag1_structure_over_clutter"]


def run_all(data_dir, out_dir, figures=True, kth=True):
    tables = {k: [] for k in ("pct", "gap", "runs", "touch", "div", "band", "hist", "surv")}
    inventory = []
    # ---- KTH
    for res in (("0.2", "0.1") if kth else ()):
        for W in (10, 2):
            f = os.path.join(data_dir, "kth00", "cells_r%s_W%d.csv" % (res, W))
            if not os.path.exists(f):
                inventory.append(["kth00", res, W, "missing"])
                continue
            cond = {"dataset": "kth00", "res": res, "W": W, "source": "kitti00", "key": "kth00_r%s_W%d" % (res, W)}
            survival = {}
            groups, touched = read_cells(f, float(res), kth_classify, min(N_MIN_SENSITIVITY), survival)
            # derived group: all static cells (ground + structure); both contribute through the same table
            sa = Group()
            for nm in ("static_ground", "static_structure"):
                g = groups.get(nm)
                if g is None:
                    continue
                for k in ("N", "H", "div", "span", "hh", "mm", "sw"):
                    setattr(sa, k, list(getattr(sa, k)) + list(getattr(g, k)))
                sa.tile = list(sa.tile) + [tuple(t) for t in g.tile]
                for a in ("interior_hit", "interior_miss", "all_hit", "all_miss"):
                    for kk, vv in getattr(g, a).items():
                        getattr(sa, a)[kk] = getattr(sa, a).get(kk, 0) + vv
            if len(sa.N):
                groups["static_all"] = sa.freeze()
                touched["static_all"] = np.concatenate([touched.get("static_ground", np.array([], dtype=np.int64)),
                                                        touched.get("static_structure", np.array([], dtype=np.int64))])
            meta = read_meta(f.replace(".csv", ".meta"))
            inventory.append(["kth00", res, W, "ok", meta.get("windows_closed"), meta.get("seconds"), meta.get("peak_rss_kb")])
            pairs = [("structure_vs_moving", "static_structure", "moving", groups, groups, None),
                     ("static_all_vs_moving", "static_all", "moving", groups, groups, None),
                     ("ground_vs_moving", "static_ground", "moving", groups, groups, None)]
            for cg in ("struct_solid", "struct_vehicle", "struct_vegetation", "struct_other"):     # section 11.2, added after the measurement
                if cg in groups:
                    pairs.append((cg[len("struct_"):] + "_vs_moving", cg, "moving", groups, groups, None))
            analyse_condition(cond, groups, touched, N_MIN_SENSITIVITY, pairs, True, out_dir, tables)
            tables["surv"].extend(survivorship_rows(cond, survival))
    # ---- LILocBench
    for res, res_file in (("0.05", "0.05"), ("0.1", "0.10")):   # label, file-name token (reproduce_p1.sh writes r0.10)
        for W in (10, 2):
            gr = {}
            tc = {}
            for seq in ("mapping", "static_0", "dynamics_0"):
                f = os.path.join(data_dir, "lil", "cells_%s_r%s_W%d.csv" % (seq, res_file, W))
                if not os.path.exists(f):
                    inventory.append(["lil_" + seq, res, W, "missing"])
                    continue
                survival = {}
                groups, touched = read_cells(f, float(res), lil_classify, min(N_MIN_SENSITIVITY), survival)
                gr[seq], tc[seq] = groups, touched
                meta = read_meta(f.replace(".csv", ".meta"))
                inventory.append(["lil_" + seq, res, W, "ok", meta.get("windows_closed"), meta.get("seconds"), meta.get("peak_rss_kb")])
                cond = {"dataset": "lil", "res": res, "W": W, "source": seq, "key": "lil_%s_r%s_W%d" % (seq, res, W)}
                analyse_condition(cond, groups, touched, N_MIN_SENSITIVITY, [], False, out_dir, tables)
                tables["surv"].extend(survivorship_rows(cond, survival))
            if "static_0" in gr and "dynamics_0" in gr:
                cond = {"dataset": "lil", "res": res, "W": W, "source": "static_0+dynamics_0", "key": "lil_r%s_W%d" % (res, W)}
                pairs = [("structure@static_0_vs_clutter@dynamics_0", "structure", "hit_in_free", gr["static_0"], gr["dynamics_0"], None),
                         ("structure@dynamics_0_vs_clutter@dynamics_0", "structure", "hit_in_free", gr["dynamics_0"], gr["dynamics_0"], None),
                         ("structure@static_0_vs_hit_in_free@static_0", "structure", "hit_in_free", gr["static_0"], gr["static_0"], None)]
                # the gap table needs two group dicts; analyse_condition with empty groups adds only the gap rows
                analyse_condition(cond, {}, {}, N_MIN_SENSITIVITY, pairs, False, out_dir, tables)
    os.makedirs(out_dir, exist_ok=True)
    write_csv(os.path.join(out_dir, "p1_percentiles.csv"), HEAD_PCT, tables["pct"])
    write_csv(os.path.join(out_dir, "p1_gap.csv"), HEAD_GAP, tables["gap"])
    write_csv(os.path.join(out_dir, "p1_runs.csv"), HEAD_RUNS, tables["runs"])
    write_csv(os.path.join(out_dir, "p1_touched_windows.csv"), HEAD_TOUCH, tables["touch"])
    write_csv(os.path.join(out_dir, "p1_diversity.csv"), HEAD_DIV, tables["div"])
    write_csv(os.path.join(out_dir, "p1_matched_band.csv"), HEAD_BAND, tables["band"])
    write_csv(os.path.join(out_dir, "p1_survivorship.csv"), HEAD_SURV, tables["surv"])
    nb = int(round(1 / HIST_BIN))
    write_csv(os.path.join(out_dir, "p1_hist.csv"),
              ["dataset", "res_m", "W", "source", "group", "N_min"] + ["bin_%.2f" % (i * HIST_BIN) for i in range(nb)],
              [[c["dataset"], c["res"], c["W"], c["source"], g, N_MIN_PRIMARY] + [int(v) for v in h] for c, g, h in tables["hist"]])
    write_csv(os.path.join(out_dir, "p1_inventory.csv"), ["dataset", "res_m", "W", "status", "windows_closed", "seconds", "peak_rss_kb"], inventory)
    if figures:
        make_figures(tables, out_dir)
    return tables


def make_figures(tables, out_dir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    conds = {}
    for c, g, h in tables["hist"]:
        conds.setdefault((c["dataset"], c["res"], c["W"]), []).append((c["source"], g, h))
    nb = int(round(1 / HIST_BIN))
    xs = (np.arange(nb) + 0.5) * HIST_BIN
    show = {"kth00": ("static_structure", "moving", "static_ground"), "lil": ("structure", "hit_in_free")}
    for (ds, res, W), items in sorted(conds.items()):
        fig, ax = plt.subplots(figsize=(6.4, 3.4))
        for (src, g, h) in items:
            if g not in show[ds] or h.sum() == 0:
                continue
            if ds == "lil" and not ((g == "structure" and src == "static_0") or (g == "hit_in_free" and src == "dynamics_0")):
                continue
            style = {"static_structure": "-", "structure": "-", "static_ground": "--", "moving": ":", "hit_in_free": ":"}[g]
            ax.plot(xs, h / h.sum(), style, marker="o", ms=3, color="k", label="%s%s (n=%d)" % (g, "" if ds == "kth00" else "@" + src, h.sum()))
        ax.set_xlabel("per-cell hit rate (hit windows / touched windows), cells with N >= %d" % N_MIN_PRIMARY, fontsize=8)
        ax.set_ylabel("fraction of cells", fontsize=8)
        ax.set_title("%s  res %s m  W %d" % (ds, res, W), fontsize=9)
        ax.legend(fontsize=7)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, "fig_hist_%s_r%s_W%d.png" % (ds, res, W)), dpi=130)
        plt.close(fig)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-dir", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--no-figures", action="store_true")
    ap.add_argument("--skip-kth", action="store_true", help="LILocBench tables only (run A is re-analysed this way)")
    a = ap.parse_args(argv)
    run_all(a.data_dir, a.out_dir, figures=not a.no_figures, kth=not a.skip_kth)
    return 0


if __name__ == "__main__":
    sys.exit(main())
