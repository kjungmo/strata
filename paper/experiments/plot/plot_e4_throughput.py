"""
Figure: E4 throughput and memory.

Source: paper/experiments/results/e4_throughput.csv
  columns: backend,size_label,extent,hits_per_window,windows,us_per_integrate,
           hits_per_sec,us_per_endwindow,cell_updates_per_sec,final_cells,
           bytes_per_cell,est_bytes

Grouped bar chart, two panels: (a) per-integrate() latency in microseconds
(log scale — grid2d vs voxel3d differ by up to ~11x), (b) endWindow()
cell-update throughput in updates/second, both grouped by backend at each
grid/voxel extent. Absolute latencies are machine-dependent (see
notes/experiments_summary.md); the cross-backend/size ratios are the
reproducible quantity.
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
    ui = header.index("us_per_integrate")
    ci = header.index("cell_updates_per_sec")

    data = {}  # (backend, size) -> (us_per_integrate, cell_updates_per_sec)
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
    ax_lat.set_ylabel("time per integrate() call (µs, log scale)")
    ax_lat.set_title("Per-call latency (lower is better)")

    ax_thr.set_ylabel("endWindow() throughput (10$^6$ cell-updates/s)")
    ax_thr.set_title("Cell-update throughput (higher is better)")

    for ax in (ax_lat, ax_thr):
        ax.set_xticks(x)
        ax.set_xticklabels(SIZE_ORDER)
        ax.set_xlabel("map extent (cells / voxels per side)")
        ax.legend(frameon=False)

    fig.suptitle("E4: throughput vs. backend and map extent "
                  "(150 hits/window, 20 windows)")
    save_fig(fig, "fig_e4_throughput")


if __name__ == "__main__":
    main()
