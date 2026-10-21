# Paper: STRATA

This directory holds the STRATA manuscript, the harness that produced every number
in it, and the committed results. The manuscript is a preprint: it has not been peer
reviewed.

**Title.** STRATA: One Geometry-Free Persistence-and-Periodicity Engine for Selectable
2D/3D Lifelong LiDAR Mapping. Author: Jungmo Kang (Independent Researcher).

## What is where

| Path | What it is |
|---|---|
| [`arxiv_ref/`](arxiv_ref/) | **The manuscript** (LaTeX master: `main.tex`, `sections/`, generated `tables/` and `figures/data/`, `main.bib`, shipped `main.bbl`). Every other copy follows it. |
| [`strata_paper.pdf`](strata_paper.pdf) | PDF built from `arxiv_ref/` |
| [`strata_paper.md`](strata_paper.md) | Long-form Markdown companion: same numbers, claims and scope, more implementation detail |
| [`strata_paper_KR.md`](strata_paper_KR.md) | Korean translation of the Markdown companion |
| [`arxiv_ref/NUMBERS.md`](arxiv_ref/NUMBERS.md) | Every quantitative claim and the CSV field or code line it comes from |
| [`experiments/`](experiments/) | Harness (E0 calibration, E1–E7, both evidence arms), committed results, number guard |
| [`experiments/realdata/`](experiments/realdata/) | Real-data calibration (P1, P1b), replay (P2) and test evaluation (P3): pre-registrations with amendments, result tables and JSON, reports, deviations |
| [`figures/`](figures/) | Matplotlib figures used by the Markdown companion and the repository README |
| [`references.bib`](references.bib) | Copy of `arxiv_ref/main.bib` for the Markdown companion |
| [`archive/`](archive/) | Drafting notes and section drafts of the first release (July 2026). **Not maintained**: their numbers and claims are superseded |

Where the companion and the manuscript differ in wording, the manuscript is
authoritative. Numbers are authoritative only in `experiments/results/*.csv`.

## Code pin and tag

The engine (`strata_core/`), the node (`strata/`) and the check scripts
(`scripts/`) that the manuscript describes are those of commit `a7559d4`
(2026-10-19; `strata_core` 0.2.0, with `evidence_model = legacy | sprt`). The first
release of the manuscript pinned commit `312b09c` (2026-10-11, the v0.1 evidence
rule, tag `paper-v1`); the pin moved once, on purpose, in the v2 work (phase P4a).
The harness, the results and the manuscript were completed after `a7559d4` without
changing those three directories.

`experiments/number_guard.py` compares the tree ids of the three directories with
`a7559d4`. **A change to any of them fails the CI job `paper`** until the pin
(`PIN`, `PIN_TREES` in the guard, and the pin sentence of the paper) is moved on
purpose.

The long-form companions (`strata_paper.md`, `strata_paper_KR.md`) still describe the
code of `312b09c`. They are re-synced at the end of the v2 work; until then the guard
lists their code-state checks (pin, tree ids, test counts, standard-library record,
reproduction log) as `DEFERRED` and still checks their numbers against the result files.

## Two arms: legacy and sprt

Every seeded executable runs twice. With no argument it is the **legacy** arm: the v0.1
arithmetic with the explicit parameters of the first release (`evidence_model = legacy`);
it writes the committed `results/*.csv` byte for byte, which
`experiments/check_legacy_equivalence.sh` checks. With the argument `sprt` it is the
**sprt** arm: the same scenes with the sequential test at the shipped defaults
(`m0` 0.20, `m1` 0.70, `alpha = beta = 0.01`); it writes new files `results/*_sprt.csv`
and never overwrites a legacy file. E3 sweeps other things in the sprt arm because the
v0.1 knobs (`graduate_prob`, `demote_prob`, `survival_decay`) do not exist there. E7
(operating characteristic and autocorrelation of the test) and the E4 cell counts of the
sprt arm (`results/e4_cells_sprt.csv`; no timing) exist only for the sequential test.

## Reproduce

From the repository root, with a C++17 compiler, CMake, Eigen3 and GoogleTest
(no ROS needed):

```bash
bash paper/experiments/run_all.sh                   # build, unit tests, E0, E5, E1–E3, E6, E4 cell counts
python3 paper/arxiv_ref/tools/make_artifacts.py     # tables, plot data, the E4 sentences and (via make_v2_artifacts.py) the number macros of the manuscript
python3 paper/experiments/number_guard.py           # every numeral against the CSVs and the code
bash paper/experiments/plot/run_all_plots.sh        # Matplotlib figures (needs matplotlib and numpy)
```

- E0–E3, E5 and E6 (both arms) and E7 are seeded and rewrite `experiments/results/*.csv`
  byte for byte with GCC and libstdc++. `bash paper/experiments/check_reproduction.sh` runs
  the harness from a clean checkout and checks exactly that;
  `experiments/reproduction_log.txt` records the compilers it was run with.
- The result depends on the C++ standard library: with clang and libc++ four of the
  19 seeded files differ (`experiments/check_stdlib.sh`, record in
  `results/stdlib_libcxx/`), because `std::uniform_int_distribution` is
  implementation-defined.
- E4 reads the wall clock. `run_all.sh` only checks its cell counts
  (`experiments/check_e4_local.py`). Timings are recorded with
  `bash paper/experiments/measure_e4.sh` on a quiet host: the script refuses to
  start above a load of 0.50, pins one core and writes every repeat to
  `results/e4_recordings/`. Run it three times; `make_artifacts.py` then writes the
  E4 table, figure and sentences from the valid recordings. Earlier timings, all
  from a loaded host, are kept in `results/e4_exploratory/`.
- `experiments/mutation_check.py` changes every numeral of the manuscript in turn
  and reports those the guard misses (`experiments/mutation_report.txt`).
- `results/pre_fix_2026-09-28/`, `results/pre_calibration_2026-09-28/` and
  `results/pre_spending_2026-09-28/` hold the same harness run against three
  earlier engines (comparison columns of the manuscript's corrections table).
- `deploy_checks/jazzy_map_server/` holds one manual check that Jazzy's
  `nav2_map_server` loads a saved pair (not in CI).

## Build the PDF

```bash
cd paper/arxiv_ref
pdflatex main && pdflatex main && pdflatex main     # uses the shipped main.bbl
# after changing main.bib: pdflatex main && bibtex main && pdflatex main && pdflatex main
```

## Citation

See the top-level [`README.md`](../README.md) and [`CITATION.cff`](../CITATION.cff).
