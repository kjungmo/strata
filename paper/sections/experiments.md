# 6 Evaluation (synthetic)

## 6.1 Setup

All four experiments (E1–E4) are driven by a standalone harness in
`paper/experiments/` that links directly against the `strata_core` sources —
no ROS 2, no ament, no colcon. Reproduction from the repository root is a
single call, `bash paper/experiments/run_all.sh`, which configures and builds
`strata_core` and the four harness executables, runs each one, and writes the
CSVs in `paper/experiments/results/` from which every number and figure in
this section is taken; `strata_core`'s own `ctest` suite (1/1) is checked
first, so the harness is only trusted once the unit-level behavior it builds
on is confirmed. Every stochastic experiment (E1–E3) seeds a single
`std::mt19937` from the fixed constant `kSeed = 12345`, recorded in each
CSV's `#seed` row, so the reported precision/recall/F1/flicker/TPR/FPR
numbers are exactly reproducible. E4 additionally reads the wall clock to
time `integrate()`/`endWindow()` calls; its absolute microsecond figures are
therefore machine-dependent, while the live-cell counts it also reports are
deterministic (they follow only from ray geometry, not timing).

The four experiments map onto the two thesis claims of §3–4 (C2, C5)
differently. E1 and E4 probe *backend-behavior equivalence*: both feed
identical hit/miss streams through `Grid2DBackend` and `Voxel3DBackend` and
compare the outcome, so any divergence is attributable only to the
geometry layer (point&rarr;`CellId` mapping and the free-space ray walk),
since the classifier code both backends call is the same `LayeredMap`
instance type. E2 and E3, by contrast, drive `LayeredMap`/`PeriodicityModel`
directly, with no backend in the loop at all — a choice that is itself an
argument for C1: because persistence, hysteresis, and periodicity are
implemented once, independent of `CellId` provenance, characterizing them
against `LayeredMap` directly is valid for whichever backend supplies the
cell ids in production. Unless stated otherwise, all four experiments use
the engine's production defaults from [@tbl:params]
($l_{\text{hit}}=0.85$, $l_{\text{miss}}=-0.4$, $\lambda=0.97$,
$p_{\text{grad}}=0.8$, $p_{\text{dem}}=0.45$, $N_{\min}=3$).

## 6.2 E1 — Static-layer map quality vs. time

**Setup.** A ground-truth static cross of 161 cells (segment $y{=}60,
x\in[20,100]$ and $x{=}60, y\in[20,100]$) is hit every window, alongside
transient movers at three densities — low/med/high = 5/25/100 fresh random
cells per window — over 40 windows, run against both a $120\times120$
(resolution 1.0) `Grid2DBackend` and a $1.0$-voxel `Voxel3DBackend`, with
periodicity disabled to isolate the persistence layer. Static-set
precision/recall/F1 against the 161-cell wall set is logged per window
(`results/e1_static_quality.csv`).

| backend | density | recall $=1$ from | precision @ $w{=}39$ | F1 @ $w{=}39$ |
|---|---|---|---|---|
| grid2d | low (5/window) | window 3 | 1.0000 | 1.0000 |
| grid2d | med (25/window) | window 3 | 0.9938 | 0.9969 |
| grid2d | high (100/window) | window 3 | 0.9253 | 0.9612 |
| voxel3d | low (5/window) | window 3 | 1.0000 | 1.0000 |
| voxel3d | med (25/window) | window 3 | 0.9938 | 0.9969 |
| voxel3d | high (100/window) | window 3 | 0.9758 | 0.9877 |

