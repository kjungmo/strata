## Deviations and notes, phase 3 harness

No test evaluation has been run. Times are +09:00 on 2026-10-18. Nothing here changes an arm, a label rule, a window, a resolution or a metric of `PREREG_P3.md`.

### Order of events

1. 10:30:04 stat record of every test-split file (`test_split_stat_before*.tsv`); 10:30:34 committed (349ce30). Before it only directory listings (names, sizes) were read.
2. 10:31 to 10:33 traced runs of two committed peek scripts (7f7ddca, 8331bf9) on Boreas `boreas-2020-11-26-13-58`: header and first rows of the pose CSVs, 96 bytes of the first `.bin`, the pose rows of the first 10 frames and the GPS rows of the first 0.2 s.
3. 10:58 harness code and tests committed (6eb294f). No step before it used binaries.
4. 11:05 to 11:16 reader validations under file-open traces on the first 10 records of one test file per reader (Keble 03, Boreas 2020-11-26, KITTI 05, semi-indoor, `lt_changes_0`, `lt_changes_dynamics_0`); the Boreas pose-only overlap gate at 11:16:14 reads
   `lidar_poses.csv` and the first row of `gps_post_process.csv` of the three sessions and lists their `lidar/` directories. Every attempt is in `access_log.tsv`.
5. 11:16 to 11:50 smoke tests on CALIBRATION data (KITTI 00; `dynamics_0` with the run B cache and the `mapping` reference).
6. 11:55:00 `PREREG_P3.md` committed (f208975). 11:55:07 stat record after (`test_split_stat_after*.tsv`): no file appeared, disappeared or changed in size, mtime or ctime (the heading of `test_split_stat_compare.md` says "phase 1b" and lists the
   phase 1b allowed reads: that is the wording of `compare_stat.py`, not a statement about phase 3; the reads of phase 3 are in section 10 of `PREREG_P3.md` and in `access_log.tsv`).

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

No arm was run on a test sequence. No TLS map was read in phase 3. `eval_*` stages are written and guarded; they were exercised only against fixtures (`test_p3_guard.sh`) and, for the paths they share with the smoke tests, on calibration data. The stages `eval_spires` and `eval_boreas` (spires-tls, spires-labels, boreas-convert, tile-ground,
static-export on real data) have run on no real data; their commands were checked against the synthetic tests of the underlying subcommands only.
