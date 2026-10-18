# PREREG_P3: pre-registration of the phase 3 test evaluation

Status: written and committed before any test evaluation. The commit time of this file, the access log `results_p3/access_log.tsv`
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
| `octomap` | OctoMap update: `p_hit 0.7`, `p_miss 0.4`, clamps `0.12` and `0.97`, occupied iff `l > 0` (graduate at `0.5 + 1e-12`, demote at `0.5`), no leak, never pruned | Hornung et al. 2013 defaults |
| `elite_like` | on-ray odds factors 9 and 1/9 (`l = +-ln 9`), no leak, occupied iff `l > 0`; clamp `+-50` (the paper states none that we found) | ELite Eq. 5 and 6 at zero distance (`sources_verified.md` (6)) |
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
* **(ii) `not_rejected`**: a point is kept iff the verdict of its voxel is not Rejected. This is the removal reading, comparable to the OctoMap-family rows.
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
* Every step that touches a test-split file runs under `strace -f -e trace=openat,open`; the trace is committed (`results_p3/access/`), and `results_p3/access_log.tsv` lists every data file each step opened.
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
* A sentence stands beside every KTH table: single pass, number of windows, voxel size, pose source, 16-beam validity for semi-indoor, AA and HA labelled.

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
