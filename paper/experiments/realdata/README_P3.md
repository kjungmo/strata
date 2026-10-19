# Phase 3 harness (branch `v2/evidence-sprt`)

## Phase 3: the test evaluation harness (`PREREG_P3.md`)

Read `PREREG_P3.md` first (arms, metrics, export policies, order, what each dataset can and cannot show; committed before any evaluation).
The harness is built and tested; the test evaluation was run on 2026-10-18 to 2026-10-2x (see `results_p3/P3_REPORT.md`, `results_p3/deviations.md`). The evaluation stages of `reproduce_p3.sh` refuse to start unless
`P3_RUN_EVALUATIONS=1` is set, `PREREG_P3.md` and the arm table are committed and unmodified, and the binary comes from HEAD.

| File | Role |
|---|---|
| `p3_arms.hpp` | the five arms (and two extra `sprt` arms) as engine parameters; the table in `PREREG_P3.md` section 2.1 is checked against it |
| `p3_run.hpp` | verdicts (Static, Undecided, Rejected), export policies, per-cell tally with windows to decision, the driver, the streaming PCD writer |
| `p3_kth.hpp` | KTH exporter: original points whose voxel is kept, per policy |
| `p3_boreas.hpp` | Boreas `.bin` reader and layout check, pose CSVs, lidar pose convention, common frame (the frame of the files, checked against WGS84 ENU for every session), deskewed conversion, 10 m tile overlap, altitude comparison |
| `p3_probe.cpp` | command line: `arms kth-validate kth-export spires-validate boreas-validate boreas-overlap boreas-convert static-export tile-ground lil-validate ref-export arm-eval` |
| `p3_eval.py` | SA/DA/AA/HA (exact radius match), LILocBench false-Static and wall-run false-reject, Spires counts, Jaccard, new-Static cells, vertical offset |
| `p3_runlog.py`, `p3_traced.sh` | run records (host, load, commit, checksums) and file-open traces with the access log |
| `reproduce_p3.sh` | stages: `build check unit validate_readers validate_boreas boreas_gate smoke_kth smoke_lil smoke_manifest statafter` and the guarded `eval_lil eval_spires eval_kth eval_boreas` |
| `test_p3.cpp`, `test_p3_eval.py`, `test_p3_runlog.py`, `test_p3_guard.sh`, `test_p3_step_names.sh`, `test_p3_not_run_split.sh` | tests on synthetic fixtures (no real data) |
| `mutation_p3.py`, `results_p3/mutation_record.md` | mutation evidence |
| `make_report_p3.py`, `test_p3_report.py` | writes `results_p3/P3_REPORT.md` from the result files (Amendment 15); tests on synthetic fixtures |
| `results_p3/` | stat records before and after, access log and traces, step logs, run records, smoke tests on CALIBRATION data, deviations |

    bash reproduce_p3.sh build check unit validate_readers
