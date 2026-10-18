#!/usr/bin/env python3
"""Phase 3 evaluators (PREREG_P3.md). numpy only, deterministic (fixed seeds), no data in the repository.

    kth            SA / DA / AA / HA of a method map against a KTH ground-truth cloud (PREREG_P3.md section 4)
    kth-selfcheck  the two validation cases on a ground-truth cloud: GT static points only -> SA = 100 and DA equal to the oracle ceiling
                   (100 unless dynamic points touch static ones within tol); remove nothing (the whole GT) -> SA = 100, DA = 0
    lil            false-Static on clutter and false-reject on wall runs from an `arm-eval` cell dump (section 6)
    spires         the same rates from an `arm-eval` counts table of a 3D label file (section 7)
    boreas         cross-session Jaccard of Static voxel sets inside shared tiles (section 5)

Run with `python3 -I p3_eval.py <command> ...`.
"""
import argparse
import csv
import json
import math
import sys

import numpy as np

BOOT_SEED = 20261014          # the bootstrap seed of phase 1 (PREREG_P1 section 4)
BOOT_REPS = 2000
TILE_M = 10.0                 # the block size of phase 1


# ----------------------------------------------------------------------------------- PCD ------

def read_pcd(path):
    """Binary PCD v0.7 with float32 x, y, z (and optionally intensity). Returns (xyz float32 (N,3), intensity float32 (N,) or None)."""
    with open(path, "rb") as f:
        fields, sizes, types, counts, points = None, None, None, None, None
        consumed = 0
        while True:
            line = f.readline()
            if not line:
                raise ValueError(f"{path}: no DATA line")
            consumed += len(line)
            parts = line.decode("ascii", "replace").split()
            if not parts:
                continue
            key = parts[0]
            if key == "FIELDS":
                fields = parts[1:]
            elif key == "SIZE":
                sizes = [int(v) for v in parts[1:]]
            elif key == "TYPE":
                types = parts[1:]
            elif key == "COUNT":
                counts = [int(v) for v in parts[1:]]
            elif key == "POINTS":
                points = int(parts[1])
            elif key == "DATA":
                if parts[1] != "binary":
                    raise ValueError(f"{path}: only DATA binary is supported")
                break
        if fields is None or sizes is None or types is None or points is None:
            raise ValueError(f"{path}: incomplete header")
        counts = counts or [1] * len(fields)
        dt = []
        for name, sz, ty, ct in zip(fields, sizes, types, counts):
            if ct != 1:
                raise ValueError(f"{path}: COUNT {ct} for {name} is not supported")
            code = {"F": "f", "U": "u", "I": "i"}[ty] + str(sz)
            dt.append((name, "<" + code))
        dtype = np.dtype(dt)
        data = np.fromfile(f, dtype=dtype, count=points)
        if data.shape[0] != points:
            raise ValueError(f"{path}: truncated ({data.shape[0]} of {points} points)")
    for k in ("x", "y", "z"):
        if k not in data.dtype.names or data.dtype[k] != np.dtype("<f4"):
            raise ValueError(f"{path}: x, y, z must be float32")
    xyz = np.empty((points, 3), dtype=np.float32)
    xyz[:, 0] = data["x"]
    xyz[:, 1] = data["y"]
    xyz[:, 2] = data["z"]
    inten = data["intensity"].astype(np.float32) if "intensity" in data.dtype.names else None
    del data
    return xyz, inten


def write_pcd(path, xyz, intensity=None):
    xyz = np.asarray(xyz, dtype=np.float32)
    n = xyz.shape[0]
    names = ["x", "y", "z"] + (["intensity"] if intensity is not None else [])
    hdr = ("# .PCD v0.7 - Point Cloud Data file format\nVERSION 0.7\nFIELDS " + " ".join(names) + "\nSIZE " + " ".join(["4"] * len(names)) +
           "\nTYPE " + " ".join(["F"] * len(names)) + "\nCOUNT " + " ".join(["1"] * len(names)) + f"\nWIDTH {n}\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS {n}\nDATA binary\n")
    cols = [xyz[:, 0], xyz[:, 1], xyz[:, 2]] + ([np.asarray(intensity, dtype=np.float32)] if intensity is not None else [])
    with open(path, "wb") as f:
        f.write(hdr.encode("ascii"))
        np.stack(cols, axis=1).astype("<f4").tofile(f)


