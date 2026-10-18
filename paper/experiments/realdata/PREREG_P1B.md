# PREREG_P1B: Amendment 2 to the phase 1 pre-registration (phase 1b)

Status: written and committed BEFORE any new measurement of phase 1b. The commit time of this file, the
commit time of `results_p1b/test_split_stat_before*.tsv` (2db4f2c, 2026-10-18 07:04:53 +09:00) and the time
stamps in `results_p1b/logs/` show the order. Sections 0 to 11 of `PREREG_P1.md` stay as committed; where this
file replaces something, it says so. A change to anything below after the first phase 1b measurement is a
deviation and is listed in `results_p1b/deviations.md`.

Inputs that bind this phase: the lead's decisions after phase 1 (L-1 to L-5), the advisory memo P1_DECISION_ADVICE
(sections 1 to 4 and 7), `PREREG_P1.md` with Amendment 1, `results_p1/deviations.md`. The advisory replay numbers
in the memo are scratch numbers; none of them is used here.

## 0. Why this amendment exists, and why it is not peeking

Facts learned from CALIBRATION data only (run B of phase 1, no test-split file opened):

* KITTI 00 has 141 frames (14 s). A structure voxel is touched in a median of 4 windows at W = 10. A hit-rate test
  cannot decide most structure there, whatever the gap. A lifelong mapper needs revisits.
* The 3D half-voxel ray march can put a pass-through miss on a wall voxel next to the endpoint at grazing incidence
  (`results_p1/deviations.md` item 15). Phase 1 did not measure this.
* The wall-thickness gate of phase 1 accepts a patch on its extent alone (PREREG_P1 section 11.5). It is replaced below.
* The node ships one-second time windows (`strata/params/*.yaml`, `window_period_s: 1.0`). Phase 1 measured W = 10
  steps, which is 0.67 s at 15 Hz.

Consequences, all fixed here before any new number exists:

1. The 2D condition W = 15 (1.0 s at 15 Hz) is added (section 2).
2. The gate rules G1 and G2 replace the old gate (section 3).
3. One Spires Keble sequence moves from the TEST split to the CALIBRATION split (section 4).
4. The miss definition for 3D gets a second branch (section 5).
5. The rounding direction, the freeze rule, Option C and the engine floor rule are fixed (sections 1, 6, 7, 8).

The move of data between splits is legitimate only if it is committed before any test-split content is opened, chosen
by a rule that does not look at content, reported with its chronology and reason, and its cost in test power is
stated. Section 10 does this.

## 1. Rounding definition (replaces "rounded outward to 0.05" of DESIGN_AND_PLAN section 3 C)

"Outward" means toward each other, so that the stated error rates cover the measured percentiles:

* `m0 = ceil_0.05(q_c)`, that is the smallest multiple of 0.05 that is at least `q_c - 1e-9`.
* `m1 = floor_0.05(q_s)`, that is the largest multiple of 0.05 that is at most `q_s + 1e-9`.
* Bounds: `m0 >= 0.05` and `m1 <= 0.95`. If rounding gives `m1 - m0 < 0.15`, the first branch of the escalation rule
  did not hold for the rounded pair either, and the freeze file says so (section 7).
* `alpha = beta = 0.01`.
* Derived: `l_hit = ln(m1/m0)`, `l_miss = ln((1-m1)/(1-m0))`, `A = ln((1-beta)/alpha)`, `B = ln(beta/(1-alpha))`,
  ceiling `A + 2 l_hit`.

`q_s` is the 10th percentile of the per-cell hit rate of structure cells and `q_c` the 90th percentile of clutter cells,
numpy's default linear method, over cells with `N >= N_min = 5` (primary), computed by `analyze_p1.py` code.
Point estimates decide. Block-bootstrap intervals of `q_s`, `q_c` and of the gap are reported next to them. If an
interval crosses a rounding boundary, the report states the pair at both ends; the frozen pair does not change.
The opposite reading, `m0 = floor`, `m1 = ceil`, would put the measured percentile populations inside the indifference
zone. It is rejected for that reason.

