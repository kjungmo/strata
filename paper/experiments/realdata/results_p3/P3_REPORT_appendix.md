
The `strata_core` tree changed once during the evaluation (rows before and after 2026-10-19T10:54). The change is in `strata_core/test/test_sprt.cpp` only; the trees of `src` and `include` are the same (Amendment 14). The first row of the table above (one step on 2026-10-18T14:51) is the pose-only Boreas overlap gate (`boreas_overlap`), run before the first evaluation.

## 6. Chronology, deviations and access discipline

Full text of every deviation: `results_p3/deviations.md`. Short list in order:

1. 2026-10-18: LILocBench, all arms, veto on and off (`eval_lil`). No failure.
2. 2026-10-18 18:46: first `std::bad_alloc` on Keble 03 at 0.1 m (6 GB limit). Amendment 8: a failed run does not stop the stage; the limit of the 0.1 m runs is 9 GB. The seven Keble 03 runs at 0.1 m, `W = 10`, veto on, all failed at 9 GB. Amendment 9: the later 0.1 m runs of an arm that has failed are not started and are listed (section 0).
3. 2026-10-18: the zip reader did not read zip64 archives (Keble 04 and 05 are larger than 4 GB). Fixed by Amendment 10, with tests and a mutation record.
4. 2026-10-19: three Keble 04 runs failed with `cache.bin: truncated frame` on the `/mnt/d` mount. Amendment 11: a failed read of a frame is repeated up to six times. The three runs were run again with the identical command and finished (section 0, column "final state").
5. 2026-10-19: Amendments 12 and 13 (before any Spires table was written) fix the populations of the Spires latency and clutter rows.
6. 2026-10-19 to 2026-10-22: Spires with the veto on and off, Boreas, KTH with the veto on and off.
7. 2026-10-20 07:17: operator stop at a step boundary to commit Amendment 15 and to rebuild. The first rebuild ran with the resume switch set and skipped the build steps. The two streams that had started were stopped after 23 seconds, the binary was rebuilt without the resume switch (07:18 to 07:19) and the streams were started again at 07:19:31 (`deviations.md`, items 9 and 10; the attempt logs are kept).
8. 2026-10-20 07:19 to 2026-10-21 03:22: the Boreas stage and the Spires veto-off stage ran at the same time (Amendment 15, section 28.2). No step of either stream failed during the overlap.
9. 2026-10-21: KTH semi-indoor at 0.1 m with the veto on failed for all seven arms (`std::bad_alloc` at the 9 GB limit; the Spires veto-on failures at 0.1 m have the same cause). The runs were not repeated: the identical command ends the same way. With the veto off the same condition finished.
10. 2026-10-22: stat record of every test-split file after the evaluation (`test_split_stat_after*.tsv`) compared with the record before it: 6,644 files in each, none appeared, none disappeared, none changed in size, mtime or ctime. This shows only that no file was modified. Which files were opened is in `results_p3/access_log.tsv` and the strace files in `results_p3/access/`.

**Not run, by design.** The MTD-Map-like arm (section 2.4). KITTI 05 at 0.1 m (section 5.1; memory). Periodic on real data with a logged schedule, timing, Option C, the paper text (section 13).

**Not edited.** The paper. Nothing derived from LILocBench, KITTI, KTH, Spires or Boreas is in the repository except the JSON aggregates, meta files, logs and this report; maps, caches and exports are under `/mnt/d/wsl/strata/p3/` (not in the repository).

**Reproduce.** `bash reproduce_p3.sh build check unit`, then with `P3_RUN_EVALUATIONS=1`: `eval_lil eval_spires eval_kth eval_boreas` (with `P3_PERIODICITY=0` for the veto-off setting; Boreas runs with the veto off in both settings). Report: `python3 -I make_report_p3.py --results-dir results_p3 --out-dir /mnt/d/wsl/strata/p3 --out results_p3/P3_REPORT.md`.
