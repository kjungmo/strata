# First-run evidence (2026-10-14)

These files were kept only in a scratch directory until the independent review of phase 1. They are added
here so that the order of events can be checked. Machine specific path prefixes were replaced by
`<DATA_ROOT>`, `<REALDATA_DIR>`, `<SCRATCH>` and `~`; nothing else was changed.

| File | What it is |
|---|---|
| `*.log` | The step logs of the first full run (`reproduce_p1.sh`). The run was cut off during `lil_measure_dynamics_0_r0.05_W2`; the 0.10 m LILocBench steps never ran. The first line after `STEP` shows the start time of each step. |
| `first_run_cell_tables_sha256.txt` | sha256 of the cell tables and label files of the first run, taken on 2026-10-18 03:53 just before the full re-run. The re-run reproduced the KITTI 00 and LILocBench 0.05 m cell tables byte for byte (reviewer: 12 of 12). |
| `pre_prereg_convert_lil.sh.txt` | The author's scratch script that converted the three LILocBench bags before the pre-registration was committed (see below). |

## Order of events (all times 2026-10-14, KST)

* 09:33:43: the scratch script `convert_lil.sh` was written (file time) and ran the LILocBench conversion for `mapping`,
  `static_0` and `dynamics_0` into `datasets/strata/p1/lil`. File time of the directory `datasets/strata`: 09:33:45.
  The conversion computes the bounding box of the converted hits, pair skew and dropped-beam counts. It computes no hit rate.
* 09:36:31: the pre-registration commit `66bb076` (committer time). Its section 2 quotes the grid fit
  `x in [-14.1, 17.6], y in [-16.8, 5.5]`, which is the bounding box of the converted hits of `mapping`, rounded outward.
  So that sentence was written after a look at converted data (not at any hit statistic).
* 09:36:39: first step of the first run (`build_configure`), 8 s after the commit; the first hit-rate measurement
  (`kth_measure_r0.2_W10`) started at 09:39:46.
* The front/rear stamp bound of a pair is 0.05 s in the pre-registration (section 5) and was 0.03 s in the code.
  Measured maxima: mapping 0.042 s, static_0 0.031 s, dynamics_0 0.008 s. With 0.03 s the code flag `pair_skew_ok` is 0 for
  `mapping` and `static_0`; with the pre-registered 0.05 s the pose gate passes. The report prints both verdicts.
