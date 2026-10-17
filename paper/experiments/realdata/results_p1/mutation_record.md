# Mutation record (phase 1 tests)

Run on 2026-10-18 at base commit 66bb076 plus the working-tree fixes listed in deviations.md. Baseline: ctest 5 of 5 pass (test_realdata and four script tests), `unittest` 20 of 20 pass. Each mutation was applied alone, the test binary rebuilt with -j1, the tests run, and the file restored. The loop was a throwaway script (sed, rebuild, ctest); it is not committed.

| id | file | change | result |
|---|---|---|---|
| M1 | strata_core/src/layered_map.cpp | observer miss flag reported as `window_misses > 0` (hit-wins rule dropped) | test_realdata fails |
| M2 | realdata/probe_core.hpp | hit count incremented on miss | test_realdata fails |
| M3 | realdata/probe_core.hpp | ground rule `<=` changed to `<` at the 0.30 m boundary | test_realdata fails |
| M4 | realdata/probe_core.hpp | percentile without linear interpolation | test_realdata fails |
| M5 | realdata/probe_core.hpp | ego radius `<=` changed to `<` | test_realdata fails |
| M6 | strata_core/src/layered_map.cpp | window index reported off by one | test_realdata fails |
| M7 | realdata/analyze_p1.py | sign of the switch term in lag-1 autocorrelation flipped | unittest fails (3 failures) |

All seven mutants were killed. After restoring, both suites pass again.
