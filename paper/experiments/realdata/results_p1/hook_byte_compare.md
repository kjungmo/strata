# Hook byte comparison (E0, E1, E2, E3, E5, E6)

Observer unset and with a counting no-op observer (build switch STRATA_TEST_NOOP_OBSERVER). Harness built -j1 niced, run in a scratch directory, every seeded CSV compared with the committed results with cmp. Run again on 2026-10-18 with the committed script `byte_compare_hook.sh` (usage: `byte_compare_hook.sh unset|noop [SCRATCH_DIR]`; it takes the repository from its own location and has no machine specific path). The hook commit 166d592 is the last change to the sources of strata_core; the combined sha256 is the same as in the first record (2026-10-12) and as in the independent review.

## observer unset
```
variant=unset start 2026-10-18T06:36:31+09:00 load 2.13 2.07 2.32 3/740 365938
built 2026-10-18T06:36:47+09:00
e0_calibration rc=0 18 s load 2.13 2.08 2.31 2/731 366860
e5_trajectory rc=0 95 s load 2.15 2.10 2.29 2/733 367334
e1_static_quality rc=0 0 s load 2.15 2.10 2.29 2/733 367342
e2_periodicity rc=0 0 s load 2.15 2.10 2.29 2/733 367346
e3_sensitivity rc=0 0 s load 2.15 2.10 2.29 4/733 367350
e6_default_confusion rc=0 7 s load 2.14 2.10 2.29 3/734 367376
variant=unset compared 19 csv files, 0 differ
19
sha256 of all seeded csv (name order): 5257f1901320d01e5aeca1d3c2c18d9c67d1ee91eb138296443f1e11f5cd367d
end 2026-10-18T06:38:47+09:00
```

## counting no-op observer
```
variant=noop start 2026-10-18T06:38:47+09:00 load 2.14 2.10 2.29 2/734 367431
built 2026-10-18T06:39:03+09:00
e0_calibration rc=0 18 s load 2.23 2.12 2.29 2/743 367907
strata_core test no-op observer: 1580000 touched-cell reports
e5_trajectory rc=0 93 s load 1.94 2.08 2.26 1/755 368344
strata_core test no-op observer: 427113163 touched-cell reports
e1_static_quality rc=0 0 s load 1.94 2.08 2.26 1/748 368349
strata_core test no-op observer: 2171314 touched-cell reports
e2_periodicity rc=0 0 s load 1.94 2.08 2.26 1/748 368354
strata_core test no-op observer: 40688 touched-cell reports
e3_sensitivity rc=0 0 s load 1.94 2.08 2.26 3/746 368362
strata_core test no-op observer: 345600 touched-cell reports
e6_default_confusion rc=0 6 s load 1.80 2.04 2.24 2/732 368406
strata_core test no-op observer: 42905600 touched-cell reports
variant=noop compared 19 csv files, 0 differ
19
sha256 of all seeded csv (name order): 5257f1901320d01e5aeca1d3c2c18d9c67d1ee91eb138296443f1e11f5cd367d
end 2026-10-18T06:41:00+09:00
```
