# Mutation record, phase 3 tests

Two passes of `mutation_p3.py`, each on a copy of the sources (the worktree is never touched), one textual mutant at a time. C++ mutants run `test_p3` (with its command line tests); Python mutants run `test_p3_eval.py` and `test_p3_runlog.py`.

**Pass 1** (2026-10-18 12:54, 204 mutants of the list at commit 0a9c96e plus later edits) found 13 survivors. Each was either covered by a new test or shown to be equivalent:

* covered by new tests: `convert: rotation of the points`, `tiles: share denominator`, `tile-ground: minimum points`, `tile-ground: percentile`, `cli: latency from the first touch`, `cli: static-export modes`, `cli: overlap verdict`, `cli: pose agreement bound`, `cli: ENU agreement bound`, `cli: kth range sanity`;
* equivalent, so they stay in pass 2 as survivors with the reason: `tally: first_static sentinel` and `cli: latency sentinel` (`first_static` is -1 or a window index of at least 1, never 0); `cli: lil track start` (differs only if a scan stamp plus the offset equals the first ground-truth time to the last bit; the stamps are integer seconds and nanoseconds, a test cannot set them equal).

Earlier stopped pass (176 mutants, not kept): its survivors `elite_like: clamps`, `kth: window count`, `calib: rotation test`, `compose: rotation order` were fixed by tests before pass 1.

**Pass 2** (below the pass 1 table) re-ran the 32 mutants that touch the changed tests and the newly added mutants (frame step, PCD header, tile-ground): 29 killed, 3 equivalent survivors.

Summary: pass 1 killed 191 of 204; the 13 survivors are resolved as above; pass 2 killed 29 of 32 with 3 equivalent survivors. All 57 Python mutants were killed in pass 1.

---

## Pass 1


Mutants: 204, killed: 191, survived: 13, other: 0.

