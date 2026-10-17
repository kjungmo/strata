## Deviations and notes, phase 1b

Everything here happened after the commit of `PREREG_P1B.md` (d343dad, 2026-10-18 07:11:31 +09:00). None of it changes a definition,
a threshold or a rule of the amendment. Times are +09:00 on 2026-10-18.

### Order of events

1. 07:04:10 stat record of every test-split file written; 07:04:53 committed (2db4f2c).
2. 07:11:31 Amendment 2 committed (d343dad).
3. 07:22:51 phase 1b code and tests committed (43fa004). First measurement step started 07:23:03 (`gate2d_mapping`).
4. Later code commits, each before the step that needed it (listed in the report, section 1): 571acf6 (07:30:04), d2e2579 (07:30:59),
   26f7df0 (07:35:53), 475f728 (07:38:59), 7f3c157 (07:41:02), and the commit that carries the results.

### Code changes after the first measurement (none changes a number of an earlier step)

1. `realdata_io.hpp`: `readPcdHeader` takes `std::istream&` instead of `std::ifstream&`, so that a PCD can be parsed from memory. No
   behaviour change; `test_realdata` (47 cases) and the hook comparison inputs are unaffected.
2. Commit 571acf6: strict check that the size of a Spires scan equals what its header says, and the exploratory options of `gate2d`
   (`--max-steps`, `--laser`; their defaults are the pre-registered gate). The three `gate2d` steps of the gate (07:23 to 07:27) ran with
   the binary of 43fa004; the steps from `equiv_kth_r0.1` on ran with the rebuilt binary. `gate2d` code on the default path is unchanged.
3. 07:34:04 the first `spires-offset` call stopped on that strict check and wrote no number: the real scans are 48-byte records
   (`FIELDS x y z _ intensity _ timestamp ring _`) followed by 3856 zero bytes. The format was inspected with a Python snippet
   (header text, a handful of records, the timestamp range of one scan, the first and last GT line; not a statistic). Commit 26f7df0
   accepts a tail of zero bytes (at most 64 KiB) and still rejects anything else. The step was started again at 07:35:54.
4. The amendment lists the expected fields as `x y z intensity timestamp ring`; the file has the same fields plus padding. No definition changes.
5. `gate-spires` got the exploratory option `--labels` (07:44), used only in `results_p1b/exploratory/`; the default path is the pre-registered gate.

### Things the amendment did not foresee

6. **The TLS area is most of the map.** 53,718,421 of 64,603,395 TLS points (83 %) lie within 30 m of the Keble 02 track; 10,884,974 were discarded
   at read time. The area rule is as pre-registered; the practical restriction is smaller than the text suggests.
7. **G2 as specified is a weak test.** It compares each return with the nearest point of the union of 60 to 90 other scans. A union of mutually
   inconsistent scans is a thick band, so every return finds a near neighbour: G2 gave medians of 1 to 2 mm (2D) and 17 mm (3D) on sequences whose
   accumulated walls are 0.17 to 0.32 m thick. G2 passed everywhere; it is reported and not changed.
8. **n_max differs from the memo.** The memo's Monte Carlo (0.024 undecided at 64 windows) lets a cell be rejected from window 1. With the engine rule of
   section 8 (reject only once n >= 24), the exact probability at 64 windows is 0.0314 for (0.15, 0.65) and the rule `<= 0.03` gives 80, not 64.
   `derive_defaults.py` computes it exactly; `test_p1b_analysis.py` checks the dynamic program against enumeration.
9. **Two scans of Keble 02 were dropped** (the last two: their reference time lies after the last GT pose; no extrapolation). 92,706 of 164,130,845 kept-range points
   (0.06 %) had no pose and were dropped one by one.
10. **The advisory replay scripts ran once at 07:30:55** from a tree where they were not yet committed (commit d2e2579 followed at 07:30:59); their output is identical
    to the advisor's. They are re-run on the final commit (see the report).
11. **Exploratory, post hoc, sets nothing:** `explore_gate2d.sh` (G1 on 300 and 900 steps, on the front and the rear laser alone), G1 and G2 on the run A caches (offset 0 instead of -0.13 s; static_0: 69 patches, median 0.228 m; the run B cache gives 391 patches, 0.171 m), and `gate-spires --labels`
    (G1 on returns inside TLS structure voxels), `explore_keble_clutter.py`. They were run after the gate verdicts were known, to help the lead decide. The pre-registered verdicts do not change.

### Added during and after the measurements

12. **A script was edited while it ran.** `reproduce_p1b.sh` got the stages `statafter analyze freeze report replay` while the `measure` stage was running (07:40 to 07:52).
    Bash reads a script as it executes it, so the shell stopped with a syntax error after `spires_measure_r0.1` had returned rc=0 (`/tmp` log: `unexpected EOF`).
    Both measure steps had finished and written their cell tables and meta files; the rest of the stage only copies the meta files, which were then copied by hand
    (the `cp` loop of the script). No number is affected.
13. **The stat record "before" was written by an earlier version of `stat_test_split.sh`** (one file; `split_before_stat.sh` then moved the Boreas lines into
    `test_split_stat_before_boreas.tsv`; the lines are unchanged). The final version writes the two files directly and was used for the "after" record.
14. **The analysis was run twice.** The first full run (commit c86a93a) printed the rounded pair `m0`, `m1` also for rows whose gap is below 0.15, where the pair is meaningless.
    The second run prints `n/a` there. All other columns are identical (checked by `cmp` after removing the last two columns).
15. **Mutation evidence in two passes.** Pass 1 (`mutation_record_pass1.md`): 68 mutants, 43 killed, 25 survived; the survivors were verdict boundaries (G1, G2), the label thresholds,
    the offset tie band, the rounding bound of `m1`, and three branches of the freeze rule. Boundary tests were added (`VerdictBoundariesAreInclusive`, `LagRangeAndCapAreInclusive`,
    `ThresholdsAreInclusive`, `QuotaEqualToTheCountKeepsEverything`, `TieBandIsInclusiveAndTheScoreThresholdToo`, a strip that is not a plane, and the Python tests for exact 0.15 gaps), then pass 2
    ran on the final tests (`mutation_record.md`). Two survivors are equivalent in practice and are marked as such in the record.
16. **Equivalence of the lean probe** was shown on KITTI 00 at both resolutions and both windows (`equivalence_kth.txt`, 4 of 4 byte-identical). The refined miss
    exists only in the lean probe, and its tables for KITTI 00 are secondary checks.
17. **Gate verdicts and the freeze.** All five calibration sequences fail G1; none fails G2. By the rule of `PREREG_P1B.md` section 3.3 the 2D pair is not frozen. By section 6 the
    3D pair is branch F, and the fallback (the 2D pair) does not exist, so 3D is not frozen either. The measured gaps would give branch F in any case for 3D (the gap is below 0.15 under both
    miss definitions). `PREREG.md` states this and prints the conditional 2D values as "NOT A FREEZE".
