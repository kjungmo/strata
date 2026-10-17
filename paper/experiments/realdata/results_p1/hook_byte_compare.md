# Hook byte comparison (E0, E1, E2, E3, E5, E6)

Observer unset and with a counting no-op observer (build switch STRATA_TEST_NOOP_OBSERVER). Harness built -j1 niced, run in a scratch directory, every seeded CSV compared with the committed results with cmp. Run on 2026-10-12 at the hook commit 166d592 (source identical). The script is byte_compare_hook.sh in this directory.

## observer unset
```
variant=unset start 2026-10-12T18:38:33+09:00 load 1.99 4.22 4.50 2/684 367408
built 2026-10-12T18:38:49+09:00
e0_calibration rc=0 18 s load 1.63 3.88 4.37 1/676 367825
e5_trajectory rc=0 120 s load 5.91 4.60 4.56 4/949 368757
e1_static_quality rc=0 0 s load 5.91 4.60 4.56 1/949 368762
e2_periodicity rc=0 0 s load 5.91 4.60 4.56 4/949 368766
e3_sensitivity rc=0 0 s load 5.91 4.60 4.56 7/949 368770
e6_default_confusion rc=0 9 s load 6.84 4.85 4.64 10/962 368800
variant=unset compared 19 csv files, 0 differ
19
sha256 of all seeded csv (name order): 5257f1901320d01e5aeca1d3c2c18d9c67d1ee91eb138296443f1e11f5cd367d
end 2026-10-12T18:41:16+09:00
```

## counting no-op observer
```
variant=noop start 2026-10-12T18:41:16+09:00 load 6.84 4.85 4.64 14/962 368851
built 2026-10-12T18:41:43+09:00
e0_calibration rc=0 23 s load 7.78 5.42 4.85 2/961 369665
strata_core test no-op observer: 1580000 touched-cell reports
e5_trajectory rc=0 124 s load 8.52 6.72 5.41 6/959 370457
strata_core test no-op observer: 427113163 touched-cell reports
e1_static_quality rc=0 0 s load 8.52 6.72 5.41 3/955 370463
strata_core test no-op observer: 2171314 touched-cell reports
e2_periodicity rc=0 0 s load 8.52 6.72 5.41 5/955 370468
strata_core test no-op observer: 40688 touched-cell reports
e3_sensitivity rc=0 1 s load 8.52 6.72 5.41 6/955 370473
strata_core test no-op observer: 345600 touched-cell reports
e6_default_confusion rc=0 8 s load 7.75 6.61 5.39 12/977 370517
strata_core test no-op observer: 42905600 touched-cell reports
variant=noop compared 19 csv files, 0 differ
19
sha256 of all seeded csv (name order): 5257f1901320d01e5aeca1d3c2c18d9c67d1ee91eb138296443f1e11f5cd367d
end 2026-10-12T18:44:19+09:00
```
