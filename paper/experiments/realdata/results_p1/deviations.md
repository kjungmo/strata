## Deviations and notes

### From the first measurement (run A)

1. `analyze_p1.py` (code committed before the first measurement in 32bf5b6) was changed after the first
   measurement. Defect: it looked for the LILocBench alternative-resolution cell tables under the name
   `..._r0.1_...`, but `reproduce_p1.sh` writes `..._r0.10_...`, so the first analysis listed all six
   0.10 m LILocBench tables as "missing" (visible in `p1_inventory.csv`). Fix: the file-name token is now
   `0.10`; the label in the tables stays `0.1`. `make_report.py` had the same naming defect for the reference
   meta files and was fixed the same way. No statistic, definition or threshold changed.
2. `reproduce_p1.sh` was changed after the pre-registration commit and before the second run, and this was not listed
   before: it now sets an address-space limit per step (`ulimit -v`, `MEM_LIMIT_KB`, default 5 GB, so that a step that
   needs more fails instead of starving other jobs on the shared host) and takes `KTH_RES_LIST` and `LIL_RES_LIST` to run
   a subset of resolutions. Neither change alters a number. The changes of Amendment 1 are in item 14.
3. The first measurement run (2026-10-14, 09:36 to 10:02) was cut off during `lil_measure_dynamics_0_r0.05_W2`
   and before the 0.10 m LILocBench steps. The whole pipeline was re-run on 2026-10-18 (03:53 to 04:36 KST)
   from `reproduce_p1.sh all`. The KITTI 00 and LILocBench 0.05 m cell tables of the second run are
   byte-identical (sha256) to the first run: the probe is deterministic. The step logs and the checksums of the
   first run are now committed in `first_run_logs/` (they were kept only in a scratch directory before).
4. The "alternative miss definition" was not measured: today's definition is already pass-through only
   (PREREG_P1 section 1).
5. The published tables of run A came from a tree with 4 uncommitted files at commit 66bb076 (`tree_dirty_files=4` in
   its manifest): the analysis code of the re-run was committed only with the results in bcb2017. The reviewer
   reproduced the run A outputs at bcb2017. Run B starts from a committed tree: its manifest counts the changed files
   outside `results_p1` (`tree_dirty_files_outside_results`).

### Found by the independent review of phase 1; handled by Amendment 1 (PREREG_P1 section 11) and run B

6. **LILocBench time base.** Run A placed each scan with the GT pose at the header stamp plus `i * time_increment` per beam.
   The reviewer showed that the map is sharpest when the GT pose is taken about 0.10 s earlier, so the run A `q_s` mostly
   measures pose-timing smear. Amendment 1 fixes a search procedure on `mapping` (section 11.1, tie-break in 11.7). The
   search chose anchor `start` with offset -0.13 s. Run B uses it for all three sequences. Both runs are in the report.
   Effect on the LILocBench headline (0.05 m, W 10, N_min 5): run A `q_s` 0.339, `q_c` 0.167, gap 0.173; run B `q_s` 0.696,
   `q_c` 0.142, gap 0.554. The KITTI numbers do not depend on the time base: the 36 KITTI gap rows and 48 KITTI percentile rows that run A also has
   (N_min 3, 5, 10) are identical in run B, bootstrap intervals included. The score of the search at the
   run A setting is 38251 against 19571 at the chosen one (cells holding 90 % of the hits, `mapping`). The reviewer's own
   probe-equivalent numbers (one pose per scan) are in the review and are not reproduced here; run B uses per-beam time and
   its own procedure.
7. **Semantic classes on KITTI.** PREREG_P1 section 3 said the KTH files carry no semantic classes. That was wrong: the
   `intensity` field of every frame PCD carries the SemanticKITTI label (the frames concatenate to `gt_cloud.pcd` point for
   point, which the probe now checks). Class groups `solid`, `vehicle`, `vegetation` and `other` were added to the KITTI
   tables after the measurement (section 11.2), as a refinement of `static_structure` and not a replacement.
8. **Wall-thickness gate.** The rule is unchanged (a redesigned gate is a new pre-registered item and is left to the lead).
   The report states that the gate accepts a patch on extent alone, with no linearity or planarity test, so it cannot
   tell a smeared wall from a non-wall patch. In run B the LILocBench medians moved little (`mapping` 0.376 to 0.345 m,
   `static_0` 0.350 to 0.297 m, `dynamics_0` 0.420 to 0.403 m) while the search score halved (see the gate table and the score
   curve), which is consistent with that statement. All four gates still fail.
