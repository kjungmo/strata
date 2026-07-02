# strata_core experiments — measured results

All numbers below are real program output from the harness in
`paper/experiments/`, compiled directly against the `strata_core` sources (no
ROS/ament). Every stochastic experiment uses `std::mt19937` seeded from the fixed
constant `kSeed = 12345` (recorded in each CSV's `#seed` row). Only E4 reads the
wall clock (for timing); its absolute microsecond figures are machine-dependent,
but the cross-backend/size ratios and the cell counts are deterministic.

**Reproduction (from the worktree root
`/home/cona/kangj/strata/.claude/worktrees/paper`):**
```
bash paper/experiments/run_all.sh
```
This configures + builds `strata_core` and the four harness executables, then runs
them, writing CSVs into `paper/experiments/results/`.

Baseline confirmation: `strata_core`'s own `ctest` suite passes (1/1) before the
harness is trusted.

---

## E1 — Static-layer map quality vs time
Setup: a ground-truth static cross of **161 wall cells** (segment `y=60, x∈[20,100]`
and `x=60, y∈[20,100]`) hit every window, plus transient movers at densities
low/med/high = **5 / 25 / 100 fresh random cells per window**, over 40 windows, for
**both grid2d (120×120, res 1.0) and voxel3d (voxel 1.0)** backends. Engine runs at
its production defaults (l_hit 0.85, l_miss −0.4, decay 0.97, graduate 0.8, demote
0.45, min_obs 3), periodicity disabled. Metric: Static-set precision/recall/F1 vs
the wall set, per window. Output: `results/e1_static_quality.csv`.

| backend | density | recall reaches 1.0 | precision @w39 | F1 @w39 |
|---|---|---|---|---|
| grid2d | low (5) | window 3 | 1.000 | 1.000 |
| grid2d | med (25) | window 3 | 0.9938 | 0.9969 |
| grid2d | high (100) | window 3 | 0.9253 | 0.9612 |
| voxel3d | low (5) | window 3 | 1.000 | 1.000 |
| voxel3d | med (25) | window 3 | 0.9938 | 0.9969 |
| voxel3d | high (100) | window 3 | 0.9758 | 0.9877 |

Takeaway: walls graduate to Static within 3 windows (recall 0→1.0), and recall then
stays 1.0; the only errors are a handful of false-positive statics that appear only
under heavy clutter (100 movers/window), where a random cell occasionally gets
re-hit enough to graduate — precision 1.0 → ~0.93 (grid2d) / 0.98 (voxel3d).

## E2 — Periodicity detection
Setup: three periodic doors (`p8` 4-on/4-off, `p8` 2-on/6-off, `p4` 2-on/2-off — all
detectable against FreMEn base period **T=8, 3 harmonics**), a constant wall, and
four deterministic Bernoulli(0.5) aperiodic movers as negative controls; 64 windows
driven through the real `LayeredMap` pipeline. Metrics: per-cell final class
(Periodic TPR/FPR vs GT), and FreMEn amplitude vs observation length. Outputs:
`results/e2_classification.csv`, `results/e2_amplitude_vs_length.csv`,
`results/e2_summary.csv`.

| quantity | value |
|---|---|
| Periodic **TPR** | **0.667 (2/3)** |
| Periodic **FPR** | **0.200 (1/5)** |
| door_p8_4on4off amplitude / class | 0.653 → **Periodic** (correct) |
| door_p4_2on2off amplitude / class | 0.707 → **Periodic** (correct) |
| door_p8_2on6off (25% duty) class | **Transient** (miss — see below) |
| aperiodic_1 amplitude / class | 0.329 → **Periodic** (false positive) |
| amplitude before T windows (L=6) | 0.0 (gated: needs n ≥ period_windows) |

Takeaway: 50%-duty doors are detected cleanly (amplitude ≫ a_min=0.3); the miss is
the 25%-duty door, which a probe confirms is **pruned in ~10 of its 64 windows** —
during each 6-window vacant stretch its log-odds falls below prune_prob while its
FreMEn amplitude is still 0, erasing its history so periodicity never matures. The
lone false positive is a random 50/50 cell whose Fourier energy (0.329) just clears
the 0.30 threshold. (The `ref_amplitude` column is the non-pruning reference model,
so it can read ≥ a_min for a cell the real, pruning pipeline still labels non-periodic.)

## E3 — Sensitivity: hysteresis band and decay
Setup: 50 GT wall cells observed occupied with prob 0.6 each window (noisy) plus 10
fresh movers/window, 80 windows; the **identical noise sequence** replayed for every
config. Sweep graduate_prob ∈ {0.6,0.7,0.8,0.9} × demote_prob (a low floor 0.3, a
mid floor 0.5, and the **degenerate demote==graduate, i.e. no hysteresis**) ×
survival_decay ∈ {0.90,0.97,1.0}. Metrics: final Static F1 and count of "flicker"
transitions (per-window isStatic toggles). Output: `results/e3_sensitivity.csv`.

| config | hysteresis band | flicker transitions | final F1 |
|---|---|---|---|
| g0.9 d0.9 decay0.90 | 0 (degenerate) | **574** | **0.810** |
| g0.9 d0.3 decay0.90 | 0.6 | **54** | **1.000** |
| g0.8 d0.8 decay0.90 | 0 (degenerate) | 343 | 0.947 |
| g0.7 d0.7 decay0.90 | 0 (degenerate) | 250 | 0.980 |
| g0.9 d0.3 decay1.0 (best) | 0.6 | **52** | 1.000 |

Takeaway: removing hysteresis is catastrophic — the worst degenerate config flickers
**574** times and loses ~19% F1 (0.81), while an equal-threshold-pair-but-wide-band
config on the same noise flickers **52–54** times at F1 1.0; lower survival_decay
(0.90) also amplifies flicker (e.g. 574 vs 150 at decay 0.97 for the same degenerate
0.9/0.9). Hysteresis + slow forgetting is what keeps the static map stable.

## E4 — Throughput and memory
Setup: 150 random hits/window over 20 windows, grid2d vs voxel3d at extents
100/250/500. Metrics: per-`integrate()` latency, endWindow cell-update rate, live
cell count, and rough memory = 56 B/cell (CellEvidence 24 B + ~32 B map node,
periodicity off). Output: `results/e4_throughput.csv`.

| backend | size | µs/integrate | cell-updates/s | live cells | est. memory |
|---|---|---|---|---|---|
| grid2d | 100×100 | 1303 | 9.74e6 | 8,553 | 479 KB |
| voxel3d | 100×100 | 5139 | 7.43e6 | 85,399 | 4.78 MB |
| grid2d | 250×250 | 1332 | 2.40e7 | 45,657 | 2.56 MB |
| voxel3d | 250×250 | 19298 | 5.34e6 | 295,300 | 16.5 MB |
| grid2d | 500×500 | 5077 | 1.08e7 | 150,096 | 8.41 MB |
| voxel3d | 500×500 | 38308 | 3.92e6 | 661,410 | 37.0 MB |

Takeaway: grid2d is 4–8× cheaper per `integrate()` than voxel3d at equal extent,
because 3D ray-casting spawns far more free-space voxels (voxel3d holds ~6–10× the
live cells for the same scene), which dominates both time and memory; per-cell
footprint is a flat 56 B, so memory tracks the live-cell count linearly (≤37 MB even
at the largest 3D case). Absolute times are machine-dependent; cell counts and ratios
are deterministic.