| file | mutant | result |
|---|---|---|
| p3_arms.hpp | symmetric: sign of l_miss | killed |
| p3_arms.hpp | octomap: p_hit | killed |
| p3_arms.hpp | octomap: p_miss | killed |
| p3_arms.hpp | octomap: lower clamp | killed |
| p3_arms.hpp | octomap: upper clamp | killed |
| p3_arms.hpp | elite_like: hit factor | killed |
| p3_arms.hpp | elite_like: miss factor | killed |
| p3_arms.hpp | elite_like: clamps | killed |
| p3_arms.hpp | octomap: binary flag | killed |
| p3_arms.hpp | elite_like: binary flag | killed |
| p3_arms.hpp | octomap: strict occupancy threshold | killed |
| p3_arms.hpp | sprt: m0 | killed |
| p3_arms.hpp | sprt: m1 | killed |
| p3_arms.hpp | sprt_low_end: m1 | killed |
| p3_arms.hpp | sprt_high_end: m0 | killed |
| p3_arms.hpp | sprt: alpha | killed |
| p3_arms.hpp | sprt: beta | killed |
| p3_arms.hpp | sprt: n_max_undecided | killed |
| p3_arms.hpp | window length lower bound | killed |
| p3_arms.hpp | periodicity switch | killed |
| p3_arms.hpp | window length passed on | killed |
| p3_arms.hpp | decay passed on | killed |
| p3_arms.hpp | min_observations passed on | killed |
| p3_arms.hpp | prune_prob passed on | killed |
| p3_arms.hpp | demote_prob passed on | killed |
| p3_arms.hpp | graduate_prob passed on | killed |
| p3_arms.hpp | l_miss passed on | killed |
| p3_arms.hpp | l_min passed on | killed |
| p3_arms.hpp | l_max passed on | killed |
| p3_arms.hpp | sprt m0 passed on | killed |
| p3_arms.hpp | sprt n_max passed on | killed |
| p3_run.hpp | verdict: static test | killed |
| p3_run.hpp | verdict: binary arms | killed |
| p3_run.hpp | verdict: pruned means rejected | killed |
| p3_run.hpp | policy static_only | killed |
| p3_run.hpp | policy not_rejected | killed |
| p3_run.hpp | policy all | killed |
| p3_run.hpp | verdict names | killed |
| p3_run.hpp | tally: hits | killed |
| p3_run.hpp | tally: watch filter | killed |
| p3_run.hpp | tally: latency tracking switch | killed |
| p3_run.hpp | tally: first_static sentinel | SURVIVED (first_static is -1 (not yet) or a window index >= 1, never 0: `<= 0` and `< 0` select the same cells) |
| p3_run.hpp | tally: window index | killed |
| p3_run.hpp | driver: step boundary | killed |
| p3_run.hpp | driver: poll after a window | killed |
| p3_run.hpp | driver: window count | killed |
| p3_run.hpp | driver: off-grid count | killed |
| p3_run.hpp | dump: binary flag | killed |
| p3_run.hpp | dump: tally lookup | killed |
| p3_run.hpp | pcd writer: header rewrite | killed |
| p3_run.hpp | pcd writer: record size | killed |
| p3_run.hpp | pcd writer: point count | killed |
| p3_kth.hpp | kth: max_frames 0 means all | killed |
| p3_kth.hpp | kth: max_frames value | killed |
| p3_kth.hpp | kth: viewpoint required | killed |
| p3_kth.hpp | kth: integration origin | killed |
| p3_kth.hpp | kth: window count | killed |
| p3_kth.hpp | kth: range from the viewpoint | killed |
| p3_kth.hpp | kth: verdict histogram | killed |
| p3_kth.hpp | kth: policy applied | killed |
| p3_kth.hpp | kth: binary flag | killed |
| p3_kth.hpp | kth: file filter | killed |
| p3_boreas.hpp | boreas: record size | killed |
| p3_boreas.hpp | boreas: size test | killed |
| p3_boreas.hpp | boreas: finite test | killed |
| p3_boreas.hpp | boreas: column order | killed |
| p3_boreas.hpp | layout: point count | killed |
| p3_boreas.hpp | layout: non-finite share | killed |
| p3_boreas.hpp | layout: max range | killed |
| p3_boreas.hpp | layout: z range | killed |
| p3_boreas.hpp | layout: median range low | killed |
| p3_boreas.hpp | layout: median range high | killed |
| p3_boreas.hpp | layout: intensity low | killed |
| p3_boreas.hpp | layout: intensity high | killed |
| p3_boreas.hpp | layout: ring integer share | killed |
| p3_boreas.hpp | layout: ring low | killed |
| p3_boreas.hpp | layout: ring high | killed |
| p3_boreas.hpp | layout: time low | killed |
| p3_boreas.hpp | layout: time high | killed |
| p3_boreas.hpp | layout: time span | killed |
| p3_boreas.hpp | csv: time must increase | killed |
| p3_boreas.hpp | csv: window end inclusive | killed |
| p3_boreas.hpp | csv: window start inclusive | killed |
| p3_boreas.hpp | csv: origin columns | killed |
| p3_boreas.hpp | csv: time scale | killed |
| p3_boreas.hpp | csv: column count | killed |
| p3_boreas.hpp | rotation: axes of heading and pitch | killed |
| p3_boreas.hpp | rotation: sign of roll | killed |
| p3_boreas.hpp | calib: number count | killed |
| p3_boreas.hpp | calib: last row | killed |
| p3_boreas.hpp | calib: rotation test | killed |
| p3_boreas.hpp | compose: rotation order | killed |
| p3_boreas.hpp | compose: translation | killed |
| p3_boreas.hpp | ecef: polar radius | killed |
| p3_boreas.hpp | ecef: curvature radius | killed |
| p3_boreas.hpp | enu: north axis | killed |
| p3_boreas.hpp | common frame: rotation | killed |
| p3_boreas.hpp | common frame: translation sign | killed |
| p3_boreas.hpp | common frame: z offset sign | killed |
| p3_boreas.hpp | common frame: both origins needed | killed |
| p3_boreas.hpp | convert: dt of the origin | killed |
| p3_boreas.hpp | convert: r_min boundary | killed |
| p3_boreas.hpp | convert: r_max boundary | killed |
| p3_boreas.hpp | convert: per-point time | killed |
| p3_boreas.hpp | convert: dt of the points | killed |
| p3_boreas.hpp | convert: rotation of the points | SURVIVED |
| p3_boreas.hpp | frame time: microseconds | killed |
| p3_boreas.hpp | frame time: digits only | killed |
| p3_boreas.hpp | tiles: floor of x | killed |
| p3_boreas.hpp | tiles: near neighbourhood | killed |
| p3_boreas.hpp | tiles: share denominator | SURVIVED |
| p3_boreas.hpp | lidar pose: yaw shift on the world side | killed |
| p3_boreas.hpp | lidar pose: inverse of the file rotation | killed |
| p3_boreas.hpp | lidar pose: translation sign | killed |
| p3_boreas.hpp | lidar pose: translation frame | killed |
| p3_boreas.hpp | convert: calibration of the origin | killed |
| p3_boreas.hpp | convert: calibration of the points | killed |
| p3_probe.cpp | cli: validate compares the calibrated pose | killed |
| p3_probe.cpp | cli: spires scans before the track | killed |
| p3_probe.cpp | cli: spires pose check | killed |
| p3_probe.cpp | cli: lil expected scans | killed |
| p3_probe.cpp | cli: lil track start | SURVIVED |
| p3_probe.cpp | tile-ground: radius | killed |
| p3_probe.cpp | tile-ground: minimum points | SURVIVED |
| p3_probe.cpp | tile-ground: percentile | SURVIVED |
| p3_probe.cpp | tile-ground: bin centre | killed |
| p3_kth.hpp | kth: no range cut by default | killed |
| p3_kth.hpp | kth: range rule is strict | killed |
| p3_kth.hpp | kth: default range | killed |
| p3_run.hpp | tally: first touch | killed |
| p3_run.hpp | dump: first_touch column | killed |
| p3_probe.cpp | cli: latency from the first touch | SURVIVED |
| p3_probe.cpp | cli: latency counts the first window | killed |
| p3_probe.cpp | cli: flag true | killed |
| p3_probe.cpp | cli: boreas-convert reference | killed |
| p3_probe.cpp | cli: eligibility boundary | killed |
| p3_probe.cpp | cli: latency sentinel | SURVIVED |
| p3_probe.cpp | cli: dimension check | killed |
| p3_probe.cpp | cli: static-export dimension | killed |
| p3_probe.cpp | cli: static-export modes | SURVIVED |
| p3_probe.cpp | cli: overlap verdict | SURVIVED |
| p3_probe.cpp | cli: overlap uses the common frame | killed |
| p3_probe.cpp | cli: overlap common frame | killed |
| p3_probe.cpp | cli: pose agreement bound | SURVIVED |
| p3_probe.cpp | cli: ENU agreement bound | SURVIVED |
| p3_probe.cpp | cli: kth range sanity | SURVIVED |
| p3_probe.cpp | cli: kth quaternion | killed |
| p3_eval.py | matched: boundary inclusive | killed |
| p3_eval.py | matched: neighbourhood | killed |
| p3_eval.py | matched: tol must be positive | killed |
| p3_eval.py | matched: range guard | killed |
| p3_eval.py | scores: SA numerator | killed |
| p3_eval.py | scores: DA numerator | killed |
| p3_eval.py | scores: AA | killed |
| p3_eval.py | scores: HA | killed |
| p3_eval.py | scores: both classes needed | killed |
| p3_eval.py | cli: GT flag values | killed |
| p3_eval.py | wilson: denominator | killed |
| p3_eval.py | wilson: half width | killed |
| p3_eval.py | bootstrap: lower percentile | killed |
| p3_eval.py | bootstrap: upper percentile | killed |
| p3_eval.py | bootstrap: seed | killed |
| p3_eval.py | bootstrap: resample size | killed |
| p3_eval.py | bootstrap: empty denominator | killed |
| p3_eval.py | bootstrap: numerator | killed |
| p3_eval.py | wall run: cell count | killed |
| p3_eval.py | wall run: ratio | killed |
| p3_eval.py | wall run: extent | killed |
| p3_eval.py | wall run: radius | killed |
| p3_eval.py | wall run: search window | killed |
| p3_eval.py | lil: hit_in_free needs a hit | killed |
| p3_eval.py | lil: near-wall cells excluded | killed |
| p3_eval.py | lil: nmin boundary (clutter) | killed |
| p3_eval.py | lil: nmin boundary (wall) | killed |
| p3_eval.py | lil: wall runs only | killed |
| p3_eval.py | lil: false reject is Rejected | killed |
| p3_eval.py | lil: false static is Static | killed |
| p3_eval.py | lil: band lower edge | killed |
| p3_eval.py | lil: band upper edge | killed |
| p3_eval.py | lil: latency sentinel | killed |
| p3_eval.py | lil: cell centre | killed |
| p3_eval.py | lil: qualitative rejected count | killed |
| p3_eval.py | spires: eligible clutter | killed |
| p3_eval.py | spires: rejected count | killed |
| p3_eval.py | spires: eligibility in totals | killed |
| p3_eval.py | ids: x field | killed |
| p3_eval.py | ids: z field | killed |
| p3_eval.py | jaccard: union | killed |
| p3_eval.py | tiles: size | killed |
| p3_eval.py | tiles: shared tiles | killed |
| p3_eval.py | tiles: observed set of A | killed |
| p3_eval.py | selfcheck: remove nothing expects DA 0 | killed |
| p3_eval.py | selfcheck: remove nothing expects SA 100 | killed |
| p3_eval.py | selfcheck: DA against the ceiling | killed |
| p3_eval.py | selfcheck: GT static only | killed |
| p3_eval.py | selfcheck: ceiling | killed |
| p3_eval.py | shadowed: window start | killed |
| p3_eval.py | shadowed: window end | killed |
| p3_eval.py | shadowed: boundary | killed |
| p3_eval.py | lil: latency from the first touch | killed |
| p3_eval.py | lil: latency counts the first window | killed |
| p3_eval.py | zoffset: median | killed |
| p3_eval.py | zoffset: sign | killed |
| p3_eval.py | zoffset: minimum count | killed |