9. **Order of events before the first run.** The author's scratch script `convert_lil.sh` converted the three LILocBench
   bags at 09:33:43 on 2026-10-14, before the pre-registration commit at 09:36:31, into `datasets/strata/p1/lil`
   (the directory entry appeared at 09:33:45). The pre-registration quotes the bounding box of those converted hits in
   section 2 (`x in [-14.1, 17.6], y in [-16.8, 5.5]`). No hit rate had been computed; the first hit-rate step started
   at 09:39:46, after the commit. The script is in `first_run_logs/`. The pre-registration therefore was not fixed
   before any data had been processed, although it was fixed before any hit statistic existed.
10. **Pair-skew bound.** The pre-registration says 0.05 s (section 5); the code of run A used 0.03 s. The measured maxima are
    0.042 s (`mapping`) and 0.031 s (`static_0`), between the two bounds, so the code flag said `pair_skew_ok=0` while the
    pre-registered rule passes. Run B uses 0.05 s in the code and the meta file also carries the flag for 0.03 s; the report
    prints the pose verdict under both bounds for both runs.
11. **Personal paths.** Committed logs, the run manifest, the report and `byte_compare_hook.sh` contained the author's home
    directory. The scripts now take paths from the environment or arguments, and the logs show `<DATA_ROOT>`,
    `<BUILD_DIR>`, `<REALDATA_DIR>`, `<RESULTS_DIR>`. The hook byte comparison was run again with the new script.
12. **Tests.** Two of five mutants of the reviewer survived (the 90th percentile of `q_c` changed to the 80th, and the
    window-origin index +1 changed to +2). New tests: an end-to-end `run_all` test with spread-out rates and known
    `q_s` and `q_c` on KITTI-style and LILocBench-style tables, and a command line test with moving viewpoints that checks
    the diversity column; unit tests for the report verdicts. At the end: 47 gtest cases and 31 Python tests, `ctest` 5 of 5.
    `mutation_record.md` lists the 30 mutants, the survivors of the first pass included.
13. **Report gaps.** The report now states that `static` contains parked vehicles, vegetation, kerbs and unlabelled points;
    it shows the touched-window table together with the survivorship table (cells dropped by `N < 5` against kept cells)
    and the sensitivity to `N_min` (2, 3, 5, 7, 10).
14. **Changes of Amendment 1 to code and scripts** (commits after the pre-registration, listed in the amendment text):
    `hitrate_probe lil-offset`, `--anchor`, `--time-offset`, `--classes`; `probe_core.hpp` class groups and offset scoring;
    `analyze_p1.py` nested groups, survivorship table, `--skip-kth`; `reproduce_p1.sh` offset stage, `LIL_TIMEBASE`,
    `LIL_SUBDIR`, scrubbed logs, clean-tree count; `make_report.py` rewritten for run A against run B.
15. **Not measured.** The 3D half-voxel march of `Voxel3DBackend` can put a miss on wall voxels beside the endpoint at grazing
    incidence (critic section 1.10, second concern). This phase does not measure it, and it may contribute to the low
    KITTI structure tail. The miss-definition claim (a beam without a return adds no observation) is unaffected.

### Notes on the reviewer's recomputation

* The reviewer's point estimates agree with this report to the last printed digit. The block-bootstrap intervals differ in
  the last digits because the reviewer used another random number stream (1000 replicates, own seed); the analysis here uses
  2000 replicates with seed 20261014 as pre-registered. The AUC of the lag-1 autocorrelation at KITTI 0.2 m W10
  differs by 0.001 (0.337 against 0.338) through the handling of ties.
* The reviewer's per-class split was exploratory and used the majority class per voxel, as the class groups here do. The
  groups differ in detail (the `solid` group here is {building, fence, other-structure, pole, trunk}, and the split here
  is made at N_min 5 and at both resolutions and windows), so the numbers differ in the last digits from the exploratory ones.
* The run B LILocBench numbers and the class groups were produced after the review. The author recomputed `q_s` and `q_c`
  of LILocBench 0.05 m W10, of KITTI 0.2 m W10 and of the KITTI solid group with a separate script (agreement to four digits);
  no reviewer has recomputed them.
