#!/usr/bin/env python3
"""Follow-ups: attribute clutter false-Static by rate band; effect of the 'reject only when n >= T'
gate (floor-reflecting for T windows); near-wall control group; structure rejects by N band."""
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
    A = math.log((1 - beta) / alpha); B = math.log(beta / (1 - alpha))
    return dict(l_hit=a, l_miss=b, A=A, B=B, latency=math.ceil(A / a - 1e-12))

def replay_gate(runs, p, T):
    """Engine rule of DESIGN 3A: clamp to [B, A+2a]; graduate at l>=A; reject at l<=B only when n>=T."""
    a, b, A, B = p["l_hit"], p["l_miss"], p["A"], p["B"]; ceil_ = A + 2 * a
    l = 0.0; n = 0
    for st, length in runs:
        x = a if st else b
        for _ in range(length):
            n += 1; l += x
            if l > ceil_: l = ceil_
            if l < B: l = B
            if l >= A: return 'S', n
            if l <= B and n >= T: return 'R', n
    return 'U', n

def load(path, keep):
    cells = []
    with open(path) as fh:
        rd = csv.reader(fh); next(rd)
        for row in rd:
            lab = int(row[0]); N = int(row[4]); H = int(row[5])
            if not keep(lab, N, H): continue
            cells.append((N, H, parse_runs(row[9])))
    return cells

for W in (10, 2):
    key = {10: (0.15, 0.65), 2: (0.10, 0.55)}[W]
    st = load("%s/lil/cells_static_0_r0.05_W%d.csv" % (ROOT, W), lambda lab, N, H: lab == 1)
    cl = load("%s/lil/cells_dynamics_0_r0.05_W%d.csv" % (ROOT, W), lambda lab, N, H: lab == 2 and H >= 1)
    nw_d = load("%s/lil/cells_dynamics_0_r0.05_W%d.csv" % (ROOT, W), lambda lab, N, H: lab == 3 and H >= 1)
    nw_s = load("%s/lil/cells_static_0_r0.05_W%d.csv" % (ROOT, W), lambda lab, N, H: lab == 3 and H >= 1)
    free_s = load("%s/lil/cells_static_0_r0.05_W%d.csv" % (ROOT, W), lambda lab, N, H: lab == 2 and H >= 1)
    for ab in (0.01, 0.05):
        p = derive(key[0], key[1], ab, ab)
        print("\nW=%d (m0,m1,a=b)=(%.2f,%.2f,%.2f)" % (W, key[0], key[1], ab))
        for T in (1, 8, 24):
            outS = {'le_m0': [0, 0], 'gt_m0': [0, 0]}
            tot = 0; S = 0
            for N, H, runs in cl:
                o, n = replay_gate(runs, p, T); band = 'le_m0' if H / N <= key[0] else 'gt_m0'
                outS[band][1] += 1; tot += 1
                if o == 'S': outS[band][0] += 1; S += 1
            sS = sum(1 for N, H, runs in st if replay_gate(runs, p, T)[0] == 'S') / len(st)
            sR = sum(1 for N, H, runs in st if replay_gate(runs, p, T)[0] == 'R') / len(st)
            print("  T=%2d: clutter false-Static overall %.4f (n=%d) | among r<=m0: %.4f (n=%d) | among r>m0: %.4f (n=%d) || structure S %.4f R %.4f" % (
                T, S / tot, tot, outS['le_m0'][0] / max(outS['le_m0'][1], 1), outS['le_m0'][1],
                outS['gt_m0'][0] / max(outS['gt_m0'][1], 1), outS['gt_m0'][1], sS, sR))
        # control groups at T=1 (pure SPRT)
        for nm, grp in (("near-wall free, dynamics_0 (H>=1)", nw_d), ("near-wall free, static_0 (H>=1)", nw_s), ("ref_free hit in static_0 (H>=1)", free_s)):
            S = sum(1 for N, H, runs in grp if replay_gate(runs, p, 1)[0] == 'S')
            print("  control %-36s n=%6d graduated %.4f" % (nm, len(grp), S / max(len(grp), 1)))
        # structure rejects: by N band and by rate band
        rej_lowN = sum(1 for N, H, runs in st if N < 5 and replay_gate(runs, p, 1)[0] == 'R')
        rej_hiN = sum(1 for N, H, runs in st if N >= 5 and replay_gate(runs, p, 1)[0] == 'R')
        rej_hiN_lowr = sum(1 for N, H, runs in st if N >= 5 and H / N < key[1] and replay_gate(runs, p, 1)[0] == 'R')
        print("  structure rejected: N<5: %d, N>=5: %d (of which r<m1: %d) out of %d structure cells" % (rej_lowN, rej_hiN, rej_hiN_lowr, len(st)))

# KITTI: moving graduated by rate band (the 'stopped mover' confusion)
print("\nKITTI 0.2 W10 moving cells graduated under (0.15,0.65,0.01), by rate band")
p = derive(0.15, 0.65, 0.01, 0.01)
mv = load("%s/kth00/cells_r0.2_W10.csv" % ROOT, lambda lab, N, H: lab == 1)
bands = {'r<=0.15': [0, 0], '0.15<r<0.65': [0, 0], 'r>=0.65': [0, 0]}
for N, H, runs in mv:
    r = H / N; b = 'r<=0.15' if r <= 0.15 else ('r>=0.65' if r >= 0.65 else '0.15<r<0.65')
    bands[b][1] += 1
    if replay_gate(runs, p, 1)[0] == 'S': bands[b][0] += 1
for k, v in bands.items(): print("  %-12s n=%5d graduated=%d (%.3f)" % (k, v[1], v[0], v[0] / max(v[1], 1)))