## Pass 2


Mutants: 32, killed: 29, survived: 3, other: 0.

| file | mutant | result |
|---|---|---|
| p3_arms.hpp | elite_like: clamps | killed |
| p3_run.hpp | tally: first_static sentinel | SURVIVED (first_static is -1 (not yet) or a window index >= 1, never 0: `<= 0` and `< 0` select the same cells) |
| p3_run.hpp | pcd writer: header rewrite | killed |
| p3_run.hpp | pcd writer: record size | killed |
| p3_run.hpp | pcd writer: point count | killed |
| p3_run.hpp | pcd writer: header padding | killed |
| p3_run.hpp | pcd writer: plain decimal count | killed |
| p3_kth.hpp | kth: window count | killed |
| p3_boreas.hpp | calib: number count | killed |
| p3_boreas.hpp | calib: last row | killed |
| p3_boreas.hpp | calib: rotation test | killed |
| p3_boreas.hpp | compose: rotation order | killed |
| p3_boreas.hpp | compose: translation | killed |
| p3_boreas.hpp | convert: rotation of the points | killed |
| p3_boreas.hpp | tiles: share denominator | killed |
| p3_probe.cpp | cli: lil track start | SURVIVED (differs only if a scan stamp plus the offset equals the first ground-truth time to the last bit; the stamps come from integer seconds and nanoseconds and cannot be set equal by a test) |
| p3_probe.cpp | tile-ground: radius | killed |
| p3_probe.cpp | tile-ground: minimum points | killed |
| p3_probe.cpp | tile-ground: percentile | killed |
| p3_probe.cpp | tile-ground: bin centre | killed |
| p3_probe.cpp | cli: latency from the first touch | killed |
| p3_probe.cpp | cli: latency counts the first window | killed |
| p3_probe.cpp | boreas-convert: frame step | killed |
| p3_probe.cpp | boreas-convert: frame step group | killed |
| p3_probe.cpp | boreas-convert: frame step bound | killed |
| p3_probe.cpp | boreas-convert: frame step meta | killed |
| p3_probe.cpp | cli: latency sentinel | SURVIVED (first_static is -1 (not yet) or a window index >= 1, never 0: `> 0` and `>= 0` select the same cells) |
| p3_probe.cpp | cli: static-export modes | killed |
| p3_probe.cpp | cli: overlap verdict | killed |
| p3_probe.cpp | cli: pose agreement bound | killed |
| p3_probe.cpp | cli: ENU agreement bound | killed |
| p3_probe.cpp | cli: kth range sanity | killed |

