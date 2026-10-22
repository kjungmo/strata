# PREREG: frozen STRATA v2 evidence defaults (phase 1b)

Written by `freeze_prereg.py` from the committed result tables of `results_p1b/` (commit `91532aa`, 2026-10-18T10:27:02+0900). Not edited by hand.
The rule is `PREREG_P1B.md` sections 1, 6, 7 and 8; the definitions are those of `PREREG_P1.md` with Amendments 1 and 2.
`m0 = ceil_0.05(q_c)`, `m1 = floor_0.05(q_s)`, `alpha = beta = 0.01`; `l_hit = ln(m1/m0)`, `l_miss = ln((1-m1)/(1-m0))`, `A = -B = ln(99)`,
ceiling `A + 2 l_hit`; engine floor rule: no lower clamp before `n >= 24` touched windows, reject at `l <= B` once `n >= 24`.
`n_max` is the smallest multiple of 16 with P(undecided after n windows) <= 0.03 for iid windows at the zero-drift rate `m*`, exact under that rule.

## 2D backend (`grid2d`)

Status: **NOT FROZEN**

Condition: LILocBench, resolution 0.05 m, W = 15 steps (1.0 s at 15 Hz), N_min = 5, structure@static_0 against hit_in_free@dynamics_0.

- q_s 0.7130 [0.6730, 0.7500], q_c 0.1725 [0.1628, 0.2727], gap 0.5404 [0.4435, 0.5795]; N_structure 10126, N_clutter 23888

Reasons:

- lil_mapping fails: G1 FAIL (accepted 590, median 0.1930 m, p90 0.2506 m; limits 0.1, 0.2)
- lil_static_0 fails: G1 FAIL (accepted 391, median 0.1705 m, p90 0.2545 m; limits 0.1, 0.2)
- lil_dynamics_0 fails: G1 FAIL (accepted 470, median 0.1832 m, p90 0.2285 m; limits 0.1, 0.2)

Conditional values, **NOT A FREEZE**: what the rounding rule gives from the measured gap if the failed gate or check were waived. They are printed so that the lead can decide; no engine default may take them from here.

| m0 | m1 | alpha = beta | A = -B | l_hit | l_miss | ceiling | n_max | windows for an always-hit cell | m* |
|---|---|---|---|---|---|---|---|---|---|
| 0.20 | 0.70 | 0.01 | 4.5951 | 1.2528 | -0.9808 | 7.1006 | 80 | 4 | 0.4391 |

The interval crosses a rounding boundary (`PREREG_P1B.md` section 1), so both ends are stated: pair at the low interval end (0.20, 0.65), at the high end (0.30, 0.75); point pair (0.20, 0.70).

## 3D backend (`voxel3d`)

Status: **NOT FROZEN** (branch F)

Condition: Spires Keble 02, resolution 0.2 m, W = 10 steps (1.0 s at 10 Hz), N_min = 5, structure (non-ground) against clutter.

Checks before the rule is read:

- G1 and G2 on Keble 02: **FAIL** (G1 FAIL (accepted 2251, median 0.3212 m, p90 0.4627 m), G2 PASS (matched 0.9995, median 0.0174 m, p90 0.0415 m))
- time offset search valid: **pass** (valid=1 reason=ok selected_dt_s=0)
- alignment check (share >= 0.5): **pass** (alignment_share=0.9921536283)
- lean probe equals the engine on KITTI 00 (4 of 4 tables byte-identical): **pass** (4 of 4 identical)

- gap under today's miss: q_s 0.4667 [0.4000, 0.5238], q_c 1.0000 [1.0000, 1.0000], gap -0.5333 [-0.6000, -0.4762]; N_structure 195641, N_clutter 15864
- gap under the refined miss: q_s 0.5357 [0.4667, 0.6000], q_c 1.0000 [1.0000, 1.0000], gap -0.4643 [-0.5333, -0.4000]; N_structure 192255, N_clutter 13990

Reasons:

- G1 and G2 on Keble 02
- the gap is below 0.15 under both miss definitions (today -0.5333, refined -0.4643), whatever the checks say. Caveat (review finding 1, deviations item 23): 64 % of the Keble 02 clutter cells lie more than 0.5 m above the device, so the clutter class is mostly not walking people and this gap is not evidence that hit rates do not separate in 3D
- the fallback is the 2D pair, and the 2D pair is not frozen

## Inputs (sha256)

- `results_p1b/equivalence_kth.txt` 4f9ca982ec5ce059ca0d0ca526e46bc43fd1d61142034ee7ca1483993536f244
- `results_p1b/meta/spires_convert.meta` 3ded56ffd20af464a7a4e3e82e25cab0269915b36a55a8fb3840de4315e9dba2
- `results_p1b/meta/spires_offset.meta` 0122e802af31f900b9494481fbe8e3bf54b46480d11df98c2b556520f5811139
- `results_p1b/p1b_gap.csv` fad74c8545f6cb39da5a34be7fbec4c6d12949dbd46d34a62da6ab042f8200ba
- `results_p1b/p1b_gates.csv` e211c2f70f6076ce09504a0dedb708aab9b57a70f1a8b722657ef26c5cf87d3b