Table: E1 — final static-set precision/recall/F1, both backends, all three
clutter densities. {#tbl:e1}

![E1: static-set F1 vs. window index, one panel per backend, one line per
clutter density. Both backends recover F1$\approx$1.0 within three windows;
the 100-movers/window regime is the only one with a visible
precision/F1 dip, and the dip is larger for
grid2d.](../figures/fig_e1_f1_vs_time.pdf){#fig:e1}

With $N_{\min}=3$, a cell needs three touched windows before it is eligible
to graduate; the CSV confirms recall jumps from 0 to 1.0 exactly at the
third window (window index $t{=}2$) for every one of the six
backend$\times$density combinations, and holds at 1.0 through $w{=}39$ in
every case — no wall cell is ever demoted by clutter. The only errors are a
handful of false-positive statics that appear when a random mover happens to
re-hit the same cell often enough to graduate on its own; at low density
(5 movers/window) this never happens at all, and precision stays exactly
1.0 through $w{=}39$ for both backends. At medium density it saturates at a
single spurious cell (precision floors at 0.9938 and never falls further),
but at high density (100 movers/window) false statics keep accumulating
window over window — from 162 predicted cells at $w{=}17$ (grid2d) /
$w{=}25$ (voxel3d) to 174 (grid2d) / 165 (voxel3d) by $w{=}39$ — giving the
only material gap between backends:
precision 0.9253 (grid2d) vs. 0.9758 (voxel3d) at the final window. Both
backends run the identical `LayeredMap` graduation rule on the identical
mover stream; the gap traces to geometry, not the classifier — `Grid2DBackend`
projects every hit onto a shared 2D $(x,y)$ cell (dropping $z$), so
independent movers collide into the same cell more often than in
`Voxel3DBackend`'s full 3D hash, and each collision is exactly the
accumulation event that produces a spurious static.

## 6.3 E2 — Periodicity detection

**Setup.** Three periodic doors (`p8` 4-on/4-off, `p8` 2-on/6-off, `p4`
2-on/2-off, all detectable against the FreMEn base period $T{=}8$ with
$H{=}3$ harmonics), a constant wall, and four deterministic Bernoulli(0.5)
aperiodic movers as negative controls, are driven through the real
`LayeredMap` pipeline for 64 windows. Reported: per-cell final class against
ground truth (Periodic TPR/FPR), and FreMEn dominant-harmonic amplitude vs.
observation length (`results/e2_classification.csv`,
`results/e2_amplitude_vs_length.csv`, `results/e2_summary.csv`).

| cell | ground truth | final class | amplitude | note |
|---|---|---|---|---|
| `door_p8_4on4off` (50% duty) | periodic | Periodic | 0.653 | correct |
| `door_p4_2on2off` (50% duty) | periodic | Periodic | 0.707 | correct |
| `door_p8_2on6off` (25% duty) | periodic | Transient | — | **miss** |
| `wall_constant` | non-periodic | Static | $\approx$0 | correct |
| `aperiodic_1` | non-periodic | Periodic | 0.329 | **false positive** |
| `aperiodic_0`, `_2`, `_3` | non-periodic | Transient | 0.14–0.25 | correct |

Table: E2 — per-cell classification against ground truth ($a_{\min}=0.3$);
Periodic TPR $=2/3$, FPR $=1/5$. {#tbl:e2}

![E2: (left) Periodic-class TPR/FPR bars; (right) FreMEn dominant-harmonic
amplitude vs. observation length for all 8 probe cells, with the
$a_{\min}=0.3$ threshold and the $n\ge T$ validity gate marked — the two
50%-duty doors clear the threshold cleanly, the 25%-duty door is pruned
before it matures, and one aperiodic mover produces the false
positive.](../figures/fig_e2_periodicity.pdf){#fig:e2}

Both 50%-duty doors are detected cleanly: amplitude 0.653 and 0.707 clear
$a_{\min}=0.3$ by a wide margin. The one miss, the 25%-duty door
(`door_p8_2on6off`), is a **prune/maturity race**, not a modeling failure:
the notes accompanying this harness report that the real pipeline prunes
this cell in roughly 10 of its 64 windows — each 6-window vacant stretch
lets its log-odds fall below $p_{\text{prune}}$ while its FreMEn amplitude
is still 0 (the $n\ge T$ gate has not yet been reached), erasing its
accumulator state before periodicity can mature. The CSV's `ref_amplitude`
column — a non-pruning reference `PeriodicityModel` fed the identical
occupancy stream — reads 0.46194 for this same cell, comfortably above
$a_{\min}$, confirming the signal itself is periodic and detectable; the
miss is specifically an interaction between pruning and the touched-window
amplitude gate. The lone false positive, `aperiodic_1` at amplitude 0.329,
is a Bernoulli(0.5) cell whose Fourier energy over 64 windows happens to
clear the 0.30 threshold by a small margin (0.029 above $a_{\min}$) — the
kind of chance alignment expected from a fixed-length random negative
control rather than a systematic classifier flaw. The amplitude-vs-length
data also confirms the validity gate operates as specified: for
`door_p8_4on4off`, amplitude is exactly 0 at observation length 6 ($n<T{=}8$)
and jumps to 0.653 at length 8 ($n\ge T$), where it then stabilizes.

## 6.4 E3 — Sensitivity: hysteresis band and decay

**Setup.** 50 ground-truth wall cells are observed occupied with probability
0.6 each window (noisy) alongside 10 fresh movers/window, over 80 windows,
with the identical noise sequence replayed for every configuration in the
sweep. The sweep spans `graduate_prob` $\in\{0.6,0.7,0.8,0.9\}$
$\times$ `demote_prob` (a low floor 0.3, a mid floor 0.5, and the degenerate
`demote_prob == graduate_prob`, i.e. zero hysteresis band)
$\times$ `survival_decay` $\in\{0.90,0.97,1.0\}$ — 36 configurations total.
Reported: final Static-layer F1 and the count of per-window `isStatic`
flicker transitions (`results/e3_sensitivity.csv`).

| `graduate_prob` | `demote_prob` | hysteresis band | `survival_decay` | flicker | final F1 |
|---|---|---|---|---|---|
| 0.9 | 0.9 (degenerate) | 0 | 0.90 | **574** | **0.810** |
| 0.8 | 0.8 (degenerate) | 0 | 0.90 | 343 | 0.947 |
| 0.7 | 0.7 (degenerate) | 0 | 0.90 | 250 | 0.980 |
| 0.9 | 0.9 (degenerate) | 0 | 0.97 | 150 | 1.000 |
| 0.9 | 0.9 (degenerate) | 0 | 1.00 | 122 | 1.000 |
| 0.9 | 0.3 | 0.6 | 0.90 | 54 | 1.000 |
| 0.9 | 0.3 | 0.6 | 0.97 | 52 | 1.000 |
| 0.9 | 0.3 | 0.6 | 1.00 | **52** | **1.000** |

Table: E3 — selected sweep configurations (full 36-row sweep in the CSV);
identical replayed noise throughout. {#tbl:e3}

![E3: three heatmap panels (one per `survival_decay`); rows =
`graduate_prob`, columns = hysteresis-band width, cells colored by
flicker-transition count (log scale) and annotated with the raw count and
final F1 — the degenerate (zero-band) column drives both the worst flicker
and the worst F1 at every decay
setting.](../figures/fig_e3_sensitivity.pdf){#fig:e3}

Removing hysteresis is the single largest effect in the sweep: at
`graduate_prob = demote_prob = 0.9`, `survival_decay = 0.90` (the worst
degenerate configuration), the wall layer flickers 574 times and loses
19% of its final F1 (0.810, recall collapses to 0.68), while a
same-noise, same-decay run with the hysteresis band widened to 0.6
(`demote_prob = 0.3`) flickers only 54 times at F1 1.000 — the band alone
accounts for a $>10\times$ reduction in flicker. The degenerate
(zero-band) configurations are also internally monotonic in threshold: at
`survival_decay = 0.90`, flicker rises with the coincident threshold value
(250 at 0.7, 343 at 0.8, 574 at 0.9) even though a higher threshold is
ordinarily the more conservative, more "static" setting — a threshold pair
with no gap between graduate and demote is not a substitute for a
hysteresis band, regardless of where that pair sits.
`survival_decay` is the second-order effect: holding the worst degenerate
threshold pair (0.9/0.9) fixed, flicker falls monotonically as forgetting
slows — 574 at $\lambda{=}0.90$, 150 at $\lambda{=}0.97$, 122 at
$\lambda{=}1.0$ — because faster decay lets noisy log-odds cross the
(here, coincident) threshold more often. With a wide hysteresis band,
`survival_decay` barely matters (52–54 flicker across all three decay
settings at the best band): hysteresis, not decay, is what keeps the
static layer stable.

## 6.5 E4 — Throughput and memory

**Setup.** 150 random hits per window over 20 windows, for `Grid2DBackend`
vs. `Voxel3DBackend` at three nominal extents (100/250/500, resolution/voxel
size 1.0), periodicity disabled. Reported: mean per-`integrate()` call
latency, mean per-`endWindow()` cell-update rate, final live-cell count, and
estimated memory at the fixed 56 B/cell footprint documented in §4
(`CellEvidence` 24 B + $\approx$32 B `unordered_map` node overhead, with
FreMEn storage excluded since periodicity is off in this run)
(`results/e4_throughput.csv`).

| backend | extent | µs/`integrate()` | endWindow updates/s | live cells | est. memory |
|---|---|---|---|---|---|
| grid2d | $100^2$ | 320.8 | $9.41\times10^{6}$ | 8,553 | 479 KB |
| voxel3d | $100^2$ | 4,796.8 | $7.06\times10^{6}$ | 85,399 | 4.78 MB |
| grid2d | $250^2$ | 1,702.6 | $1.07\times10^{7}$ | 45,657 | 2.56 MB |
| voxel3d | $250^2$ | 18,469.5 | $4.46\times10^{6}$ | 295,300 | 16.5 MB |
| grid2d | $500^2$ | 6,548.9 | $8.64\times10^{6}$ | 150,096 | 8.41 MB |
| voxel3d | $500^2$ | 35,958.3 | $4.45\times10^{6}$ | 661,410 | 37.0 MB |

Table: E4 — per-call latency, throughput, live-cell count, and estimated
memory, both backends, all three extents. Absolute µs are single-machine
wall-clock samples; live-cell counts (and the ratios derived from them) are
geometry-determined and reproducible. {#tbl:e4}

![E4: grouped bars by backend $\times$ extent — per-`integrate()` latency in
µs (log scale, left) and `endWindow()` cell-update throughput in
$10^6$ updates/s (right). grid2d is cheaper per call than voxel3d at every
tested extent; the gap tracks voxel3d's larger live-cell
count.](../figures/fig_e4_throughput.pdf){#fig:e4}

At every tested extent, `Grid2DBackend`'s `integrate()` call is cheaper than
`Voxel3DBackend`'s at the identical nominal extent and hit rate — by
$4{,}796.8/320.8\approx14.9\times$ at $100^2$, $18{,}469.5/1{,}702.6\approx
10.8\times$ at $250^2$, and $35{,}958.3/6{,}548.9\approx5.5\times$ at
$500^2$, the multiple shrinking as extent grows. The live-cell counts
explain why: `Voxel3DBackend` ends each run holding
$85{,}399/8{,}553\approx10.0\times$, $295{,}300/45{,}657\approx6.5\times$,
and $661{,}410/150{,}096\approx4.4\times$ as many cells as `Grid2DBackend`
at the same extent, because its free-space ray-sample march (§4.6) walks a
full 3D volume and spawns far more traversed voxels than `Grid2DBackend`'s
exact 2D Bresenham clear over the same nominal footprint — the same
geometric asymmetry that drives the latency gap also drives the cell-count
gap, and both narrow together as the extent grows and the ray paths (and
hence the per-ray voxel counts) become a smaller fraction of the total
volume. Memory is exactly what a flat 56 B/cell predicts: estimated memory
tracks live-cell count linearly across every row of [@tbl:e4] with no
additional constant, topping out at 37.0 MB for the largest 3D
configuration tested (661,410 cells $\times$ 56 B) — inexpensive even at
the largest map in this sweep. Absolute microsecond values here are
single-run wall-clock measurements on one machine and are not claimed to be
portable; the live-cell counts, and the ratios and memory figures derived
from them, follow only from ray/hash geometry and reproduce under the fixed
seed regardless of machine.

## 6.6 Synthesis

Across E1–E4, the two backends behave as one engine wearing two geometries,
exactly as C1 asserts by construction: E1's near-identical F1 trajectories
(both backends graduate the wall by the third window and hold recall 1.0
through $w{=}39$) and E4's flat 56 B/cell tracking live-cell count are the
empirical face of "no per-dimension logic or cost," since `LayeredMap` and
`PeriodicityModel` — exercised directly and identically in E2 and E3 — never
see which backend produced the `CellId` they are updating. The two places
where the backends *do* diverge are both traced, not merely asserted, to
geometry rather than to the shared classifier: E1's high-clutter precision
gap (0.9253 vs. 0.9758) follows from `Grid2DBackend`'s 2D $z$-projection
colliding independent movers into shared cells more often than
`Voxel3DBackend`'s full-3D hash, and E4's per-call latency gap
(5.5–14.9$\times$, narrowing with extent) follows from `Voxel3DBackend`'s
free-space ray-sample march spawning 4.4–10.0$\times$ more live voxels than
`Grid2DBackend`'s exact line clear at the same nominal extent. E2 and E3,
run once against `LayeredMap` with no backend involved at all, characterize
the shared engine's own behavior — clean 50%-duty periodicity detection with
a diagnosed prune/maturity-race failure mode at low duty cycle, and
hysteresis as the dominant flicker suppressor, decay a secondary one — and
that characterization is, by the same construction argument, valid for
whichever backend feeds it cell ids in production.
