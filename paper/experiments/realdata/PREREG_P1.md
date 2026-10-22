# PREREG_P1: phase 1 calibration measurement of per-cell hit rates

Status: written and committed before the first hit-rate measurement run. The commit
time of this file and the time stamps in `results_p1/logs/` (written by
`reproduce_p1.sh`) show the order. Anything below that changes after the first run is a
deviation and is listed in `P1_REPORT.md` under "Deviations".

## 0. Purpose and limits

Measure, with the engine's own window semantics, how often real structure and real
clutter are hit per window. The measurement feeds the lead's choice of `(m0, m1)` and the
decision whether a hit-rate test is enough (DESIGN_AND_PLAN section 3 C). This phase
changes no decision rule, chooses no `(m0, m1)` and implements no SPRT.

Only the CALIBRATION split is read: KTH DynamicMap sequence KITTI 00, and the LILocBench
sequences `mapping`, `static_0`, `dynamics_0` with their public GT poses. No TEST-split
file (KITTI 05, semi-indoor, `lt_changes_*`, Boreas, Spires) is opened, measured or
summarised. The input files are listed with their sha256 in `data_inputs.sha256`.

One fact about the KITTI 00 labels was looked at before this file was written, because it
decides the label rule in section 3: 65,399 of the 95,983 dynamic GT points lie within 3.5 m
of a frame origin (the KTH extraction marks every point within 3 m of the sensor with label
0 or 1, the ego vehicle, as dynamic). That is a property of the GT labels, not a hit
statistic. No hit rate had been computed.

## 1. What the engine reports (definition of hit and miss)

`hitrate_probe` registers a `WindowObserver` on the real `LayeredMap` (through
`Voxel3DBackend` for KTH, `Grid2DBackend` for LILocBench). At every window close, before
the evidence update, the engine calls the observer once per touched cell with
`(window index, cell, hit, miss)`.

* hit: the window saw at least one `observeHit` on the cell.
* miss: the window saw at least one `observeMiss` on the cell and no hit (a hit wins).
  Exactly one of the two is true for a touched cell. Untouched cells are not reported.
* A cell is "touched" in a window if it is reported. `N` = number of touched windows of the
  cell, `H` = number of those with a hit, hit rate `r = H / N`.

