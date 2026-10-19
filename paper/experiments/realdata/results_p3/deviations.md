## Deviations and notes, phase 3 harness

No test evaluation (`eval_*` stage) has been run. The only runs of an arm on test data are the Boreas feasibility probe on the first 10 frames of one session (order of events, item 7). Times are +09:00 on 2026-10-18. Nothing here changes an arm, a label rule, a window, a resolution or a metric of `PREREG_P3.md`; changes of the harness are in Amendment 3 (section 16 of `PREREG_P3.md`).

### Order of events

1. 10:30:04 stat record of every test-split file (`test_split_stat_before*.tsv`); 10:30:34 committed (349ce30). Before it only directory listings (names, sizes) were read.
2. 10:31 to 10:33 traced runs of two committed peek scripts (7f7ddca, 8331bf9) on Boreas `boreas-2020-11-26-13-58`: header and first rows of the pose CSVs, 96 bytes of the first `.bin`, the pose rows of the first 10 frames and the GPS rows of the first 0.2 s.
3. 10:58 harness code and tests committed (6eb294f). No step before it used binaries.
4. 11:05 to 11:16 reader validations under file-open traces on the first 10 records of one test file per reader (Keble 03, Boreas 2020-11-26, KITTI 05, semi-indoor, `lt_changes_0`, `lt_changes_dynamics_0`); the Boreas pose-only overlap gate at 11:16:14 reads
   `lidar_poses.csv` and the first row of `gps_post_process.csv` of the three sessions and lists their `lidar/` directories. Every attempt is in `access_log.tsv`.
5. 11:16 to 11:50 smoke tests on CALIBRATION data (KITTI 00; `dynamics_0` with the run B cache and the `mapping` reference).
6. 11:55:00 `PREREG_P3.md` committed (f208975). 11:55:07 stat record after (`test_split_stat_after*.tsv`): no file appeared, disappeared or changed in size, mtime or ctime (the heading of `test_split_stat_compare.md` says "phase 1b" and lists the
   phase 1b allowed reads: that is the wording of `compare_stat.py`, not a statement about phase 3; the reads of phase 3 are in section 10 of `PREREG_P3.md` and in `access_log.tsv`).
7. **12:11 to 12:15 Boreas feasibility probe** (`p3_feas_boreas.sh`, commits 214891e and c671879, after the commit of `PREREG_P3.md` at 11:55). `boreas-convert` of the first 10 frames of `boreas-2020-11-26-13-58` at three range windows (`feas_boreas_convert`, `_r50`, `_r25`, `_r15` in `access_log.tsv`),
   then `static-export` of the 10-frame caches with the engine: first the arms `shipped_v0_1` and `sprt` at 3 to 50 m, 0.2 m, Periodic veto on (12:11 and 12:12), then the `sprt` arm over range windows, voxel sizes and veto switch. The same 10 frames had been opened for the reader validation. No label, rate or metric was computed.
   The terminal table was not saved and most per-configuration files were overwritten (the surviving files are in `feas/original/`). This corrects the sentence "No arm was run on a test sequence" that stood at the end of this file: two arms of the engine were run on 10 test frames.
8. 12:16 Amendment 1 of `PREREG_P3.md` (Boreas configuration and measured cost); 12:19 Amendment 2 (how inputs and outputs are recorded).
9. Engine commits after the first `PREREG_P3.md`: `f382498` (12:11, loop guards), `8ccf56a` (12:31, the window count restarts at demotion), `33ef9b8` (12:52, tests). See Amendment 3, item 16.4.
10. **Review of the harness (Opus), six findings.** Resolution below. The review read the first GPS rows of the three Boreas sessions with its own code (text pose files, outside the harness trace); the traced gate reads the same files.
11. 13:34 stat record of every test-split file (`review/review_stat_before*.tsv`); 13:34 commits 95b97d9 (findings 2 and 3) and a68266b (findings 1 and 4).
12. 13:35:59 Boreas gate re-run on pose files (traced, `boreas_overlap`, binary a68266b): PASS in the file frame, ENU worst miss 0.0551 m for each session, altitude columns within centimetres. The log of the first (11:16) gate, which used the wrong frame, is kept as `logs/boreas_overlap.attempt-20261018T133559.log`; this 13:35:59 log is `logs/boreas_overlap.attempt-20261018T141925.log`; the gate ran a third time at 14:19:25 with the pinned-engine binary (item 14) and its log is `logs/boreas_overlap.log`. The three runs read the same pose files; the result is identical.
13. 13:37 to 13:39 feasibility probe repeated on the same 10 frames, with its table and meta files kept (`feas/`; `feas_rerun_boreas_convert_r*` in `access_log.tsv`). 13:41 Amendment 3 committed (f925cd2), before any evaluation.
14. 13:42 to 14:19 on the binary built from 38782a5: `check`, `unit`, `smoke_lil`, `smoke_kth`, `smoke_spires`, `boreas_gate` on the pinned engine (below).