## 2. Window W = 15 for the 2D backend (extends PREREG_P1 section 2)

* Data: the LILocBench CALIBRATION sequences `mapping`, `static_0`, `dynamics_0`, the run B frame caches (anchor
  `start`, offset -0.13 s), the run B reference labels (`mapping`, `W = 1`). Nothing in the labels changes.
* Condition: `layer_interval = 15` steps (one step is one front/rear pair; 15 steps are 1.0 s at 15 Hz), resolutions
  0.05 m (shipped) and 0.10 m. The window counts closed are 598 on `static_0`. A last partial window is not closed.
* Statistics: exactly those of PREREG_P1 section 4 and 11.3 (percentiles, gap, 2000-replicate bootstrap with seed
  20261014, tiles of 10 m, touched windows, runs and lag-1, matched-rate bands and AUC, diversity, survivorship), computed by
  `analyze_p1b.py`, which imports the functions of `analyze_p1.py` unchanged.
* The shipped 2D condition is `(0.05 m, W = 15, N_min = 5)`, structure = `structure` on `static_0`, clutter =
  `hit_in_free` on `dynamics_0`. It sets the 2D pair. W = 10 and W = 2 stay reported and set nothing.

## 3. Quality gates (replace PREREG_P1 section 5, wall thickness; pose sanity rules there stay)

Thresholds come from sensor specifications. They are not tuned on the data and do not change after this commit.

### 3.1 G1, wall thickness with a shape filter

A patch is a neighbourhood of a random seed point: all points of the point set within radius `rho` of the seed, with
`n >= 50`. Covariance eigenvalues `l1 >= l2 >= l3` (2D: `l1 >= l2`). Thickness `t = sqrt(12 * l_min)`, the width of a uniform
slab of equal variance. The patch is accepted by shape only; the thickness is not looked at:

* 2D (`rho = 1.0 m`): linear, that is `l2 / l1 <= 0.02`, and extent `sqrt(12 l1) >= 1.0 m`.
* 3D (`rho = 2.0 m`): planar, that is `l3 / l1 <= 0.02` and `(l2 - l3) / l1 >= 0.5`; vertical, that is the normal
  (eigenvector of `l3`) has `|n_z| <= 0.25`; extent `sqrt(12 l2) >= 1.0 m`.

20,000 seeds drawn uniformly (mt19937-64, seed 20261018) from the point set. Point sets: 2D, every 5th converted hit of
the run B frame cache; KITTI 00, every 4th static GT point (as in phase 1); Keble 02, every 50th return of the frame
cache (section 4.4). Pass: at least 500 accepted patches, median `t` at most `T50`, and 90th percentile at most `T90`:

| Sensor class | T50 | T90 | Where the number comes from |
|---|---|---|---|
| 2D scanning LiDAR (statistical error 20 mm) | 0.10 m | 0.20 m | sigma 20 mm plus 10 mm ground-truth pose error gives sigma 22 mm, so `t = 3.46 sigma` is 0.077 m (1.5 cells at 0.05 m); T50 leaves 30 % margin, T90 is 2.6 times the expected value |
| 3D 64-beam LiDAR (accuracy 30 mm) | 0.20 m | 0.40 m | sigma 30 mm plus 20 mm pose error gives sigma 36 mm, so `t` is 0.125 m; T50 leaves 60 % margin; this is the absolute limit of phase 1 (0.20 m, 0.40 m, one and two cells at 0.2 m) |

The shape filter caps the thickness of an accepted patch at `sqrt(0.02) * sqrt(12 l1)`: 0.28 m in 2D (a linear patch is at most
2 m long) and 0.49 m in 3D (a disc of radius 2 m has `sqrt(12 l1)` = 3.46 m). Both caps lie above T90, so the p90 rule can fail. A
badly smeared sequence loses patches (below 500: fail) instead of showing a large thickness; the report prints the accepted count with every
verdict.