Miss definition. The scan adapter (`strata/src/scan_adapter.cpp:14`) drops every beam that
is not finite or lies outside `[range_min, range_max]`; the cloud adapter
(`strata/src/cloud_adapter.cpp:11`) drops non-finite points. The backends march a ray only
to a returned endpoint (`voxel3d_backend.cpp`, `grid2d_backend.cpp` `raycastClear`). So
today's miss is already "a ray passed through the cell to a return beyond it"; a beam
without a return adds no observation. The alternative "pass-through only" miss measurement
of DESIGN_AND_PLAN section 3 C, second branch, is therefore identical to the first and is
not run separately. The probe uses the shipped parameters of `LayeredMapParams` for every
evidence field. Only `layer_interval = W` is set, and periodicity is switched off (the
observer's reports do not depend on it; `test_window_observer.cpp`).

Windows. A window closes after `W` integration steps (`tick()` once per step). A step is
one KTH frame, or one pair of front and rear LILocBench scans. A last partial window is
never closed and its observations are not reported.

* Shipped window: `W = 10` (the `layer_interval` default).
* Short window: `W = 2`.

## 2. Conditions

| Dataset | Resolution (m) | W (steps) |
|---|---|---|
| KITTI 00, 141 frames, 10 Hz, 64 beams, `Voxel3DBackend` | 0.2 (shipped), 0.1 (alternative) | 10, 2 |
| LILocBench `mapping`, `static_0`, `dynamics_0`, 15 Hz, 2 x 811 beams, `Grid2DBackend` | 0.05 (shipped), 0.10 (alternative) | 10, 2 |

All frames of KITTI 00 are used (141 frames; 14 windows close at W = 10, 70 at W = 2).
All scans of the three LILocBench sequences are used. The grid is 60 m x 60 m with its
origin at (-30, -30); the converted hits fit inside (x in [-14.1, 17.6], y in [-16.8, 5.5]).

Pose sources, fixed: KITTI 00 uses the `VIEWPOINT` header of each frame (SemanticKITTI
poses from SuMa, not ground truth). LILocBench uses `gt_poses.txt` (ceiling AprilTag
poses of `base_link`, x, y, yaw only), linearly interpolated per beam (deskew on; no
extrapolation; a hole of more than 0.2 s drops the scan or beam and is counted), and the
laser extrinsics from `/tf` (first sample; the converter reports the spread over all
samples). Front and rear scans are paired by order of arrival; both scans of a pair are
one step. KITTI frames are fed unfiltered (no range or ego filter), as the cloud adapter
would feed them.

## 3. Labels (cell level, fixed before measuring)

### KITTI 00 (labels from `gt_cloud.pcd`; intensity 1 = dynamic, 0 = static)

A GT point is "ego" if it is dynamic and lies within `3.01 m` of the `VIEWPOINT` origin of
any of the 141 frames. A voxel (at the resolution of the condition, using the engine's own
`Voxel3DBackend::voxelId`) gets:

* `moving` (code 1): at least one dynamic GT point that is not ego.
* `ego` (code 4): contains an ego point and no other dynamic point. Excluded from every
  statistic. Counts are reported.
* otherwise, all points static: `static`, split by a height rule into
  * `static_ground` (code 2): the lowest GT point of the voxel is at most `0.30 m` above
    the lowest GT point (any label) of the 1 m x 1 m horizontal column that contains the
    voxel centre;
  * `static_structure` (code 3): all other static voxels. This is the structure proxy
    ("static excluding ground").
* all other voxels are unlabeled and not recorded.

State plainly: `static_structure` contains parked vehicles, vegetation, poles, kerbs and
building facades. These files carry no semantic classes. `moving` means "contains a GT
point labelled dynamic by the KTH extraction" (points on objects that were moving when
seen); one pedestrian and a few vehicles in this sequence. A real mover within 3.01 m of
the sensor is lost to the ego rule (conservative for the clutter side: it removes
nothing that could make clutter look more structure-like except a few voxels).

### LILocBench

Reference occupancy from `mapping`, at the resolution of the condition: the sequence is
run through `Grid2DBackend` with `W = 1`; per cell, `n` = number of steps that touched it,
`h` = number of those with a hit.

* `ref_occupied`: `n >= 10` and `h / n >= 0.5`.
* `ref_free`: `n >= 10` and `h / n <= 0.05`.
* `ref_free_near_occupied`: a `ref_free` cell within a Chebyshev distance of `0.10 m`
  (2 cells at 0.05 m, 1 cell at 0.10 m) of a `ref_occupied` cell. These are set aside
  because edge noise of walls would otherwise be counted as clutter.
* all other cells (including never touched in `mapping`): no label.

Measured groups (the same label file is applied to each sequence):

* `structure` = `ref_occupied` cells. Primary: measured on `static_0`. Also reported on
  `dynamics_0` and on `mapping` itself (self-consistency check, not evidence).
* `clutter` (table name `hit_in_free`) = `ref_free` cells that received `H >= 1` hits in
  `dynamics_0`. Primary: `dynamics_0`. Also reported on `static_0` (what the static
  sequence puts into free cells: noise, moved objects).
* `hit_near_wall` = `ref_free_near_occupied` cells with `H >= 1`: reported as a control.

Limit stated here: `clutter` is whatever hits a reference-free cell during `dynamics_0`.
That is mostly the 10 or so people, but the data have no per-cell object labels and the
group can also contain a moved object or a sensor outlier. The gap is measured against this
group as it is.

## 4. Statistics (computed by `analyze_p1.py` from the cell tables)

Per condition and label group. Cells are eligible if `N >= N_min`.