### Defects found by the validations and smoke tests (each fixed in a commit, tests added)

1. **Validator rule too strict (Keble 03, 11:03 and 11:05 attempts).** The first scan of Keble 03 starts before the first ground-truth pose; the validator called a missing pose a reader failure. It now accepts a scan whose time lies before the first GT pose and reports it (b718e41);
   the same for `lil-validate` (7748121). A missing pose after the track start is still a failure (tested).
2. **Boreas lidar pose convention (11:08 attempt, rotation 85 degrees off).** Composing the applanix pose with `T_applanix_lidar.txt` as written does not reproduce `lidar_poses.csv`. First fix (inverse of the file rotation, right-multiplied): 1.03 degrees off. Second fix: the dataset's
   own convention `R_lidar = R_file^-1 * R_applanix`, `t = t_applanix + t_file` reproduces the first 10 frames to 0.0003 degrees and 3 mm (748d928). The physical composition would differ by up to a degree at road tilt (0.9 m at 50 m); the choice is stated in `PREREG_P3.md` section 8.1.
3. **No 50 m cut in the KTH export** (9848b41). The task text and the advice say "inside 50 m"; the KITTI 00 GT cloud holds every frame point (sum of frame `POINTS` = GT `POINTS` = 17,362,230; the first 125,883 GT points are frame 0). With the cut, "remove nothing" scored SA 98.79 instead of 100.
4. **DA = 100 is not reachable on KITTI 00** (7ed1de9). The GT static points as the map give SA 100 and DA 98.8185: 1,134 of 95,983 dynamic points have a static point within 5 cm. The self-check compares DA with an independently counted ceiling. The task text said "SA = DA = 100" for the GT cloud; that holds on
   synthetic data only.
5. **Windows to decision were counted from the start of the sequence** (smoke test: p50 31 to 35 windows for walls). Now counted from the first touch of the cell (`first_touch`; b545319); an all-hit wall gives 4 windows for the `sprt` arm in the unit test.
6. **One transient read error** on the `/mnt/d` mount (`~/datasets/strata/kth/00/pcd/004488.pcd: read error`, step `smoke_kth00_export_octomap`, 11:32, load 7). The step was repeated with the identical command and succeeded (11:37 to 11:39). The log of the failed attempt was overwritten by the repeat
   (the step runner now keeps earlier attempts as `*.attempt-<time>.log`, `runs/*.attempt-*.json`, `access/*.attempt-*.strace`; that change was made after `PREREG_P3.md` and changes no definition).
7. **Stray files from tests.** The error cases of the command line tests wrote `x` and `y` into the working directory; the tests now run inside their temporary directory (f160ad9). The stray file was removed before any traced run.
8. **Clean-tree rule.** Other agents edit other files of the same worktree (`strata_core`, `results_p2`, `mutation_p2.py`, `README.md`). The rule of `p3_traced.sh` and `reproduce_p3.sh` now covers the harness files only (p3_*, test_p3*, `reproduce_p3.sh`, `PREREG_P3.md`, the shared readers and `CMakeLists.txt`);
   binaries are built from `git archive HEAD` of the whole repository (4fda8f3 fixed the first version, which archived the subdirectory only), so a half-edited engine of another agent cannot enter a binary.
9. **A mutation run was stopped** at about 100 of 176 mutants (kill by PID) because the code had changed; its survivors (elite clamps, window count parity, calibration rotation test, composition order) are covered by new tests (a record of the complete run is `mutation_record.md`).

