"""
Figure: E2 periodicity detection.

Sources:
  paper/experiments/results/e2_summary.csv
    columns: metric,value,note   (periodic_TPR, periodic_FPR, gt_periodic, gt_nonperiodic)
  paper/experiments/results/e2_amplitude_vs_length.csv
    columns: cell,gt_periodic,obs_length,amplitude,periodic_prob

Left panel: TPR/FPR bars (with the underlying k/n fractions annotated).
Right panel: FreMEn dominant-harmonic amplitude vs. observation length (in
windows) for every probe cell, with the periodic_amplitude_min = 0.3
classification threshold and the period_windows = 8 amplitude-validity gate
(both from notes/experiments_summary.md / core_math.md) drawn as reference
lines.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from style import (BLACK, BLUE, GREEN, ORANGE, PURPLE, SKY_BLUE, VERMILLION,
                    YELLOW, read_csv, save_fig)  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402

A_MIN = 0.3          # periodic_amplitude_min used in this experiment (see core_math.md)
T_GATE = 8           # period_windows used in E2 (see e2_amplitude_vs_length.csv '#seed' row)

# cell -> (color, linestyle, marker, group label used once in the legend)
CELL_STYLE = {
    "door_p8_4on4off": (BLUE, "-", "o", "periodic door (T=8, 50% duty)"),
    "door_p4_2on2off": (GREEN, "-", "o", "periodic door (T=4, 50% duty)"),
    "door_p8_2on6off": (SKY_BLUE, "-", "o", "periodic door (T=8, 25% duty) — missed"),
    "wall_constant":   (BLACK, ":", "s", "constant wall (non-periodic GT)"),
    "aperiodic_0":     (ORANGE, "--", "x", "aperiodic Bernoulli(0.5) mover"),
    "aperiodic_1":     (VERMILLION, "--", "x", "aperiodic mover — false positive"),
    "aperiodic_2":     (PURPLE, "--", "x", None),
    "aperiodic_3":     (YELLOW, "--", "x", None),
}
CELL_ORDER = ["door_p8_4on4off", "door_p4_2on2off", "door_p8_2on6off",
              "wall_constant", "aperiodic_0", "aperiodic_1", "aperiodic_2", "aperiodic_3"]


def load_summary():
    header, rows = read_csv("e2_summary.csv")
    mi, vi, ni = header.index("metric"), header.index("value"), header.index("note")
    d = {r[mi]: (float(r[vi]), r[ni]) for r in rows}
    return d


def main():
    summary = load_summary()
    tpr, tpr_note = summary["periodic_TPR"]
    fpr, fpr_note = summary["periodic_FPR"]
    n_pos = int(summary["gt_periodic"][0])
    n_neg = int(summary["gt_nonperiodic"][0])

    header, rows = read_csv("e2_amplitude_vs_length.csv")
    ci, li, ai = header.index("cell"), header.index("obs_length"), header.index("amplitude")
    series = {}
    for r in rows:
        series.setdefault(r[ci], ([], []))
        series[r[ci]][0].append(int(r[li]))
        series[r[ci]][1].append(float(r[ai]))

    fig = plt.figure(figsize=(9.6, 4.0))
    gs = fig.add_gridspec(1, 2, width_ratios=[1.0, 2.1], wspace=0.32)
    ax_bar = fig.add_subplot(gs[0, 0])
    ax_line = fig.add_subplot(gs[0, 1])

    # --- left: TPR / FPR bars ---
    bars = ax_bar.bar(["TPR", "FPR"], [tpr, fpr], color=[BLUE, VERMILLION], width=0.55)
    ax_bar.set_ylim(0, 1.05)
    ax_bar.set_ylabel("rate (unitless)")
    ax_bar.set_title("Periodic-class TPR/FPR")
    labels = ["%d/%d" % (int(tpr_note), n_pos), "%d/%d" % (int(fpr_note), n_neg)]
    for b, lab in zip(bars, labels):
        ax_bar.annotate(lab, (b.get_x() + b.get_width() / 2, b.get_height()),
                         xytext=(0, 3), textcoords="offset points",
                         ha="center", va="bottom", fontsize=9)

    # --- right: amplitude vs observation length ---
    for cell in CELL_ORDER:
        x, y = series[cell]
        order = sorted(range(len(x)), key=lambda k: x[k])
        x = [x[k] for k in order]
        y = [y[k] for k in order]
        color, ls, marker, label = CELL_STYLE[cell]
        ax_line.plot(x, y, color=color, linestyle=ls, marker=marker, markersize=4,
                      linewidth=1.4, label=label)

    ax_line.axhline(A_MIN, color=BLACK, linestyle="-.", linewidth=1.0)
    ax_line.annotate("a_min = 0.3 (Periodic threshold)", xy=(50, A_MIN),
                       xytext=(0, 4), textcoords="offset points", fontsize=8, ha="right")
    ax_line.axvline(T_GATE, color=BLACK, linestyle=(0, (1, 2)), linewidth=1.0)
    ax_line.annotate("T=8 windows\n(amplitude-validity gate)", xy=(T_GATE, 0.95),
                       xytext=(4, 0), textcoords="offset points", fontsize=8, va="top")

    ax_line.set_xlabel("observation length (windows)")
    ax_line.set_ylabel("FreMEn dominant-harmonic amplitude (occ. units)")
    ax_line.set_title("Amplitude vs. observation length")
    ax_line.set_xlim(4, 66)
    ax_line.set_ylim(-0.03, 1.0)
    ax_line.legend(loc="upper left", bbox_to_anchor=(1.02, 1.0), frameon=False, fontsize=7.5)

    fig.suptitle("E2: periodicity detection (TPR/FPR and amplitude growth)")
    save_fig(fig, "fig_e2_periodicity")


if __name__ == "__main__":
    main()
