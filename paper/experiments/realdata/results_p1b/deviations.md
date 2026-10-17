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
11. **Exploratory, post hoc, sets nothing:** `explore_gate2d.sh` (G1 on 300 and 900 steps, on the front and the rear laser alone) and `gate-spires --labels`
    (G1 on returns inside TLS structure voxels). They were run after the gate verdicts were known, to help the lead decide. The pre-registered verdicts do not change.