### 3.2 G2, pose consistency (what `q_s` depends on)

For every 10th step (query step), every return `p` of the step is compared with the union `R` of the returns of the
steps whose index differs by 15 to 60 (2D, 1 s to 4 s at 15 Hz) or by 10 to 40 (3D, 10 Hz), in both directions, among the
steps that exist. `d(p)` = distance from `p` to the nearest point of `R` (3D distance; 2D distance in the plane).
A return with `d > 0.5 m` is "unmatched" (a surface not seen before, or something that moved); it is counted and left out of
the percentiles. Subsampling, fixed here: 2D uses all returns of query and reference; 3D uses every 8th return of
a query step and every 4th return of a reference step (the subsampling can only raise `d`).

Pass: the matched share is at least 0.5 (otherwise the gate has no evidence and fails), the median of `d` over matched
returns is at most one cell and the 90th percentile at most two cells of the finest grid of the sensor class: 2D
0.05 m and 0.10 m; 3D 0.10 m and 0.20 m.

### 3.3 What a failure does

* A sequence that fails G1 or G2 is excluded from headline tables. It is still run and reported in a table marked
  EXCLUDED with the reason (as in PREREG_P1 section 5).
* The 2D pair needs `mapping` (it builds the reference labels), `static_0` (structure) and `dynamics_0` (clutter) to
  pass G1 and G2. If one fails, the 2D pair is NOT frozen and the lead decides (advice section 4: "stop; re-examine the
  time base first").
* KITTI 00 gets the gates for the record; it cannot set the 3D pair (section 5).

### 3.4 Gate step

`p1b_probe gate2d` (frame caches of the three LILocBench sequences), `gate-kth` (KITTI 00 frames and GT cloud),
`gate-spires` (Keble 02 frame cache) write `gate_*.meta`. Only the gate step is re-run for the calibration sequences of
phase 1; no percentile of phase 1 changes. The report of phase 1 keeps its old gate columns and states that the first
gate was replaced before the test split was touched.

## 4. The Keble 02 re-split and its 3D calibration

### 4.1 Selection rule (content-neutral)

Spires Keble College core sequences with a `processed/trajectory/gt-tum.txt` are 02, 03, 04, 05 (Keble 01 is optional and has
no GT file; DATA_MANIFEST). The lowest-numbered one, `2024-03-12-keble-college-02`, becomes CALIBRATION data. Keble
03, 04, 05 and the TLS map stay TEST. The rule uses the sequence number and the existence of a file. It does not use
any property of the content.

### 4.2 What of the TLS map is read

The TLS file `ground_truth_map/keble-college/merged-cloud-1cm.pcd` is TEST data. Only this part of it is allowed to
enter any statistic: **the points whose horizontal position (x, y) lies within 30 m of at least one (x, y) position in the
Keble 02 `gt-tum.txt`** (the "Keble 02 area"). The file is read sequentially from beginning to end, because a binary PCD
cannot be read by region. Every point outside the area is discarded at read time and enters nothing: no count, no
voxel, no column. The reader reports points read, kept and discarded. The area is the union of discs, tested with an
exact distance check against all positions of the track.

The other test files are not opened in this phase: KITTI 05, semi-indoor, `lt_changes_*`, Keble 03 to 05, Boreas.
The `stat` records before (2db4f2c) and after (section 11) show this. The disk is mounted without access times, so
`atime` is not evidence; `mtime`, `ctime` and size are.

### 4.3 Data, frames and poses

* Scans: `sequences/2024-03-12-keble-college-02/raw/lidar-clouds.zip`, one PCD per scan (read with a zip reader
  that inflates the member; no extraction to disk). Expected fields (from the P0 sizing note): `x y z intensity timestamp ring`.
* Poses: `processed/trajectory/gt-tum.txt` (`t x y z qx qy qz qw`: pose of the base frame in the world frame, the frame of
  the TLS map). Interpolation: linear in position, spherical in orientation; no extrapolation; a bracket wider than
  0.25 s gives no pose.
* Extrinsic: `calibration/README.md` of the dataset: `T_base_lidar` (xyz, quaternion xyzw) = `[0, 0, 0.124, 0, 0, 1, 0]`.
  A point in the lidar frame maps to the world frame by `p_W = T_W_base(t) * T_base_lidar * p_lidar`. (The README says the processed clouds
  are in the base frame "not the lidar frame", so the raw clouds are in the lidar frame.)
* Point time: the per-point `timestamp` field (unix seconds) plus the offset `dt` of section 4.5, each point with its own
  interpolated pose (deskew). Fallback, fixed now: if the field is missing, or more than 1 % of the timestamps lie outside
  the GT span by more than 1 s, every point of a scan uses the scan time from the file name plus `dt`. A fallback is a
  deviation and is reported.
* Frame (integration step): one scan. Frame origin: the lidar position `(T_W_base * T_base_lidar).t` at the median
  timestamp of the retained points. Group: the scan index.
* Returns kept: finite, with lidar-frame range between 1.5 m (the device and the operator) and 20 m (`R_max`, near the
  region where the TLS reference is dense). A return outside has no ray and adds no observation. Points without a
  pose are dropped and counted; if more than 1 % of the points of a candidate lack a pose, the candidate is invalid.
* Cache: a frame cache (format `SCAC` of phase 1) under `/mnt/d/wsl/strata/p1b/`, not in the repository.

### 4.4 Alignment check

On every 30th scan, every 5th kept return, at the selected `dt`: the share of returns whose 0.1 m voxel has a TLS
point (at least one) in its 3 x 3 x 3 neighbourhood. Pass: at least 0.5. If it fails, poses, extrinsic and TLS frame
do not agree, the sequence is invalid for 3D calibration, and section 6 branch F applies.

### 4.5 Time offset between scan and ground truth

The lesson of phase 1 (a 0.13 s offset hid in LILocBench) is applied up front, on Keble 02 only. Candidates
`dt` in `{-0.15, -0.14, ..., +0.15}` s (31 values). Score(dt) = share of returns, over every 30th scan and every 5th kept
return, whose 0.05 m voxel or one of its six face neighbours holds at least 2 TLS points (TLS area as in 4.2); higher is
better. Selection: the highest score; among candidates with a score of at least 0.995 times the highest, the smallest
`|dt|`. The search is invalid if the selected `|dt| = 0.15` or if more than 1 % of points lack a pose; then section 6
branch F applies. The score at `dt = 0` is reported. The selected `dt` is used for all of Keble 02.

### 4.6 Labels (per resolution r in {0.2, 0.1} m; voxel indexing by `Voxel3DBackend::voxelId`)

From the TLS area (the cloud is the merged 1 cm cloud; no further down-sampling):

* A voxel is TLS-occupied if it holds at least 3 TLS points. TLS-present: at least 1 point.
* Ground rule as in PREREG_P1 section 3: a TLS-occupied voxel whose lowest TLS point is at most 0.30 m above the lowest TLS point of
  the 1 m x 1 m column containing the voxel centre is `structure_ground` (code 2), else `structure` (code 3, the
  "non-ground structure" proxy and the primary structure group).
* A voxel next to structure: not TLS-occupied, but TLS-present or with a TLS-present voxel in its 26-neighbourhood:
  a one-voxel buffer, unlabelled.
* `clutter` (code 1): a voxel that holds at least one kept return endpoint of Keble 02 (an observeHit), is not TLS-present, has no TLS-present
  voxel in its 26-neighbourhood, and whose 1 m x 1 m column holds at least one TLS point (the "support" rule: the TLS
  scanned there; roofs and areas the TLS never reached are left out).
* Voxels of other kinds are unlabelled.
* Thinning (memory): a class with more than 2,000,000 voxels keeps each voxel when `splitmix64(id) < p * 2^64` with
  `p = 2,000,000 / n_class`. The kept fraction is reported per class.

Limits stated now: TLS and the walks were not recorded at the same time, so a semi-static object (parked bicycle, chair,
door) counts as clutter; a static surface the TLS did not capture counts as clutter, but only inside supported
columns. Both are acceptable for a lifelong mapper and are named in the report.

### 4.7 Measurement

* Windows: 10 and 20 steps (1.0 s and 2.0 s at 10 Hz). Resolutions 0.2 m (shipped) and 0.1 m. Miss definitions (section 5).
  A window closes after W steps; a last partial window is not closed.
* The probe is `p1b_probe spires-measure` (and `kth-measure3`), a lean re-implementation of `Voxel3DBackend::integrate`
  that records labelled voxels only. Reason: the engine map holds every free-space voxel (KITTI 00 at 0.1 m: 29 million
  cells for 141 frames); for 3007 scans that exceeds the memory of the host. The march arithmetic is the engine's
  (half-voxel steps, `voxelId`). The window rule is the engine's (a tick per step, a window per `layer_interval`
  ticks, hit wins over miss). **Equivalence is demonstrated before use**: on KITTI 00 with today's miss, for
  `r in {0.2, 0.1}` and `W in {10, 2}`, the lean probe must write cell tables byte-identical to the run B tables written by the
  real engine. If not, the lean probe is not used and branch F applies.