### Smoke tests on CALIBRATION data (in-sample, harness checks, not results)

* KITTI 00, 0.2 m, `W = 10`, Periodic veto on, final run: `all` policy SA 100.000 DA 0.000 (17,362,230 points = GT). `shipped_v0_1`: static_only SA 86.725 DA 98.235; not_rejected SA 99.992 DA 18.338. `octomap`: SA 98.454 DA 97.812 (both policies).
* LILocBench `dynamics_0` against the `mapping` reference (the sequence that defined `q_c`; false-Static on clutter 247/23,888 = 1.03 % `elite_like`, 1,285/23,888 = 5.38 % `octomap` and `shipped_v0_1`, 683/23,888 = 2.86 % `symmetric`, 257/23,888 = 1.08 % `sprt`). In-sample; not to be cited.

### Open before the first evaluation (see `PREREG_P3.md` section 12)

The `sprt` pair is not frozen; the Spires clutter class is mostly not people (Amendment 4 item 1 of `PREREG_P1B.md`); the per-laser time offset of `dynamics_0` (Amendment 3 item 3) is not examined for the lt_changes bags except as a diagnostic; the Boreas vertical offset is estimated; the Periodic veto
convention; the MTD-Map-like arm is not built.

### Not done

No arm was run on a test sequence except in the Boreas feasibility probe (item 7). No TLS map was read in phase 3. `eval_*` stages are written and guarded; they were exercised only against fixtures (`test_p3_guard.sh`) and, for the paths they share with the smoke tests, on calibration data. The stages `eval_spires` and `eval_boreas` (spires-tls, spires-labels, boreas-convert, tile-ground,
static-export on real data) have run on no real data except `boreas-convert` and `static-export` on the 10 probe frames and `arm-eval --format counts` on Keble 02 (calibration); their commands were checked against the synthetic tests of the underlying subcommands only.

### Review findings (Opus) and how each was resolved

| # | Finding | Resolution | Where |
|---|---|---|---|
| 1 | The Boreas common frame moved the 2021 sessions a second time (4.3 m and 0.94 m); the validator checked ENU on the reference session only. | Reproduced from the gate: first rows (-4.302, -0.149) and (-0.450, 0.821); worst ENU miss 0.055 m for all three sessions. Common frame = frame of the files; every session checked against the reference origin (limit 0.10 m) in the gate, the validator (`--ref` required) and the converter; no vertical offset (altitude columns agree to 0.04 m median, 0.13 m at the 90th percentile); tile ground kept as a diagnostic. Gate re-run before any Boreas run: PASS. | a68266b, 518af80; Amendment 3 item 16.1; `logs/boreas_overlap.log` |
| 2 | Spires ground (label 2) was never reported. | `p3_eval.py spires` reports labels 3 and 2 as separate blocks with their own latency (default `--structure-labels 3,2`); tests. Committed before `eval_spires`. | 95b97d9; item 16.2 |
| 3 | The Jaccard shared tiles came from the Static sets, not from the observed sets, and were pairwise. | `--observed` required; shared tiles = tiles every compared session observed (primary), tiles of the pair (secondary, `pairwise_tiles`); refuses a Static set outside its observed set; `reproduce_p3.sh` passes the `*_observed.ids` files; tests. | 95b97d9; item 16.3 |
| 4 | The engine was not pinned. | Tree hash `87bee34e3d6e7861d45eef80ad5069ff54fd12da` in the amendment; `guard_eval` refuses another tree; step logs and run records carry the tree; engine changes after the first `PREREG_P3.md` disclosed; smoke tests and the Spires cost run repeated on the pinned engine (below). | a68266b; item 16.4 |
| 5 | The access and deviation records of the Boreas probe were wrong or incomplete. | Order of events (items 7 to 14) and the "Not done" sentences corrected; the probe's table and meta files committed (`feas/`, `feas/original/`) after a repeat on the same 10 frames (cells and peak memory equal Amendment 1 in every row); the citation of a non-existent section 8.5 replaced by section 14. | 518af80, f925cd2; item 16.6 |
| 6 | KTH evaluator matches the official rule (note). | The note is added beside the KTH table sentence (item 16.5). It is the reviewer's measurement; PCL is not installed here and the script is not in the repository, so the numbers are quoted, not reproduced. | item 16.5 |

