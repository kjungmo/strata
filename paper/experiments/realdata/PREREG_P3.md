# PREREG_P3: pre-registration of the phase 3 test evaluation

Status: written and committed before any test evaluation. The commit time of this file, the access log `results_p3/access_log.tsv.gz` (gzip; `zcat` reads it)
and the step logs in `results_p3/logs/` show the order. A change to anything below after the first test evaluation is an amendment
(dated, committed before the run it affects, listed in `results_p3/deviations.md`).

Binding documents: `LEAD_DECISIONS_P1.md` (L-1 to L-10, L-7 and L-8 in particular), `P1_DECISION_ADVICE.md` section 5 and 7,
`DESIGN_AND_PLAN.md` section 4, `PREREG_P1.md` with Amendments 1 to 2, `PREREG_P1B.md` with Amendments 2 to 5, `PREREG.md` (not frozen),
`results_p1/deviations.md`, `results_p1b/deviations.md`, the implementer checklist (items 1 to 13) and the submission audit rails R1 to R6.
Where this file and the lead's decisions differ, the lead's decisions win.

## 0. What this phase is, and what has been done before this commit

Phase 3 runs the five evidence rules (arms) on the TEST split and reports what they do. Phase 3 does not tune anything: no arm
parameter, label rule, window, resolution or metric changes after the first evaluation run. The outcome wording belongs to the paper
phase (L-10). The guarantee sentence of the paper is "under independent windows", and every realised rate stands beside its nominal
bound (L-7).

Done before this commit (the harness task; nothing was evaluated):

1. `results_p3/test_split_stat_before*.tsv`: `stat` of every test-split file, committed before anything else (349ce30).
2. Harness code with unit tests and a mutation runner (`p3_*.hpp`, `p3_probe.cpp`, `p3_eval.py`, `p3_runlog.py`, `reproduce_p3.sh`,
   `test_p3*`, `mutation_p3.py`); the mutation record is committed as `results_p3/mutation_record.md` and may follow this file (tests only, no change of an arm).
3. Reader validation on the first 10 records of one test file per reader, under file-open traces (section 10.2). These are the only
   test-split files opened so far, besides the pose files of the three Boreas sessions that the overlap gate reads (section 8.2).
4. Smoke tests of the whole pipeline on CALIBRATION data (KITTI 00, LILocBench `dynamics_0`). They are in-sample, they are not results,
   and no number of them goes into a paper table. Their purpose was to find defects in the harness (section 11).

## 1. What each dataset is asked

| Dataset | Question | Primary quantity |
|---|---|---|
| LILocBench `lt_changes_dynamics_0` | Does the shipped error rate hold on held-out people? | false-Static rate on reference-free cells hit by people (and moved objects), per arm |
| LILocBench `lt_changes_0` | What does each arm do with rearranged furniture, without people? | counts of reference-free cells that become Static and of reference-occupied cells that are not Static; false-reject on wall runs |
| Spires Keble 03 to 05 + TLS | Held-out 3D error against an independent structure reference | false-Static on TLS-absent hits, false-reject on TLS structure, agreement of the Static sets of the three sequences |
| KTH KITTI 05, semi-indoor | The boundary result: single-pass data, comparable removal rows | SA, DA, AA, HA under two export policies |
| Boreas, 3 sessions | Does a cell stay Static from one season to the next? | Jaccard of the Static sets in shared tiles; Static cells that need more than one session |

## 2. Arms

### 2.1 The five arms

Source of truth: `p3_arms.hpp`; the lines below are the output of `p3_probe arms` and a test (`P3Prereg.ArmsAreDocumented`) fails if this block and
the code differ.

```text arms
shipped_v0_1: legacy l_hit=0.85 l_miss=-0.4 l_min=-5 l_max=5 decay=0.97 graduate_prob=0.8 demote_prob=0.45 min_observations=3 prune_prob=0.05 binary=0
symmetric: legacy l_hit=0.85 l_miss=-0.85 l_min=-5 l_max=5 decay=0.97 graduate_prob=0.8 demote_prob=0.45 min_observations=3 prune_prob=0.05 binary=0
octomap: legacy l_hit=0.847298 l_miss=-0.405465 l_min=-1.99243 l_max=3.4761 decay=1 graduate_prob=0.5 demote_prob=0.5 min_observations=1 prune_prob=0 binary=1
elite_like: legacy l_hit=2.19722 l_miss=-2.19722 l_min=-50 l_max=50 decay=1 graduate_prob=0.5 demote_prob=0.5 min_observations=1 prune_prob=0 binary=1
sprt: sprt m0=0.2 m1=0.7 alpha=0.01 beta=0.01 n_max_undecided=80 binary=0
sprt_low_end: sprt m0=0.2 m1=0.65 alpha=0.01 beta=0.01 n_max_undecided=80 binary=0
sprt_high_end: sprt m0=0.3 m1=0.75 alpha=0.01 beta=0.01 n_max_undecided=80 binary=0
```

| Arm | Meaning | Where the numbers come from |
|---|---|---|
| `shipped_v0_1` | the v0.1 shipped rule, unchanged (`evidence_model = legacy`) | `LayeredMapParams` defaults of v0.1 |
| `symmetric` | `shipped_v0_1` with `l_miss = -l_hit`; nothing else changes | DESIGN_AND_PLAN section 4 rule 4 |
| `octomap` (table label: OctoMap parameters on window evidence; Amendment 6) | OctoMap parameters, one increment per touched window: `p_hit 0.7`, `p_miss 0.4`, clamps `0.12` and `0.97`, occupied iff `l > 0` (graduate at `0.5 + 1e-12`, demote at `0.5`), no leak, never pruned | Hornung et al. 2013 defaults |
| `elite_like` (table label: ELite factors on window evidence; Amendment 6) | on-ray odds factors 9 and 1/9, one increment per touched window (`l = +-ln 9`), no leak, occupied iff `l > 0`; clamp `+-50` (the paper states none that we found) | ELite Eq. 5 and 6 at zero distance (`sources_verified.md` (6)) |
| `sprt` | Wald test on the per-window hit rate; increments and thresholds derived by the engine from `(m0, m1, alpha, beta)`; Intermittent read-out after `n_max_undecided` windows; floor rule and reverse-test demotion of L-6 | `PREREG.json` conditional pair of the 2D backend |

