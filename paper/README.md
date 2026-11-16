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
(`scripts/`) that the manuscript describes are those of commit `874c17d`
(2026-11-14; `strata_core` 0.2.0, with `evidence_model = legacy | sprt`). The first
public version of the manuscript (preprint v1) pinned commit `708d62f` (2026-10-22), and the
tag `paper-v2` marks that revision. Before it, the first release of the manuscript pinned commit
`312b09c` (2026-10-11, the v0.1 evidence rule). `0f5befc` (2026-10-18) added the evidence
model `sprt`. `a7559d4` (2026-10-19, phase P4a) is the last commit of the v2 work in
`strata_core/`, `strata/` and `scripts/` before the status-text commits. `5c20bba` and
`708d62f` differ from it only in the status text of the node and the comments of
`params/grid2d.yaml` and `params/voxel3d.yaml` (they state the scope of alpha and beta).
`strata_core/` and `scripts/` are identical in the three commits.

Between `708d62f` and `874c17d` four pull requests (#16 to #19) were merged; three changed code and
one only documents. The window close visits only the cells that the closing window touched (it
visited every live cell), the Fourier sums of a cell live in a pool of fixed-size slots, `Undecided`
is an alias of the class `Transient`, and three optional parameters were added (`export_undecided`,
`undecided_min_evidence`, `periodic_min_periods`) whose defaults keep the old behaviour. `MapBackend`
gained the two non-pure methods `cellCount()` and `lastWindowTouched()`. With the defaults, every
seeded result file is byte for byte the same as before; `experiments/results/*.csv` of E0-E3 and
E5-E7 did not change. The pin moved on purpose, following the procedure below: the guard,
the pin sentence, the test count (146) and the sentences on the cost of a window changed, the E4
timings were recorded again with the new tree, and the standard-library record and the reproduction
log were redone.

The harness builds only `strata_core`. Every seeded synthetic result file (E0-E3, E5
and E6 in both arms, E7, and the E4 cell counts of the sprt arm) reproduces byte for
byte from the `strata_core` tree `e01fc06` of `874c17d`, and from the tree `c780639` of `a7559d4`,
which is identical in `708d62f` (`experiments/reproduction_log.txt`, its last three lines, sha256
`07a4ce75...`). The E4 recordings of the earlier pin (tree `c780639`) are kept in
`experiments/results/e4_recordings_708d62f/`; the paper reads only the recordings of the new tree in
`experiments/results/e4_recordings/`. The real-data evaluation runs were built from the `strata_core` tree
`bf96e78`, `d8db00e` or `c780639`; their source and header trees are identical (Appendix A, Real
data), and none of them was repeated with the new tree. The calibration runs record a commit in their
run manifests and step logs (`results_p1/`, `results_p1b/`); for P1b, `results_p1b/deviations.md`
items 25 and 30 list the steps whose build tree is not recorded. After the pin only documents, the
checks of the paper, the E4 recordings, the standard-library record and the reproduction log
changed; no file in `strata_core/`, `strata/` or `scripts/` and no seeded result file changed.

`experiments/number_guard.py` compares the tree ids of the three directories with
`874c17d`. **A change to any of them fails the CI job `paper`** until the pin
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
  of the pin `708d62f` (tree `c780639`, 2026-10-22; they agreed within a factor of 1.09) are kept in
  `results/e4_recordings_708d62f/`; the tools do not read them, because the pinned tree changed.
  Three valid recordings of the pin `874c17d` (tree `e01fc06`) were taken on 2026-11-16 (load
  below 0.50 at the start, as the script requires); they disagree on the ratio between the backends by
  more than the 20% that the rule allows (at 100^2 the ratios are 7.0 to 8.7), so the manuscript reports the
  direction and the cell counts but no latency and no ratio. Earlier timings, all
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
- Release items: the local annotated tag `paper-v2` marks the revision that carries the v2
  manuscript and its reproduction scripts (the code it describes is `708d62f`, the pin of the first public version; this revision pins `874c17d` and is not tagged); the
  code URL of the paper opens `tree/paper-v2`. The tag is not pushed: pushing the branch
  `v2/evidence-sprt` and the tag is an author action, and the URL resolves only after it. `main` is still the
  v0.1 code, and the first release of the manuscript has no tag (it pinned `312b09c`). The citation audit of the keys added in v2 is
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