# ------------------------------------------------------------------------ radius matching ------

_OFF = 1 << 20


def _pack(ix, iy, iz):
    return ((ix + _OFF) << 42) | ((iy + _OFF) << 21) | (iz + _OFF)


def _keys(xyz, tol):
    c = np.floor(np.asarray(xyz, dtype=np.float64) / tol).astype(np.int64)
    return _pack(c[:, 0], c[:, 1], c[:, 2]), c


def matched(queries, targets, tol, chunk=2_000_000):
    """bool (len(queries),): True if some target lies within distance tol (<=) of the query. Exact: a grid of edge tol
    makes every match lie in the 3 x 3 x 3 cells around the query cell; distances are then computed in float64.
    Memory: the targets are kept once (as given, float32 stays float32) plus one sorted copy and the cell tables."""
    q = np.asarray(queries).reshape(-1, 3)
    t = np.asarray(targets).reshape(-1, 3)
    out = np.zeros(q.shape[0], dtype=bool)
    if q.shape[0] == 0 or t.shape[0] == 0:
        return out
    if tol <= 0:
        raise ValueError("tol must be positive")
    lim = (_OFF - 2) * tol
    if np.abs(q).max() >= lim or np.abs(t).max() >= lim:
        raise ValueError("coordinates exceed the grid range")
    tk = np.empty(t.shape[0], dtype=np.int64)
    for lo in range(0, t.shape[0], chunk):
        tk[lo:lo + chunk] = _keys(t[lo:lo + chunk], tol)[0]
    order = np.argsort(tk, kind="stable")
    tk = tk[order]
    ts = t[order]                                              # sorted copy, original dtype
    del order
    brk = np.flatnonzero(tk[1:] != tk[:-1]) + 1
    start = np.concatenate(([0], brk))
    cnt = np.diff(np.concatenate((start, [tk.size])))
    uk = tk[start]
    del tk
    tol2 = tol * tol
    offsets = [(0, 0, 0)] + [(a, b, c) for a in (-1, 0, 1) for b in (-1, 0, 1) for c in (-1, 0, 1) if (a, b, c) != (0, 0, 0)]
    for lo in range(0, q.shape[0], chunk):
        qc = np.asarray(q[lo:lo + chunk], dtype=np.float64)
        _, qi = _keys(qc, tol)
        done = np.zeros(qc.shape[0], dtype=bool)
        for (a, b, c) in offsets:
            todo = np.nonzero(~done)[0]
            if todo.size == 0:
                break
            key = _pack(qi[todo, 0] + a, qi[todo, 1] + b, qi[todo, 2] + c)
            pos = np.searchsorted(uk, key)
            pos[pos >= uk.size] = uk.size - 1
            hit = uk[pos] == key
            idx = todo[hit]
            if idx.size == 0:
                continue
            st = start[pos[hit]]
            ct = cnt[pos[hit]]
            for k in range(int(ct.max())):
                sel = np.nonzero((ct > k) & ~done[idx])[0]
                if sel.size == 0:
                    continue
                d = ts[st[sel] + k].astype(np.float64) - qc[idx[sel]]
                ok = (d * d).sum(axis=1) <= tol2
                done[idx[sel[ok]]] = True
        out[lo:lo + chunk] = done
    return out


# ------------------------------------------------------------------------------- KTH scores ------

def kth_scores(gt_xyz, gt_dynamic, out_xyz, tol=0.05):
    """The scores of the KTH benchmark (evaluate_all.py on the output of export_eval_pcd): a GT point is predicted static
    (kept) if some point of the method map lies within tol; otherwise predicted dynamic (removed).
    SA = kept static / static; DA = removed dynamic / dynamic; AA = sqrt(SA DA); HA = 2 SA DA / (SA + DA). In percent."""
    dyn = np.asarray(gt_dynamic).astype(bool)
    n_dyn = int(dyn.sum())
    n_sta = int((~dyn).sum())
    if n_dyn == 0 or n_sta == 0:
        raise ValueError("the ground truth needs static and dynamic points")
    kept = matched(gt_xyz, out_xyz, tol)
    sa = 100.0 * int((kept & ~dyn).sum()) / n_sta
    da = 100.0 * int((~kept & dyn).sum()) / n_dyn
    aa = math.sqrt(sa * da)
    ha = 0.0 if sa + da == 0 else 2.0 * sa * da / (sa + da)
    return {"SA": sa, "DA": da, "AA": aa, "HA": ha, "gt_static": n_sta, "gt_dynamic": n_dyn,
            "map_points": int(np.asarray(out_xyz).reshape(-1, 3).shape[0]), "tol_m": tol}