`sprt_low_end` and `sprt_high_end` are extra arms, not part of the five. They run the interval ends of the conditional pair (`PREREG.md`: "pair at the
low interval end (0.20, 0.65), at the high end (0.30, 0.75)"). They are reported next to `sprt` and change nothing about it.

Derived constants of `sprt` (engine, `deriveSprt`): `l_hit = 1.2528`, `l_miss = -0.9808`, `A = -B = 4.5951`, ceiling `7.1006`, an always-hit cell
graduates in 4 windows, the zero-drift rate `m* = 0.4391`, floor gate `T = 24` touched windows.

### 2.2 Settings shared by all arms

* One engine for all arms: `strata_core` at the commit recorded in each step log (`BINARY_COMMIT`); version 0.2.0 or later; the build is a `git archive` copy of that commit.
* The Periodic veto (`enable_periodicity`) is the same for all arms of one run. **Primary: on** (the node ships it). **Secondary: off**, to isolate the
  evidence rule. Both are run and reported; no table mixes them. The rejection gate `T = 24` of the `sprt` arm is `periodicity.period_windows` and does not depend on the veto switch.
* Window length `W` (integration steps per window), resolution and conditions are fixed per dataset in sections 5 to 8.
* Cells with fewer than `N_min = 5` touched windows are not eligible for any rate (the rule of the calibration, PREREG_P1 section 4). Eligibility counts are always printed.
* A last partial window is never closed (engine rule, as in the calibration).

### 2.3 Status of the `sprt` pair

The 2D pair is NOT FROZEN (`PREREG.md`: G1 fails on all three LILocBench sequences) and the 3D pair is NOT FROZEN (branch F). The `sprt` arm therefore runs the
point pair `(0.20, 0.70, 0.01, 0.01)` of `PREREG.json` for both backends. For 3D this pair is "not calibrated for 3D" (SPEC 3.2a). If the lead freezes
another pair before the first run, the arm table is changed in a dated amendment committed before that run. The three `sprt*` rows then show how much the answer depends on the pair.

### 2.4 The MTD-Map-like arm is not built

Reason (P1_DECISION_ADVICE section 5): no released code was found; the paper keeps three hand thresholds `p`, `pi`, `beta` tied to 0.1 m voxels. A reimplementation
from the paper alone would fix choices the paper does not fix, and a table row would claim more than we know. It is reported as not run.

### 2.5 Binary arms

`octomap` and `elite_like` have no undecided state. For them "not Static" is "Rejected" (flag `binary = 1`). The other arms read Rejected as "the engine erased the cell".

## 3. Verdicts, export policies and metrics

### 3.1 Verdict of a cell at the end of a run (`verdictOf`)

* **Static**: `LayeredMap::isStatic`.
* **Rejected**: the engine holds no cell (`classify == Unknown`): it was erased after rejection or pruning, or it was never held; for binary arms: every cell that is not Static.
* **Undecided**: every other cell that exists (Transient, Periodic, Intermittent).

A rejected `sprt` cell is erased; a later ray through its voxel creates a new history, which is read as Undecided. So a voxel that was Rejected at some time can be Undecided
at the end. The meta file of every KTH export lists the verdict of every exported point (`points_voxel_rejected`, `_undecided`, `_static`).

### 3.2 Export policies for the KTH benchmark

* **(i) `static_only`**: a point is kept iff the verdict of its voxel is Static. This is the lifelong reading; SA is expected to collapse on single-pass data.
* **(ii) `not_rejected`**: a point is kept iff the verdict of its voxel is not Rejected. This is the removal reading. It is not comparable to published OctoMap or ELite rows (Amendment 6: every arm updates once per touched window).
* For binary arms (i) and (ii) are identical.

### 3.3 Metrics

* False-Static rate on clutter = (clutter cells with verdict Static) / (eligible clutter cells). False-reject rate on structure = (structure cells with verdict Rejected) / (eligible structure cells).
  "Not Static" shares and Undecided shares are reported next to them.
* Windows to decision = `first_static - first_touch + 1` over cells that were Static at some window close (first window at which the engine reports the cell Static).
* Intervals: for LILocBench, Wilson 95 % (cells treated as independent) and a 95 % percentile interval of a block bootstrap over 10 m tiles (2000 replicates, seed 20261014; neighbouring cells are not independent; read the block interval).
  For Spires only Wilson (the 3D counts table has no cell coordinates). KTH and Boreas runs are single deterministic passes and carry no interval.
* SA, DA, AA, HA exactly as `evaluate_all.py` of the KTH benchmark (section 5.3). AA is the geometric mean, HA the harmonic mean; columns are labelled; no table mixes them.
* The nominal bound is printed beside every realised false-Static rate: `alpha = 0.01` per history for clutter at `m0`, under independent windows (L-7). Realised rates are not expected to equal it; the
  gap is the result. The autocorrelation inflation (lag-1 0.2 to 0.5 on real windows; Monte Carlo about 13 times at exactly `m0`) is reported with it.

## 4. Order, rules and what is reported

Order (P1_DECISION_ADVICE section 7): (1) `lt_changes_dynamics_0`, (2) `lt_changes_0`, (3) Spires Keble 03, 04, 05, (4) KTH 05 and semi-indoor, (5) Boreas.

* One process at a time, `nice -n 19` (`reproduce_p3.sh`), binaries from a `git archive` copy of a committed tree, `-j4` for builds.
* Every step is logged with host, load before and after, the top CPU users, commit, the command, return code, and the size and sha256 of its outputs (`results_p3/runs/<step>.json`);
  test inputs are listed with sizes and sha256 in `data_inputs_p3.sha256` (from the download verification) and checked at the `check` stage; inputs without a recorded sha256 are hashed by the run record.
* Every step that touches a test-split file runs under `strace -f -e trace=openat,open`; the trace is committed (`results_p3/access/`), and `results_p3/access_log.tsv.gz` (gzip; `zcat` reads it) lists every data file each step opened.
* All arms, all conditions, all runs are reported, including failed ones (with the reason) and the ones with the Periodic veto off. A run is repeated only after a crash, with an identical configuration, and the
  repetition is listed. No number is chosen among runs.
* No timing is measured in this phase (R6); the host load is recorded and only stated.
* Nothing derived from LIL, KITTI, KTH, Spires or Boreas enters the repository: JSON aggregates and tables only. Bulky outputs stay in `/mnt/d/wsl/strata/p3/`.
* The evaluation stages of `reproduce_p3.sh` refuse to run unless `P3_RUN_EVALUATIONS=1` is set, this file and the arm table are committed and unmodified, the harness files are clean and the binary was built from HEAD.

## 5. KTH DynamicMap benchmark: KITTI 05 and semi-indoor

### 5.1 Data and runs

* KITTI 05: 321 frames (`002350` to `002670`), 10 Hz, 64 beams, poses of SuMa (SemanticKITTI), not ground truth. Semi-indoor: 960 frames, VLP-16, poses of an NDT SLAM run. Frames are fed unfiltered, one tick per frame, sensor origin from `VIEWPOINT`.
* Conditions: KITTI 05 at 0.2 m with `W = 10` (32 windows) and `W = 2` (160 windows). Semi-indoor at 0.2 m with `W = 10` (96 windows) and `W = 2` (480), and at 0.1 m with `W = 10`. KITTI 05 at 0.1 m is not run (memory: the engine map holds every free-space voxel; KITTI 00 at 0.1 m has 29 million cells for 141 frames, and KITTI 05 has 321 frames).
* 5 arms x 2 policies x conditions x Periodic veto on and off. The `sprt` pair is the 2D pair (section 2.3); this is stated beside the table.
* A sentence stands beside every KTH table: single pass, number of windows, voxel size, pose source, 16-beam validity for semi-indoor, AA and HA labelled, and (Amendment 6) the words "every arm applies its increment once per touched window, so the rows labelled OctoMap parameters on window evidence and ELite factors on window evidence are not the published OctoMap and ELite methods".

### 5.2 Export

For each arm and policy the exporter writes the ORIGINAL points of the frames whose voxel verdict is kept, as `x y z` float32 (`kth-export`). Voxel centres would fail the 0.05 m match.
**Every point of every frame is exported; there is no 50 m cut.** The advice and the lead's task text say "inside 50 m". The harness validation showed that the ground-truth cloud of KITTI 00 holds
every frame point (sum of the 141 frame `POINTS` = `POINTS` of `gt_cloud.pcd` = 17,362,230, and the first 125,883 GT points are frame 0 in order); the 50 m limit of the benchmark README is not a cut of the points. The
option `--max-range` stays available and is not used. The same check on the test sequences is part of the run (the `all` policy, see 5.3, would reveal a difference by SA < 100).

### 5.3 Evaluator (`p3_eval.py kth`)

A ground-truth point is predicted static (kept) if some point of the method map lies within 0.05 m (`<=`, exact, float64); otherwise predicted dynamic (removed). With
`gt` the ground-truth flag (`intensity = 1` dynamic): `SA = kept static / static`, `DA = removed dynamic / dynamic`, `AA = sqrt(SA * DA)`, `HA = 2 SA DA / (SA + DA)`, in percent. This replaces
`export_eval_pcd` + `evaluate_all.py` (C++ with PCL) by the same rule; the replacement was validated (section 5.4).

### 5.4 Validation of the exporter and the evaluator (done on CALIBRATION data, KITTI 00; `smoke_kth00_*`, `results_p3/smoke_kth00_selfcheck`)

1. Remove nothing (every frame point exported): 17,362,230 points, SA = 100, DA = 0 exactly.
2. The GT static points as the method map: SA = 100, and DA = 98.8185 %. **DA = 100 is not reachable on KITTI 00 by any map**: 1,134 of the 95,983 dynamic points have a static point within 5 cm
   (ground contact points of moving objects). The count 1,134 was obtained by a second method that shares no code with the evaluator (sorted window search) and equals the evaluator's number; the ceiling is printed in every KTH table
   (`kth-selfcheck` runs on each test ground-truth cloud as part of the run, reading no arm output).
3. Unit tests: random clouds against brute force, the inclusive boundary, negative coordinates, many points in one cell, hand-computed SA/DA/AA/HA.

### 5.5 What it can and cannot show

Can show: the boundary result of the occupancy-rate family on single-pass data under two stated export policies, with the share of points in each verdict. Cannot show: a competitive removal score
(declared beforehand), anything about revisits, seasonal change. A 3-point SA/DA swing between pose sources is documented in the literature and is not tested here.

## 6. LILocBench `lt_changes_dynamics_0` and `lt_changes_0`

### 6.1 Conversion and reference

* Conversion exactly as run B of phase 1: `hitrate_probe lil-convert --deskew 1 --anchor start --time-offset -0.13`, front and rear laser as one step (15 Hz), GT poses of the sequence, `/tf` extrinsics from the bag.
  The offset -0.13 s is a property of the recording system and is applied to the lt_changes sequences without a new search. Amendment 3 item 3 of `PREREG_P1B.md` records that on `dynamics_0` the rear laser's own optimum was -0.06 s.
  The scan-to-GT search (`lil-offset`) is run on both lt_changes bags as a **diagnostic only**; it selects nothing and its result is reported. The pair-skew bound 0.05 s is reported for each sequence.
* Reference: the occupancy labels of the `mapping` session at 0.05 m, run B (`p1/lil/ref_r0.05.bin`): `ref_occupied` (n >= 10 and hit share >= 0.5), `ref_free` (n >= 10 and hit share <= 0.05), `ref_free_near_occupied` (a free cell within 0.10 m of an occupied cell; set aside).
* Window `W = 15` steps (1.0 s at 15 Hz), resolution 0.05 m, grid 60 m x 60 m at (-30, -30), `N_min = 5`. Periodic veto on (primary) and off (secondary).

### 6.2 Metrics

* **Clutter** = `ref_free` cells with `H >= 1` hit windows and `N >= 5` (`hit_in_free` of the calibration; near-wall cells are excluded by their label). The primary quantity is the **false-Static rate on clutter**, per arm.
  It is also reported by hit-rate band of the cell (`[0, 0.15)`, `[0.15, 0.30)`, `[0.30, 0.65)`, `[0.65, 1]`).
* **Wall-run cells** = `ref_occupied` cells whose neighbourhood patch (all `ref_occupied` cells within 1.0 m) has at least 20 cells, minor/major eigenvalue ratio <= 0.02 and extent `sqrt(12 * major eigenvalue) >= 1.0 m` (the shape rule of G1
  applied to cell centres; corners and short furniture drop out). Restricting the structure truth to wall runs keeps moved furniture from counting as false rejects. The **false-reject rate on wall runs** is the share of eligible wall-run cells with verdict Rejected;
  the share not Static and the windows to decision are printed with it.
* `lt_changes_0` (no people) additionally reports counts: `ref_free` cells that become Static, `ref_occupied` cells that are not Static or Rejected. These are counts and a description of the change; there is no per-cell truth.

### 6.3 What it can and cannot show

Can show: held-out false-Static on real people at the shipped window (`lt_changes_dynamics_0`), windows to decision on walls, and what each arm does with rearranged furniture. Cannot show: per-object truth. **Confounder, stated now:**
`lt_changes_dynamics_0` contains people and rearranged objects. Moved furniture puts new structure into cells that were free in `mapping`; such cells are legitimately Static and would be counted as false-Static. The rate is therefore an upper bound for
people alone. `lt_changes_0` (same kind of change, no people) gives the counts of this effect without people, as a control, not as a correction; the two sessions need not have the same arrangement.

## 7. Oxford Spires Keble 03, 04, 05 and the TLS map

### 7.1 Conversion and labels

* Per sequence `S` in {03, 04, 05}: TLS voxel tables inside 30 m of the track of `S` (`p1b_probe spires-tls`, the TLS file is read sequentially and every point outside the area is discarded at read time); scans from `raw/lidar-clouds.zip` converted with the extrinsic, per-point deskew, range window 1.5 m to 20 m and
  the time offset of the Keble 02 search (`selected_dt_s = 0`, `results_p1b/meta/spires_offset.meta`); labels at 0.2 m and 0.1 m exactly as `PREREG_P1B.md` section 4.6 (structure = TLS-occupied voxel with at least 3 points, ground rule 0.30 m, clutter = hit voxel with no TLS point in its 26-neighbourhood and a TLS-supported column, a one-voxel buffer set aside; thinning at 2,000,000 voxels per class).
* Windows 10 and 20 steps (1.0 s and 2.0 s at 10 Hz), resolutions 0.2 m (shipped) and 0.1 m, `N_min = 5`.
* The `sprt` arm uses the 2D pair, "not calibrated for 3D".

### 7.2 Metrics

* False-Static rate on clutter (label 1); false-reject rate, undecided share and windows to decision on non-ground structure (label 3) and, separately, on ground (label 2).
* Agreement of the Static sets of the three sequences: Jaccard of the Static voxel sets (independent maps, 0.2 m, `W = 10`) inside the 10 m tiles that all compared sequences observed. All three sequences were recorded on one day in one TLS frame.

### 7.3 What it can and cannot show, and a caveat that is not resolved

Can show: the only held-out 3D error against an independent reference (L-3), and same-day agreement. Cannot show: seasonal change (one day), doors. TLS and the walks were not recorded together, so a semi-static object counts as clutter.
**Open (Amendment 4 item 1 of `PREREG_P1B.md`):** the support rule of the clutter class is weak; on Keble 02 64.3 % of the clutter cells with `N >= 5` lie more than 0.5 m above the device, so the class is mostly not walking people. The Keble 03 to 05 clutter rates are
reported as defined above, with this caveat, and are not to be cited as evidence about people. A corrected definition (vertical support or a height band) is the lead's decision and requires a dated amendment before the Spires run (section 12, item 2).

## 8. Boreas, three sessions

### 8.1 Data and conversion

* Sessions `boreas-2020-11-26-13-58` (late autumn), `boreas-2021-04-08-12-44` (spring), `boreas-2021-06-17-17-52` (summer); a three-minute lidar slice each (1736 frames). No labels exist: no false-Static truth.
* Lidar `.bin`: six float32 per point (`x y z intensity ring t`), `t` seconds relative to the frame time (about +-0.052 s), confirmed on the first frames by bytes/24, ranges, ring 0 to 127 and time span.
* Pose: the post-processed GPS at 200 Hz (`gps_post_process.csv`) interpolated at the time of each point (deskew); the lidar pose follows the convention of `lidar_poses.csv` (yaw shift on the world side; agreement on the first 10 frames: 0.0003 degrees and 3 mm).
  The physical composition would differ by up to a degree at road tilt; the dataset's own convention is used so that maps lie in the frame everybody evaluates against. The validator checks the agreement on every session it reads.
* Range window 3 m to 50 m (lidar frame). One frame is one step; `W = 10`, resolution 0.2 m, `N_min = 5`.
* Common frame: the ENU frame of the first session (reference), built from the latitude and longitude of each session's origin (WGS84, exact); sessions' ENU origins coincide with the first GPS row. The ENU of the files and the geodetic ENU agree to 5 cm (worst of 387 GPS rows up to 1.7 km from the origin).
* Vertical level: the files carry no absolute height, so sessions can differ in z. The ground height per 10 m tile (5th percentile of z within 25 m of the sensor) is compared between sessions and the median difference over shared tiles is applied as `z_offset`
  (`tile-ground`, `p3_eval.py zoffset`), identical for all arms. The tables, the offsets and the tile counts are reported.

### 8.2 Gate before any Boreas run (already run: pose files only, no lidar content)

The 10 m tile overlap of the three trajectories in the common frame (`boreas-overlap`). Rule: pass if every ordered pair has at least 0.8 of its tiles within one tile of a tile of the other session. Result on file (`results_p3/logs/boreas_overlap.log`): **PASS**; the exact-tile shares are
0.67 to 0.93 and the near shares 0.89 to 1.00. The sessions start at nearly the same place, so the raw-local rows are similar; the check shows the slices follow one route, not that the common frame is needed to the metre.

### 8.3 Metrics

Per arm: Static and observed voxel counts per session (independent maps); Jaccard of Static sets in shared tiles for each pair; cumulative map (one map, three sessions in turn): Static count after each session and the share of final Static cells that are Static in no independent session
(`p3_eval.py jaccard` and `newstatic`). Windows per cell is not recorded for 3D; windows per session are reported instead.

### 8.4 What it can and cannot show

Can show: cross-session agreement of the Static label across months (snow and no snow), and cells that graduate only after a second session. Cannot show: any false-Static or false-reject truth. Engineering risks stated: sessions differ in vehicle speed and parked cars; the vertical offset is estimated, not measured.

## 9. What each dataset can and cannot show (summary)

| Dataset | Can show | Cannot show |
|---|---|---|
| `lt_changes_dynamics_0` | held-out false-Static on real people and moved objects; windows to decision on walls | per-object truth; people alone |
| `lt_changes_0` | semi-static change, qualitatively (counts) | per-cell change truth |
| Spires 03 to 05 | held-out 3D false-Static and false-reject against TLS; same-day agreement | seasons, doors, people (clutter class caveat) |
| KTH 05, semi-indoor | boundary result on single-pass data, two export policies | a competitive removal score, revisits |
| Boreas | cross-session Jaccard, cells that need a second session | any false-Static truth |

## 10. Provenance and access discipline

1. Access evidence is the file-open trace per step (`results_p3/access/*.strace`) and the access log; the `stat` comparison before and after (`test_split_stat_*`) shows only that no test-split file was modified (the mount has no `atime`).
2. Test-split files opened before this commit, with the step that opened them: Keble 03 `lidar-clouds.zip` (central directory and the first 10 scans) and `gt-tum.txt`; Boreas `2020-11-26-13-58`: text of the pose files and the first 10 `.bin`; KITTI 05 and semi-indoor: the first 10 frames; `lt_changes_0` and `lt_changes_dynamics_0`: the first messages of the bag until
   10 scan pairs and the GT file; the Boreas pose files and listings of the three sessions (overlap gate). The log names are `validate_*`, `boreas_peek*`, `boreas_overlap`.
3. The first Keble 03 and Boreas validations were repeated after validator fixes (a missing pose before the first GT pose was treated as a reader failure; the lidar rotation convention was found against `lidar_poses.csv`); the access log keeps every attempt.
4. Binaries are built from a `git archive` copy of a clean committed state (`reproduce_p3.sh build`); a traced step refuses to run if a harness file is uncommitted.

## 11. Findings of the harness validation that changed the plan (before this commit)

1. **No 50 m cut in KTH exports** (section 5.2).
2. **DA = 100 is unreachable on real KTH data**; the validation case "GT static points as the map" is checked against an independently counted ceiling (section 5.4).
3. **Boreas rotation convention**: the file `T_applanix_lidar.txt` cannot be composed as written; `lidar_poses.csv` is reproduced to 0.0003 degrees with `R_lidar = R_file^-1 * R_applanix` (section 8.1).
4. **Windows to decision** were first computed from the start of the sequence; they are now counted from the first touch of the cell (a defect found in the smoke test).
5. **Engine behaviour that the verdict must respect**: a rejected SPRT cell is erased and a later ray re-creates it (section 3.1).
6. A transient read error (`read error` on one KITTI 00 frame on the `/mnt/d` mount under load) stopped one smoke step; the step was repeated with the identical command (it is listed in `deviations.md`).

## 12. Open items for the lead (decisions needed before the first evaluation run)

1. **Freeze or waive the pair.** `sprt` runs the point pair (0.20, 0.70) of `PREREG.json`; the interval-end pairs are extra arms. A different freeze means an amendment of section 2.
2. **Spires clutter definition** (section 7.3): keep as defined (reported with the caveat) or amend with a vertical band before the run.
3. **Per-laser time offset on `dynamics_0`** (Amendment 3 item 3 of `PREREG_P1B.md`): the lt_changes conversion uses -0.13 s for both lasers; the diagnostic search is reported, not used.
4. **Vertical offset between Boreas sessions** is estimated from tile ground heights (section 8.1); confirm or replace.
5. **Periodic veto**: primary on, secondary off (section 2.2); confirm.
6. **MTD-Map-like arm**: not built (section 2.4).

## 13. Not in this phase

Changing any arm, label, window or metric after seeing a result; Option C; Periodic on real data (no logged schedule); timing; the paper text; companions; a push or a pull request.

## 14. Amendment 1 (2026-10-18, before any evaluation): Boreas configuration and measured cost

Written and committed before any test evaluation, after a feasibility probe on calibration data and on data that was already opened. It replaces section 8.1 where it says so and changes no arm, no label, no window of another dataset and no metric.

**Probe** (`p3_feas_boreas.sh`, committed before it ran; engine `sprt` arm, the first 10 frames of `boreas-2020-11-26-13-58`, which were opened for reader validation, `W = 10`):

| range window | voxel | Periodic veto | cells after 10 frames | peak memory | time |
|---|---|---|---|---|---|
| 3 to 50 m | 0.2 m | off | 2,582,017 | 221 MB | 26 s |
| 3 to 50 m | 0.2 m | on | 2,582,017 | 1,212 MB | 29 s |
| 3 to 25 m | 0.2 m | off | 491,113 | 45 MB | 10 s |
| 3 to 25 m | 0.2 m | on | 491,113 | 234 MB | 7 s |
| 3 to 25 m | 0.4 m | off | 72,613 | 15 MB | 1.4 s |
| 3 to 15 m | 0.2 m | off | 143,421 | 17 MB | 1.2 s |

The vehicle covers about 1.7 km in a slice, which is about 34 sensor footprints of a strip 50 m wide at 25 m range. At 3 to 50 m that is of the order of 90 million cells (about 8 GB with the veto off, about 40 GB with it on). That is not feasible on this host
(11.9 GB, shared). At 3 to 25 m, 0.2 m and veto off it is of the order of 17 million cells (about 1.5 GB) and about 1 s per frame, 30 minutes for one session.

**Changes to section 8.1.** The range window of Boreas is **3 m to 25 m** (not 50 m). The **Periodic veto is off** for all Boreas runs (memory: the veto costs about five times the memory per cell). All 1736 frames are used, `W = 10`, 0.2 m.
`boreas-convert --frame-step N` exists to halve the cost; using it needs an amendment before the first Boreas run (with `W` scaled to keep one second). Estimated cost: one single-session run 30 minutes; the cumulative run over three sessions 90 minutes; per arm about 3 hours; seven arms about 20 hours.

**Measured cost of the other paths (for planning; calibration data only).**

* Spires, Keble 02 cache (3005 scans, 164 million points, 0.2 m, `W = 10`), one arm run: `shipped_v0_1` with the veto off took 5.2 minutes and 261 MB; `sprt` with the veto on took 6.2 minutes and 1.5 GB. Keble 04 and 05 have about twice as many scans.
  The counts table of that run holds 195,641 eligible structure cells and 15,864 eligible clutter cells, equal to the numbers in `PREREG.md`: the evaluation path reproduces the calibration counts.
* KTH KITTI 00 (141 frames, 0.2 m, `W = 10`, veto on): about 2 minutes per export with 4.6 million cells. KITTI 05 has 321 frames; the KTH export steps run with a 9 GB address-space limit instead of 6 GB.
* LILocBench `dynamics_0`: seconds per arm run.

If a run exceeds memory or time it is reported as failed with its resource limit. The configuration is not changed after a result is seen.

## 15. Amendment 2 (2026-10-18, before any evaluation): how inputs and outputs are recorded

Section 4 said every step record lists the size and sha256 of its outputs. The step records (`results_p3/runs/<step>.json`) carry host, load, commit, command, return code and time; inputs and outputs are recorded at stage level instead, which is where the files belong together:

* `results_p3/outputs_<stage>.sha256` (`eval_lil`, `eval_spires`, `eval_kth`, `eval_boreas`): sha256 of every output file up to 1 GB under the stage's output directories and result JSONs; larger files (the Boreas caches) by size and path.
* `results_p3/inputs_check_<stage>.txt`: the size check of every listed test input (`data_inputs_p3.sha256`), and the sha256 check when `P3_HASH_INPUTS=1` is set (about 60 GB read for the full list; not done by default).
* Inputs without a recorded checksum are hashed by the step that first reads them (`--hash`): the GT poses of the two lt_changes sequences and the ground-truth clouds of KITTI 05 and semi-indoor.
* A step that fails keeps its log and trace as `*.attempt-<time>.*` when it is repeated.

The KTH stage also exports the policy `all` once per condition (with the first arm) as the identity check of section 5.2.

## 16. Amendment 3 (2026-10-18, before any evaluation): review findings on the harness

Written and committed before any test evaluation. No arm, label, window, resolution or metric of another dataset changes. It resolves the six findings of the Opus review of the harness. Where it says "replaces", the earlier text stays in the file as written and this section is the binding one.
No evaluation stage has run (`results_p3/eval_start.txt` does not exist). The only runs of an arm on test data are the feasibility probe on 10 frames (item 16.6).

### 16.1 Boreas common frame (finding 1). Replaces section 8.1 "Common frame" and "Vertical level", and the premise of section 8.2

* **Fault.** Section 8.1 assumed that the ENU origin of every session is the first row of its own `gps_post_process.csv`, and moved each session by the geodetic difference of those origins. The easting and northing of all three sessions are already measured from ONE
  origin, the first row of the 2020-11-26 session. The first rows of the other sessions are not at zero: 2021-04-08 at (-4.302, -0.149) m and 2021-06-17 at (-0.450, 0.821) m (gate log, `first_row_east,north`). The old frame therefore moved those sessions a second time, by 4.3 m and 0.94 m.
  The old gate passed only because its near-tile rule tolerates a shift of one tile. Exact-tile shares in the old log were 0.69 and 0.67 in the common frame and 0.84 and 0.80 in raw file coordinates (`results_p3/logs/boreas_overlap.attempt-20261018T133559.log`, kept). The old validator checked the ENU premise on the reference session only, whose first row is (0, 0).
* **Rule.** The common frame is the frame of the files: the map from a session to the common frame is the identity (`commonFrame`). No session is moved horizontally.
* **Premise check, every session.** For every GPS row of the slice (window of the frames plus or minus 1 s), the geodetic ENU of its latitude and longitude at the REFERENCE origin (the first GPS row of 2020-11-26-13-58) must agree with the easting and northing of the file within **0.10 m**
  (horizontal distance, worst row). `boreas-overlap` checks it for all sessions and fails the gate if one session fails; `boreas-validate` requires `--ref` and checks the session against the reference origin; `boreas-convert` refuses to write a cache for a session that fails.
  The limit 0.10 m was named by the review; the measured worst miss is 0.055 m.
* **Gate re-run** (`results_p3/logs/boreas_overlap.log`, pose files only, traced; the old log is kept as an attempt): worst ENU miss 0.0551 m in each of 2020-11-26, 2021-04-08 and 2021-06-17 (36,386 / 36,385 / 36,385 rows); the tile overlap in the file frame has exact shares 0.80 to 0.97 and near shares 0.89 to 1.00. **PASS.**
  The constant part of the miss is a mean of about (-0.021, 0.047) m (east, north), the same in all three sessions: it belongs to the reference origin and not to a session.
* **Vertical level.** The altitude columns of the three sessions are on one datum to centimetres (gate log, `altitude` rows, nearest trajectory row within 3 m, 1565 to 1704 pairs): median difference 2020-11 to 2021-04 +0.042 m, 2020-11 to 2021-06 +0.041 m, 2021-04 to 2021-06 -0.009 m; 10th to 90th percentile within -0.03 m to +0.13 m.
  **No vertical offset is applied** (`z_offset = 0` for every session). The tile-ground comparison (`tile-ground`, `p3_eval.py zoffset`) stays as a **diagnostic**: it is run on the caches, reported next to the Boreas table, and never applied. The limit stays: a difference of 0.04 m to 0.13 m can move a point across a 0.2 m voxel boundary.
* The conversion in `reproduce_p3.sh` therefore runs once per session (no pass 2).

### 16.2 Spires ground (finding 2). Implements section 7.2 as written

`p3_eval.py spires` reports label 3 (non-ground structure) and label 2 (ground) as separate blocks: false-reject rate, undecided share, Static share and windows to first Static, each per label. The default is `--structure-labels 3,2`. Unit tests: `TestSpiresAndBoreas.test_spires_metrics`, `test_cli_boreas_and_spires`.
Committed (95b97d9) before `eval_spires`.

### 16.3 Shared tiles of the Jaccard (finding 3). Replaces section 8.3 "in shared tiles for each pair"; makes section 7.2 testable

The shared tiles are the 10 m tiles that ALL compared sessions observed (Spires: the three sequences; Boreas: the three sessions). A session observed a tile if its map holds at least one voxel there at the end of the run, Static or not (`*_observed.ids`: Static, Transient, Periodic and Intermittent voxels; a Rejected voxel was erased and does not count).
The primary Jaccard of each pair is computed inside this set of tiles. The Jaccard inside the tiles that only the two sessions of the pair observed is printed next to it as `pairwise_tiles` (secondary). The command takes `--observed` (required, one file per `--ids` file) and refuses a Static set that is not inside its observed set.
The earlier command used the tiles of the Static sets, which drops every tile in which one session or arm has no Static voxel and so depends on the arm. Unit tests: `test_jaccard_over_sessions_uses_the_tiles_all_sessions_observed`, `test_cli_boreas_and_spires`.
`newstatic` is unchanged.

### 16.4 Engine pin (finding 4). Replaces section 2.2, first bullet ("the commit recorded in each step log")

All five datasets run on ONE engine: the `strata_core` tree below (`git rev-parse HEAD:strata_core`). `guard_eval` refuses to run if the tree at HEAD differs from the last `STRATA_CORE_TREE=` line of this file (another agent commits to `strata_core` on this branch; a change needs a new amendment that re-pins).
Every step log records `STRATA_CORE_TREE` (the tree of the commit the binary was built from) and `STRATA_CORE_TREE_PINNED`; every run record in `results_p3/runs/` carries `strata_core_tree_head` and `strata_core_tree_binary`; `eval_start.txt` and `run_manifest.txt` carry the tree.

```text pin
STRATA_CORE_TREE=87bee34e3d6e7861d45eef80ad5069ff54fd12da
```

**Engine changes after the first commit of this file (f208975, 11:55), disclosed.** (a) `8ccf56a` (12:31): after a reverse-test demotion the window count `n` restarts at 0 together with the statistic (the periodicity gate, the rejection and the `n_max_undecided` read-out count the length of the current test). This changes the `sprt` arm only, and only for cells that were demoted.
(b) `f382498` (12:11): the loops that derive latency and demotion misses stop for a pair that needs over a million windows (a configuration error); no value changes for any pair of this file. (c) `33ef9b8` (12:52): tests only. The smoke tests (11:16 to 11:50) and the Spires cost and count figures of section 14 ran on the engine before (a).
They were repeated on the pinned engine (`smoke_kth`, `smoke_lil`, `smoke_spires`; `deviations.md`, section "Repeated on the pinned engine"); the figures of those repeats replace the earlier ones. The derived constants of section 2.1 are unchanged by (a) and (b).

### 16.5 KTH evaluator note (finding 6). Adds to the sentence beside every KTH table (section 5.1)

The review compared the evaluator with the official rule on a 30,000-point crop of KITTI 00 (jittered and boundary-seeded map points): the harness and an exact float64 brute force disagree on 0 points; an emulation of the official `export_eval_pcd` (PCL squared distance in float32, square root, `> 0.05f`) disagrees on 3 points, all placed at exactly 0.05 m (SA 61.768 against 61.778, DA identical).
The exported points are the original frame points, at distance 0 from themselves, so a real run cannot hit that boundary. This is the reviewer's measurement; its script is not in the repository and PCL is not installed here, so it is quoted and not reproduced. The sentence beside each KTH table adds: "the evaluator applies `<= 0.05 m` in float64; the official tool's float32 rule can differ only for points at exactly 0.05 m".

### 16.6 Records of the Boreas feasibility probe (finding 5). Corrects section 14

Section 14 says the probe ran on "data that was already opened". More exactly: the probe ran **two arms of the engine (`shipped_v0_1` and `sprt`) on the first 10 frames of the test session boreas-2020-11-26-13-58** (the same 10 frames as the reader validation), with `static-export`. It computed no label, no rate and no metric, and nothing was chosen from a result except the range window and the veto switch, by cost.
The files read are in `access_log.tsv.gz` (`feas_boreas_convert*`). The first run printed its table to the terminal and did not save it. The files that survived are in `results_p3/feas/original/`; the probe was run again on the same 10 frames (`feas_rerun_boreas_convert_r*` in `access_log.tsv.gz`),
and its table and meta files are in `results_p3/feas/`. Cells and peak memory equal the table of section 14 in every row; the seconds differ with host load and are for planning only (R6). `deviations.md` carries the corrected order of events.

### 16.7 Re-pin (2026-10-18, before any evaluation)

Commit `bb93d84` (13:56, other work on the branch) added three lines to `strata_core/test/test_sprt.cpp`. The trees of `strata_core/src` (`be2e48e0f03b5340989480bbf25f1681fdcff1ce`), `strata_core/include` (`50d9d7f4710fa5e27884cf0b8956a14a72005f5e`) and the file `strata_core/CMakeLists.txt` are the same as at the pin of item 16.4;
only `strata_core/test` changed. So the code that the smoke tests and the Spires repeat of item 16.4 ran on is the code of the new pin. The guard compares the whole `strata_core` tree and therefore refused at the new HEAD, as designed. The last `STRATA_CORE_TREE=` line of this file is the pin; it replaces the line of item 16.4.
A further commit to `strata_core`, test-only or not, needs a new amendment of this kind before an evaluation can start.

```text pin
STRATA_CORE_TREE=d8db00efc9e04d1c1880cd55cf75bd90a018b5c0
```

## 17. Amendment 4 (2026-10-18, before any evaluation and before the re-run of `validate_boreas`, the gate and the feasibility probe): the Boreas rotation convention

Written and committed before the re-run. It replaces the "Pose" bullet of section 8.1, finding 3 of section 11, and the evidence that section 8.1 and Amendment 3 give for the convention ("agreement on the first 10 frames: 0.0003 degrees"). It changes no arm, label, window, resolution or metric, and no other dataset.
No evaluation stage has run (`results_p3/eval_start.txt` does not exist). No Boreas map or number computed with the old rotation was reported anywhere. The feasibility probe of section 14 used the old rotation and is repeated (item 17.4).

### 17.1 Fault (Opus review, finding 1)

`rotationOf` returned Rz(heading) Ry(pitch) Rx(roll). The post-processed angles of Boreas are the angles of the pyboreas `yawPitchRollToRot`, which builds the passive (transposed) matrices Rr Rp Ry. The rotation block that maps applanix coordinates to ENU is therefore (Rz(heading) Ry(pitch) Rx(roll))^T.
`lidarPoseFromApplanix` had put R_f^-1 on the world side to reproduce `lidar_poses.csv`, while both files were parsed with the same wrong function. The "yaw shift on the world side" of section 11.3 was the symptom. With the true rotation, the lidar pose is the ordinary composition of `T_applanix_lidar.txt` as written.
A frame would have been rotated by the wrong attitude (yaw error of twice the lidar yaw, doubled roll and pitch). The validator agreement of 0.0003 degrees could not show this, because both files went through the same parser.

**Evidence.** The review recomputed it from pose files the gate had opened. I recomputed the first two lines for this amendment (step `r2_explore_pose`, the script `explore_boreas_pose.py` run from an identical copy outside the repository, trace `results_p3/access/r2_explore_pose.strace`; rows of the lidar slice plus or minus 0.5 s of `gps_post_process.csv`, speed above 3 m/s):

| session | moving rows | velocity direction + heading: 10th / 50th / 90th percentile | correlation of d(heading)/dt with `angvel_z` |
|---|---|---|---|
| 2020-11-26 | 33,441 | 87.88 / 88.45 / 88.83 deg | -0.99991 |
| 2021-04-08 | 30,009 | 87.69 / 88.42 / 88.82 deg | -0.99993 |
| 2021-06-17 | 32,883 | 87.84 / 88.43 / 88.83 deg | -0.99992 |

The velocity direction is atan2(vel_north, vel_east). The sum is constant at every heading, so heading grows clockwise and the applanix forward axis is +y. The velocity direction minus heading spreads over the whole circle.
The review also measured the frames: the share of frame-9 points of 2020-11-26 within one 0.1 m voxel of frame 0 (2.26 m apart, `lidar_poses.csv` rows, no deskew) is 0.534 with the old rotation and 0.809 with the transposed one (0.864 against 0.95 at 0.3 m).
I did not reproduce that measurement before the fix. Item 17.3 adds it as a check, and the repeated validation reports it.

### 17.2 Rule (replaces section 8.1 "Pose")

* `rotationOf(row)` = (Rz(heading) Ry(pitch) Rx(roll))^T.
* `lidarPoseFromApplanix`: R_lidar = R_applanix R_f and t_lidar = t_applanix + R_applanix t_f, with the true rotation.
* The post-processed GPS at 200 Hz is interpolated at the time of each point as before (deskew). Range window 3 m to 25 m, W = 10, 0.2 m, Periodic veto off (Amendment 1), common frame of Amendment 3. Nothing else of section 8.1 changes.
* The overlap gate, the ENU check and the altitude check use positions only and are unaffected.

### 17.3 New checks, limits fixed here before they run

Pose only, in `boreas-overlap` (the gate), for every session, over the GPS rows of the slice plus or minus 1 s, rows with speed above 3 m/s:

1. **The forward axis follows the velocity.** The azimuth of `rotationOf(row)` applied to (0, 1, 0), minus the azimuth of the velocity: the 10th and the 90th percentile each within 4 degrees of zero, and the median of each heading quadrant (quadrant of the forward azimuth, counted if it holds at least 200 moving rows) within 4 degrees. At least 500 moving rows. The expected offset from the table is about 1.5 degrees.
2. **The gyro sign.** The correlation of d(forward azimuth)/dt (successive rows, at most 0.02 s apart) with `angvel_z` is at least +0.99.

A failure of either check fails the gate. The gate log prints both per session.

With lidar content, in `boreas-validate`, on the first and the tenth frame of 2020-11-26 (already opened; no deskew; 3 m to 50 m in the lidar frame; one pose per frame):

3. **Frame consistency.** The share of the points of the tenth frame within one 0.1 m voxel (26 neighbours) of an occupied voxel of the first frame is at least **0.70**, and at least **0.92** at 0.3 m. The check runs for the pose of `lidar_poses.csv` and for the pose from GPS times calibration. The vehicle must have moved more than 1 m between the frames.
4. **The test must discriminate.** The same share with the transposed rotation (the old reading, applied to the rows of `lidar_poses.csv`) must be at least 0.10 lower at 0.1 m. It is printed as `contrast_transposed_rotation`.
5. The pose agreement with `lidar_poses.csv` (0.10 m, 0.5 degrees) stays. Both sides now use the true rotation.

The limits come from the numbers of the review (0.809 and 0.95 with the true rotation, 0.534 and 0.864 with the old one). They leave margin below the first and above the second. They were not tuned on this code. If a check fails on the real frames, the failure is reported, and a new amendment is needed before the gate is called passed.

Tests (`test_p3.cpp`; mutants in `mutation_p3.py` with notes beginning `a4`):

* the rotation against the pyboreas matrices written out;
* the forward axis, the gyro sign, the quadrant rule and a missing velocity column on synthetic drives;
* the frame consistency on a synthetic room seen from two poses, with the true and the transposed rotation;
* the probe commands on synthetic sessions whose forward axis points the wrong way, whose gyro sign is flipped, whose velocity is 10 degrees off, and whose frames are rendered through the transposed rotation.

`reproduce_p3.sh` gets the stage `validate_boreas`.

### 17.4 What is repeated

* `validate_boreas_2020` (new rotation, new checks), `boreas_overlap` (gate with the convention checks), and the feasibility probe (`p3_feas_boreas.sh`, the same 10 frames) on a binary built from the committed code.
* The earlier logs stay as `*.attempt-*`. The old probe records stay in `results_p3/feas/original/` and in git history.
* The probe measures cost. Cells, memory and time may differ slightly from the table of section 14. The configuration (3 m to 25 m, 0.2 m, veto off) changes only if a row exceeds the memory of the host. Read section 14 together with the repeated table.

## 18. Amendment 5 (2026-10-18, after the fourth Opus review of phase 1, before any evaluation): the unit of the alpha test; re-pin

### 18.1 Fault

The bound `alpha = 0.01` is stated per history of a cell (L-7). The replay of the shipped pair on the W = 15 calibration sequences (`results_p2/replay_shipped_pair_W15.txt`) stopped at the first decision of each cell and did not say so. The deployed engine erases a rejected cell, the next observation creates it again, and the cell gets a new history. Run to the end of the sequence, one cell per map, the share of clutter cells that were Static at any window is:

| unit | at H/N <= m0 (n = 22,273) | above m0 (n = 1,823) | all clutter (n = 24,096) |
|---|---|---|---|
| first history (first decision), periodicity off | 0.40 % | 22.3 % | 2.06 % |
| cell, ever-Static, periodicity off | 1.64 % | 33.8 % | 4.07 % |
| cell, ever-Static, periodicity on (as the node ships it) | 1.51 % | 30.5 % | 3.71 % |

The per-cell figure at or below `m0` is above `alpha`. "The realised rate is lower than the bound" holds per first history, not per cell. `PREREG_P3.md` sections 3.3 and 6.2 said "false-Static rate on clutter" without a unit; the harness computed the share of clutter cells whose verdict at the END of the run is Static.

### 18.2 Rule

1. **The unit of the `lt_changes_dynamics_0` alpha test is the cell.** The primary quantity is `ever_static_on_clutter`: the share of eligible clutter cells with `first_static >= 0` (Static at the close of any window), per arm, with the Wilson interval and the tile bootstrap of section 3.3. It replaces `false_static_on_clutter` as the primary quantity of section 6.2 for all arms and all LILocBench sequences.
2. **Printed beside it, unchanged:** `false_static_on_clutter` (Static at the end of the run), `clutter_undecided`, `clutter_rejected`, and the band table. New row: `ever_static_on_clutter_at_or_below_m0`, the same quantity on the clutter cells with observed rate `H/N <= m0 = 0.20`, the population of the calibration reference rows above.
3. **Reference rows, quoted with every table of this quantity:** the three rows of the table in 18.1 (calibration sequences, same code, same pair), and the bound `alpha = 0.01` per history under independent windows. The comparison is held-out per-cell ever-Static against the per-cell calibration rows, not against `alpha` alone.
4. **The first-history unit is not computed on the test data.** The harness records the final verdict and the first Static window of a cell, not its histories. This is stated in every table. No claim of the form "the realised rate is below the bound" is made from a per-cell figure that is above `alpha`.
5. No threshold, arm, label, window, resolution or verdict rule changes. KTH, Spires and Boreas rows are unchanged (they count voxels at the end of a run, section 3.3).

Tests (`test_p3_eval.py`): a fixture with cells that were Static and rejected later (ever-Static 7 of 40, end-of-run Static 4 of 40; at or below `m0`: 3 of 36, with one cell at exactly `m0`; `m0` default 0.20); mutants in `mutation_p3.py` (notes beginning `lil: ever-Static`).

### 18.3 Re-pin

Commit `2f3cead` (test and node work on the branch, from the review) changed `strata_core/test/test_replay_p1.cpp` and `strata_core/test/check_defaults.py` only. The trees of `strata_core/src` (`be2e48e0f03b5340989480bbf25f1681fdcff1ce`) and `strata_core/include` (`50d9d7f4710fa5e27884cf0b8956a14a72005f5e`) are those of Amendment 3 item 16.7; `strata_core/CMakeLists.txt` is unchanged. The engine code of every earlier run is therefore the code of the new pin; only `strata_core/test` differs. The guard compares the whole `strata_core` tree and refused at the new HEAD, as designed. The last `STRATA_CORE_TREE=` line of this file is the pin; it replaces the line of item 16.7. A further commit to `strata_core` needs a new amendment of this kind before an evaluation can start.

```text pin
STRATA_CORE_TREE=bf96e78bb16d164f02768fc7ae1738bb5bd35d2e
```

## 19. Amendment 6 (2026-10-18, before any evaluation): every arm updates once per touched window; labels of the two baseline rows

Resolves finding 1 of the third Opus review. It changes no arm value, label rule, window, resolution or metric. It replaces the wording of section 2.1 (rows `octomap` and `elite_like`), section 3.2 (policy ii) and section 5.1 (the sentence beside each KTH table); the earlier text stays and this section is binding.

### 19.1 What the engine does

`LayeredMap::endWindow` (`strata_core/src/layered_map.cpp`, lines 110 to 119) adds `l_hit` or `l_miss` once per touched window: hit if any return fell in the window, miss if only free-space rays did. It then clamps, and applies graduate and demote. All five arms run through this one path. Voxel ray casting at the stated resolution gives the hits and misses of each window.

OctoMap and ELite update once per ray per scan at 0.1 m. So the `octomap` arm is the OctoMap parameters (`p_hit`, `p_miss`, clamps, threshold) applied to window outcomes. It is not OctoMap. The same holds for `elite_like` and ELite. Evidence from the smoke run on KITTI 00 (calibration data): the `octomap` arm scores SA 98.45 and DA 97.81 (`results_p3/smoke_kth00_octomap_not_rejected.json`). The published OctoMap row of the KTH benchmark is about SA 68.

### 19.2 Rule

1. Every arm, `octomap` and `elite_like` included, applies its increment once per touched window, through the same voxel ray casting at the stated resolution.
2. In every table, figure and sentence the two rows are named **OctoMap parameters on window evidence** and **ELite factors on window evidence**. The names OctoMap and ELite alone are not used for them. The arm ids `octomap` and `elite_like` stay in code, file names and the arm block of section 2.1.
3. The sentence beside each KTH table adds: "every arm applies its increment once per touched window, so the rows labelled OctoMap parameters on window evidence and ELite factors on window evidence are not the published OctoMap and ELite methods; their numbers are not comparable to the published rows".
4. No KTH table puts these two rows next to published OctoMap or ELite numbers as if they were the same quantity.
5. No arm value changes. Section 2.1 and the `arms` block stay as written.

## 20. Amendment 7 (2026-10-18, before any evaluation): the lead decisions of section 12; n_max_undecided

Resolves finding 2 of the third Opus review. Section 12 lists six open items and had no recorded answer. The lead ordered all findings of the review to be resolved without changing any arm. The decisions below follow from that order and from the binding documents. Five are "keep as written". Every decision can be replaced by a further dated amendment that is committed before the first evaluation. The guard refuses to run until this section exists (marker line at the end).

| Item of section 12 | Decision | Reason |
|---|---|---|
| 1. Freeze or waive the pair | **Keep as written.** The pair is waived. `sprt` runs the point pair (0.20, 0.70, 0.01, 0.01) of `PREREG.json`. `sprt_low_end` and `sprt_high_end` stay extra arms. | `PREREG.md`: G1 fails, so neither pair is frozen (section 2.3). The three `sprt*` rows show how much the answer depends on the pair. |
| 2. Spires clutter definition (section 7.3) | **Keep as written.** No vertical band. The definition is reported with its caveat. | A band chosen now would be a new label rule. Section 13 forbids changes after a result and the definition is already fixed. |
| 3. Per-laser time offset on `dynamics_0` | **Keep as written.** The conversion uses -0.13 s for both lasers. The diagnostic search is reported and not used. | A per-laser offset chosen by a search would tune on test data. |
| 4. Vertical offset between Boreas sessions | **Settled by Amendment 3 (16.1).** No offset is applied (`z_offset = 0`). The tile-ground comparison is a diagnostic only. | Amendment 3 measured the altitude columns on one datum to centimetres. |
| 5. Periodic veto | **Keep as written.** Primary on, secondary off. Boreas runs with the veto off (Amendment 1). | Section 2.2. The node ships the veto. The secondary run isolates the evidence rule. |
| 6. MTD-Map-like arm | **Keep as written.** Not built. It is reported as not run, with the reason of section 2.4. | No released code was found. A reimplementation from the paper alone would claim more than we know. |

### 20.1 n_max_undecided = 80 (not 64)

`LEAD_DECISIONS_P1.md` L-6 says 64. The arm table of section 2.1 runs 80 (`results_p1b/deviations.md` items 8 and 20). The memo's Monte Carlo gives P(still undecided) = 0.024 at 64 windows, but it lets a cell be rejected from window 1. The engine rejects only once n >= 24 (the gate `T`). With that rule the exact P(undecided) is 0.0314 and 0.0388 at 64, and 0.0113 and 0.0150 at 80 (two pairs; the reviewer's own dynamic program gives the same). The memo's own bound, P <= 0.03, is met at 80 and not at 64.