I did not disagree with a finding. One detail differs from the review text: the review asks to re-examine `tile-ground`; the altitude comparison needs no lidar content (pose files only), so the decision is made from the gate log and `tile-ground` stays as a diagnostic.

### Repeated on the pinned engine (Amendment 3, item 16.4)

Binary built from commit 38782a5 (`STRATA_CORE_TREE 87bee34e3d6e7861d45eef80ad5069ff54fd12da` in every step log of these runs). The older figures above came from the engine before `8ccf56a`.

* LILocBench `dynamics_0` against `mapping` (in-sample, not to be cited): false-Static on clutter `shipped_v0_1` 1,285 / 23,888 = 5.38 %, `octomap` 1,285 = 5.38 %, `symmetric` 683 = 2.86 %, `elite_like` 247 = 1.03 % (all four equal the earlier run); `sprt` **259** / 23,888 = 1.08 % (earlier: 257; the demotion restart moved two cells). Files: `smoke_lil_dynamics_0_*.json`.
* KITTI 00, 0.2 m, `W = 10`, veto on: every figure equals the earlier run (`all` policy SA 100.000 DA 0.000; `shipped_v0_1` static_only SA 86.725 DA 98.235, not_rejected SA 99.992 DA 18.338; `octomap` SA 98.454 DA 97.812). These arms do not use the SPRT code.
* Spires Keble 02 (calibration; `smoke_spires_*`), 0.2 m, `W = 10`: the counts table holds **195,641** eligible structure cells (label 3), **118,059** eligible ground cells (label 2) and **15,864** eligible clutter cells, equal to `PREREG.md` for labels 3 and 1. `shipped_v0_1` with the veto off: peak memory 261 MB (earlier 261 MB). `sprt` with the veto on: peak memory 1,529 MB (earlier 1.5 GB).
  Seconds: 587 s and 928 s against 5.2 and 6.2 minutes before. The host was busy (load 6 to 12; mutation runs and builds of other work ran at the same time), so these seconds are not a timing result (R6) and are only a reason to plan about 15 minutes per `sprt` Spires run on a loaded host.
  The new `spires` command printed both blocks on real counts: `sprt` ground false-reject 0.61 % (721 / 118,059), undecided 18.9 %, windows to first Static p50 8, p90 19; non-ground structure false-reject 0.79 % (1,543 / 195,641), undecided 16.6 %, p50 5, p90 15. In-sample (Keble 02 defined the calibration), not results.
* Re-pin: the binary of these repeats was built from 38782a5 (tree `87bee34e...`). At 13:56 another commit (`bb93d84`) changed `strata_core/test/test_sprt.cpp` only (`src`, `include` and `CMakeLists.txt` trees unchanged), so the whole-tree pin no longer matched HEAD; Amendment 3 item 16.7 re-pins to `d8db00efc9e04d1c1880cd55cf75bd90a018b5c0`. Binary and engine code of the repeats are the code of the new pin. The guard refuses at any further change of `strata_core`, test-only or not, until a new amendment re-pins.
* Unit tests on the final harness: 43 C++ tests, 46 Python tests, the guard test; mutation record pass 3 (`mutation_record.md`).
* Boreas gate at the same binary (14:19:25): PASS, numbers identical to item 12 (`logs/boreas_overlap.log`).

### Test-split stat record, review work

`review/review_stat_before*.tsv` (13:34:34) and `review/review_stat_after*.tsv` (taken after the last read) and their comparison `review/review_stat_compare.md`: no file appeared, disappeared or changed in size, mtime or ctime. As before, this shows only that no file was modified; the opens are in `access_log.tsv` (new rows: `boreas_overlap`, `feas_rerun_boreas_convert_r50`, `_r25`, `_r15`).

### Second review of the harness (Opus), two findings (2026-10-18, after Amendment 3)

Both findings are resolved on `v2/evidence-sprt`. I did not disagree with either. No evaluation stage has run (`results_p3/eval_start.txt` does not exist).

