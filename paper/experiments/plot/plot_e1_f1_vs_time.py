"""
Figure: E1 static-layer map quality vs time.

Source: paper/experiments/results/e1_static_quality.csv
  columns: backend,density,n_movers,window,gt_static,pred_static,tp,
           precision,recall,f1

One line per (backend, density); F1 vs window index (the discrete
integration-window time axis), split into two panels (grid2d | voxel3d) so
the two backends' curves are legible on a shared y-scale.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from style import BLUE, ORANGE, VERMILLION, read_csv, save_fig  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402

DENSITY_ORDER = ["low", "med", "high"]
DENSITY_LABEL = {"low": "low (5 movers/window)", "med": "med (25 movers/window)",
                  "high": "high (100 movers/window)"}
DENSITY_COLOR = {"low": BLUE, "med": ORANGE, "high": VERMILLION}
BACKEND_ORDER = ["grid2d", "voxel3d"]
BACKEND_TITLE = {"grid2d": "grid2d (120×120, res 1.0 m)",
                  "voxel3d": "voxel3d (voxel 1.0 m)"}


def main():
    header, rows = read_csv("e1_static_quality.csv")
    bi, di, wi, fi = (header.index(c) for c in ("backend", "density", "window", "f1"))

    series = {}  # (backend, density) -> ([windows], [f1])
    for r in rows:
        key = (r[bi], r[di])
        series.setdefault(key, ([], []))
        series[key][0].append(int(r[wi]))
        series[key][1].append(float(r[fi]))

    fig, axes = plt.subplots(1, 2, figsize=(9.0, 3.6), sharey=True)
    for ax, backend in zip(axes, BACKEND_ORDER):
        for density in DENSITY_ORDER:
            w, f1 = series[(backend, density)]
            order = sorted(range(len(w)), key=lambda k: w[k])
            w = [w[k] for k in order]
            f1 = [f1[k] for k in order]
            ax.plot(w, f1, color=DENSITY_COLOR[density], linewidth=1.6,
                     marker="o", markersize=2.6, label=DENSITY_LABEL[density])
        ax.set_title(BACKEND_TITLE[backend])
        ax.set_xlabel("window index (time step)")
        ax.set_ylim(-0.03, 1.05)
        ax.set_xlim(0, 39)

    axes[0].set_ylabel("Static-set F1 (unitless)")
    axes[0].legend(title="transient-mover density", loc="lower right", frameon=False)

    fig.suptitle("E1: static-map F1 vs. time, by backend and clutter density")
    save_fig(fig, "fig_e1_f1_vs_time")


if __name__ == "__main__":
    main()
