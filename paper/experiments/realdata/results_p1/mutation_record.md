# Mutation record (phase 1 tests)

Run on 2026-10-18 at commit ad6246b plus the tests added afterwards (stride check, class mask check, `test_report.py`).
Baseline before every pass: `ctest` 5 of 5 pass (`test_realdata` with 47 gtest cases, three script tests, and the unit tests of
`analyze_p1.py` and `make_report.py`). Each mutant was applied alone by an exact one-place replacement (the replacement script
refuses to run unless the old text occurs exactly once), the C++ test binary and `hitrate_probe` were rebuilt with `-j1`
for C++ files, `ctest` was run, and the file was restored from a copy. The driver scripts are throwaway shell
scripts with the author's scratch paths and are not committed; the table below gives every replacement verbatim.

This record replaces the earlier one, which said that all mutants were killed but did not cover two lines that the
independent reviewer mutated (R1 and R2). Both were survivors of the earlier tests. They are killed now.

| id | file | change | result |
|---|---|---|---|
| M1 | strata_core/src/layered_map.cpp | `onTouchedCell(window_count_, it->first, occ, freed)` -> `... occ, e.window_misses > 0)` (hit-wins rule dropped) | killed by test_realdata |
| M2 | realdata/probe_core.hpp | `++s.n; if (hit) ++s.h;` -> `if (!hit) ++s.h;` | killed by test_realdata |
| M3 | realdata/probe_core.hpp | ground rule `- cmin <= ...ground_height_m` -> `<` | killed by test_realdata |
| M4 | realdata/probe_core.hpp | percentile without linear interpolation | killed by test_realdata |
| M5 | realdata/probe_core.hpp | ego radius `<=` -> `<` | killed by test_realdata |
| M6 | strata_core/src/layered_map.cpp | window index reported off by one | killed by test_realdata |
| M7 | realdata/analyze_p1.py | sign of the switch term in the lag-1 autocorrelation flipped | killed by unit tests of analyze_p1 |
| R1 | realdata/analyze_p1.py | `float(np.percentile(rc_, 90))` -> `80` (the headline `q_c`) | **survived the earlier tests**; killed by the end-to-end test with spread-out rates |
| R2 | realdata/hitrate_probe.cpp | `setWindowOrigin(b.layered().windowCount() + 1, ...)` -> `+ 2` | **survived the earlier tests**; killed by the command line test with moving viewpoints (div column) |
| R3 | realdata/realdata_io.hpp | per-beam time `stamp + dt + i * ti` -> `- i * ti` (deskew sign) | killed by test_realdata |
| R4 | realdata/probe_core.hpp | reference occupancy `f >= occupied_frac` -> `>` | killed by test_realdata |
| R5 | realdata/analyze_p1.py | `lil_classify`: `h >= 1` -> `h >= 0` | killed by unit tests of analyze_p1 |
| N1 | realdata/realdata_io.hpp | end anchor `(n - 1 - i)` -> `(n - i)` | killed by test_realdata |
| N2 | realdata/probe_core.hpp | offset selection: 0.5 % tie band removed | killed by test_realdata |
| N3 | realdata/probe_core.hpp | class 71 (trunk) no longer in the solid group | killed by test_realdata |
| N4 | realdata/probe_core.hpp | class mask `0xFFFF` -> `0xFF` | **survived the first pass** (all test classes were below 256); killed after the test for class 259 was added |
| N5 | realdata/analyze_p1.py | survivorship: `n >= n_min` -> `n > n_min` | killed by unit tests of analyze_p1 |
| N6 | realdata/probe_core.hpp | cells holding a fraction: `acc >= need` -> `>` | killed by test_realdata |
| N7 | realdata/analyze_p1.py | class group bits `lab >> 4` -> `lab >> 3` | killed by unit tests of analyze_p1 |
| N8 | realdata/hitrate_probe.cpp | the 0.03 s skew flag compares with the 0.05 s bound | killed by test_realdata (command line test) |
| N9 | realdata/probe_core.hpp | selection: anchor order reversed | killed by test_realdata |
| N10 | realdata/probe_core.hpp | edge-of-grid rule `>= edge` -> `> edge + 1e-9` | killed by test_realdata |
| N11 | realdata/probe_core.hpp | dropped-beam limit 0.01 -> 0.02 | killed by test_realdata |
| N12 | realdata/probe_core.hpp | majority class tie to the larger code | killed by test_realdata |
| N13 | realdata/probe_core.hpp | offset search ignores the stride | killed by test_realdata (hit count with stride 2) |
| N14 | realdata/analyze_p1.py | a cell with several groups only joins the first | killed by unit tests of analyze_p1 |
| N15 | realdata/analyze_p1.py | `N_MIN_SENSITIVITY` without 7 | killed by unit tests of analyze_p1 |
| N16 | realdata/make_report.py | pose verdict: skew `>` bound -> `>=` | killed by unit tests of make_report |
| N17 | realdata/make_report.py | thickness gate p90 `<=` 0.40 -> `<` | killed by unit tests of make_report |
| N18 | realdata/probe_core.hpp | `cellKey2d`: floor -> truncation | killed by test_realdata |

30 mutants: 28 killed in the first pass, N4 killed after one added assertion, N17 not applied in the first pass because
its replacement text was not unique (a corrected replacement was applied in the second pass and killed).
The python mutants M7, R1, R5, N5, N7, N14, N15 and N16 were run again against a clean build in a second pass, because in
the first pass they ran against a test binary left over from the previous C++ mutant; all were killed again by the unit tests of
the Python files. After the last mutant every file was restored and `ctest` passed 5 of 5.

Mutants not applied because they are equivalent (the result does not change): `per_beam = opt.deskew && opt.anchor != Anchor::Single`
without the second clause gives the same positions, because `beamTime` already returns the same time for `Single`.
