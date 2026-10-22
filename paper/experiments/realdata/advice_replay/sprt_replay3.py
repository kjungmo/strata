#!/usr/bin/env python3
"""Floor variants under the periodicity gate: floor=B always (DESIGN 3A) vs floor=2B until n>=T then B,
vs no floor until n>=T. LILocBench 0.05 W10, (0.15,0.65,0.01)."""
import os, csv, math
ROOT = os.environ.get("P1_DIR", os.path.expanduser("~/datasets/strata/p1"))   # run B cell tables (calibration split only)

def parse_runs(s):
    out = []; i = 0
    while i < len(s):
        st = s[i] == 'h'; j = i + 1
        while j < len(s) and s[j].isdigit(): j += 1
        out.append((st, int(s[i+1:j]))); i = j
    return out

def derive(m0, m1, alpha, beta):
    a = math.log(m1 / m0); b = math.log((1 - m1) / (1 - m0))
    return dict(l_hit=a, l_miss=b, A=math.log((1 - beta) / alpha), B=math.log(beta / (1 - alpha)))

def replay(runs, p, T, pre_floor):
    a, b, A, B = p["l_hit"], p["l_miss"], p["A"], p["B"]; ceil_ = A + 2 * a
    l = 0.0; n = 0
    for st, length in runs:
        x = a if st else b
        for _ in range(length):
            n += 1; l += x
            if l > ceil_: l = ceil_
            fl = B if n >= T else pre_floor
            if l < fl: l = fl
            if l >= A: return 'S', n
            if l <= B and n >= T: return 'R', n
    return 'U', n

def load(path, keep):
    cells = []
    with open(path) as fh:
        rd = csv.reader(fh); next(rd)
        for row in rd:
            lab = int(row[0]); N = int(row[4]); H = int(row[5])
            if keep(lab, N, H): cells.append((N, H, parse_runs(row[9])))
    return cells

st = load("%s/lil/cells_static_0_r0.05_W10.csv" % ROOT, lambda lab, N, H: lab == 1)
cl = load("%s/lil/cells_dynamics_0_r0.05_W10.csv" % ROOT, lambda lab, N, H: lab == 2 and H >= 1)
for ab in (0.01, 0.05):
    p = derive(0.15, 0.65, ab, ab)
    for T in (8, 24):
        for name, pf in (("floor=B always", p["B"]), ("floor=2B before gate", 2 * p["B"]), ("no floor before gate", -1e9)):
            cS = sum(1 for N, H, r in cl if replay(r, p, T, pf)[0] == 'S') / len(cl)
            res = [replay(r, p, T, pf) for N, H, r in st]
            sS = sum(1 for o, n in res if o == 'S') / len(st); sR = sum(1 for o, n in res if o == 'R') / len(st); sU = 1 - sS - sR
            lat = sorted(n for o, n in res if o == 'S')
            print("a=b=%.2f T=%2d %-22s clutter false-Static %.4f | structure S %.4f R %.4f U %.4f | lat p50 %d p90 %d" % (
                ab, T, name, cS, sS, sR, sU, lat[len(lat)//2], lat[int(0.9*len(lat))]))