**Decision: 80 binds P3 and the paper.** The arm table is not changed. The paper states that L-6 named 64, that the engine rule needs 80 to reach the stated undecided share, and gives both figures. If the lead replaces this decision, the arm block of section 2.1 and `p3_arms.hpp` change in a dated amendment committed before the first evaluation.

```text lead decisions
LEAD_P3_DECISIONS=amendment-7
```

## 21. Amendment 8 (2026-10-18, during the evaluation, after the first failed run): failures do not stop a stage; resume; memory limit of the Spires 0.1 m runs

Written and committed after the first failure of the evaluation and before any run that it affects. The two LILocBench stages (veto on and off) had finished without a failure. It changes no arm, no label, no window, no resolution and no metric.

**What happened.** The stage `eval_spires` (veto on) ran `spires_03_shipped_v0_1_r0.1_W10_p1` with the default address-space limit of 6 GB (`MEM_LIMIT_KB = 6000000`) and ended with `std::bad_alloc` after 17 minutes
(`results_p3/logs/spires_03_shipped_v0_1_r0.1_W10_p1.attempt-*.log` and its run record, kept). The script stopped the whole stage at the failed step (`must`). The 0.2 m runs of Keble 03 (28 arm runs) had finished.

**Changes (all in `reproduce_p3.sh`; committed before the run they affect).**

