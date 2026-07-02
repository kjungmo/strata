"""
Shared plotting style/helpers for the strata_core paper figures.

Colorblind-safe palette (Okabe & Ito, 2008), consistent font sizes, and a
save_fig() helper that always writes both a .pdf and a .png into
paper/figures/. Every figure script in this directory imports this module
instead of re-declaring style.
"""
import csv
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# --- Okabe-Ito colorblind-safe categorical palette -------------------------
BLACK = "#000000"
ORANGE = "#E69F00"
SKY_BLUE = "#56B4E9"
GREEN = "#009E73"
YELLOW = "#F0E442"
BLUE = "#0072B2"
VERMILLION = "#D55E00"
PURPLE = "#CC79A7"

# Ordered list for cycling through categorical series.
PALETTE = [BLUE, VERMILLION, GREEN, ORANGE, PURPLE, SKY_BLUE, BLACK, YELLOW]

# --- Shared rcParams ---------------------------------------------------------
FONT_SIZE = 10
plt.rcParams.update({
    "font.size": FONT_SIZE,
    "axes.titlesize": FONT_SIZE + 1,
    "axes.labelsize": FONT_SIZE,
    "xtick.labelsize": FONT_SIZE - 1,
    "ytick.labelsize": FONT_SIZE - 1,
    "legend.fontsize": FONT_SIZE - 1,
    "legend.title_fontsize": FONT_SIZE - 1,
    "figure.titlesize": FONT_SIZE + 2,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.grid": True,
    "grid.linewidth": 0.4,
    "grid.alpha": 0.4,
    "axes.axisbelow": True,
    "savefig.dpi": 300,
    "figure.dpi": 150,
    "pdf.fonttype": 42,
    "ps.fonttype": 42,
})

# --- Paths --------------------------------------------------------------
_HERE = os.path.dirname(os.path.abspath(__file__))
PAPER_DIR = os.path.dirname(os.path.dirname(_HERE))  # .../paper
RESULTS_DIR = os.path.join(PAPER_DIR, "experiments", "results")
FIGURES_DIR = os.path.join(PAPER_DIR, "figures")


def result_path(name):
    return os.path.join(RESULTS_DIR, name)


def read_csv(name):
    """Read a results CSV of the form:
        header row
        '#seed,<seed>,...'   <- comment/provenance row, skipped
        data rows...
    Returns (header: list[str], rows: list[list[str]]) with all-string cells;
    callers cast the columns they need.
    """
    path = name if os.path.isabs(name) else result_path(name)
    with open(path, newline="") as f:
        all_rows = list(csv.reader(f))
    header = all_rows[0]
    rows = [r for r in all_rows[1:] if r and not r[0].startswith("#")]
    return header, rows


def col(header, rows, name, cast=str):
    """Extract one named column from rows as a list, cast to `cast`."""
    i = header.index(name)
    return [cast(r[i]) for r in rows]


def save_fig(fig, name, tight=True, rect=(0.0, 0.0, 1.0, 0.94)):
    """Write fig as both <name>.pdf and <name>.png into paper/figures/.

    `tight=True` (default) calls fig.tight_layout(rect=rect); the default
    rect reserves headroom at the top for a fig.suptitle() (matplotlib 3.1's
    tight_layout does not know about suptitle on its own, so without this
    the suptitle collides with subplot titles). Pass tight=False when the
    caller has already hand-tuned the layout via subplots_adjust (e.g. a
    figure with a shared colorbar). Both branches finish with
    savefig(bbox_inches="tight") so legends/colorbars placed outside the
    axes (bbox_to_anchor) are never clipped.
    """
    os.makedirs(FIGURES_DIR, exist_ok=True)
    if tight:
        fig.tight_layout(rect=rect)
    pdf_path = os.path.join(FIGURES_DIR, name + ".pdf")
    png_path = os.path.join(FIGURES_DIR, name + ".png")
    fig.savefig(pdf_path, bbox_inches="tight")
    fig.savefig(png_path, bbox_inches="tight")
    print("wrote %s" % pdf_path)
    print("wrote %s" % png_path)