* Primary `N_min = 5`. Sensitivity `N_min = 3` and `10` are reported.
* Hit-rate histogram: 20 bins of width 0.05, `[0, 0.05) ... [0.95, 1]`.
* Percentiles of `r` (1, 5, 10, 25, 50, 75, 90, 95, 99), numpy's default linear method.
* `q_s` = 10th percentile of `r` over structure cells. `q_c` = 90th percentile of `r` over
  clutter cells. `gap = q_s - q_c`.
  * KITTI: structure = `static_structure`, clutter = `moving`. Also reported: structure =
    all static voxels; and `static_ground` alone.
  * LILocBench: structure = `structure` on `static_0`, clutter = `hit_in_free` on
    `dynamics_0`. Also reported: both groups from `dynamics_0`; and structure against the
    hits that `static_0` puts into free cells.
* Interval of the gap: 2000 bootstrap replicates, seed 20261014, 95 % percentile interval.
  Two kinds: resampling cells (independent), and resampling 10 m x 10 m ground tiles
  (blocks; the same tiles for both groups when both come from one sequence, independent
  tiles when they come from different sequences). The block interval is the one to read;
  neighbouring cells are not independent.
* Distribution of touched windows `N` per cell and label: percentiles, maximum, and the
  fraction of cells with `N >= k` for `k` in 1, 2, 3, 5, 7, 10, 14, 20, 30, 50. This answers
  whether a cell can reach a sequential threshold at all (critic section 1.1). For the
  development placeholder `(0.3, 0.6, 0.01, 0.01)` of DESIGN_AND_PLAN section 3 B an
  always-hit cell needs `ceil(ln(99) / ln(2)) = 7` windows. This number is arithmetic for
  reading the table; it is not a choice of parameters.
* Run statistics per label, over eligible cells: share of constant cells (`H = 0` or
  `H = N`); over mixed cells (`0 < H < N`): the ratio of observed to expected hit/miss
  switches (expected under a random placement of the cell's own `H` hits among its `N`
  windows, `2 H (N - H) / N`; 1 means no run structure beyond the hit rate, below 1 means
  longer runs than chance), the same ratio for hit-hit adjacencies (`H (H - 1) / N`), the
  mean and median lag-1 autocorrelation about the cell's own mean (iid reference: mean of
  `-1 / (N - 1)`), and mean, median and 90th percentile of interior run lengths (runs with
  a run on both sides), separately for hit runs and miss runs.
* Matched hit rate (evidence for or against Option C): mixed cells with `r` in
  `[0.2, 0.4)`, `[0.4, 0.6)`, `[0.6, 0.8)`; per band and label: cell count, switch ratio,
  mean lag-1 autocorrelation; and the AUC of the per-cell lag-1 autocorrelation for
  separating structure from clutter (0.5 = no information).
* Viewpoint diversity: for each cell, the greedy count of sensor positions at least 1 m
  apart (positions = origin of the first step of every window that touched the cell, taken
  in window order). Reported as percentiles and the fraction of cells with 2, 3, 5 or more.
  The order dependence of the greedy rule is stated; the count is a lower bound of the
  largest set.

## 5. Quality gates (fixed before measuring; a failing sequence is reported and excluded)

* KITTI 00 pose sanity: every frame has a finite pose with `| |q| - 1 | <= 1e-3`; no step
  between consecutive frames longer than 5 m or turning more than 10 degrees.
* KITTI 00 wall thickness: vertical planar patches (`|n_z| <= 0.25`, extent along the second
  axis at least 0.6 m) fitted to 1 m neighbourhoods (at least 50 points) of 20,000 random seeds in every fourth
  static GT point; thickness = `sqrt(12 * variance along the normal)` (width of a uniform
  slab of equal variance). Pass if the median is at most 0.20 m and the 90th percentile at
  most 0.40 m, with at least 500 accepted patches. The width in cells is reported at both
  resolutions.
* LILocBench pose sanity, per sequence: front and rear scan counts equal; largest GT gap
  at most 0.2 s; largest GT step at most 0.1 m; scans dropped for missing pose at most 1 %;
  beams dropped for missing pose at most 1 %; extrinsic spread over `/tf` zero (below
  1e-6); largest front/rear stamp difference of a pair at most 0.05 s.
* LILocBench wall thickness: straight 2D patches (neighbourhood radius 0.5 m, at least 20
  points, major-axis extent at least 0.6 m) on every fifth converted hit; same thickness
  definition; same pass rule (median at most 0.20 m, 90th percentile at most 0.40 m, at
  least 500 patches). Computed for each of the three sequences.
