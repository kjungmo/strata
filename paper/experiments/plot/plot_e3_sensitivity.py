"""
Figure: E3 hysteresis / decay sensitivity.

Source: paper/experiments/results/e3_sensitivity.csv
  columns: graduate_prob,demote_prob,survival_decay,hysteresis_band,
           degenerate,final_f1,final_precision,final_recall,pred_static,
           flicker_transitions

Three heatmap panels (one per survival_decay in {0.90, 0.97, 1.0}); rows =
graduate_prob, columns = hysteresis-band configuration (no hysteresis /
demote=0.5 / demote=0.3, in increasing-band order). Cell color = flicker
transition count (log scale, colorblind-safe sequential colormap); each cell
is annotated with the raw flicker count and the resulting final Static F1.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from style import read_csv, save_fig  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.colors import LogNorm  # noqa: E402
import numpy as np  # noqa: E402

DECAY_ORDER = [0.9, 0.97, 1.0]
COL_ORDER = ["degenerate\n(demote=graduate)", "mid floor\n(demote=0.5)", "low floor\n(demote=0.3)"]
GRAD_ORDER = [0.6, 0.7, 0.8, 0.9]


def col_label(graduate, demote):
    if abs(demote - graduate) < 1e-9:
        return COL_ORDER[0]
    if abs(demote - 0.5) < 1e-9:
        return COL_ORDER[1]
    if abs(demote - 0.3) < 1e-9:
        return COL_ORDER[2]
    raise ValueError("unexpected demote_prob %r for graduate_prob %r" % (demote, graduate))


def main():
    header, rows = read_csv("e3_sensitivity.csv")
    gi = header.index("graduate_prob")
    di = header.index("demote_prob")
    si = header.index("survival_decay")
    fi = header.index("flicker_transitions")
    f1i = header.index("final_f1")

    # grid[decay][graduate_idx][col_idx] = (flicker, f1)
    grid = {d: [[None] * 3 for _ in GRAD_ORDER] for d in DECAY_ORDER}
    for r in rows:
        g, dem, dec = float(r[gi]), float(r[di]), float(r[si])
        flicker, f1 = int(r[fi]), float(r[f1i])
        gidx = GRAD_ORDER.index(g)
        cidx = COL_ORDER.index(col_label(g, dem))
        grid[dec][gidx][cidx] = (flicker, f1)

    all_flicker = [int(r[fi]) for r in rows]
    vmin, vmax = min(all_flicker), max(all_flicker)

    fig, axes = plt.subplots(1, 3, figsize=(11.5, 4.2), sharey=True)
    norm = LogNorm(vmin=vmin, vmax=vmax)
    im = None
    for ax, dec in zip(axes, DECAY_ORDER):
        mat = np.array([[grid[dec][gi_][ci_][0] for ci_ in range(3)] for gi_ in range(4)],
                        dtype=float)
        im = ax.imshow(mat, cmap="viridis", norm=norm, aspect="auto", origin="lower")
        for gi_ in range(4):
            for ci_ in range(3):
                flicker, f1 = grid[dec][gi_][ci_]
                # pick readable text color against the viridis cell shade
                frac = (np.log(flicker) - np.log(vmin)) / (np.log(vmax) - np.log(vmin))
                txt_color = "white" if frac < 0.6 else "black"
                ax.text(ci_, gi_, "%d\nF1=%.2f" % (flicker, f1), ha="center", va="center",
                         fontsize=8.5, color=txt_color)
        ax.set_xticks(range(3))
        ax.set_xticklabels(COL_ORDER, fontsize=8)
        ax.set_title("survival_decay = %.2f" % dec)
        ax.set_yticks(range(4))
        ax.grid(False)

    axes[0].set_yticklabels(["graduate_prob=%.1f" % g for g in GRAD_ORDER])
    fig.colorbar(im, ax=axes, orientation="vertical", fraction=0.025, pad=0.02,
                 label="flicker transitions (isStatic toggles, log scale)")

    fig.suptitle("E3: hysteresis-band × decay sensitivity "
                  "(cell = flicker count, annotated with final Static F1)")
    fig.subplots_adjust(left=0.14, right=0.90, top=0.85, bottom=0.18, wspace=0.12)
    save_fig(fig, "fig_e3_sensitivity", tight=False)


if __name__ == "__main__":
    main()