* Statistics: PREREG_P1 sections 4 and 11.3 on the classes `structure` against `clutter`, plus `structure_ground` against
  `clutter`, same code, same bootstrap (tiles of 10 m, joint resampling, seed 20261014, 2000 replicates).
* Shipped 3D condition: `(0.2 m, W = 10, N_min = 5)`. It decides section 6. Other conditions are reported.

## 5. Two miss definitions in 3D and the grazing diagnostic

* Today's miss (the engine): every sample of the half-voxel march before the endpoint is a miss on its voxel,
  including samples in the endpoint voxel and in its neighbours.
* Refined miss: a march sample is a miss only if its voxel is farther than one voxel (Chebyshev distance in voxel
  indices) from the endpoint voxel. In other words, no pass-through miss is counted within one voxel of the endpoint
  along the ray. The hit is unchanged. A voxel touched only by suppressed samples in a window is untouched in that window.
* Both are measured in the same pass on Keble 02 (4.7) and on KITTI 00 (secondary check, same calibration data, labels of
  phase 1, `r in {0.2, 0.1}`, `W in {10, 2}`). The refined miss exists only in the lean probe. It is a proposal for a
  backend change, not a property of the shipped engine.
* Grazing diagnostic, on `structure` voxels (non-ground), per condition: (a) among miss events (one per ray and voxel
  sample, repeated samples in one voxel counted once) the share whose ray endpoint voxel lies in the closed 26-neighbourhood of the
  missed voxel; (b) among (cell, window) pairs with a miss under today's definition, the share with at least one such
  event; (c) the total touched windows of the structure cells under today's and the refined definition.

