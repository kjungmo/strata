# Phase 1: real-data hit-rate calibration (branch `v2/evidence-sprt`)

Measures how often real structure and real clutter are hit per window, with the engine's own
window semantics, on the CALIBRATION split only (KTH KITTI 00; LILocBench `mapping`,
`static_0`, `dynamics_0`). It chooses no `(m0, m1)` and implements no SPRT.

Read first: `PREREG_P1.md` (all definitions, committed before the first measurement; section 11 is
Amendment 1, committed before the amended run), then `results_p1/P1_REPORT.md` (generated).

## Two runs

* Run A: the original definitions (LILocBench GT pose at the header stamp, KITTI without class groups).
  Kept in `results_p1/run_a_dt0/` and re-analysed with the same code as run B.
* Run B: Amendment 1. The scan-to-GT time offset of LILocBench is estimated on `mapping` by
  `hitrate_probe lil-offset`; KITTI structure voxels get class groups; the `N_min` set is {2, 3, 5, 7, 10}.
  Run B is the one to read. Run A is not a basis for `m1`.

## Branch note: the paper number guard fails on this branch

This branch adds a read-only window observer to `strata_core` (`WindowObserver`,
`LayeredMap::setWindowObserver`). That moves the code pin on purpose. `number_guard.py` still
checks the old pin and is expected to fail here until the pin is moved in phase 4. The hook has
no behaviour change: with no observer set, and with a no-op observer set, the seeded CSV outputs
of E0, E1, E2, E3, E5 and E6 are byte-identical to the committed ones (19 files; see
`results_p1/hook_byte_compare.md`).

## Files

| File | Role |
|---|---|
| `realdata_io.hpp` | Readers: binary PCD with `VIEWPOINT`, ROS 1 bag v2 (LaserScan, TFMessage), TUM poses, SE(2) interpolation, frame cache, scan placement with the time base (anchor and offset) |
| `probe_core.hpp` | Labels and class groups, per-cell recorder on the window observer, run coding, viewpoint diversity, wall thickness, time offset score and selection |
| `hitrate_probe.cpp` | Command line tool: `kth-labels kth-gate kth-measure lil-offset lil-convert lil-gate lil-reference lil-measure` |
| `analyze_p1.py` | Tables, intervals and figures from the cell tables |
| `make_report.py` | Writes `results_p1/P1_REPORT.md` from the result tables |
| `reproduce_p1.sh` | Runs every step; records host, load, commit, parameters. Logs show placeholder paths |
| `byte_compare_hook.sh` | Proves the observer hook changes no seeded output; takes the repository from its own location and a scratch directory from an argument or `$SCRATCH` |
| `check_realdata.sh`, `data_inputs.sha256` | Verifies the input files; skips cleanly when the data are absent |
| `test_realdata.cpp`, `test_analysis.py`, `test_*.sh` | Tests on tiny synthetic fixtures (no real data) |
| `results_p1/` | Result tables, meta files, logs, figures, report; `run_a_dt0/` is run A; `first_run_logs/` is the evidence of the first run |

Data never enter the repository: they live under `~/datasets/strata/` (outputs under
`~/datasets/strata/p1/`). `test_no_data_staged.sh` fails if a file over 1 MB or a data file is
staged under this directory.

## Build and test (no ROS needed)

    cmake -S paper/experiments -B /tmp/strata_p1_build -DCMAKE_BUILD_TYPE=Release
    cmake --build /tmp/strata_p1_build -j1 --target hitrate_probe test_realdata
    (cd /tmp/strata_p1_build && ctest --output-on-failure)

## Reproduce

    bash paper/experiments/realdata/reproduce_p1.sh all

Run A again (needs its caches under `p1/lil_run_a_dt0`): `LIL_TIMEBASE=run_a LIL_SUBDIR=lil_run_a_dt0 RESULTS_DIR=<dir> bash reproduce_p1.sh convert gates lil analyze`.