| # | Finding | Resolution | Where |
|---|---|---|---|
| 1 (major) | The Boreas pose rotation is the transpose of the true one (`rotationOf` = Rz Ry Rx; the lidar pose put R_f^-1 on the world side). | Reproduced, then fixed before `eval_boreas`. `rotationOf` = (Rz Ry Rx)^T (pyboreas `yawPitchRollToRot`); `lidarPoseFromApplanix` = R_a R_f, t_a + R_a t_f. Two new pose-only gate checks (forward axis follows the velocity at every heading; gyro sign) and a frame-consistency check on frames 0 and 9. Amendment 4 written and committed before the re-run; `validate_boreas`, the gate and the feasibility probe repeated. | `PREREG_P3.md` section 17; commits 420db38, a5e6e63, f260296, 69b819d |
| 2 (minor) | The primary and secondary Periodic-veto runs collide (step names, `spires_agree_*`, `finish_eval` records carry no `p$PER`). | `p$PER` in every step name that carries an arm, in the `spires_agree` prefix and JSON, and in the `finish_eval` names (`eval_lil_p1`, `eval_spires_p0`, and so on). The steps that do not depend on the veto (`lil_convert`, `lil_offset`, `spires_*_tls`, `_convert`, `_labels`) run once and are skipped on the second pass when their output exists. `test_p3_step_names.sh` checks it. | commit e879018 |

**Finding 1, what was measured.**