1. A failed arm run, export or evaluation is appended to `results_p3/failed_steps.tsv` and the stage goes on with the next run (`try`). The failure stays a reported result (section 4: all runs are reported, failures with the reason). A run that depends on a failed run (its evaluation, the Jaccard of an arm whose export failed) is not started and is listed as not run.
2. `P3_RESUME=1`: a step whose run record exists with return code 0 is not run again. Nothing else is skipped. A failed step is run again with an identical command and its earlier log is kept as an attempt (section 4: a run is repeated only after a crash with an identical configuration; the repetition is listed).
3. The Spires arm runs at 0.1 m use the address-space limit **9 GB** (the limit of the KTH exports, section 14), 0.2 m runs stay at 6 GB. The host has 11.9 GB of RAM and 32 GB of swap. If a 0.1 m run fails at 9 GB it is reported as failed with this limit and is not run again with a larger one.
   This is a resource limit of the harness, not a setting of an arm. The limit is stated beside every Spires 0.1 m table.

The same rule (1 and 2) applies to the KTH and Boreas stages. Their memory limits stay as written (KTH 9 GB; Boreas conversion and static exports 6 GB, section 14 and Amendment 1).

## 22. Amendment 9 (2026-10-18, during the evaluation): Spires 0.1 m runs of an arm that failed at 0.1 m

