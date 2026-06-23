# prism_map

**A pluggable lifelong mapping tool for ROS 2 Humble — one Bayesian-persistence
layered engine, two LiDAR backends (2D occupancy grid or 3D voxel), full 6-DoF,
producing a static map plus periodic/transient layers.**

`prism_map` incrementally builds and maintains a robot map from EITHER a **2D
LiDAR** (`LaserScan` → `OccupancyGrid`) OR a **3D LiDAR** (`PointCloud2` → voxel
cloud), backend chosen by one parameter. A single Bayesian-persistence engine
copes with dynamic AND semi-static environments: it **graduates** durably
occupied cells into a permanent static map, flags **periodically** occupied
cells (FreMEn) instead of baking them in, and lets **transient** observations
fade. Sensor poses are full 6-DoF.

`prism_map` is **not a SLAM system** — it does not estimate the robot's pose. It
consumes an external pose source via TF (`map → sensor`), e.g. the sibling
[`prism_loc`](../prism_loc) localizer or a plain `odom → base_link` chain, and
maps against it.

## Why "prism"

A prism splits one beam of light into its component colors. `prism_map` splits
one layered-map engine into two LiDAR backends: the same `LayeredMap`
persistence core (log-odds occupancy + survival decay + Schmitt-trigger
graduation) and the same `PeriodicityModel` (FreMEn-lite) drive **both** a 2D
occupancy-grid backend and a 3D voxel backend. One engine, decomposed into the
two sensor paths a ground robot actually has.

## Architecture

The map engine is pure C++17 + Eigen, keyed by an integer cell id, and is
unit-tested with gtest **without ROS or PCL**. rclcpp, tf2, and PCL live only in
the ROS node package.

```
prism_map_core   (pure C++17 + Eigen, no ROS / no PCL, gtest-tested):

  LayeredMap  ── THE HEART ──────────────────────────────────────────
    log-odds occupancy (l_hit/l_miss + clamp)
    survival decay  (Persistence Filter forgetting toward unknown)
    Schmitt trigger graduate(graduate_prob) / demote(demote_prob)   ─► Static
        │                                                              │
        └─► PeriodicityModel (FreMEn-lite, incremental Fourier)     ─► Periodic
                amplitude >= periodic_amplitude_min                    else Transient

  MapBackend  (interface: integrate(obs, sensor_origin) / tick())
    ├─ Grid2DBackend   6-DoF hits projected to a 2D plane, Bresenham ray clearing
    └─ Voxel3DBackend  fully volumetric voxel-hash, ray-sample clearing

ROS 2 node   (rclcpp / tf2 / PCL here only):

  prism_map  ── MappingNode ──  backend = grid2d | voxel3d
    in :  /scan | /points ,  TF (map -> sensor, full 6-DoF)
    out:  ~/map (OccupancyGrid) | ~/map_points (PointCloud2) ,  ~/save_map (Trigger)
```

## Quick start

```bash
# 1. Map engine — build & test with the plain system toolchain (no ROS):
cmake -S prism_map_core -B build/core -DPRISM_MAP_CORE_BUILD_TESTS=ON
cmake --build build/core -j && ( cd build/core && ctest --output-on-failure )

# 2. Full ROS 2 Humble build & test (micromamba env; clean env to avoid distro leak):
env -u ROS_DISTRO -u ROS_VERSION -u ROS_PACKAGE_PATH \
  /home/cona/.local/bin/micromamba run -n ros2_humble bash -c '
    cd /home/cona/kangj/prism_map_ws &&
    colcon build --symlink-install &&
    colcon test --packages-select prism_map_core prism_map &&
    colcon test-result --verbose'

# 3. Run — 2D occupancy-grid mapping:
ros2 launch prism_map grid2d.launch.py
#    3D voxel mapping:
ros2 launch prism_map voxel3d.launch.py
#    save the current static map (PGM+YAML for grid2d, PCD for voxel3d):
ros2 service call /prism_map/save_map std_srvs/srv/Trigger
```

A pose source must already be publishing TF from `global_frame` (default `map`)
to the LiDAR's frame. Run `prism_loc` (or any localizer / odometry chain) first.

## Interface

| Backend | Package | Input | Output |
|---|---|---|---|
| **grid2d** | `prism_map` | `/scan` (`LaserScan`), TF `map→sensor` | `~/map` (`OccupancyGrid`), `~/save_map` (`Trigger`) |
| **voxel3d** | `prism_map` | `/points` (`PointCloud2`), TF `map→sensor` | `~/map_points` (`PointCloud2`), `~/save_map` (`Trigger`) |

There is **no** `/initialpose` input and **no** `map→odom` output — `prism_map`
maps, it does not localize. The robot's pose comes in via TF from an external
source. Occupancy values render as static→100, periodic→75, transient→50,
unknown→-1.

See [`SPEC.md`](SPEC.md) for the full design.

## Status

v0.1 — one Bayesian-persistence engine with log-odds occupancy + Schmitt-trigger
graduation to a static layer, FreMEn periodicity classification, full 6-DoF
sensor poses, and two backends: a 2D occupancy grid and a 3D voxel map. The map
engine is pure C++/Eigen with 22 gtests across 7 suites (plus 2 node tests),
including a deterministic room integration test (wall→Static, mover→never,
door→Periodic).

Roadmap: TSDF surfaces (replacing the voxel-hash), multi-session map merge, loop
closure, and learned ephemerality.

## License

Apache-2.0.
