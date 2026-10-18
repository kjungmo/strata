## Deviations and notes, phase 1b

Everything here happened after the commit of `PREREG_P1B.md` (d343dad, 2026-10-18 07:11:31 +09:00). None of it changes a definition,
a threshold or a rule of the amendment. Times are +09:00 on 2026-10-18.

### Order of events

1. 07:04:10 stat record of every test-split file written; 07:04:53 committed (2db4f2c).
2. 07:11:31 Amendment 2 committed (d343dad).
3. 07:22:51 phase 1b code and tests committed (43fa004). First measurement step started 07:23:03 (`gate2d_mapping`).
4. Later code commits (listed in the report, section 1; the first generated list was empty, see item 18): 571acf6 (07:30:04), d2e2579 (07:30:59),
   26f7df0 (07:35:53), 475f728 (07:38:59), 7f3c157 (07:41:02), d759627 (07:52:47, carries the results). **Corrected after the review (item 24): not every step ran on committed code.**
   The exploratory `gate-spires --labels` (log start 07:41:41) and `spires_measure_r0.1` (07:43:51 to 07:52:34) ran on a binary rebuilt at 07:41 from a tree that held the uncommitted
   `--labels` option of `p1b_probe.cpp`; that option was first committed in d759627 (07:52:47).

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
5. `gate-spires` got the exploratory option `--labels` (13 lines of `p1b_probe.cpp`, the only code difference between 7f3c157 and d759627), used only in `results_p1b/exploratory/`; the default path is
   the pre-registered gate. **Corrected (item 24):** the option existed in the working tree before 07:41:41, not at 07:44, and was committed at 07:52:47 in d759627, after the exploratory step that used it.

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

### After the Opus review of phase 1b (Amendment 3 in `PREREG_P1B.md`; no measurement re-run, no number of a table changes)

18. **The chronology list of the report was empty (finding 1, fixed).** `make_report_p1b.py` took the repository root as `prereg_dir/../..`, which is `paper/`; the pathspec became
    `paper/paper/experiments/realdata`, and the helper that runs the version-control command swallowed the error. It now raises, the root comes from `rev-parse --show-toplevel`, and an empty list is an error. Tests:
    `ReportGit` and the smoke test of the report (it now runs against the real repository). The report is regenerated; section 1 lists the commits from 2db4f2c on. The "report" step row still has no end time or rc
    (it is written while the step runs; cosmetic).