The run `spires_03_shipped_v0_1_r0.1_W10_p1` (Keble 03, 0.1 m, `W = 10`, Periodic veto on) failed twice with `std::bad_alloc`: at 6 GB (17 minutes) and at the 9 GB limit of Amendment 8 (about 20 minutes). Each failing 0.1 m run costs about 20 minutes. The host has 11.9 GB.
Rule from now on: the first 0.1 m run of an arm and veto setting is Keble 03 `W = 10` (the loop order). If it fails, the later 0.1 m runs of that arm and veto setting (Keble 03 `W = 20`, Keble 04, Keble 05) are not started, because a longer window, a longer sequence and the same arm need at least the memory of the failed run. They are listed in `results_p3/not_run_steps.tsv` with this reason, and the Spires 0.1 m table says "not run" for them, not "failed".
An arm that succeeds on Keble 03 `W = 10` runs everywhere as before. The 0.1 m runs with the veto off (the secondary pass) use the same rule. No limit is raised again.

## 23. Amendment 10 (2026-10-18, during the evaluation): the zip reader reads zip64 archives

The step `spires_04_convert` failed with `zip64 archives are not supported` (`results_p3/logs/spires_04_convert.log`, kept). The Keble 04 and Keble 05 archives are 4.6 GB and 4.1 GB; the reader of phase 1 (`ZipArchive` in `p1b_io.hpp`) accepted only archives below 4 GB (Keble 02 and 03).
The harness validation read Keble 03 only, so the defect was not found before. No Keble 04 or 05 scan was read by the failed step: the reader stopped at the end-of-central-directory record.

