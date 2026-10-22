#!/usr/bin/env python3
"""Light computation for P1_DECISION_ADVICE: derived SPRT parameters from the pre-registered
rounding rule and an empirical replay of the SPRT over the committed per-cell hit/miss
sequences of the CALIBRATION split (LILocBench static_0/dynamics_0, KITTI 00). Read-only."""
import csv, math, os, sys, random

ROOT = os.environ.get("P1_DIR", os.path.expanduser("~/datasets/strata/p1"))   # run B cell tables (calibration split only)

def parse_runs(s):
    out = []
    i = 0
    while i < len(s):
        st = s[i] == 'h'
        j = i + 1
        while j < len(s) and s[j].isdigit():
            j += 1
        out.append((st, int(s[i+1:j])))
        i = j
    return out

def derive(m0, m1, alpha, beta):
    a = math.log(m1 / m0); b = math.log((1 - m1) / (1 - m0))
    A = math.log((1 - beta) / alpha); B = math.log(beta / (1 - alpha))
    mstar = -b / (a - b)
    var = mstar * a * a + (1 - mstar) * b * b
    EN_star = -A * B / var
    lat = math.ceil(A / a - 1e-12)
    return dict(l_hit=a, l_miss=b, A=A, B=B, mstar=mstar, EN_star=EN_star, latency=lat)

def oc(m, m0, m1, alpha, beta):
    """Wald OC (no overshoot): P(graduate | rate m)."""
    a = math.log(m1 / m0); b = math.log((1 - m1) / (1 - m0))
    A = (1 - beta) / alpha; B = beta / (1 - alpha)
    d = m * a + (1 - m) * b
    if abs(d) < 1e-9:
        return 0.5 * 0 + (math.log(1/B)) / (math.log(A) - math.log(B))  # linear interpolation at zero drift
    # solve m*(m1/m0)^h + (1-m)*((1-m1)/(1-m0))^h = 1 for h != 0 by bisection
    f = lambda h: m * (m1/m0)**h + (1-m) * ((1-m1)/(1-m0))**h - 1
    lo, hi = (-60.0, -1e-6) if d > 0 else (1e-6, 60.0)
    for _ in range(200):
        mid = 0.5 * (lo + hi)
        if f(lo) * f(mid) <= 0: hi = mid
        else: lo = mid
    h = 0.5 * (lo + hi)
    P0 = (A**h - 1) / (A**h - B**h)
    return 1 - P0

def EN(m, m0, m1, alpha, beta):
    a = math.log(m1 / m0); b = math.log((1 - m1) / (1 - m0))
    A = math.log((1 - beta) / alpha); B = math.log(beta / (1 - alpha))
    d = m * a + (1 - m) * b
    if abs(d) < 1e-9:
        return -A * B / (m * a * a + (1 - m) * b * b)
    P1 = oc(m, m0, m1, alpha, beta)
    return ((1 - P1) * B + P1 * A) / d

def replay(runs, p, ceiling_extra=2, open_ended=False):
    """Replay the SPRT over one cell's run-length sequence. Returns (outcome, windows_to_decision, n).
    outcome: 'S' graduated, 'R' rejected (first crossing), 'U' undecided at end."""
    a, b, A, B = p["l_hit"], p["l_miss"], p["A"], p["B"]
    ceil_ = A + ceiling_extra * a
    l = 0.0; n = 0
    for st, length in runs:
        x = a if st else b
        for _ in range(length):
            n += 1
            l += x
            if l > ceil_: l = ceil_
            if l >= A:
                return 'S', n
            if not open_ended and l <= B:
                return 'R', n
            if open_ended and l < B:
                l = B  # floor clamp, keep going (Ville reading with a floor)
    return 'U', n

def pct(xs, q):
    if not xs: return float('nan')
    xs = sorted(xs); k = (len(xs) - 1) * q; f = math.floor(k); c = math.ceil(k)
    if f == c: return xs[int(k)]
    return xs[f] + (xs[c] - xs[f]) * (k - f)

def load(path, keep):
    cells = []
    with open(path) as fh:
        rd = csv.reader(fh); head = next(rd)
        for row in rd:
            lab = int(row[0]); N = int(row[4]); H = int(row[5])
            if not keep(lab, N, H): continue
            cells.append((N, H, parse_runs(row[9])))
    return cells