19. **The stat comparison proves non-modification only (finding 2, wording corrected).** Reading a file changes neither mtime, ctime nor size, and `atime` does not move on this mount
    (identical `atime` before and after for the Keble 02 zip, `gt-tum.txt` and the TLS PCD, which were read). The `atime` part of the advice section 7 acceptance check cannot be verified here.
    The evidence for "no other test-split file was opened" is the step logs and the committed scripts: test-split paths occur only in the stat scripts (reviewer's search). For P3 a file-open
    trace (`strace -f -e trace=openat,open`) per step is committed. `compare_stat.py`, the report (section 11) and Amendment 3 say so.
20. **`n_max` = 80 is not the lead's 64 (finding 3, open for the lead).** Already disclosed in item 8. Recomputed: P(undecided) at 64 is 0.0314 and 0.0388, at 80 it is 0.0113 and 0.0150
    (the reviewer's own dynamic program gives the same; `ReportGit.test_n_max_numbers_quoted_in_the_report` pins them). The lead records which value binds P2. The implementer does not choose.
21. **Time base of `dynamics_0` was not examined (finding 4, open for the lead).** The P1 offset tables: front optimum -0.13 s, rear optimum -0.06 s on `dynamics_0` (rear count 8904 at -0.13 s,
    7258 at -0.06 s); both lasers at -0.13 s on `mapping` and `static_0`. In P1 the per-sequence search itself selected -0.09 s for `dynamics_0` (lowest sum at -0.11 s) and run B applied -0.13 s to all three
    sequences. The reviewer's post hoc check (own uncommitted code, not reproduced by the implementer, numbers removed from the record in item 26; they must never reach the paper) indicates that the rear laser of
    `dynamics_0` is smeared at -0.13 s and that the W = 15 cell table rebuilt at -0.13 s is byte-identical to the run B cache. Consequence: the `dynamics_0` clutter cells may be partly smeared by the rear laser.
    The lead must settle the rear-laser offset (a per-laser rule, as an amendment, before the re-run) before waiving any gate for `dynamics_0`. Nothing was re-run.
22. **G1 does not measure pose smear (finding 5, open for the lead, blocks the freeze).** Verdicts stand as pre-registered. The reviewer reproduces G2 and G1 (within sampling) with independent code
    and shows that the G1 statistic is dominated by second surfaces inside "linear" patches, so the G1 failures do not measure pose smear (numbers removed, item 26). Together with item 7 (G2 cannot see smear)
    the two gates, as specified, do not test what they claim. A replacement gate is a second gate change after seeing data and the lead's decision; it needs an
    amendment with this chronology. Report section 13.3 states it, marked post hoc.
23. **The Keble 02 clutter class is mostly not clutter (finding 1; open for the lead, matters when G1 is replaced).** The labels match the spec. The support rule of `PREREG_P1B.md` section 4.6
    asks for one TLS point anywhere in the 1 m column; it does not exclude heights the TLS never reached (section 4.6 says "roofs and areas the TLS never reached are left out"). The implementer's own
    script `explore_keble_clutter_height.py` (committed; output `results_p1b/exploratory/keble_clutter_height_r0.2_W10_today.txt`) reproduces the reviewer: of 15,864 clutter cells with N >= 5, 10,196
    (64.3 %) lie more than 0.5 m above the nearest track position (above head height), 5,059 (31.9 %) in the person band (-1.8 to +0.5 m), 609 (3.8 %) below. q90 is 1.0000 in every band.
    The reviewer's 31.8 % for the person band differs from 31.9 % in the last digit (rounding of the reviewer's script or of the nearest-position rule); no conclusion depends on it. So q_c = 1.0 mostly measures static
    surfaces that the TLS missed or captured at another time. **The Keble gaps (-0.5333 and -0.4643) must not be cited as evidence that hit rates do not separate in 3D.** The reason line in `PREREG.md`
    now carries this qualifier (`freeze_prereg.py`; `PREREG.md` and `PREREG.json` regenerated by `reproduce_p1b.sh freeze`, no other value changes). No verdict changes: 3D is branch F because G1 fails and 2D is
    not frozen. A corrected clutter definition (vertical support, or a height band) would be a further dated amendment, decided by the lead before any 3D re-run. Report section 13.4 is the open item.
24. **Chronology wording was false in two places (finding 2, corrected in items 4 and 5 above).** An exploratory step ran ahead of its commit: `gate_spires_structure_filter.log` starts 07:41:41, `--labels` was first
    committed in d759627 at 07:52:47, and `spires_measure_r0.1` (07:43:51) ran on the binary rebuilt from that uncommitted tree. `spires_measure_r0.2` (start 07:40:35) ran on the binary built before it. No number is
    affected: the reviewer rebuilt at fd08285 and all 8 Keble 02 cell tables are byte-identical to the committed run; the implementer repeated the check for the four `r = 0.1` tables on a binary built from a
    clean committed tree: see the result line below this item.
25. **Build provenance (finding 3).** `run_manifest.txt` records `commit=43fa004` for the whole phase, but the steps from `equiv_kth_r0.1` on ran on binaries rebuilt at later commits (571acf6, 26f7df0, then the uncommitted tree
    of item 24), and no step log recorded the commit of its binary. Nothing is changed retroactively. From now on `reproduce_p1b.sh` writes `COMMIT` and `TREE_STATUS_PORCELAIN_LINES` into every step log, and the `build`
    stage refuses a tree with uncommitted files outside the results directories (`ALLOW_DIRTY_BUILD=1` overrides and must be disclosed). For P3 the rule is: rebuild only from a clean committed tree.
26. **R1 hygiene in the report (finding 4).** Sections 13.1 and 13.3 quoted the reviewer's uncommitted numbers next to the pre-registered values, with different baselines (the reviewer's 485 patches and 0.185 m against the
    gate's 470 and 0.1832 m on `dynamics_0`, which the reviewer reproduced exactly). Those numbers are removed from the report generator; the sections keep a pointer and say that the numbers are post hoc, uncommitted
    and must never reach the paper. Items 21 and 22 above were rewritten the same way. The reviewer's scripts are not in the repository (`/mnt/d/wsl/strata/review_p1b/` holds data only).
27. **Two tests failed outside a git checkout (finding 5).** In a `git archive` copy `Report.test_report_smoke` and `ReportGit.test_repo_root_is_the_top_level_not_paper` errored with "not a git repository". Both are now skipped
    unless the directory is inside a git work tree (`skipUnless`); `make_report_p1b.py` still fails loudly there. Checked in a `git archive` copy: see the result line below.
28. **Nits (finding 6).** (a) `laser_optima` now reads only rows with `anchor == start` (test added: an `end` row and a `single` row that would win are ignored); the table in report section 13.1 is unchanged in value.
    (b) `logs/report.log` showed the 08:48 run while the committed report came from about 09:13 without a step log; the report and `PREREG.md` are regenerated through `reproduce_p1b.sh freeze report`, so both logs are current.