**Change** (`p1b_io.hpp`, tests in `test_p1b.cpp`): the reader follows the zip64 end-of-central-directory locator and record when a field of the end record is 0xFFFF or 0xFFFFFFFF, and reads usize, csize and the local header offset of an entry from the zip64 extra field (id 0x0001) when the central directory entry holds 0xFFFFFFFF.
Multi-disk archives, a missing locator, a wrong signature and a missing extra field are errors. Archives below 4 GB take the old path unchanged (the 35 earlier tests of `test_p1b` pass). Mutation record: `results_p3/mutation_record_zip64.md` (12 mutants, 11 killed, 1 equivalent).
No arm, label, window, resolution or metric changes. The change reaches the conversion of Keble 03 (not zip64) in no way; the cache of Keble 03 already made is not rebuilt.

## 24. Amendment 11 (2026-10-19, during the evaluation): a failed read of a cache frame is repeated

**What happened.** On Keble 04, three arm runs ended with `cache.bin: truncated frame <n>` (`spires_04_sprt_r0.2_W10_p1` at frame 6299, `spires_04_sprt_low_end_r0.2_W10_p1` at frame 1782, `spires_04_sprt_low_end_r0.2_W20_p1`; listed in `failed_steps.tsv`).
The file is complete: it is 4,157,305,924 bytes, it was written by one conversion step (6826 frames), and 12 earlier arm runs read all 6826 frames of it. The failing runs ran while other jobs loaded the host (load 7 to 13) and the cache sits on the /mnt/d mount.
The earlier note on a transient read error of this mount is in section 11, item 6. A run that fails this way is a crash; section 4 allows a repeat with an identical configuration. The failures are reported, not hidden.