## 6. Mechanical rule for the 3D pair

Branches, applied in order to the shipped 3D condition of Keble 02 (4.7), with gates G1, G2, the alignment check
(4.4), a valid offset search (4.5) and the lean probe equivalence (4.7) all passed:

* Branch T (today's miss): `gap_today = q_s - q_c >= 0.15`. Then `(m0, m1)` from today's miss and section 1.
* Branch R (refined miss): `gap_today < 0.15` and `gap_refined >= 0.15`. Then `(m0, m1)` from the refined miss and
  section 1, and the freeze file records "backend change: refined miss" and Option A.
* Branch F (fallback): otherwise, or if a gate or check failed. The 3D backend ships with `evidence_model = sprt`
  and the 2D pair of section 2, flagged "not calibrated for 3D; on single-pass driving data most structure remains
  Undecided". The limitation is a headline, not a footnote. The calibration evidence of 3D is still reported.
* KITTI 00 never sets a pair: its structure voxels have a median of 4 touched windows (reachability failure of
  PREREG_P1 section 7.3), and it is single-pass. It is reported as a secondary check and as the boundary case.
* The point estimate decides. If the lower end of the block interval of the gap is below 0.15 while the point estimate is
  above, the freeze file says so; the branch does not change.

For the 2D pair: if `gap(0.05 m, W = 15, N_min = 5) >= 0.15` and the gates pass, the pair is frozen from section 1. If
the gap is below 0.15, no pair is frozen and the lead decides (the pass-through miss is already today's 2D miss, and
Option C is not implemented, section 8).

## 7. The freeze

`PREREG.md` is written by `freeze_prereg.py` from the committed result tables and gate meta files. It lists, per
backend: `m0, m1, alpha, beta, A, B, l_hit, l_miss, ceiling, n_max, miss definition`, the branch taken, the numbers it came
from (with intervals), the gate verdicts, and the commit and time of the inputs. Mechanical content:

* `n_max_undecided`: the smallest multiple of 16 such that the probability of being undecided after `n` windows,
  for iid windows at `m* = -l_miss / (l_hit - l_miss)` (the zero-drift rate), is at most 0.03. The probability is computed
  exactly (dynamic programming over hits and windows) with the engine rule of section 8, by `derive_defaults.py`.
* A pair, its derived numbers and the OC values at 0.30 / 0.50 / `m1` come from `derive_defaults.py`.
* A backend without a frozen pair says "NOT FROZEN" and why.

## 8. Option C and the engine floor rule

* Option C (two-state Markov statistic) is not implemented in v2. The pre-registered trigger (PREREG_P1 section 7.4) fired in both
  calibration datasets with opposite signs (run B: LILocBench 0.05 m, W = 10, band [0.4, 0.6): AUC 0.656, walls have
  the longer runs; KITTI 00 0.2 m, W = 10, band [0.2, 0.4): AUC 0.338, movers have the longer runs). A test with one
  fixed sign helps one platform and hurts the other, and it costs the independence argument of the bound. The reversal is reported
  as measured. The lag-1 statistic and the matched-rate bands are computed again on W = 15 and on Keble 02, so the sign
  for walking data is known. Revisit only if a later phase shows an AUC gain of at least 0.05 at matched latency with a
  sign that agrees across the 3D sets. The reserved padding byte stays reserved.
* Floor rule for the engine (L-6): no lower clamp on `l` before the cell has `n >= T` touched windows (`T = 24`, the
  periodicity gate); once `n >= T`, reject when `l <= B`; before that the cell is only Undecided, even below `B`. The ceiling is
  `A + 2 l_hit`. A rejected cell is pruned and may be re-created by the next hit. The advisory replay says the clamp
  before the gate costs about one percentage point of false-Static (2.8 % against 3.5 %); the replay is committed and re-run, and its numbers are
  compared with the memo in the report, not trusted from the memo.

## 9. Outputs of this phase

`results_p1b/`: gate metas and report, W = 15 tables (`p1b_*.csv`), Keble 02 tables, the equivalence record, the
diagnostics, logs with host and load, `deviations.md`, `P1B_REPORT.md`. `PREREG.md` at the top of `paper/experiments/realdata/`.
No dataset file and no map derived from LILocBench, KITTI or Spires enters the repository (licences). Bulky files go to
`/mnt/d/wsl/strata/p1b/`.

## 10. Chronology and cost of the re-split

* The split was frozen on 2026-10-12 and DATA_MANIFEST recorded that no test-split sensor file had been analysed. Bytes of
  the test files were read for sha256 verification on 2026-10-12 to 2026-10-18; no content was decoded. Of Keble 02 the P0
  sizing note records the first GT line and the header and point count of the first scan; no statistic comes from them.
* The re-split is caused by a fact learned from calibration data (section 0).
* Cost: held-out 3D data shrinks to Keble 03, 04, 05 (three sequences of one day on one site) and the TLS map. Seasonal
  change stays untested (one day). The paper must say so.

## 11. Access discipline for this phase

Files opened for content: calibration data of phase 1; Keble 02 `lidar-clouds.zip` and `gt-tum.txt`; the Spires
`calibration/README.md`; the TLS map, read in full but kept only inside the Keble 02 area (4.2). After the phase,
`results_p1b/test_split_stat_after*.tsv` is written by the same script and compared with the before record; the
comparison is committed as `results_p1b/test_split_stat_compare.md`. Any difference in `mtime` or size of a file other than
the ones named here is an incident and is reported.

## 12. Not in this phase

Engine changes; test-split runs; Option C; Boreas; anything with Keble 03 to 05. The advisory replay scripts are
committed and re-run (they use calibration cell tables only) and compared with the memo at the end.

## Amendment 3 (2026-10-18, after the Opus review of phase 1b; committed before any re-run)

This amendment changes no definition, threshold or rule of Amendment 2 and re-runs no measurement. It corrects wording and records what the review found.

1. **Access evidence (replaces the last sentence of section 4.2 and the comparison wording of section 11).** The stat comparison before and after proves only that no test-split
   file was modified. Reading a file changes neither `mtime`, `ctime` nor size, and `atime` does not move on this 9p mount with `noatime`: the files that were certainly read in this
   phase (Keble 02 `lidar-clouds.zip`, `gt-tum.txt`, the TLS `merged-cloud-1cm.pcd`) have identical `atime` in the two records. So the `atime` part of the advice section 7
   acceptance check cannot be verified on this mount. The evidence that no other test-split file was opened is the step logs and the committed scripts; a search of all committed
   scripts, logs and exploratory logs finds test-split paths only in the stat scripts. For P3: every measurement step that touches a test-split file runs under
   `strace -f -e trace=openat,open -o <step>.strace` (or an equivalent file-open trace), the trace is committed next to the step log, and the paper's "only file X was opened"
   claim cites it.
2. **`n_max_undecided`.** Section 7 replaced the lead's fixed 64 (L-6) with a rule that gives 80. Exact undecided shares under the engine rule: P(undecided at 64) = 0.0314 for
   (0.15, 0.65) and 0.0388 for (0.20, 0.70); at 80, 0.0113 and 0.0150. The value that binds P2 is the lead's to record in `LEAD_DECISIONS_P1.md` (80 by the rule, or 64 with the
   0.039 share reported). Until then the rule's 80 is only the conditional value printed in `PREREG.md`.