* Independent check of the review's pose evidence (step `r2_explore_pose`, `explore_boreas_pose.py`, traced): velocity direction + heading has 10th / 50th / 90th percentile 87.88 / 88.45 / 88.83 deg (2020-11-26), 87.69 / 88.42 / 88.82 (2021-04-08), 87.84 / 88.43 / 88.83 (2021-06-17) over 30,009 to 33,441 moving rows; the correlation of d(heading)/dt with `angvel_z` is -0.9999 in each session. The review's claim holds.
* Repeated reader validation (`results_p3/logs/validate_boreas_2020.log`, binary of a5e6e63): GPS times calibration against `lidar_poses.csv`: worst position difference **6.3e-6 m** (before: 3.2e-3 m) and worst rotation difference 0.00026 deg. The position difference fell by a factor of 500: the translation of the calibration file is rotated by the vehicle attitude, as the true composition says.
* Frame consistency, frames 0 and 9 of 2020-11-26 (moved 2.256 m, no deskew, 3 m to 50 m, 188,293 points): within one 0.1 m voxel **0.802** (rows of `lidar_poses.csv`) and 0.802 (GPS times calibration); within one 0.3 m voxel **0.951** and 0.951. The transposed rotation (old reading): 0.524 and 0.862. The review measured 0.809 / 0.95 and 0.534 / 0.864; the small differences come from the range window (the review's is not stated) and are inside the limits of Amendment 4 (0.70 and 0.92; contrast at least 0.10; here 0.278).
* Repeated gate (`results_p3/logs/boreas_overlap.log`): PASS in all three parts. Forward axis minus velocity direction (10th / 50th / 90th percentile): 1.17 / 1.54 / 2.12 deg, 1.19 / 1.58 / 2.31 deg, 1.17 / 1.57 / 2.15 deg for the three sessions; worst quadrant median 1.77, 1.77 and 1.61 deg (limit 4); gyro correlation 0.999995, 0.999997 and 0.999993 (limit 0.99). The ENU and tile figures are those of Amendment 3 (positions only, unaffected).
* Repeated feasibility probe (`results_p3/feas_a4/feas_table.txt`, same 10 frames, binary of a5e6e63): cells are about 11 % fewer and memory 8 to 10 % lower than in section 14 (3 m to 25 m, 0.2 m, veto off: 436,001 cells, 42 MB, 7.6 s; section 14: 491,113 cells, 45 MB, 10 s; 3 m to 50 m, 0.2 m, veto on: 1,087 MB; section 14: 1,212 MB). No row exceeds the host; the Boreas configuration of Amendment 1 stays. The old table is in git history and in `feas/`.
* The old pose-only reading was not used in any Boreas number that exists: no Boreas arm run, map or metric was produced (only the cost probe above).

**Tests and mutants.** 17 C++ tests of `P3Boreas` and `P3Probe.Boreas*` (new: the rotation against the pyboreas matrices, `PoseConventionCheck`, the CSV velocity columns, `FrameConsistency`, and synthetic sessions with a wrong forward axis, a flipped gyro, a velocity 10 degrees off, frames rendered through the transposed rotation, 5 % clutter, a symmetric heading, a slow drive and a GPS-only heading error). The synthetic Boreas sessions now show one fixed world to every frame and a drive whose forward axis follows the velocity.
Mutants of Amendment 4 (`a4` notes in `mutation_p3.py`): 30 mutants. The first pass killed 17 and left 13 survivors. Tests were added for 12; the gyro scale mutant is equivalent (the correlation does not depend on the scale of either series). Second pass: 29 killed, 1 equivalent.
The 94 Boreas mutants of the earlier lists, rerun on the final tests: 91 killed, 3 equivalent survivors (the same three as before). The record is `results_p3/mutation_record_r2.md`.

**Test-split stat record.** `test_split_stat_before_r2*.tsv` (14:39:57, before the first read of this work) and `test_split_stat_after_r2*.tsv` (after the last read), comparison `test_split_stat_compare_r2.md`: 6,648 files in each record; no file appeared, disappeared or changed in size, mtime or ctime. As before, this shows only that no file was modified.
The opens are in `access_log.tsv` (steps `r2_explore_pose`, `validate_boreas_2020`, `boreas_overlap`, `feas_a4_boreas_convert_r*`): the GPS and lidar-pose text files of the three Boreas sessions and the first 10 lidar frames of 2020-11-26 only; no other lidar frame of any session was opened.

### Third review of the harness (Opus), three findings (2026-10-18, before any evaluation)

I did not disagree with any finding. No evaluation stage has run (`results_p3/eval_start.txt` does not exist). The two amendments are committed before any run.

| # | Finding | Resolution | Where |
|---|---|---|---|
| 1 | The `octomap` and `elite_like` arms apply one increment per touched window, not per ray per scan, so they are not OctoMap and ELite. | Checked: `layered_map.cpp` `endWindow` adds `l_hit` or `l_miss` once per touched window; the smoke row `octomap` on KITTI 00 has SA 98.45 and DA 97.81. Amendment 6 states that every arm updates once per touched window, renames the rows "OctoMap parameters on window evidence" and "ELite factors on window evidence", and adds the words to the sentence beside each KTH table. No arm value changes. | `PREREG_P3.md` section 19 |
| 2 | Section 12 has no recorded resolution; n_max 80 against the lead's 64 is missing from it. | Amendment 7 records a decision for each of the six items (five "keep as written", item 4 settled by Amendment 3) and decides n_max 80 with the figures. `guard_eval` refuses to run without the marker line `LEAD_P3_DECISIONS=amendment-N`. The decisions are the implementer's record of the lead's order to resolve the finding without changing an arm; the lead can replace any of them by a dated amendment before the first evaluation. | `PREREG_P3.md` section 20; `reproduce_p3.sh` `guard_eval` |
| 3 | The reviewer read test-split files without strace. | Eight rows added to `access_log.tsv` (step `review3_untraced_reads`, open calls "unrecorded"). Listed below. A stat record was taken after the reads. | `access_log.tsv`; `review/review3_stat_*` |

**Reads of the reviewer (untraced).** Test split:

* Boreas `boreas-2020-11-26-13-58`: `applanix/gps_post_process.csv` (rows within 30 s of the slice start), `applanix/lidar_poses.csv`, `calib/T_applanix_lidar.txt`, lidar frames 0 and 9 (`1606417543533737.bin`, `1606417544467377.bin`).
* Spires Keble 03: `raw/lidar-clouds.zip` (central directory and scans 1 to 9), `processed/trajectory/gt-tum.txt`.
* Spires TLS map `ground_truth_map/keble-college/merged-cloud-1cm.pcd`: not opened by the harness before. The reviewer read it once, sequentially, and kept only the voxels inside the bounding box of the first 9 Keble 03 scans (about 409k voxels at 0.1 m).

The reviewer computed no arm, label, metric or rate. The purpose was a reader check against TLS. KTH 00 (calibration data) was also read; it is not test split and has no row in the access log. No configuration changed because of these reads. The TLS map is read by the `spires_*_tls` steps of `eval_spires` under strace as before.

**Test-split stat record.** `results_p3/review/review3_stat_now*.tsv` (taken after the reads) against `test_split_stat_after_r2*.tsv`: comparison `results_p3/review/review3_stat_compare.md`. 6,644 files in each record; no file appeared, disappeared or changed in size, mtime or ctime. As before, this shows only that no file was modified; it does not show that a file was not opened.

## Evaluation run (2026-10-18): deviations during the run

1. **Spires 0.1 m, 6 GB limit (first failure).** `spires_03_shipped_v0_1_r0.1_W10_p1` ended with `std::bad_alloc` at the 6 GB address-space limit after 17 minutes. The stage stopped (`must`). `PREREG_P3.md` Amendment 8 lets failed runs be listed and the stage go on, adds `P3_RESUME`, and sets the limit of the Spires 0.1 m arm runs to 9 GB. The failed attempt's log and run record are kept as `*.attempt-*`.
2. The stat record before the evaluation run is `test_split_stat_before_p3run*.tsv` (committed in 724ecf4, before the first evaluation step).
3. **Operator stop (2026-10-18, about 18:50).** After the second `bad_alloc` the script of Amendment 8 started `spires_03_shipped_v0_1_r0.1_W20_p1`. I stopped the stage by PID after 3 minutes (the step was not finished; its partial log `logs/spires_03_shipped_v0_1_r0.1_W20_p1.log` has no END line) to write Amendment 9, which lists this run as not run. This is not a result of the run.
4. **Zip64 (Keble 04, 05).** `spires_04_convert` failed on the first read of the archive (`zip64 archives are not supported`). Fixed in `p1b_io.hpp` by `PREREG_P3.md` Amendment 10 with tests and a mutation record. The failed step's log, trace and run record are kept as an attempt.
5. **Spires 0.1 m with the Periodic veto on, Keble 03 `W = 10`:** all seven arms failed with `std::bad_alloc` at the 9 GB limit (the shipped arm also at 6 GB). The later 0.1 m runs of those arms were not started (Amendment 9). The listing is `failed_steps.tsv` and `not_run_steps.tsv`.
   (The first line of `not_run_steps.tsv`, `spires_03_shipped_v0_1_r0.1_W10_p1`, is the skip of a repeat of a step that had already failed twice; the other lines are runs that were never started.)
6. **Cache read errors on Keble 04 (2026-10-19).** Three arm runs of the 0.2 m pass failed with `cache.bin: truncated frame <n>` (see `PREREG_P3.md` Amendment 11). The stage was stopped by me by PID while `spires_04_sprt_high_end_r0.2_W10_p1` was running (about 10 minutes in; no END line in its partial log) to add the repeat of a failed frame read. The failed runs and the stopped run are run again with identical commands; their first logs and run records stay as `*.attempt-*`.
7. **Run lists (2026-10-19, `PREREG_P3.md` Amendment 12).** A step that is found in `failed_steps.tsv` is failed, not not-run: the rows of the seven `spires_03_<arm>_r0.1_W10_p1` steps in `not_run_steps.tsv` at 21:44:36 and 02:10:07 are skips of repeats of steps that ran and failed (the first line is explained in item 5). The script now writes such skips to `skipped_repeat.tsv`. The Spires latency rows count all labelled cells that were ever Static, eligible or not (the LILocBench latency counts eligible cells only), and the Spires end-of-run false-Static row is not the unit of the alpha test; both are labelled in the output.
8. **Sixth review (2026-10-19, `PREREG_P3.md` Amendment 13).** The all-cells ever-Static clutter row is removed and replaced by a bound on eligible cells (it diluted the rate with ineligible cells, differently per arm). `test_p3_not_run_split.sh` and `test_p3_step_names.sh` are now registered in CMake, run in the `unit` stage and listed in the README. No run is repeated.
9. **Operator stop for Amendment 15 (2026-10-20, 2026-10-20T07:17:15+09:00).** The Spires stage with the veto off (`P3_PERIODICITY=0`) was stopped by PID at a step boundary, right after `step spires_eval_04_shipped_v0_1_r0.1_W10_p0 rc=0`, to commit Amendment 15 (report generator, two streams) and to rebuild the binary from the new HEAD. The step that had just started has a partial log without an END line; it is run again by the resume pass (`P3_RESUME=1`) with the identical command. This is not a result of that run.