---

## Pass 3 (review findings 1 to 4: Boreas shared frame, Spires ground, Jaccard tiles, engine pin)

Mutants of the code added for the review findings (`mutation_p3.py`, the entries after `# ---- Boreas shared frame` and `# ---- review findings 2 and 3`) and the older mutants whose text matched the same filter. Two runs of the same runner on a copy of the sources.

* **Run 1** (50 mutants, commit a68266b plus the Python mutants): 33 killed. 17 survived. Ten of the survivors were Python mutants of `p3_eval.py` that had been listed among the C++ mutants, so they ran against the C++ tests only (a list error, fixed in the next commit; they are re-run in run 2). The other seven were real survivors of the C++ tests.
* **Tests added after run 1** (commit after 38782a5): a miss in north alone and a diagonal miss below the limit on each axis (`enu agreement: horizontal distance`), a partner in the negative neighbour cell (`altitude: neighbour cells`), the tile count of the overlap gate taken from `lidar_poses.csv` (`cli: overlap trajectory source`), and a session with no GPS row in the window (`cli: overlap frame check needs rows`).
* **Run 2** (21 mutants: the seven C++ survivors and the Python mutants of `p3_eval.py` that touch the new code): 18 killed. Three survived and are equivalent, with the reason in the table.

Result of pass 3: 50 distinct mutants, 47 killed, 3 equivalent survivors, none open.