3. **Time base of `dynamics_0`.** Advice section 4 required the time base to be re-examined before stopping on a LIL gate failure. Phase 1b did not do it. The phase 1 offset tables show
   a per-laser disagreement on `dynamics_0` only (front optimum -0.13 s, rear optimum -0.06 s; `mapping` and `static_0`: both lasers -0.13 s); run B used -0.13 s for all sequences.
   Report section 13.1 has the table and the reviewer's post hoc numbers. No rule is changed here. The lead decides whether a per-laser offset rule is adopted, as a later
   dated amendment, committed before the corresponding re-run, and before any gate is waived for `dynamics_0`.
4. **G1 as a measurement.** The reviewer's post hoc check shows that sqrt(12 lambda_min) in an accepted patch is not robust to off-line points (second surfaces inside "linear" patches),
   so the G1 failures of 0.17 to 0.19 m do not measure pose smear. G1 and G2 stay exactly as pre-registered and their verdicts stand (2D and 3D not frozen). Replacing G1 would be a
   second gate change after seeing data; it is the lead's decision and, if taken, an amendment reported with this chronology. A robust-width variant at the 0.10 m limit is only
   post hoc evidence and is not to be presented as pre-registered.

## Amendment 4 (2026-10-18, after the second Opus review of phase 1b; committed before any re-run)

