# Paper: STRATA Lifelong Mapping

This directory contains the STRATA paper — the peer-reviewed writeup of the engine,
its architecture, experimental validation, and evaluation results.

## Paper Artifact

- **Manuscript**: [`strata_paper.md`](strata_paper.md) (primary, readable markdown)
  - Compiled from section files in [`sections/`](sections/) + front matter
  - Self-contained: title, author, keywords, abstract, and full body
  - Renders to PDF via [Pandoc](https://pandoc.org) (or similar tools)
- **PDF** (when generated): `strata_paper.pdf` — readable snapshot for reviews/sharing

## Structure

### `sections/`
Individual paper sections (YAML front matter + body markdown):
- `intro.md` — Problem statement, lifelong mapping challenges
- `method.md` — The LayeredMap engine, log-odds persistence, FreMEn periodicity, Schmitt-trigger graduation
- `experiments.md` — E1–E4 synthetic validation suite + results
- `related.md` — Lifelong mapping, dynamic-map, and periodicity literature
- `discussion.md` — Limitations, SLAM scope, future work

### `notes/`
Background research and design notes (not in the paper, for context):
- `core_math.md` — Log-odds math, survival decay, thresholds, Fourier basis
- `system.md` — High-level system architecture
- `experiments_summary.md` — Experiment setups, parameters, and key results
- `tests.md` — Deterministic gtests in `strata_core`
- `outline.md` — Sectional roadmap and writing notes

### `experiments/`
Reproducible evaluation harness (E1–E4 suite):
```
run_all.sh              # Build & run all experiments, write CSVs
├─ src/                 # C++ sources for e1_static_quality, e2_periodicity, e3_sensitivity, e4_throughput
├─ CMakeLists.txt       # Build configuration
├─ results/             # Output CSVs (e1_static_quality.csv, e2_*.csv, e3_sensitivity.csv, e4_throughput.csv)
└─ plot/
   ├─ run_all_plots.sh  # Regenerate all figures from CSVs
   ├─ plot_*.py         # Matplotlib scripts (one per figure)
   └─ style.py          # Shared plotting style (Okabe–Ito palette, font sizes, layout helpers)
```

Run reproduction from worktree root:
```bash
bash paper/experiments/run_all.sh  # Compiles & runs E1–E4, populates results/ CSVs
bash paper/experiments/plot/run_all_plots.sh  # Regenerates all figures from CSVs
```

### `figures/`
Generated figures (one figure per experiment evaluation axis):
- `fig_e1_f1_vs_time.pdf` — Static-set F1 vs. window (time), both backends, transient-mover density sweep
- `fig_e2_periodicity.pdf` — Periodic-class TPR/FPR and FreMEn amplitude maturity
- `fig_e3_sensitivity.pdf` — Hysteresis + graduation threshold sensitivity (flicker vs. F1)
- `fig_e4_throughput.pdf` — Per-call latency and cell-update throughput (log scale, backends × map size)
- `fig_architecture.pdf` — Block diagram (LayeredMap → backends → ROS 2 node)

See [`figures/README.md`](figures/README.md) for figure design notes and CSV sources.

### `references.bib`
24 BibTeX entries (lifelong mapping, occupancy grids, voxel maps, FreMEn periodicity, ROS 2).
Verified via primary sources (GitHub, arXiv, IEEE/ACM DL, official docs).

## Rebuild Everything

### Markdown → PDF
Requires [Pandoc](https://pandoc.org/installing.html) + LaTeX toolchain.

```bash
# (Once implemented) Pandoc template-driven build
pandoc paper/strata_paper.md \
  --template paper/template.tex \
  --bibliography paper/references.bib \
  --citeproc \
  -o paper/strata_paper.pdf
```

### Experiments → CSVs
```bash
cd /path/to/strata
bash paper/experiments/run_all.sh
```

### CSVs → Figures
```bash
bash paper/experiments/plot/run_all_plots.sh
```

All outputs land in `paper/experiments/results/` and `paper/figures/` respectively.

## Citation

See the top-level [`README.md`](../README.md) for how to cite this work if you use STRATA in academic research.