def summarize(name, cells, p, nmin_report=5):
    out = {'S': 0, 'R': 0, 'U': 0}; lat = []; out_open = 0
    reach = 0; out_reach = {'S': 0, 'R': 0, 'U': 0}
    for N, H, runs in cells:
        o, n = replay(runs, p); out[o] += 1
        if o == 'S': lat.append(n)
        o2, _ = replay(runs, p, open_ended=True)
        if o2 == 'S': out_open += 1
        if N >= p["latency"]:
            reach += 1; out_reach[o] += 1
    tot = len(cells)
    print("  %-34s cells=%6d  S=%.4f R=%.4f U=%.4f | open-ended ever>=A %.4f | cells with N>=%d: %d of which S=%.4f U=%.4f | windows-to-S p50=%s p90=%s" % (
        name, tot, out['S']/tot, out['R']/tot, out['U']/tot, out_open/tot, p["latency"], reach,
        out_reach['S']/max(reach,1), out_reach['U']/max(reach,1),
        pct(lat, 0.5) if lat else '-', pct(lat, 0.9) if lat else '-'))

def rates(cells, nmin):
    return [H / N for N, H, _ in cells if N >= nmin]

def round_out(q_s, q_c, step=0.05):
    m1 = math.floor(q_s / step + 1e-9) * step
    m0 = math.ceil(q_c / step - 1e-9) * step
    return round(m0, 2), round(m1, 2)

print("=== Derived parameters (rounding: m1 = floor(q_s), m0 = ceil(q_c) to 0.05) ===")
cases = {
    "LIL 0.05 W10": (0.6957, 0.1417),
    "LIL 0.05 W2": (0.5770, 0.0697),
    "LIL 0.10 W10": (0.7008, 0.2034),
    "LIL 0.10 W2": (0.5975, 0.1185),
}
params = {}
for k, (qs, qc) in cases.items():
    m0, m1 = round_out(qs, qc)
    for ab in (0.01, 0.05):
        p = derive(m0, m1, ab, ab)
        params[(k, ab)] = (m0, m1, p)
        print("%-14s q_s=%.4f q_c=%.4f -> (m0,m1)=(%.2f,%.2f) alpha=beta=%.2f: l_hit=%.3f l_miss=%.3f A=%.3f B=%.3f m*=%.3f latency(all-hit)=%d E[N|m*]=%.1f  OC@m0=%.3f OC@m1=%.3f OC@0.3=%.3f OC@0.5=%.3f E[N|m1]=%.1f E[N|m0]=%.1f" % (
            k, qs, qc, m0, m1, ab, p["l_hit"], p["l_miss"], p["A"], p["B"], p["mstar"], p["latency"], p["EN_star"],
            oc(m0, m0, m1, ab, ab), oc(m1, m0, m1, ab, ab), oc(0.3, m0, m1, ab, ab), oc(0.5, m0, m1, ab, ab),
            EN(m1, m0, m1, ab, ab), EN(m0, m0, m1, ab, ab)))
# the anti-conservative reading, for contrast
m0x, m1x = 0.10, 0.70
px = derive(m0x, m1x, 0.01, 0.01)
print("contrast (anti-conservative rounding) (0.10,0.70) a=b=0.01: l_hit=%.3f l_miss=%.3f latency=%d m*=%.3f E[N|m*]=%.1f" % (px["l_hit"], px["l_miss"], px["latency"], px["mstar"], px["EN_star"]))
pp = derive(0.3, 0.6, 0.01, 0.01)
print("placeholder (0.30,0.60) a=b=0.01: l_hit=%.3f l_miss=%.3f latency=%d m*=%.3f E[N|m*]=%.1f" % (pp["l_hit"], pp["l_miss"], pp["latency"], pp["mstar"], pp["EN_star"]))

print("\n=== Empirical replay on LILocBench 0.05 m (structure = ref_occupied on static_0; clutter = ref_free with H>=1 on dynamics_0) ===")
for W in (10, 2):
    st = load("%s/lil/cells_static_0_r0.05_W%d.csv" % (ROOT, W), lambda lab, N, H: lab == 1)
    cl = load("%s/lil/cells_dynamics_0_r0.05_W%d.csv" % (ROOT, W), lambda lab, N, H: lab == 2 and H >= 1)
    rs, rc = rates(st, 5), rates(cl, 5)
    print("W=%d check: q_s(p10,N>=5)=%.4f (n=%d)  q_c(p90,N>=5)=%.4f (n=%d)" % (W, pct(rs, 0.10), len(rs), pct(rc, 0.90), len(rc)))
    key = "LIL 0.05 W%d" % W
    for ab in (0.01, 0.05):
        m0, m1, p = params[(key, ab)]
        # population shares relative to (m0, m1), N>=5
        s_below_m1 = sum(1 for r in rs if r < m1) / len(rs); s_below_m0 = sum(1 for r in rs if r <= m0) / len(rs)
        c_above_m0 = sum(1 for r in rc if r > m0) / len(rc); c_above_m1 = sum(1 for r in rc if r >= m1) / len(rc)
        print(" (m0,m1,a=b)=(%.2f,%.2f,%.2f): structure(N>=5) share r<m1=%.4f (indifference+below), r<=m0=%.4f | clutter(N>=5) share r>m0=%.4f, r>=m1=%.4f" % (m0, m1, ab, s_below_m1, s_below_m0, c_above_m0, c_above_m1))
        summarize("structure static_0 (all N>=1)", st, p)
        summarize("clutter dynamics_0 (all N>=1)", cl, p)
    # also: the placeholder for contrast at W10
    if W == 10:
        summarize("structure, placeholder (0.3,0.6,.01)", st, pp)
        summarize("clutter,   placeholder (0.3,0.6,.01)", cl, pp)

