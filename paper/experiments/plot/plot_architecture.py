"""
Figure: strata / strata_core architecture block diagram.

Source: paper/notes/system.md, section 7 "ASCII architecture diagram spec"
(and the figure-caption guidance immediately below it). This is a clean
matplotlib block diagram (no screenshot) rendering that spec: two horizontal
bands split by the MapBackend interface, a shared LayeredMap box feeding two
sibling backend boxes, the ROS 2 MappingNode selecting one backend at
construction, its pub/sub/service surface, and the external TF arrow
entering the node from outside the diagram.

No experiment CSV backs this figure; it is a structural rendering of the
architecture notes, not measured data.

Layout is computed top-down/bottom-up from each box's line count (see
box_h()) so text never overflows its box — every number below is derived,
not hand-tuned, to keep this maintainable if the notes text changes.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from style import BLACK, BLUE, VERMILLION, FIGURES_DIR  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import FancyBboxPatch, Rectangle  # noqa: E402

CORE_FACE = "#EAF2FB"     # light blue tint: strata_core band
ROS_FACE = "#FCEFE6"      # light orange tint: strata (ROS2) band
NODE_FACE = "#FFFFFF"
GRID_FACE = "#DCEBFA"
VOXEL_FACE = "#FBE3D5"

LINE_H = 0.26
HEADER_GAP = 0.50
PAD_BOTTOM = 0.16


def box_h(n_lines):
    """Height that comfortably fits a title line + n_lines of body text."""
    return HEADER_GAP + n_lines * LINE_H + PAD_BOTTOM


def rounded_box(ax, x, y, w, h, title, lines, facecolor, edgecolor=BLACK,
                 title_size=8.6, body_size=7.2, z=3):
    box = FancyBboxPatch((x, y), w, h,
                          boxstyle="round,pad=0.02,rounding_size=0.10",
                          linewidth=1.1, edgecolor=edgecolor, facecolor=facecolor,
                          zorder=z)
    ax.add_patch(box)
    ax.text(x + 0.12, y + h - 0.16, title, fontsize=title_size, fontweight="bold",
             ha="left", va="top", zorder=z + 1)
    for i, line in enumerate(lines):
        ax.text(x + 0.16, y + h - HEADER_GAP - i * LINE_H, line, fontsize=body_size,
                 ha="left", va="top", family="monospace", zorder=z + 1)


def band(ax, x, y, w, h, label, facecolor):
    r = Rectangle((x, y), w, h, linewidth=1.6, edgecolor=BLACK, facecolor=facecolor,
                   zorder=1)
    ax.add_patch(r)
    ax.text(x + 0.15, y + h - 0.32, label, fontsize=9.3, fontweight="bold",
             ha="left", va="top", zorder=2)


def arrow(ax, start, end, style="<->", color=BLACK, lw=1.2, z=4):
    ax.annotate("", xy=end, xytext=start,
                arrowprops=dict(arrowstyle=style, color=color, lw=lw,
                                 shrinkA=2, shrinkB=2), zorder=z)


def label_at(ax, xy, text, fontsize=7.0, z=5, ha="center", style="normal"):
    ax.text(xy[0], xy[1], text, fontsize=fontsize, ha=ha, va="center",
             style=style, zorder=z,
             bbox=dict(facecolor="white", edgecolor="none", pad=1.0))


def main():
    # ---- content --------------------------------------------------------
    layered_lines = [
        "log-odds occupancy: observeHit / observeMiss (l_hit/l_miss, clamped)",
        "survival decay → toward “unknown” each window",
        "Schmitt trigger: graduate_prob / demote_prob thresholds",
        "  → CellClass: Unknown | Transient | Periodic | Static",
        "PeriodicityModel: FreMEn-lite Fourier fit (period_windows, n_harmonics)",
        "  amplitude ≥ a_min reclassifies Transient → Periodic",
    ]
    grid_lines = [
        "GridMeta: W,H,res,origin_x,origin_y (fixed-size array)",
        "hit → worldToGrid(x,y)  [z dropped implicitly]",
        "clear → Bresenham line (exact, on-grid ints)",
        "toOccupancyGrid(): -1/50/75/100",
    ]
    voxel_lines = [
        "int64 spatial hash id=(vx<<42)|(vy<<21)|vz, unbounded",
        "hit → voxelId(x,y,z)  [full 3D, no bounds check]",
        "clear → ray-sample march (0.5-voxel step points)",
        "staticPoints(): voxel centers → point cloud",
    ]
    node_lines = [
        'param "backend": grid2d | voxel3d',
        "selects ONE backend at construction time",
        "(no runtime hot-swap)",
        "mtx_ serializes on{Scan,Points} vs onPublish/onSave",
    ]
    scan_lines = ["/scan  LaserScan", "SensorDataQoS", "→ Observation (6-DoF TF)", "grid2d only"]
    cloud_lines = ["/points  PointCloud2", "SensorDataQoS", "→ Observation (6-DoF TF)", "voxel3d only"]
    map_out_lines = ["OccupancyGrid", "transient_local+reliable", "grid2d only"]
    save_lines = ["grid2d → .pgm + .yaml", "voxel3d → .pcd"]
    points_out_lines = ["PointCloud2, QoS(1)", "(volatile)", "voxel3d only"]
    tf_lines = ["map ← sensor_frame", "localizer/odom (e.g. prism_loc,", "not strata)"]

    h_layered = box_h(len(layered_lines))
    h_backend = box_h(len(grid_lines))
    h_node = box_h(len(node_lines))
    h_adapter = box_h(len(scan_lines))
    h_out = box_h(max(len(map_out_lines), len(save_lines), len(points_out_lines)))
    h_tf = box_h(len(tf_lines))

    # ---- geometry: build bottom-up so nothing overflows -----------------
    # margin: generic inner padding. margin_top: extra clearance above the
    # topmost box in each band, reserved for that band's own title label.
    gap_small, gap_med, margin, margin_top = 0.30, 0.45, 0.32, 0.58

    strata_band_bottom = 0.30
    out_y = strata_band_bottom + margin
    out_top = out_y + h_out
    adapter_y = out_top + gap_med
    adapter_top = adapter_y + h_adapter
    node_y = adapter_top + gap_med
    node_top = node_y + h_node
    strata_band_top = node_top + margin_top

    sep_y = strata_band_top + 0.14

    core_band_bottom = sep_y + 0.14
    backend_y = core_band_bottom + margin
    backend_top = backend_y + h_backend
    layered_y = backend_top + gap_med
    layered_top = layered_y + h_layered
    core_band_top = layered_top + margin_top

    fig_top = core_band_top + 0.70   # headroom for the title text
    fig_bottom = -0.25

    x_left, x_right = 0.3, 12.0
    x_tf_left, x_tf_right = -2.5, -0.35

    fig, ax = plt.subplots(figsize=(12.0, 11.6 * (fig_top - fig_bottom) / 13.0))
    ax.set_xlim(x_tf_left - 0.2, x_right + 0.3)
    ax.set_ylim(fig_bottom, fig_top)
    ax.set_aspect("equal")
    ax.axis("off")

    # ---- bands ------------------------------------------------------
    band(ax, x_left, core_band_bottom, x_right - x_left, core_band_top - core_band_bottom,
         "strata_core  —  pure C++17 + Eigen (no ROS, no PCL) — gtest-tested",
         CORE_FACE)
    band(ax, x_left, strata_band_bottom, x_right - x_left, strata_band_top - strata_band_bottom,
         "strata  —  ROS 2 node package (rclcpp / tf2 / PCL live only here)",
         ROS_FACE)

    ax.plot([x_left, x_right], [sep_y, sep_y], linestyle="--", color=BLACK, linewidth=1.3, zorder=2)
    label_at(ax, ((x_left + x_right) / 2, sep_y),
             "MapBackend interface:  integrate(Observation hits[MAP frame], origin) "
             "· tick() · staticCellCount() · transientCellCount()",
             fontsize=7.3)

    # ---- strata_core band content ------------------------------------
    layered_w = 6.7
    layered_x = (x_left + x_right) / 2 - layered_w / 2
    rounded_box(ax, layered_x, layered_y, layered_w, h_layered,
                "LayeredMap  (per-cell evidence, int64 CellId)",
                layered_lines, facecolor="white", title_size=9.0)

    grid_w = (x_right - x_left) / 2 - 0.35
    grid_x = x_left + 0.25
    voxel_x = x_right - 0.25 - grid_w
    rounded_box(ax, grid_x, backend_y, grid_w, h_backend, "Grid2DBackend",
                grid_lines, facecolor=GRID_FACE)
    rounded_box(ax, voxel_x, backend_y, grid_w, h_backend, "Voxel3DBackend",
                voxel_lines, facecolor=VOXEL_FACE)

    arrow(ax, (grid_x + grid_w * 0.55, backend_top), (layered_x + 1.3, layered_y), color=BLUE)
    label_at(ax, (grid_x + grid_w * 0.55 + 0.35, (backend_top + layered_y) / 2 - 0.05),
             "integrate()/tick()", fontsize=6.8)
    arrow(ax, (voxel_x + grid_w * 0.45, backend_top), (layered_x + layered_w - 1.3, layered_y),
          color=VERMILLION)
    label_at(ax, (voxel_x + grid_w * 0.45 - 0.35, (backend_top + layered_y) / 2 - 0.05),
             "integrate()/tick()", fontsize=6.8)

    # ---- strata band content ------------------------------------------
    node_w = 5.4
    node_x = (x_left + x_right) / 2 - node_w / 2
    rounded_box(ax, node_x, node_y, node_w, h_node,
                'MappingNode  (strata_node_main)',
                node_lines, facecolor=NODE_FACE, title_size=8.6)

    adapter_w = 2.9
    scan_x = x_left + 0.25
    cloud_x = x_right - 0.25 - adapter_w
    rounded_box(ax, scan_x, adapter_y, adapter_w, h_adapter, "scan_adapter",
                scan_lines, facecolor=GRID_FACE, title_size=8.0, body_size=6.8)
    rounded_box(ax, cloud_x, adapter_y, adapter_w, h_adapter, "cloud_adapter",
                cloud_lines, facecolor=VOXEL_FACE, title_size=8.0, body_size=6.8)

    arrow(ax, (scan_x + adapter_w * 0.85, adapter_top), (node_x + 0.55, node_y), style="->", color=BLUE)
    arrow(ax, (cloud_x + adapter_w * 0.15, adapter_top), (node_x + node_w - 0.55, node_y),
          style="->", color=VERMILLION)

    out_w = 3.15
    map_x = x_left + 0.25
    save_x = (x_left + x_right) / 2 - out_w / 2
    points_x = x_right - 0.25 - out_w
    rounded_box(ax, map_x, out_y, out_w, h_out, "~/map",
                map_out_lines, facecolor=GRID_FACE, title_size=8.0, body_size=6.6)
    rounded_box(ax, save_x, out_y, out_w, h_out, "~/save_map (Trigger)",
                save_lines, facecolor=NODE_FACE, title_size=7.7, body_size=6.6)
    rounded_box(ax, points_x, out_y, out_w, h_out, "~/map_points",
                points_out_lines, facecolor=VOXEL_FACE, title_size=8.0, body_size=6.6)

    arrow(ax, (node_x + 0.7, node_y), (map_x + out_w * 0.6, out_top), style="->", color=BLUE)
    arrow(ax, (node_x + node_w / 2, node_y), (save_x + out_w / 2, out_top), style="->", color=BLACK)
    arrow(ax, (node_x + node_w - 0.7, node_y), (points_x + out_w * 0.4, out_top), style="->",
          color=VERMILLION)

    # ---- external TF, entering from outside the diagram -----------------
    tf_w = x_tf_right - x_tf_left
    tf_mid_y = (node_y + node_top) / 2
    tf_y = tf_mid_y - h_tf / 2
    rounded_box(ax, x_tf_left, tf_y, tf_w, h_tf, "TF buffer (external)",
                tf_lines, facecolor="#F2F2F2", edgecolor="#666666",
                title_size=7.6, body_size=6.6)
    arrow(ax, (x_tf_right, tf_mid_y), (node_x + 0.05, tf_mid_y - 0.15), style="->",
          color="#666666", lw=1.4)
    label_at(ax, ((x_tf_right + node_x) / 2, tf_mid_y + 0.30),
             "TF lookup(global_frame←sensor frame, 0.1s tol)", fontsize=6.6, style="italic")
    ax.text(x_tf_left, strata_band_bottom + 0.12,
            "strata never publishes map→odom\nand never subscribes /initialpose",
            fontsize=7.0, style="italic", ha="left", va="bottom", color="#444444")

    ax.text((x_left + x_right) / 2, core_band_top + 0.30,
            "strata / strata_core architecture: two backends over one shared\n"
            "persistence + periodicity engine",
            fontsize=11.5, ha="center", va="bottom", fontweight="bold")

    os.makedirs(FIGURES_DIR, exist_ok=True)
    pdf_path = os.path.join(FIGURES_DIR, "fig_architecture.pdf")
    png_path = os.path.join(FIGURES_DIR, "fig_architecture.png")
    fig.savefig(pdf_path, bbox_inches="tight", pad_inches=0.15)
    fig.savefig(png_path, bbox_inches="tight", pad_inches=0.15)
    print("wrote %s" % pdf_path)
    print("wrote %s" % png_path)


if __name__ == "__main__":
    main()