| file | mutant | final result (run) |
|---|---|---|
| p3_boreas.hpp | altitude: boundary at the radius | killed (run 1) |
| p3_boreas.hpp | altitude: nearest row wins | killed (run 1) |
| p3_boreas.hpp | altitude: neighbour cells | killed (run 2) |
| p3_boreas.hpp | altitude: percentiles | killed (run 1) |
| p3_boreas.hpp | altitude: radius | killed (run 1) |
| p3_boreas.hpp | altitude: sign | killed (run 1) |
| p3_boreas.hpp | common frame: rotation | killed (run 1) |
| p3_boreas.hpp | common frame: z offset axis | killed (run 1) |
| p3_boreas.hpp | common frame: z offset sign | killed (run 1) |
| p3_boreas.hpp | csv: first-row east/north | killed (run 1) |
| p3_boreas.hpp | csv: lat/lon per row | killed (run 1) |
| p3_boreas.hpp | enu agreement: east difference | killed (run 1) |
| p3_boreas.hpp | enu agreement: horizontal distance | killed (run 2) |
| p3_boreas.hpp | enu agreement: limit value | killed (run 1) |
| p3_boreas.hpp | enu agreement: means | killed (run 1) |
| p3_boreas.hpp | enu agreement: north difference | killed (run 1) |
| p3_boreas.hpp | enu agreement: reference origin needed | killed (run 1) |
| p3_boreas.hpp | enu agreement: row count | killed (run 1) |
| p3_boreas.hpp | enu agreement: rows without lat/lon skipped | killed (run 1) |
| p3_boreas.hpp | enu agreement: worst is the maximum | killed (run 1) |
| p3_eval.py | jaccard: Static inside observed | killed (run 2) |
| p3_eval.py | jaccard: command line count | killed (run 2) |
| p3_eval.py | jaccard: pairwise secondary uses observed sets | killed (run 2) |
| p3_eval.py | jaccard: set count | killed (run 2) |
| p3_eval.py | jaccard: tiles come from the observed sets | killed (run 2) |
| p3_eval.py | jaccard: tiles observed by all sessions | killed (run 2) |
| p3_eval.py | jaccard: union | killed (run 2) |
| p3_eval.py | spires: eligibility in totals | killed (run 2) |
| p3_eval.py | spires: eligible clutter | killed (run 2) |
| p3_eval.py | spires: ground block by default | killed (run 2) |
| p3_eval.py | spires: ground block on the command line | killed (run 2) |
| p3_eval.py | spires: latency of unlisted labels | killed (run 2) |
| p3_eval.py | spires: latency per label | killed (run 2) |
| p3_eval.py | spires: rejected count | killed (run 2) |
| p3_probe.cpp | cli: convert meta of the frame check | killed (run 1) |
| p3_probe.cpp | cli: convert needs rows to check | SURVIVED (unreachable: `trackOf(gps)` already fails for fewer than two GPS rows in the window, and every row carries latitude and longitude when the origin is valid) (run 2) |
| p3_probe.cpp | cli: convert refuses another frame | killed (run 1) |
| p3_probe.cpp | cli: convert z offset | killed (run 1) |
| p3_probe.cpp | cli: overlap frame check against the reference | killed (run 1) |
| p3_probe.cpp | cli: overlap frame check needs rows | killed (run 2) |
| p3_probe.cpp | cli: overlap frame check of every session | killed (run 1) |
| p3_probe.cpp | cli: overlap needs both checks | killed (run 1) |
| p3_probe.cpp | cli: overlap needs the frame check | killed (run 1) |
| p3_probe.cpp | cli: overlap trajectory source | killed (run 2) |
| p3_probe.cpp | cli: overlap verdict | killed (run 1) |
| p3_probe.cpp | cli: validate compares the calibrated pose | killed (run 1) |
| p3_probe.cpp | cli: validate converts in the file frame | SURVIVED (the validator prints only counts of the converted points; a vertical shift of 1 m changes none of them) (run 2) |
| p3_probe.cpp | cli: validate frame check | killed (run 1) |
| p3_probe.cpp | cli: validate frame check rows | SURVIVED (unreachable: a valid origin needs the latitude and longitude columns, so every GPS row of the window carries them and `ea.rows > 0` equals `gps.size() > 10 n`, which is checked just before) (run 2) |
| p3_probe.cpp | cli: validate reference origin | killed (run 1) |

**Engine-pin guard** (`test_p3_guard.sh`, shell, not covered by `mutation_p3.py`): eight manual mutants of `reproduce_p3.sh` (no pin check, tree check removed, tree check inverted, loose pin pattern, first pin line wins instead of the last, start record without the tree, step log without the tree, step log without the pin).
Seven were killed by the first version of the test; `first pin wins` survived, a test with two pin lines (the later one is the pin) was added, and it is killed. Final: 8 of 8 killed.