* Consistency rail: `observer_inconsistent = 0` and `window_origin_missing = 0` in every
  measurement meta file.

A sequence that fails is still run and reported, in a separate table marked EXCLUDED with
the reason, and does not enter the headline table.

## 6. Escalation rule (copied verbatim from DESIGN_AND_PLAN section 3 C; applied by the lead, not here)

> C. Escalation rule (written before calibration data are examined):
>    - Let `q_s` = 10th percentile of per-cell hit rate over cells labelled structure, and
>      `q_c` = 90th percentile over cells labelled clutter (per window length and
>      resolution).
>    - If `q_s - q_c >= 0.15`: Option A alone; set `m1 = q_s`, `m0 = q_c` (rounded outward
>      to 0.05).
>    - Else measure the same percentiles with the PASS-THROUGH miss definition (a miss is
>      counted only when the beam returns from beyond the cell; no-return counts as no
>      observation). If the gap then is `>= 0.15`: adopt that miss definition (it is a
>      backend change) + Option A.
>    - Else implement Option C: add a transition/run-length statistic (one byte in the
>      existing padding) and a two-state Markov likelihood ratio; evaluate A vs C on the
>      calibration split by ROC at matched latency; ship the better; report both.
>    - In every branch the overlap that remains is reported as the irreducible confusion.

The report prints a column `gap_ge_0.15` as a mechanical read-out of the first line. It
is not a decision. Since the miss definition is already pass-through (section 1), the
second branch cannot change the outcome of the first.

## 7. What counts as "the hit-rate test is not enough" (fixed now, read by the lead)

Reported per condition, in `P1_REPORT.md`, without interpretation by the probe:

1. Gap failure: `gap < 0.15` at the primary `N_min = 5` for the shipped resolution and either
   window length, for a dataset. The lead decides which window counts as the deployed one.
2. Resolution failure: the block-bootstrap 95 % interval of the gap contains values below
   0.15 even if the point estimate is above. This weakens branch one; it does not by itself
   trigger escalation.
3. Reachability failure: fewer than half of the structure cells have `N >= 7` at the
   window length in question. An SPRT with the placeholder parameters cannot decide most
   structure there, whatever the gap.
4. Support for Option C: in at least one matched-rate band with at least 100 cells in each
   group, the AUC of the lag-1 autocorrelation is at least 0.65 or at most 0.35. Evidence
   against: the AUC lies in [0.45, 0.55] in every band with at least 100 cells per group.
   In between: not decided.
5. Irreducible overlap: the share of clutter cells with `r >= q_s` and of structure cells
   with `r <= q_c`, from the histograms, is reported in every case.

## 8. Reproducibility and run protocol

* One script, `reproduce_p1.sh`, runs every step and `analyze_p1.py`; it records host,
  load before and after each step, commit, parameters, tool versions and the sha256 of every
  input file (`data_inputs.sha256`, verified by `check_realdata.sh`).
* Host budget: one single-threaded process at a time, `nice -n 19`, build with `-j1`.
* Every run is reported, including failures and reruns, with the reason.
* Bulky outputs (frame caches, label files, cell tables of millions of rows) stay under
  `~/datasets/strata/p1/`; the repository holds the summary tables, the meta files, the logs,
  the figures and `P1_REPORT.md`. `test_no_data_staged.sh` fails if a file over 1 MB or a
  data file is staged under this directory.
* The analysis is deterministic (fixed seeds). Re-running `analyze_p1.py` on the same cell
  tables must give byte-identical result files.

## 9. Written after this commit

`make_report.py` (formats the result tables into `P1_REPORT.md`; it computes no statistic),
the hook byte-comparison record, the mutation record, and the results. The code of the
probe, the readers and `analyze_p1.py` is committed before this file (commit `32bf5b6`).
A change to any of them after the first measurement is a deviation and is listed.

## 10. Not in this phase