print("\n=== In-sample KITTI 00 replay (3D, single pass; structure = static_structure incl. class groups, moving = label 1) ===")
for W in (10, 2):
    path = "%s/kth00/cells_r0.2_W%d.csv" % (ROOT, W)
    st = load(path, lambda lab, N, H: (lab & 0x0F) == 3)
    solid = load(path, lambda lab, N, H: (lab & 0x0F) == 3 and (lab >> 4) == 2)
    mv = load(path, lambda lab, N, H: lab == 1)
    print("W=%d: structure cells=%d (solid %d), moving cells=%d" % (W, len(st), len(solid), len(mv)))
    for (m0, m1, ab) in ((0.15, 0.65, 0.01), (0.30, 0.60, 0.01), (0.15, 0.65, 0.05)):
        p = derive(m0, m1, ab, ab)
        print(" (m0,m1,a=b)=(%.2f,%.2f,%.2f) latency=%d" % (m0, m1, ab, p["latency"]))
        summarize("static_structure", st, p)
        summarize("struct_solid", solid, p)
        summarize("moving", mv, p)

print("\n=== Monte Carlo: P(still undecided after n windows) at m = m* (iid), for truncation n_max ===")
random.seed(20261018)
for (m0, m1, ab) in ((0.15, 0.65, 0.01), (0.15, 0.65, 0.05), (0.10, 0.55, 0.01)):
    p = derive(m0, m1, ab, ab); m = p["mstar"]
    R = 20000; nmax_grid = (8, 16, 24, 32, 48, 64, 96, 128)
    undec = {k: 0 for k in nmax_grid}; grads = 0
    for _ in range(R):
        l = 0.0; n = 0; decided_at = None
        ceil_ = p["A"] + 2 * p["l_hit"]
        while n < max(nmax_grid):
            n += 1
            l += p["l_hit"] if random.random() < m else p["l_miss"]
            if l > ceil_: l = ceil_
            if l >= p["A"] or l <= p["B"]:
                decided_at = n; break
        for k in nmax_grid:
            if decided_at is None or decided_at > k: undec[k] += 1
    print(" (%.2f,%.2f,%.2f) m*=%.3f E[N]=%.1f: " % (m0, m1, ab, m, p["EN_star"]) + "  ".join("n=%d:%.3f" % (k, undec[k] / R) for k in nmax_grid))

# Ville check at exactly m0 with iid windows vs the lag-1 autocorrelation seen on clutter (0.23 at W10)
print("\n=== Monte Carlo: false-Static at rate m0 with iid vs two-state Markov windows (lag-1 rho), open-ended, 200 windows ===")
for rho in (0.0, 0.23, 0.35, 0.5):
    for (m0, m1, ab) in ((0.15, 0.65, 0.01),):
        p = derive(m0, m1, ab, ab); R = 20000; hits = 0
        for _ in range(R):
            l = 0.0; x = 1 if random.random() < m0 else 0
            # stationary two-state chain with mean m0 and lag-1 rho: P(1|1)=m0+rho(1-m0), P(1|0)=m0(1-rho)
            p11 = m0 + rho * (1 - m0); p10 = m0 * (1 - rho)
            ever = False
            for n in range(200):
                l += p["l_hit"] if x else p["l_miss"]
                if l < p["B"]: l = p["B"]
                if l >= p["A"]: ever = True; break
                x = 1 if random.random() < (p11 if x else p10) else 0
            hits += ever
        print(" rho=%.2f (m0,m1,a)=(%.2f,%.2f,%.2f): P(ever >= A within 200 windows | rate m0) = %.4f  (alpha=%.2f)" % (rho, m0, m1, ab, hits / R, ab))
