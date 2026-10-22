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
| [`strata_paper.md`](strata_paper.md) | Markdown rendering of the manuscript, generated from the LaTeX master by [`arxiv_ref/tools/tex2md.py`](arxiv_ref/tools/tex2md.py): same text, numbers, claims and scope (figures are not reproduced) |
| [`strata_paper_KR.md`](strata_paper_KR.md) | Korean translation of the Markdown rendering (hand-translated; the number guard requires the same numerals as the English file) |
| [`arxiv_ref/NUMBERS.md`](arxiv_ref/NUMBERS.md) | Every quantitative claim and the CSV field or code line it comes from |
| [`experiments/`](experiments/) | Harness (E0 calibration, E1–E7, both evidence arms), committed results, number guard |
| [`experiments/realdata/`](experiments/realdata/) | Real-data calibration (P1, P1b), replay (P2) and test evaluation (P3): pre-registrations with amendments, result tables and JSON, reports, deviations |
| [`figures/`](figures/) | Matplotlib figures used by the repository README (the Markdown companions do not embed figures; the PDF draws them with TikZ/pgfplots) |
| [`references.bib`](references.bib) | Copy of `arxiv_ref/main.bib` for the Markdown companion |
| [`archive/`](archive/) | Drafting notes and section drafts of the first release (July 2026). **Not maintained**: their numbers and claims are superseded |

Where the companion and the manuscript differ in wording, the manuscript is
authoritative. Numbers are authoritative only in `experiments/results/*.csv`.

## Code pin and tag

The engine (`strata_core/`), the node (`strata/`) and the check scripts
(`scripts/`) that the manuscript describes are those of commit `708d62f`
(2026-10-22; `strata_core` 0.2.0, with `evidence_model = legacy | sprt`). The first
release of the manuscript pinned commit `312b09c` (2026-10-11, the v0.1 evidence
rule). `a7559d4` (2026-10-19, phase P4a) added the evidence model `sprt`. `5c20bba` and
`708d62f` differ from it only in the status text of the node and the comments of
`params/grid2d.yaml` and `params/voxel3d.yaml` (they state the scope of alpha and beta).
`strata_core/` and `scripts/` are identical in the three commits.

The harness builds only `strata_core`. Every seeded synthetic result file (E0-E3, E5
and E6 in both arms, E7, and the E4 cell counts of the sprt arm) reproduces byte for
byte from the `strata_core` tree `c780639` of `a7559d4`, which is identical at the pin
(`experiments/reproduction_log.txt`, lines of 2026-10-21 and 2026-10-22, sha256
`07a4ce75...`). The real-data evaluation runs were built from the `strata_core` tree
`bf96e78` or `c780639`; their source and header trees are identical (Appendix A, Real
data). The calibration runs record their tree in their run manifests (`results_p1/`,
`results_p1b/`). In `strata_core/`, `strata/` and `scripts/` only the node status text
and the two YAML comments changed between `a7559d4` and the pin; the results, the P3
files and the manuscript changed in that interval too. No experiment builds or reads the
status text or the comments, so the results hold for the pin. Only the manuscript was
completed after the pin.

`experiments/number_guard.py` compares the tree ids of the three directories with
`708d62f`. **A change to any of them fails the CI job `paper`** until the pin
(`PIN`, `PIN_TREES` in the guard, and the pin sentence of the paper) is moved on
purpose.