**Change** (`p1b_io.hpp`, tests in `test_p1b.cpp`): `CacheReader::next` repeats the read of a frame from its start, up to 6 times, with a wait of 1, 2, ..., 6 seconds, after clearing the stream state. Each repeat prints a line `read retry` to the step log. A file that is really short fails after the sixth repeat with the old message.
The frames that are returned are the same as before; only the failure path differs. Mutation record: `results_p3/mutation_record_cache_retry.md` (9 mutants, 9 killed). No arm, label, window, resolution or metric changes.
The three failed runs are run again with an identical command (`P3_RESUME=1` runs steps whose record has a non-zero return code again); their first logs stay as attempts and the repetition is listed in `results_p3/deviations.md`. Runs that finished are not run again.

## 25. Amendment 12 (2026-10-19, during the evaluation, before any Spires table is written): populations of the Spires latency and false-Static rows; failed and not-run lists

Written after the fifth Opus review (three findings on the Spires rows and the run lists). Committed before any Spires table and before any further run. The C++ probe is not changed: Keble 03 (0.2 m) and Keble 04 (0.2 m, partly) are already run. No arm, label, window, resolution, threshold or verdict rule changes.

### 25.1 Spires "windows to first Static" counts all labelled cells that were ever Static

`p3_probe.cpp` (`evalCommon`, format `counts`) adds a cell to the latency table whenever `first_static >= 0`, with no check that the cell has `n >= nmin` (section 2.2: cells with fewer than 5 touched windows are not eligible for any rate). The `.latency` file has no eligible column. Evidence: Keble 03, `sprt`, 0.2 m, `W = 10`, veto on: label 3 has 196,584 eligible cells; its latency table holds 178,472 cells; at least 4,222 cells with `n < 5` are Static at the end of the run and are in that population.
The LILocBench latency (`p3_eval.py`, `lil_metrics`) counts eligible cells only. The two are therefore different populations.

Rule: **the Spires latency population is all labelled cells that were ever Static, eligible or not.** This sentence stands beside every Spires latency row (`windows_to_first_static`), and the JSON of `p3_eval.py spires` carries it in the field `population`. A Spires latency is not compared with a LILocBench latency. The latency table is not re-run with an eligible column.

### 25.2 The Spires end-of-run false-Static row is not the unit of the alpha test

Section 3.3 prints `alpha = 0.01` beside every realised false-Static rate. Amendment 5 makes the cell (ever-Static) the unit of the alpha test and keeps the Spires, KTH and Boreas rows at end-of-run counts (18.2 item 5).
Rule: **the Spires row `false_static_on_clutter` is labelled "end of run, eligible cells; not the unit of the alpha test", and `alpha` is not printed beside it.** The JSON field `unit` carries the label.
An ever-Static row for clutter is recovered from the label-1 rows of the `.latency` file and **added after the run**: `ever_static_on_clutter_all_cells` (replaced by the bound of Amendment 13, section 26), k = label-1 cells with a first Static window, n = all label-1 cells. Because the `.latency` file has no eligible column, both counts include ineligible cells (25.1). The row is listed as added after the run, it is not comparable with the eligible-cell rows of Amendment 5, and `alpha` is not printed beside it either. The comparison with the calibration reference rows (18.2 item 3) is made on LILocBench only.
Code: `p3_eval.py` `spires_metrics`; tests `test_p3_eval.py` (`test_spires_metrics`); mutation record `results_p3/mutation_record_amend12.md` (3 mutants, 3 killed; the all-cells row is replaced by Amendment 13). Spires JSON files written before this amendment lack the new fields; the table is built by `p3_eval.py spires` from the counts and latency files.

### 25.3 Failed steps are not "not run"

At 21:44:36 and 02:10:07 the resume pass listed the seven `spires_03_<arm>_r0.1_W10_p1` steps in `not_run_steps.tsv` although they had run and failed (`failed_steps.tsv`, 18:46 to 21:23). Cause: the Amendment 9 skip matched the step's own name in `failed_steps.tsv` and logged a skip as "not run".
Rule: **a step that appears in `failed_steps.tsv` is failed, not not-run.** `reproduce_p3.sh` no longer writes such a step to `not_run_steps.tsv`; the skip of a repeat is written to `results_p3/skipped_repeat.tsv`. Steps that never started (the Keble 03 `W = 20` runs, Keble 04 and 05) stay in `not_run_steps.tsv`. The rows already written are not edited (the files are append-only logs); a table takes the status "failed" for every step in `failed_steps.tsv`, whatever `not_run_steps.tsv` says. Test: `test_p3_not_run_split.sh`.

