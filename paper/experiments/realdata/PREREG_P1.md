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