def cmd_kth(args):
    gt, lab = read_pcd(args.gt)
    if lab is None:
        raise SystemExit("the GT cloud has no intensity field (the dynamic flag)")
    vals = np.unique(lab)
    if not set(vals.tolist()) <= {0.0, 1.0}:
        raise SystemExit(f"GT intensity must be 0 or 1, found {vals[:10].tolist()}")
    out, _ = read_pcd(args.map)
    res = kth_scores(gt, lab != 0, out, args.tol)
    res["gt"] = args.gt
    res["map"] = args.map
    print(json.dumps(res, indent=1, sort_keys=True))
    return 0


def shadowed_independent(static_xyz, dyn_xyz, tol):
    """Number of dynamic points that have a static point within tol, by a method that shares no code with `matched`: a sort on x,
    a window of +-tol in x, and a brute-force distance test inside the window."""
    st = np.asarray(static_xyz, dtype=np.float64).reshape(-1, 3)
    dy = np.asarray(dyn_xyz, dtype=np.float64).reshape(-1, 3)
    if st.shape[0] == 0:
        return 0
    order = np.argsort(st[:, 0], kind="stable")
    sx = st[order]
    xs = sx[:, 0]
    count = 0
    for p in dy:
        lo = np.searchsorted(xs, p[0] - tol, side="left")
        hi = np.searchsorted(xs, p[0] + tol, side="right")
        if hi > lo:
            d = sx[lo:hi] - p
            if ((d * d).sum(axis=1) <= tol * tol).any():
                count += 1
    return count


def cmd_kth_selfcheck(args):
    """Two validation cases on a ground-truth cloud (PREREG_P3.md section 4.3).
    1. The map is the GT static points: SA must be 100, and DA must equal the share of dynamic points that have NO static point within
       tol, counted independently. On real clouds that share is below 100 (dynamic points that touch static ground within 5 cm cannot
       be removed by any map), so DA = 100 holds only when no dynamic point is that close; the ceiling is printed.
    2. The map is the whole GT (remove nothing): SA must be 100 and DA 0."""
    gt, lab = read_pcd(args.gt)
    dyn = lab != 0
    a = kth_scores(gt, dyn, gt[~dyn], args.tol)
    b = kth_scores(gt, dyn, gt, args.tol)
    shadowed = shadowed_independent(gt[~dyn], gt[dyn], args.tol)
    ceiling = 100.0 * (a["gt_dynamic"] - shadowed) / a["gt_dynamic"]
    out = {"gt_static_only": a, "remove_nothing": b, "dynamic_points_within_tol_of_a_static_point": shadowed, "DA_oracle_ceiling": ceiling}
    print(json.dumps(out, indent=1, sort_keys=True))
    ok = (a["SA"] == 100.0 and abs(a["DA"] - ceiling) < 1e-9 and b["SA"] == 100.0 and b["DA"] == 0.0)
    print("SELFCHECK " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 3


# ----------------------------------------------------------------------------- statistics ------

def wilson(k, n, z=1.959964):
    if n == 0:
        return (float("nan"), float("nan"))
    p = k / n
    d = 1 + z * z / n
    c = p + z * z / (2 * n)
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))
    return ((c - h) / d, (c + h) / d)


