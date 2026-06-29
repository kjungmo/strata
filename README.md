# strata

**A pluggable lifelong mapping tool for ROS 2 Humble — one Bayesian-persistence
layered engine, two LiDAR backends (2D occupancy grid or 3D voxel), full 6-DoF,
producing a static map plus periodic/transient layers.**

## 💛 Sponsor
If strata saves you time, consider [sponsoring](https://github.com/sponsors/kjungmo).
Sponsorship funds maintenance, new features, and faster issue response. Backers will
be acknowledged here — thank you.

`strata` incrementally builds and maintains a robot map from EITHER a **2D
LiDAR** (`LaserScan` → `OccupancyGrid`) OR a **3D LiDAR** (`PointCloud2` → voxel
cloud), backend chosen by one parameter. A single Bayesian-persistence engine
copes with dynamic AND semi-static environments: it **graduates** durably
occupied cells into a permanent static map, flags **periodically** occupied
cells (FreMEn) instead of baking them in, and lets **transient** observations
fade. Sensor poses are full 6-DoF.

`strata` is **not a SLAM system** — it does not estimate the robot's pose. It
consumes an external pose source via TF (`map → sensor`), e.g. the sibling
[`prism_loc`](https://github.com/kjungmo/prism_loc) localizer or a plain `odom → base_link` chain, and
maps against it.

## Why "STRATA"

Like geological **strata**, the map is built from layers of observation that
accumulate over time. Durable layers — seen again and again — consolidate into
the permanent **static** map; **periodic** layers (a door open by day, shut by
night) are recognized as recurring rather than baked in; and **transient**
layers erode away. One layered engine, two LiDAR backends: the same
`LayeredMap` persistence core (log-odds occupancy + survival decay +
Schmitt-trigger graduation) and the same `PeriodicityModel` (FreMEn-lite) drive
**both** a 2D occupancy-grid backend and a 3D voxel backend — the two sensor
paths a ground robot actually has.

## Architecture

The map engine is pure C++17 + Eigen, keyed by an integer cell id, and is
unit-tested with gtest **without ROS or PCL**. rclcpp, tf2, and PCL live only in
the ROS node package.

```
strata_core   (pure C++17 + Eigen, no ROS / no PCL, gtest-tested):

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

  strata  ── MappingNode ──  backend = grid2d | voxel3d
    in :  /scan | /points ,  TF (map -> sensor, full 6-DoF)
    out:  ~/map (OccupancyGrid) | ~/map_points (PointCloud2) ,  ~/save_map (Trigger)
```

## Quick start

```bash
# 1. Map engine — build & test with the plain system toolchain (no ROS):
cmake -S strata_core -B build/core -DSTRATA_CORE_BUILD_TESTS=ON
cmake --build build/core -j && ( cd build/core && ctest --output-on-failure )

# 2. Full ROS 2 Humble build & test (workspace):
#    place this repo at <ws>/src/strata, then:
colcon build --symlink-install
colcon test --packages-select strata_core strata
colcon test-result --verbose
#    (RoboStack/conda Humble users: run the above inside your activated env.)

# 3. Run — 2D occupancy-grid mapping:
ros2 launch strata grid2d.launch.py
#    3D voxel mapping:
ros2 launch strata voxel3d.launch.py
#    save the current static map (PGM+YAML for grid2d, PCD for voxel3d):
ros2 service call /strata/save_map std_srvs/srv/Trigger
```

A pose source must already be publishing TF from `global_frame` (default `map`)
to the LiDAR's frame. Run `prism_loc` (or any localizer / odometry chain) first.

## Interface

| Backend | Package | Input | Output |
|---|---|---|---|
| **grid2d** | `strata` | `/scan` (`LaserScan`), TF `map→sensor` | `~/map` (`OccupancyGrid`), `~/save_map` (`Trigger`) |
| **voxel3d** | `strata` | `/points` (`PointCloud2`), TF `map→sensor` | `~/map_points` (`PointCloud2`), `~/save_map` (`Trigger`) |

There is **no** `/initialpose` input and **no** `map→odom` output — `strata`
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