Choosing `(m0, m1)`; implementing the SPRT or Option C; any TEST-split measurement; the
alternative miss definition (identical to today's, section 1); Periodic on real data.

## 11. Amendment 1 (2026-10-18): changes made after the first measurement and an independent review

This section was written and committed BEFORE the re-run it describes. Sections 0 to 10 above stay
as they were committed (commit `66bb076`); where they are wrong or are replaced, this section says so.
The first measurement (2026-10-14, "run A") and its re-run on 2026-10-18 used the original definitions and are
kept and reported next to the amended run ("run B"). What triggered the amendment: an independent review of the
phase 1 deliverable. The review reported four facts that the author had not checked: the LILocBench time base,
the semantic classes in the KITTI frames, the survivorship of low-N cells, and the pair-skew bound. The
reviewer's exploratory numbers (a scan-to-GT offset near -0.10 s, a class split of the KITTI structure group,
N_min survivorship) were known to the author before this text was written. They are not blind. The amendment
fixes the procedures so that the re-run does not depend on the author's choices after the fact.

### 11.1 LILocBench time base (replaces the pose timing in section 2 for run B)

Problem. Run A takes each scan's pose at the header stamp and adds `i * time_increment` per beam. Whether the
stamp marks the start or the end of the scan, and whether the GT clock agrees with the scan clock, was not
checked. If either is wrong, hits are smeared along the path and the measured `q_s` mostly measures pose-timing
error, not structure hit rate.

Procedure (fixed here; used on the CALIBRATION sequence `mapping` only; implemented as `hitrate_probe lil-offset`):

* Candidates: an anchor in `{start, end, single}` and an offset `dt` in `{-0.30, -0.29, ..., +0.30}` s (61 values).
  * `start`: beam `i` of a scan with stamp `s` and `n` beams is placed with the GT pose at `s + dt + i * time_increment`.
  * `end`: beam `i` is placed at `s + dt - (n - 1 - i) * time_increment` (the stamp marks the last beam).
  * `single`: all beams use one pose at `s + dt` (no per-beam time).
  * The laser origin used by the engine for ray marching is the pose at the time of beam 0 under the same rule.
  * The pose interpolation, the 0.2 s hole rule, the extrinsics and the invalid-beam rule are those of section 2.
* Score. Hits of one laser are placed in the map frame and counted per 0.05 m cell. `C_laser` = smallest number of
  cells that together hold at least 90 % of that laser's hits (sort cell counts descending, accumulate). Score
  `S = C_front + C_rear`. Lower is sharper. Only steps with index divisible by 10 are used (every tenth front and
  rear pair), to keep the search cheap; the choice is fixed here.
* Selection. `S_min` = the lowest score. Among candidates with `S <= 1.005 * S_min` take the one with the smallest
  `|dt|`, then the anchor in the order `start`, `end`, `single`.
* Validity. If the selected `dt` lies on the edge of the grid (`|dt| = 0.30`), or if more than 1 % of the beams of
  the selected candidate lack a pose, the procedure has failed. It is then reported and run B is not made.
* Diagnostics, reported and not used for selection: the same score for the front and the rear laser separately
  (the two optima must agree within 0.02 s, else this is reported), and the score on `static_0` and `dynamics_0` at
  the selected candidate and at `(start, 0)`.
* The selected candidate is applied to all three sequences and to every LILocBench number of run B (reference
  maps, cell tables, wall-thickness gate). Nothing else about the LILocBench pipeline changes.

The offset is a property of the recording and not of the engine. A STRATA deployment on a live robot has no GT
clock; this offset only removes a measurement error of the evaluation data.

### 11.2 KITTI 00: semantic classes (replaces the sentence "These files carry no semantic classes" in section 3)

That sentence was wrong. The `intensity` field of each KTH frame PCD holds the SemanticKITTI label as a number:
the class is `uint32(intensity) & 0xFFFF` (class codes: 10 car, 11 bicycle, 13 bus, 15 motorcycle, 16 on-rails,
18 truck, 20 other-vehicle, 40 road, 44 parking, 48 sidewalk, 49 other-ground, 50 building, 51 fence, 52 other-structure,
60 lane-marking, 70 vegetation, 71 trunk, 72 terrain, 80 pole, 81 traffic-sign, 99 other-object, 252 to 259 moving
classes, 0 unlabelled, 1 outlier). The frames concatenate to `gt_cloud.pcd` point for point (the probe checks that
every xyz is identical and aborts if not), so each GT voxel gets a class.