def block_bootstrap_rate(flag, den_mask, tile, reps=BOOT_REPS, seed=BOOT_SEED):
    """95 % percentile interval of sum(flag & den) / sum(den) resampling whole tiles. flag, den_mask: bool arrays; tile: int tile id per cell."""
    den = np.asarray(den_mask, dtype=bool)
    if not den.any():
        return (float("nan"), float("nan"))
    t = np.asarray(tile)[den]
    f = np.asarray(flag, dtype=bool)[den]
    u, inv = np.unique(t, return_inverse=True)
    num = np.bincount(inv, weights=f.astype(float), minlength=u.size)
    cn = np.bincount(inv, minlength=u.size).astype(float)
    rng = np.random.RandomState(seed)
    r = np.empty(reps)
    for i in range(reps):
        s = rng.randint(0, u.size, u.size)
        r[i] = num[s].sum() / cn[s].sum()
    return (float(np.percentile(r, 2.5)), float(np.percentile(r, 97.5)))


def pct(v, q):
    return float("nan") if len(v) == 0 else float(np.percentile(np.asarray(v, dtype=float), q))


# ------------------------------------------------------------------------------ LILocBench ------

VERDICTS = {"rejected": 0, "undecided": 1, "static": 2}
REF_OCCUPIED, REF_FREE, REF_FREE_NEAR = 1, 2, 3


def read_dump(path):
    with open(path) as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise ValueError(f"{path}: empty")
    out = {k: np.array([int(r[k]) for r in rows]) for k in ("label", "ix", "iy", "iz", "n", "h", "first_static", "first_touch")}
    out["verdict"] = np.array([VERDICTS[r["verdict"]] for r in rows])
    return out


def wall_run_mask(ix, iy, res, rho=1.0, min_cells=20, ratio_max=0.02, min_extent=1.0):
    """Cells of a set that belong to a straight run (PREREG_P3.md section 6.2): the cells of the set within rho of a cell form a
    patch with at least min_cells cells, minor/major eigenvalue ratio <= ratio_max and extent sqrt(12 * major eigenvalue) >= min_extent.
    The shape rule of G1 (PREREG_P1B section 3.1) applied to cell centres."""
    ix = np.asarray(ix, dtype=np.int64)
    iy = np.asarray(iy, dtype=np.int64)
    n = ix.size
    if n == 0:
        return np.zeros(0, dtype=bool)
    key = (ix + _OFF) * (1 << 22) + (iy + _OFF)
    order = np.argsort(key)
    sk = key[order]
    r = int(math.floor(rho / res + 1e-9))
    cnt = np.zeros(n)
    sx = np.zeros(n)
    sy = np.zeros(n)
    sxx = np.zeros(n)
    sxy = np.zeros(n)
    syy = np.zeros(n)
    for dx in range(-r, r + 1):
        for dy in range(-r, r + 1):
            if (dx * dx + dy * dy) * res * res > rho * rho + 1e-12:
                continue
            nk = (ix + dx + _OFF) * (1 << 22) + (iy + dy + _OFF)
            pos = np.searchsorted(sk, nk)
            pos[pos >= n] = n - 1
            present = sk[pos] == nk
            x = dx * res
            y = dy * res
            cnt += present
            sx += present * x
            sy += present * y
            sxx += present * x * x
            sxy += present * x * y
            syy += present * y * y
    with np.errstate(divide="ignore", invalid="ignore"):
        mx, my = sx / cnt, sy / cnt
        cxx, cxy, cyy = sxx / cnt - mx * mx, sxy / cnt - mx * my, syy / cnt - my * my
        tr = cxx + cyy
        det = cxx * cyy - cxy * cxy
        disc = np.sqrt(np.maximum(tr * tr / 4 - det, 0.0))
        l1, l2 = tr / 2 + disc, np.maximum(tr / 2 - disc, 0.0)
        ok = (cnt >= min_cells) & (l1 > 0) & (l2 / l1 <= ratio_max) & (np.sqrt(12 * np.maximum(l1, 0)) >= min_extent)
    return ok


