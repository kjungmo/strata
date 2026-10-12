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
| [`experiments/`](experiments/) | Harness (E0 calibration, E1–E5), committed results, number guard |
| [`figures/`](figures/) | Matplotlib figures used by the Markdown companion and the repository README |
| [`references.bib`](references.bib) | Copy of `arxiv_ref/main.bib` for the Markdown companion |
| [`archive/`](archive/) | Drafting notes and section drafts of the first release (July 2026). **Not maintained**: their numbers and claims are superseded |

Where the companion and the manuscript differ in wording, the manuscript is
authoritative. Numbers are authoritative only in `experiments/results/*.csv`.

## Code pin

The engine (`strata_core/`), the node (`strata/`) and the check scripts
(`scripts/`) that the manuscript describes are those of commit `312b09c`
(2026-10-11). The harness, the results and the manuscript were completed after that
commit without changing those three directories; `experiments/number_guard.py`
checks this.

## Reproduce

From the repository root, with a C++17 compiler, CMake, Eigen3 and GoogleTest
(no ROS needed):

```bash
bash paper/experiments/run_all.sh                   # build, unit tests, E0, E5, E1–E4 (about 2 to 4 minutes)
python3 paper/arxiv_ref/tools/make_artifacts.py     # tables and plot data of the manuscript
python3 paper/experiments/number_guard.py           # every quoted number against the CSVs and the code
bash paper/experiments/plot/run_all_plots.sh        # Matplotlib figures (needs matplotlib and numpy)
```

- E0–E3 and E5 are seeded and rewrite `experiments/results/*.csv` byte for byte.
  `bash paper/experiments/check_reproduction.sh` runs the harness from a clean
  checkout and checks exactly that; `experiments/reproduction_log.txt` records the
  compilers it was run with.
- E4 reads the wall clock. `run_all.sh` writes its timings to
  `experiments/build/e4_throughput_local.csv`, prints them beside the committed
  reference and fails if a cell count or byte estimate differs
  (`experiments/check_e4_local.py`). The committed reference
  (`results/e4_throughput.csv`, host in `results/e4_environment.txt`) is replaced
  only with `STRATA_E4_RECORD=1 bash paper/experiments/run_all.sh`.
- The seeded streams use the C++ standard library's random distributions, whose
  output is implementation-defined. Reproduction was checked with libstdc++ only.
- `results/pre_fix_2026-09-28/`, `results/pre_calibration_2026-09-28/` and
  `results/pre_spending_2026-09-28/` hold the same harness run against three
  earlier engines (comparison columns of the manuscript's corrections table);
  `results/pre_repeats_2026-07-03/` holds the earlier single-run E4 timings.

## Build the PDF

```bash
cd paper/arxiv_ref
pdflatex main && pdflatex main && pdflatex main     # uses the shipped main.bbl
# after changing main.bib: pdflatex main && bibtex main && pdflatex main && pdflatex main
```

## Citation

See the top-level [`README.md`](../README.md) and [`CITATION.cff`](../CITATION.cff).