The Markdown companions (`strata_paper.md`, `strata_paper_KR.md`) were regenerated
from the final master at the end of the v2 work (P5) and describe the same pin. The
guard applies every check of the manuscript to `strata_paper.md` (nothing is deferred)
and compares the numerals of the Korean file with the English one.

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
bash paper/experiments/run_all.sh                   # build, unit tests, E0, E5, E1–E3, E6, E7, E4 cell counts
python3 paper/arxiv_ref/tools/make_artifacts.py     # tables, plot data, the E4 sentences and (via make_v2_artifacts.py) the number macros of the manuscript
python3 paper/experiments/number_guard.py           # every numeral against the CSVs and the code
# after changing the manuscript: rebuild the PDF (below), then regenerate the Markdown rendering
python3 paper/arxiv_ref/tools/tex2md.py paper/arxiv_ref paper/strata_paper.md   # reads main.aux for the numbers of sections, tables and equations
bash paper/experiments/plot/run_all_plots.sh        # Matplotlib figures (needs matplotlib and numpy)
```

- E0–E3, E5 and E6 (both arms), E7 and the sprt E4 cell counts (`e4_cells_sprt.csv`) are seeded and rewrite `experiments/results/*.csv`
  byte for byte with GCC 9.4.0 and libstdc++ (the compiler recorded in the log). `bash paper/experiments/check_reproduction.sh` runs
  the harness from a clean checkout and checks exactly that;
  `experiments/reproduction_log.txt` records the compilers it was run with.
- The result depends on the C++ standard library: with clang and libc++ seven of the
  39 seeded files differ (`experiments/check_stdlib.sh`, record in
  `results/stdlib_libcxx/summary.txt`): four of the 19 files of the legacy arm (the two
  E1 files, the E0 door file and the E6 confusion file) and three of the 20 files of the
  sprt arm and E7 (the E0 door file, the E6 confusion file and the E4 cell counts
  `e4_cells_sprt.csv`). The cause is that the standard distributions (for example
  `std::uniform_int_distribution`) are implementation-defined; E7 draws from `std::mt19937_64` directly and is identical.
- E4 reads the wall clock. `run_all.sh` only checks its cell counts
  (`experiments/check_e4_local.py`). Timings are recorded with
  `bash paper/experiments/measure_e4.sh` on a quiet host: the script refuses to
  start above a load of 0.50, pins one core and writes every repeat to
  `results/e4_recordings/`. Run it three times; `make_artifacts.py` then writes the
  E4 table, figure and sentences from the valid recordings. Three valid recordings
  were taken on 2026-10-22 (a quiet host, load below 1.0 for ten minutes and none of the
  benchmark, campaign or build processes of another project running); they agree within
  a factor of 1.09 and the manuscript reports their range. Earlier timings, all
  from a loaded host, are kept in `results/e4_exploratory/`.
- `experiments/mutation_check.py` changes every numeral of the manuscript in turn
  and reports those the guard misses (`experiments/mutation_report.txt`).
- `results/pre_fix_2026-09-28/`, `results/pre_calibration_2026-09-28/` and
  `results/pre_spending_2026-09-28/` hold the same harness run against three
  earlier engines (comparison columns of the manuscript's corrections table).
- `deploy_checks/jazzy_map_server/` holds one manual check that Jazzy's
  `nav2_map_server` loads a saved pair (not in CI).

## Not under the number guard

- The Korean companion `strata_paper_KR.md` is a hand translation. The guard requires that
  its numerals (with a decimal point or at least three digits) equal those of
  `strata_paper.md` and occur in the manuscript sources or the result files; it does not
  check the wording.
- The top-level `README.md`: the guard checks the numerals of its News, Overview,
  Evaluation and Deployment-cautions sections against the manuscript sources (versions,
  dates and interface values are listed in `README_ALLOW`); its other sections quote
  interface values and behaviour of the node that no check reads.
- Scope of alpha and beta in the code strings: the status text of the node (`evidence_status.hpp`) and the comments of `params/grid2d.yaml` and `params/voxel3d.yaml` carry the wording of Proposition 'Error bounds under independent windows' (first test of a cell history, independent windows, Periodic veto off for beta, real per-cell rates above alpha) since the pin `708d62f` (the first step was `5c20bba`; the pin adds the bound forms alpha/(1-beta) and beta/(1-alpha), the continuations of a kept periodic candidate as not covered, and the real-data clause only when the calibration label is not `user`). Next code pin, recorded only (no change this round): (i) `params/grid2d.yaml` lines 26-28 and `params/voxel3d.yaml` lines 22-24 add 'continuations of a kept periodic candidate are not covered', and `voxel3d.yaml` scopes the real-data clause to '(real 2D clutter, shipped 2D pair)'; the current text is incomplete, not false (the 3D clutter rates of 28-37% bear out 'above alpha'); (ii) `test_evidence_status.cpp` asserts that the real-data clause is present for `measured_2d` and `fallback_2d_pair` and absent for `user`, and that 'continuations of a kept periodic candidate' is present. The status text already carries the full scope. The comment on `alpha` and `beta` in `strata_core/include/strata_core/layered_map.hpp` still says only "hold for independent windows only (SPEC 3.2a)"; it is in the pinned `strata_core` tree, whose id is tied to the committed result hashes, and was not changed.
- Release items: no tag `paper-v1` exists, so the text names commit `312b09c`;
  creating the tag is an author action. The code URL opens `main`, which is still the
  v0.1 code; before submission merge the code or tag `708d62f` and name that tag in the
  pin sentence. The citation audit of the keys added in v2 is
  `CITATION_AUDIT_V2_2026-10-22.md` (in the submission-kit directory). It verified the
  Boreas, Spires, DUFOMap, BeautyMap, LILocBench and Lorden entries against Crossref.
  Only the metadata of `krajnik2014froctomap` is unverified.

## Build the PDF

```bash
cd paper/arxiv_ref
pdflatex main && pdflatex main && pdflatex main     # uses the shipped main.bbl
# after changing main.bib: pdflatex main && bibtex main && pdflatex main && pdflatex main
```

## Citation

See the top-level [`README.md`](../README.md) and [`CITATION.cff`](../CITATION.cff).