def lil_metrics(d, res, origin=-30.0, nmin=5, bands=(0.0, 0.15, 0.30, 0.65, 1.0001)):
    """Rates for one arm on one sequence from an arm-eval dump of the reference label file (section 6)."""
    n, h, lab, ver = d["n"], d["h"], d["label"], d["verdict"]
    x = origin + (d["ix"] + 0.5) * res
    y = origin + (d["iy"] + 0.5) * res
    tile = (np.floor(x / TILE_M).astype(np.int64) + 4096) * 8192 + (np.floor(y / TILE_M).astype(np.int64) + 4096)
    occ = lab == REF_OCCUPIED
    wall = np.zeros(lab.size, dtype=bool)
    wall[occ] = wall_run_mask(d["ix"][occ], d["iy"][occ], res)
    clutter = (lab == REF_FREE) & (h >= 1) & (n >= nmin)             # hit_in_free of the calibration, near-wall cells excluded by label
    structure = wall & (n >= nmin)
    out = {"nmin": nmin, "cells_labelled": int(lab.size), "wall_run_cells_total": int(wall.sum())}

    def block(mask, flag, name):
        k = int((flag & mask).sum())
        m = int(mask.sum())
        lo, hi = wilson(k, m)
        blo, bhi = block_bootstrap_rate(flag, mask, tile)
        out[name] = {"k": k, "n": m, "rate": (k / m if m else float("nan")), "wilson95": [lo, hi], "tile_bootstrap95": [blo, bhi]}

    block(clutter, ver == VERDICTS["static"], "false_static_on_clutter")
    block(clutter, ver == VERDICTS["undecided"], "clutter_undecided")
    block(clutter, ver == VERDICTS["rejected"], "clutter_rejected")
    block(structure, ver == VERDICTS["rejected"], "false_reject_on_wall_runs")
    block(structure, ver != VERDICTS["static"], "wall_runs_not_static")
    block(structure, ver == VERDICTS["static"], "wall_runs_static")
    r = np.where(n > 0, h / np.maximum(n, 1), 0.0)
    out["false_static_by_hit_rate_band"] = []
    for lo_b, hi_b in zip(bands[:-1], bands[1:]):
        m = clutter & (r >= lo_b) & (r < hi_b)
        k = int((m & (ver == VERDICTS["static"])).sum())
        out["false_static_by_hit_rate_band"].append({"band": [lo_b, min(hi_b, 1.0)], "k": k, "n": int(m.sum()), "rate": (k / int(m.sum()) if m.any() else float("nan"))})
    got = structure & (d["first_static"] >= 0)
    lat = (d["first_static"] - d["first_touch"] + 1)[got]
    out["wall_run_windows_to_first_static"] = {"n": int(lat.size), "p50": pct(lat, 50), "p90": pct(lat, 90), "min": float(lat.min()) if lat.size else float("nan")}
    # qualitative change counts (lt_changes_0): no truth per cell, counts only
    elig = n >= nmin
    out["qualitative"] = {
        "ref_free_cells_static": int(((lab == REF_FREE) & elig & (ver == VERDICTS["static"])).sum()),
        "ref_free_cells_eligible": int(((lab == REF_FREE) & elig).sum()),
        "ref_occupied_cells_not_static": int((occ & elig & (ver != VERDICTS["static"])).sum()),
        "ref_occupied_cells_rejected": int((occ & elig & (ver == VERDICTS["rejected"])).sum()),
        "ref_occupied_cells_eligible": int((occ & elig).sum()),
    }
    return out


def cmd_lil(args):
    d = read_dump(args.dump)
    print(json.dumps(lil_metrics(d, args.res, args.origin, args.nmin), indent=1, sort_keys=True))
    return 0


# --------------------------------------------------------------------------- Spires, Boreas ------

def spires_metrics(counts_csv, latency_csv=None, structure_labels=(3,), clutter_label=1):
    with open(counts_csv) as f:
        rows = list(csv.DictReader(f))
    c = {}
    for r in rows:
        c[(int(r["label"]), int(r["eligible"]), r["verdict"])] = int(r["count"])

    def tot(label, elig=1):
        return sum(v for (l, e, _), v in c.items() if l == label and e == elig)

    out = {}
    nc = tot(clutter_label)
    kc = c.get((clutter_label, 1, "static"), 0)
    out["false_static_on_clutter"] = {"k": kc, "n": nc, "rate": (kc / nc if nc else float("nan")), "wilson95": list(wilson(kc, nc))}
    for lab in structure_labels:
        ns = tot(lab)
        kr = c.get((lab, 1, "rejected"), 0)
        ku = c.get((lab, 1, "undecided"), 0)
        ks = c.get((lab, 1, "static"), 0)
        out[f"structure_label_{lab}"] = {"n": ns, "rejected": kr, "undecided": ku, "static": ks,
                                         "false_reject_rate": (kr / ns if ns else float("nan")), "wilson95": list(wilson(kr, ns)),
                                         "undecided_share": (ku / ns if ns else float("nan"))}
    if latency_csv:
        w = []
        with open(latency_csv) as f:
            for r in csv.DictReader(f):
                if int(r["label"]) in structure_labels:
                    w += [int(r["windows_to_static"])] * int(r["count"])
        out["structure_windows_to_first_static"] = {"n": len(w), "p50": pct(w, 50), "p90": pct(w, 90)}
    return out


