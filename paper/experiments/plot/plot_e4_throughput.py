"""
Figure: E4 live cells and estimated memory.

Source: paper/experiments/results/e4_throughput.csv (columns final_cells, est_bytes)

Grouped bar chart, two panels: (a) live cells at the end of the run, (b) estimated
memory at 56 B per cell, grouped by backend at each extent. Both follow from the
seeded geometry and are exact. Latency is not plotted here: timings are taken
separately on a quiet host (measure_e4.sh) and appear only in the manuscript.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from style import BLUE, VERMILLION, read_csv, save_fig  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

BACKEND_COLOR = {"grid2d": BLUE, "voxel3d": VERMILLION}
SIZE_ORDER = ["100x100", "250x250", "500x500"]
BACKEND_ORDER = ["grid2d", "voxel3d"]


def main():
    header, rows = read_csv("e4_throughput.csv")
    bi = header.index("backend")
    si = header.index("size_label")
    ui = header.index("final_cells")
    ci = header.index("est_bytes")

    data = {}  # (backend, size) -> (final_cells, est_bytes)
    for r in rows:
        data[(r[bi], r[si])] = (float(r[ui]), float(r[ci]))

    x = np.arange(len(SIZE_ORDER))
    width = 0.35

    fig, (ax_lat, ax_thr) = plt.subplots(1, 2, figsize=(9.6, 3.8))

    for i, backend in enumerate(BACKEND_ORDER):
        offset = (i - 0.5) * width
        lat = [data[(backend, s)][0] for s in SIZE_ORDER]
        thr = [data[(backend, s)][1] / 1e6 for s in SIZE_ORDER]
        ax_lat.bar(x + offset, lat, width, color=BACKEND_COLOR[backend], label=backend)
        ax_thr.bar(x + offset, thr, width, color=BACKEND_COLOR[backend], label=backend)

    ax_lat.set_yscale("log")
    ax_lat.set_ylabel("live cells at end of run (log scale)")
    ax_lat.set_title("Live cells (exact)")

    ax_thr.set_yscale("log")
    ax_thr.set_ylabel("estimated memory (MB, 56 B per cell, log scale)")
    ax_thr.set_title("Estimated memory")

    for ax in (ax_lat, ax_thr):
        ax.set_xticks(x)
        ax.set_xticklabels(SIZE_ORDER)
        ax.set_xlabel("map extent (cells / voxels per side)")
        ax.legend(frameon=False)

    fig.suptitle("E4: map size vs. backend and extent "
                  "(150 hits/window, 20 windows)")
    save_fig(fig, "fig_e4_throughput")

if __name__ == "__main__":
    main()
