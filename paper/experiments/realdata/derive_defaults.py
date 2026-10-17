#!/usr/bin/env python3
"""Derived SPRT numbers for a pair (m0, m1) (PREREG_P1B.md sections 1, 7 and 8). Standard library only.

    l_hit  = ln(m1/m0)                      l_miss = ln((1-m1)/(1-m0))
    A      = ln((1-beta)/alpha)             B      = ln(beta/(1-alpha))
    ceiling = A + 2 l_hit                   m*     = -l_miss / (l_hit - l_miss)   (zero drift of l)
    latency = ceil(A / l_hit)               windows for an always-hit cell
    OC(m)   : Wald operating characteristic, P(graduate | hit rate m), no overshoot
    E[N|m]  : Wald expected number of windows, no overshoot
    undecided_probability : exact probability that an iid cell of hit rate m is still undecided after n windows under the
        engine rule of PREREG_P1B.md section 8 (graduate when l >= A at any n; reject when l <= B once n >= T; no lower
        clamp before n >= T)
    n_max_undecided : smallest multiple of 16 with undecided_probability(m*) <= 0.03
"""
import math

PERIODICITY_T = 24      # touches before the periodicity gate opens (period_windows)
EPS = 1e-9


def derive(m0, m1, alpha=0.01, beta=0.01):
    if not (0.0 < m0 < m1 < 1.0):
        raise ValueError("need 0 < m0 < m1 < 1, got %r %r" % (m0, m1))
    l_hit = math.log(m1 / m0)
    l_miss = math.log((1 - m1) / (1 - m0))
    A = math.log((1 - beta) / alpha)
    B = math.log(beta / (1 - alpha))
    mstar = -l_miss / (l_hit - l_miss)
    var = mstar * l_hit ** 2 + (1 - mstar) * l_miss ** 2
    return {"m0": m0, "m1": m1, "alpha": alpha, "beta": beta, "l_hit": l_hit, "l_miss": l_miss, "A": A, "B": B,
            "ceiling": A + 2 * l_hit, "mstar": mstar, "EN_mstar": -A * B / var, "latency_all_hit": math.ceil(A / l_hit - 1e-12)}


def oc(m, m0, m1, alpha=0.01, beta=0.01):
    """Wald OC: P(graduate | hit rate m), without overshoot."""
    a = math.log(m1 / m0)
    b = math.log((1 - m1) / (1 - m0))
    A = (1 - beta) / alpha
    B = beta / (1 - alpha)
    d = m * a + (1 - m) * b
    if abs(d) < 1e-9:
        return math.log(1 / B) / (math.log(A) - math.log(B))

    def f(h):
        return m * (m1 / m0) ** h + (1 - m) * ((1 - m1) / (1 - m0)) ** h - 1

    lo, hi = (-60.0, -1e-6) if d > 0 else (1e-6, 60.0)
    for _ in range(200):
        mid = 0.5 * (lo + hi)
        if f(lo) * f(mid) <= 0:
            hi = mid
        else:
            lo = mid
    h = 0.5 * (lo + hi)
    p0 = (A ** h - 1) / (A ** h - B ** h)
    return 1 - p0


def expected_n(m, m0, m1, alpha=0.01, beta=0.01):
    a = math.log(m1 / m0)
    b = math.log((1 - m1) / (1 - m0))
    A = math.log((1 - beta) / alpha)
    B = math.log(beta / (1 - alpha))
    d = m * a + (1 - m) * b
    if abs(d) < 1e-9:
        return -A * B / (m * a * a + (1 - m) * b * b)
    p1 = oc(m, m0, m1, alpha, beta)
    return ((1 - p1) * B + p1 * A) / d


def outcome_probabilities(m0, m1, m, n_max, alpha=0.01, beta=0.01, T=PERIODICITY_T):
    """Exact (graduated, rejected, undecided) probabilities after n_max windows for iid windows of hit rate m,
    under the engine rule: graduate at l >= A at any n; reject at l <= B only once n >= T. Returns three lists
    indexed by n = 0..n_max (cumulative)."""
    d = derive(m0, m1, alpha, beta)
    lh, lm, A, B = d["l_hit"], d["l_miss"], d["A"], d["B"]
    mass = {0: 1.0}            # h (hits so far) -> probability mass of still-undecided histories; n is the step
    grad = rej = 0.0
    out_g, out_r, out_u = [0.0], [0.0], [1.0]
    for n in range(1, n_max + 1):
        nxt = {}
        for h, p in mass.items():
            nxt[h + 1] = nxt.get(h + 1, 0.0) + p * m
            nxt[h] = nxt.get(h, 0.0) + p * (1 - m)
        mass = {}
        for h, p in nxt.items():
            l = h * lh + (n - h) * lm
            if l >= A - EPS:
                grad += p
            elif n >= T and l <= B + EPS:
                rej += p
            else:
                mass[h] = p
        out_g.append(grad)
        out_r.append(rej)
        out_u.append(sum(mass.values()))
    return out_g, out_r, out_u


def undecided_probability(m0, m1, m, n, alpha=0.01, beta=0.01, T=PERIODICITY_T):
    return outcome_probabilities(m0, m1, m, n, alpha, beta, T)[2][n]


def n_max_undecided(m0, m1, alpha=0.01, beta=0.01, limit=0.03, step=16, cap=4096, T=PERIODICITY_T):
    """Smallest multiple of `step` with P(undecided after n windows | iid at m*) <= limit."""
    d = derive(m0, m1, alpha, beta)
    n_top = step
    while n_top <= cap:
        _, _, u = outcome_probabilities(m0, m1, d["mstar"], n_top, alpha, beta, T)
        for n in range(step, n_top + 1, step):
            if u[n] <= limit:
                return n, u[n]
        n_top *= 2
    raise ValueError("no n_max up to %d" % cap)


def table(m0, m1, alpha=0.01, beta=0.01):
    d = derive(m0, m1, alpha, beta)
    d["oc"] = {"%.2f" % m: oc(m, m0, m1, alpha, beta) for m in (m0, 0.30, 0.50, m1)}
    d["EN"] = {"m0": expected_n(m0, m0, m1, alpha, beta), "mstar": expected_n(d["mstar"], m0, m1, alpha, beta), "m1": expected_n(m1, m0, m1, alpha, beta)}
    n_max, u = n_max_undecided(m0, m1, alpha, beta)
    d["n_max_undecided"] = n_max
    d["undecided_at_n_max"] = u
    return d


if __name__ == "__main__":
    import json
    import sys
    m0, m1 = float(sys.argv[1]), float(sys.argv[2])
    print(json.dumps(table(m0, m1), indent=1))