def decode_voxel_ids(ids):
    ids = np.asarray(ids, dtype=np.int64)
    mask = (1 << 21) - 1
    return (((ids >> 42) & mask) - _OFF, ((ids >> 21) & mask) - _OFF, (ids & mask) - _OFF)


def jaccard(a, b):
    a = np.unique(np.asarray(a, dtype=np.int64))
    b = np.unique(np.asarray(b, dtype=np.int64))
    inter = np.intersect1d(a, b, assume_unique=True).size
    union = a.size + b.size - inter
    return {"intersection": int(inter), "union": int(union), "jaccard": (inter / union if union else float("nan")), "size_a": int(a.size), "size_b": int(b.size)}


def tiles_of_ids(ids, res, tile_m=TILE_M):
    ix, iy, _ = decode_voxel_ids(ids)
    t = int(round(tile_m / res))
    return (np.floor_divide(ix, t) + 4096) * 8192 + (np.floor_divide(iy, t) + 4096)


def jaccard_in_shared_tiles(a, b, res, all_a=None, all_b=None, tile_m=TILE_M):
    """Jaccard of two Static voxel sets inside the tiles where BOTH sessions have observed anything (all_a, all_b: the tiles of the
    observed voxels; default the tiles of the sets themselves)."""
    ta = np.unique(tiles_of_ids(all_a if all_a is not None else a, res, tile_m))
    tb = np.unique(tiles_of_ids(all_b if all_b is not None else b, res, tile_m))
    shared = np.intersect1d(ta, tb, assume_unique=True)
    a = np.asarray(a, dtype=np.int64)
    b = np.asarray(b, dtype=np.int64)
    ka = a[np.isin(tiles_of_ids(a, res, tile_m), shared)]
    kb = b[np.isin(tiles_of_ids(b, res, tile_m), shared)]
    out = jaccard(ka, kb)
    out["shared_tiles"] = int(shared.size)
    return out


def cmd_spires(args):
    print(json.dumps(spires_metrics(args.counts, args.latency), indent=1, sort_keys=True))
    return 0


def cmd_boreas(args):
    sets = [np.fromfile(p, dtype="<i8") for p in args.ids]
    out = []
    for i in range(len(sets)):
        for j in range(i + 1, len(sets)):
            r = jaccard_in_shared_tiles(sets[i], sets[j], args.res)
            r.update({"a": args.ids[i], "b": args.ids[j]})
            out.append(r)
    print(json.dumps(out, indent=1, sort_keys=True))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("kth"); p.add_argument("--gt", required=True); p.add_argument("--map", required=True); p.add_argument("--tol", type=float, default=0.05); p.set_defaults(fn=cmd_kth)
    p = sub.add_parser("kth-selfcheck"); p.add_argument("--gt", required=True); p.add_argument("--tol", type=float, default=0.05); p.set_defaults(fn=cmd_kth_selfcheck)
    p = sub.add_parser("lil"); p.add_argument("--dump", required=True); p.add_argument("--res", type=float, required=True); p.add_argument("--origin", type=float, default=-30.0); p.add_argument("--nmin", type=int, default=5); p.set_defaults(fn=cmd_lil)
    p = sub.add_parser("spires"); p.add_argument("--counts", required=True); p.add_argument("--latency"); p.set_defaults(fn=cmd_spires)
    p = sub.add_parser("boreas"); p.add_argument("--ids", nargs="+", required=True); p.add_argument("--res", type=float, required=True); p.set_defaults(fn=cmd_boreas)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