This amendment changes no definition, threshold or rule of Amendments 2 and 3 and re-runs no measurement (the only run is a byte-identity check of existing Keble 02 tables, `deviations.md` item 24).

1. **Support rule of the clutter class (section 4.6) is weaker than its stated purpose.** It asks for one TLS point anywhere in the 1 m column; it does not exclude heights the TLS never reached. On Keble 02 (0.2 m, W = 10, today's miss)
   64.3 % of the clutter cells with N >= 5 lie more than 0.5 m above the device (`explore_keble_clutter_height.py`), so the class is mostly not walking people. The Keble gaps must not be cited as evidence that hit rates do not
   separate in 3D. No rule is changed here. A corrected definition (vertical support or a height band) is a further dated amendment, decided by the lead before any 3D re-run. The reason line of `PREREG.md` carries the qualifier.
2. **Provenance rule for P3 and later.** Every step log records `git rev-parse HEAD` and the `git status --porcelain` line count; binaries are built only from a clean committed tree. The chronology of this phase is corrected in
   `deviations.md` items 4, 5 and 24: two exploratory/measurement steps ran on an uncommitted `--labels` option; no number depends on it.
3. **Reviewer's post hoc numbers are not part of the record.** Numbers from uncommitted reviewer code (Amendment 3 items 3 and 4, report sections 13.1 and 13.3) are named in words only and never reach the paper.


## Amendment 5 (2026-10-18, after the third Opus review of phase 1b; committed before the byte-identity re-runs and before the regeneration of PREREG.md and the report)

This amendment changes no definition, threshold or rule of Amendments 2 to 4 and no number. It completes the chronology and widens the provenance rule.

1. **Pair at both interval ends (section 1) is now printed.** The rule of section 1 existed but no generator applied it. `pair_ends.py` computes, from the block-bootstrap interval ends, the pair
   (`ceil_0.05` of the `q_c` end, `floor_0.05` of the `q_s` end) at the low and at the high end. `freeze_prereg.py` prints it under the conditional table and under the freeze table whenever an
   interval crosses a 0.05 boundary, and `make_report_p1b.py` adds the column `pair at interval ends (low / high)` to every gap table. The frozen pair and the conditional pair do not change; they stay the
   pair of the point estimates. For the shipped 2D row the ends are (0.20, 0.65) and (0.30, 0.75); the lead may waive a gate and take (0.20, 0.70) only with this range in view.
2. **Clean-tree rule (Amendment 4 item 2) covers the whole repository.** `p1b_probe` and `hitrate_probe` link `strata_core`, which `paper/experiments/CMakeLists.txt` builds from `../../strata_core`.
   `reproduce_p1b.sh` now counts uncommitted files with `git status --porcelain -- ':(top)'` minus the two result directories, in the `build` stage, in the `TREE_STATUS_PORCELAIN_LINES` line of every
   step log and in `run_manifest.txt`. `test_reproduce_tree_check.sh` fails against the earlier check.
3. **Chronology (replaces the count in Amendment 4 item 2).** The sentence "two exploratory/measurement steps ran on an uncommitted `--labels` option" was incomplete. `deviations.md` item 24 lists every
   exploratory output that was written before its script or option was committed.
4. **Provenance rule extended to scripts.** An exploratory or measurement script (`explore_*.sh`, `explore_*.py`, `reproduce_*.sh` stages, analysis scripts) runs only from committed code: from a clean
   committed tree or from a `git archive` copy of a commit, and the output records that commit. An exploratory output whose script is not yet committed is not kept; it is re-run after the commit.