## 26. Amendment 13 (2026-10-19, during the evaluation, before any Spires table is written): the Spires ever-Static clutter row is a bound on eligible cells

Written after the sixth Opus review (two findings). Committed before any Spires table and before any re-run. No run is repeated: no committed result file holds the row, it is computed from the counts and `.latency` files by `p3_eval.py spires`.

**Finding.** The row `ever_static_on_clutter_all_cells` of 25.2 took all label-1 cells as its denominator. That includes cells with `n < 5` and cells that were never touched, which cannot be Static. The dilution differs between arms. Recomputed on Keble 03, 0.2 m, `W = 10`: 41,053 label-1 cells, 22,262 of them ineligible. For `sprt` the row reads 6,893 / 41,053 = 0.168, below the end-of-run rate on eligible cells (0.283). Ever-Static contains end-Static, so the two cannot describe one population. For `octomap` the row reads 0.815 against an end-of-run rate of 0.514. The row stretched the `sprt` to `octomap` contrast from about 2x to about 5x.

**Rule.** **The all-cells row is removed.** It is replaced by `ever_static_on_clutter_eligible_bound`, a bound on the ever-Static rate over eligible label-1 cells (`n >= 5`):

- `k_lo` = eligible label-1 cells that are Static at the end of the run (`counts` rows `1,1,static`). A cell Static at the end has been Static.
- `k_hi` = min(ever-Static label-1 cells in the `.latency` file minus ineligible label-1 cells that are Static at the end (`counts` rows `1,0,static`), `n_elig`). Those ineligible cells are ever-Static and are not in the eligible population.
- `lo = k_lo / n_elig`, `hi = k_hi / n_elig`, `n = n_elig`. The field `consistent` is false if `k_hi < k_lo`. Then the files disagree and the row is not used.

The bound is not an exact rate and no `alpha` is printed beside it. It is tight for the `sprt` arms and wide for `octomap` and `elite_like`. Values on Keble 03, 0.2 m, `W = 10` (n = 18,791): `sprt` [0.283, 0.287], `sprt_low_end` [0.285, 0.287], `sprt_high_end` [0.156, 0.158], `shipped_v0_1` [0.443, 0.448], `symmetric` [0.387, 0.410], `octomap` [0.514, 0.635], `elite_like` [0.394, 0.622]. A wide bound is reported as wide. The exact rate needs an eligible column in the `.latency` file; the probe is not changed and the runs are not repeated (25.1).

Code: `p3_eval.py` `spires_metrics`. Tests: `test_p3_eval.py` (`test_spires_metrics`, `test_ever_static_bound_on_eligible_clutter`). Mutation record: `results_p3/mutation_record_amend13.md` (5 mutants of the bound, 1 of the latency wording, 6 killed).

**Test registration.** `test_p3_not_run_split.sh` (Amendment 12) and `test_p3_step_names.sh` were run by nothing. Both are now `add_test` entries in `CMakeLists.txt` (`p3_not_run_split`, `p3_step_names`, next to `p3_guard`), both run in the `unit` stage of `reproduce_p3.sh` (step `unit_sh`), and both are listed in `README_P3.md`.

## 27. Amendment 14 (2026-10-19, during the evaluation, before any further run): re-pin of the engine tree

Commits 0d09639 and a7559d4 (review findings on the P2 engine) and 9355770 and 718460c changed `strata_core/test/test_sprt.cpp`, `strata/src/mapping_node.cpp`, `strata/include/strata/evidence_status.hpp` and `scripts/check_evidence_params.sh`. The trees of `strata_core/src` (`be2e48e0f03b5340989480bbf25f1681fdcff1ce`) and `strata_core/include` (`50d9d7f4710fa5e27884cf0b8956a14a72005f5e`) are those of the pin of Amendment 5, and `strata_core/CMakeLists.txt` is unchanged. Only `strata_core/test` differs (one test file). The engine code of every earlier run is the engine code of the new pin. The guard compares the whole `strata_core` tree and refused at the new HEAD, as designed. The last `STRATA_CORE_TREE=` line of this file is the pin. No arm, label, window, resolution, threshold or metric changes, and no run is repeated because of this amendment.

STRATA_CORE_TREE=c780639aec9402da556bd33b91ff9acbc06c0cd0

## 28. Amendment 15 (2026-10-20, during the evaluation, before the Boreas stage and before any KTH stage): report-only rows; two streams; the report generator

Written during the evaluation. Committed before the Boreas stage, before any KTH stage and before any further Spires run. No arm, label, window, resolution, threshold, verdict rule or metric of a run changes. No run is repeated because of this amendment.

### 28.1 Report generator and report-only rows

The report `results_p3/P3_REPORT.md` is written by `make_report_p3.py` from the JSON files of `p3_eval.py`, the meta files of the probe and the run records. The script copies numbers. Two kinds of row are computed in the script and are labelled report-only in the report:

1. **Near-wall thickening** (LILocBench). Cells that were free in the mapping session and lie within 0.10 m of an occupied cell are the label 3 of the reference file (`ref_free_near_occupied`). They are set aside by the clutter definition of section 6.2. The advice (`P1_DECISION_ADVICE.md`, controls) asks for their Static share to be measured and reported. The row is computed from the committed arm dumps (`dump_*.csv`): cells with label 3 and `n >= 5`, Static at the end and ever Static, with the Wilson 95 % interval of `p3_eval.wilson`, and the same on the subset with at least one hit. The dumps already existed when the row was defined; no run is started for it. Test: `test_report_p3.py` (`Thickening`).
2. **Spires metrics from the counts and latency files.** The JSON files of `p3_eval.py spires` written before Amendments 12 and 13 lack the fields `ever_static_on_clutter_eligible_bound` and `population`. The report recomputes every Spires row from the counts and latency files with `p3_eval.spires_metrics` (the same function) and lists any field of a committed JSON that differs from the recomputed value (Amendment 12 said the table is built this way).

### 28.2 Two streams (replaces the sentence "One process at a time" of section 4 for the Boreas stage)

Section 4 required one process at a time. The reason was a clean record of host load; no timing is measured in this phase. The Spires runs with the veto off at 0.1 m (Keble 04 and 05) take about one to two hours each, and the Boreas stage takes about a day. To finish in reasonable time the Boreas stage (`eval_boreas`) may run at the same time as the Spires stage. Rules:

* At most two evaluation processes run at the same time: one `eval_spires` or `eval_kth` stream and one `eval_boreas` stream. The KTH stage never runs together with a Spires 0.1 m run (the KTH exports have a 9 GB address-space limit; the host has 11.9 GB shared).
* The step logs already record the load before and after each step. The host load of a run is stated, not controlled.
* A run that fails with `std::bad_alloc` or a kill while two streams run is listed in `failed_steps.tsv` like every failure. It is repeated alone, with the identical command, and the first log stays as an attempt (section 4). The report lists both.
* Memory limits per step do not change.

### 28.3 Re-pin of the build

The commit of this amendment changes HEAD. The binary is rebuilt from the new HEAD (`reproduce_p3.sh build check unit`) before the next evaluation step. `strata_core` is the tree of the pin (Amendment 14): the diff to the earlier commit holds `make_report_p3.py`, `test_report_p3.py`, `results_p3` files and this file only.

## 29. Amendment 16 (2026-10-22, after the evaluation, review 4 of the P3 report): report corrections only

No arm, label, window, resolution, threshold, verdict rule or metric changes. No evaluation run is repeated. The report is regenerated by `make_report_p3.py` from the committed JSON and meta files.

1. KTH tables print the DA ceiling of the ground-truth static cloud (`kth_selfcheck_*.json`, section 5.4 item 2) in each header, with the float64 sentence of Amendment 3 item 16.5 and the "not comparable to the published rows" clause of Amendment 6 item 3.
2. The Boreas ground-height diagnostic is printed as session minus reference. The stored JSON holds reference minus session (`p3_eval.zoffset` returns `ground_a - ground_b`, `a` is the reference). The table negates the values and swaps p10 and p90. The ground heights lie on a 0.1 m step.
3. Section 1 of the report gets the `lil-offset` diagnostic and the pair-skew bound per sequence (section 6.1). The diagnostic selected nothing.
4. Rows defined after results existed are marked "added after the results were seen": the Spires ever-Static bound row (Amendments 12 and 13) and the near-wall thickening rows (Amendment 15).
5. The Spires veto effect on the false-reject of non-ground structure is added to summary item 8. The at-or-below-`m0` rate quotes the block interval.
6. A review of the report read test-split files (KITTI 05 `pcd/` frames twice under `strace`, `gt_cloud.pcd` once under `strace` and once untraced, first 400 bytes). Only reads. Rows are in `access_log.tsv.gz` (`review4_*`); traces are in `results_p3/access/review4_kth_*.strace`.
   Sub-item (2026-10-22, review 5): the reviewer also ran `p3_probe kth-export` read-only on KITTI 05 for `sprt` (00:51:21 to 00:54:46) and `shipped_v0_1` (to 00:58:15), 0.2 m, `W = 10`, veto on, policies `static_only` and `not_rejected`. Each export opened the 321 `pcd/*.pcd` frames twice and the `pcd/` directory once. From 00:58:27 to 01:17:44 the reviewer's evaluator opened `kth/05/gt_cloud.pcd` 5 times. Only reads; the exports were deleted after scoring. Rows are in `access_log.tsv.gz` (`review5_*`); traces are in `results_p3/access/review5_kth_*.strace`.

Editorial, 2026-10-22, commit 2a1c144: access_log.tsv renamed to access_log.tsv.gz (identical bytes after zcat, sha256 6030048f...a690); file-name rename only, not an amendment; no rule, arm or threshold changed. Order evidence is this file's first commit (git log --follow).