The class groups below are added after the measurement and after the reviewer reported a first exploratory split.
They refine `static_structure`; they do not replace it, and the headline pair stays `static_structure` against `moving`.

* Class of a voxel: the class that most points of the voxel have (ties: the smaller class code).
* Groups of `static_structure` voxels by that class:
  * `struct_solid` = `{50 building, 51 fence, 52 other-structure, 80 pole, 71 trunk}`
  * `struct_vehicle` = `{10, 11, 13, 15, 16, 18, 20}` (static, so parked)
  * `struct_vegetation` = `{70}`
  * `struct_other` = every other class (unlabelled, traffic sign, other-object, ground classes above 0.30 m, ...)
* Each group gets the same statistics as `static_structure` against `moving` (gap, intervals, touched windows).
* Consistency check, reported: the number of GT points whose dynamic flag (`intensity` 1 in the GT cloud) disagrees
  with "class 252 to 259" in the frames.

### 11.3 N_min survivorship (extends section 4)

The sensitivity set of `N_min` becomes `{2, 3, 5, 7, 10}` (it was `{3, 5, 10}`); the primary value stays 5.
A new table lists, per condition and label group: cells with `N >= 1`, cells with `N >= 5`, the share dropped by
`N < 5`, the mean per-cell hit rate of the dropped cells and of the kept cells. Reason: cells with few touched
windows are not a random subset, so `q_s` can depend on `N_min` through who survives.

### 11.4 LILocBench pose gate: the pair-skew bound (replaces one clause of section 5)

Section 5 fixes the largest front/rear stamp difference of a pair at 0.05 s. The code of run A compared with 0.03 s
and set a flag `pair_skew_ok` from it. For run B the code uses the pre-registered 0.05 s, and the meta file also
carries the flag for 0.03 s. The report prints the pose verdict for both bounds.

### 11.5 Wall-thickness gate

No change to the rule. The report states that the present gate accepts a patch on its extent along the major
axis and does not test linearity or planarity, so corners, furniture and vegetation also set the thickness; its
failure says little about pose quality and its pass or fail cannot tell a smeared wall from a non-wall patch.
A redesigned gate would be a new pre-registered item and is left to the lead.

### 11.6 What is reported

Run A (original definitions, dt = 0, `start`) and run B (amended time base, class groups, extended N_min) both
appear in `P1_REPORT.md`, with the same analysis code. Run A numbers must not be used to set `m1`. Everything not
named in this section (labels of the LILocBench reference, windows, resolutions, percentiles, bootstrap, seeds,
tile size, the escalation rule, section 7) is unchanged.

### 11.7 Amendment 1a (2026-10-18, before any run B number exists): selection tie-break

While writing the code for section 11.1 the author saw that the anchors `start` and `end` describe the same
family of timings: `start` with offset `dt` equals `end` with offset `dt + (n - 1) * time_increment`, so
the two curves of the score nearly coincide, and the rule "smallest `|dt|` first" could pick `end` with a small `dt`
instead of `start` with a larger one for no physical reason. The tie-break of section 11.1 is therefore changed:
among candidates with `S <= 1.005 * S_min`, take the first anchor in the order `start`, `end`, `single`, and
within that anchor the smaller `|dt|`. Nothing else in section 11.1 changes. The report prints the beam-0 time
offset of the selected candidate (`dt` for `start` and `single`, `dt - (n - 1) * time_increment` for `end`), so the
result reads the same under either anchor.

## 12. Amendment 2 (2026-10-18)

Phase 1b is pre-registered in `PREREG_P1B.md` (rounding definition, W = 15 condition, gates G1 and G2 replacing the
wall-thickness gate of section 5, the move of Spires Keble 02 to the CALIBRATION split, the refined-miss branch, the
mechanical 3D rule, Option C disposition, the engine floor rule). Section 7 item 4 (support for Option C) is
read as stated in `PREREG_P1B.md` section 8. Sections 0 to 11 above stay as committed.
