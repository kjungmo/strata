# prism_map — Specification

A real, open-source ROS 2 **Humble** lifelong-mapping tool for mobile robots
that takes **either a 2D LiDAR scan or a 3D LiDAR point cloud** as input and
incrementally builds and maintains a map, coping with dynamic AND semi-static
environments. One Bayesian-persistence **layered engine** drives **two pluggable
geometry backends** (2D occupancy grid or 3D voxel), selected at runtime by a
single parameter. Sensor poses are full 6-DoF.

`prism_map` is **not SLAM**: it does not estimate pose. It consumes an external
`map → sensor` transform (from the sibling `prism_loc`, another localizer, or a
plain odometry chain) and maps against it.

License: **Apache-2.0**. No proprietary dependencies.

---

## 1. Why this design

A robot that runs for weeks sees three kinds of occupancy: **static** structure
(walls, pillars) that should become the durable map; **transient** clutter
(people, a parked cart) that should appear briefly and fade; and **semi-static /
periodic** features (a door that is open mornings, a shutter) that are neither
permanent nor noise. Baking every hit into one occupancy grid corrupts the map
with everything that ever moved; clearing aggressively erases real structure.

`prism_map` resolves this with a single layered engine that accumulates evidence,
confirms it over time, and **graduates** only durable cells into the static map:

- **Occupancy as log-odds** with hit/miss increments + clamp — the standard
  inverse-sensor-model accumulation (*Probabilistic Robotics* ch. 9).
- **Survival decay** — each window the log-odds is pulled back toward unknown,
  so evidence must be *refreshed* to persist. This is the forgetting term of a
  **Persistence Filter** (Rosen/Mason/Leonard, ICRA 2016): an unobserved cell's
  belief that it is still occupied decays over time.
- **Schmitt-trigger graduation** — a cell promotes to **Static** when its
  occupancy probability rises above `graduate_prob` (and it has been observed at
  least `min_observations` times), and demotes only when it falls below the lower
  `demote_prob`. The `[demote_prob, graduate_prob]` gap is hysteresis: it stops a
  cell flickering between map and not-map on borderline evidence.
- **FreMEn periodicity** — a parallel per-cell frequency model
  (Krajník et al., T-RO 2017) detects cells whose occupancy oscillates. A cell
  with a strong dominant harmonic is classified **Periodic** and predicted per
  phase, rather than frozen into the static layer.

The same engine, keyed by an integer cell id, drives two geometry backends:

| Backend | Map | Input topic | Geometry | Clearing |
|---|---|---|---|---|
| `grid2d` | `nav_msgs/OccupancyGrid` | `/scan` (`sensor_msgs/LaserScan`) | 2D occupancy grid; 6-DoF endpoints projected to the plane | Bresenham ray-trace |
| `voxel3d` | PointCloud2 / PCD | `/points` (`sensor_msgs/PointCloud2`) | fully volumetric voxel-hash | ray-sample along the beam |

This is not a hack: it is one `LayeredMap` + one `PeriodicityModel` behind a
`MapBackend` interface, so the persistence and periodicity behavior is identical
across 2D and 3D.

---

## 2. I/O contract & frames

Modeled on ROS 2 mapping conventions and **REP-105**. `prism_map` is a map
*consumer of pose*, not a localizer: the robot pose enters as a TF lookup, never
as a topic, and there is no `map → odom` output.

### Inputs (subscriptions / TF / params)
| Name | Type | Backend | Notes |
|---|---|---|---|
| `/scan` (`scan_topic`) | `sensor_msgs/LaserScan` | grid2d | planar scan |
| `/points` (`points_topic`) | `sensor_msgs/PointCloud2` | voxel3d | 3D cloud |
| pose | **TF** `global_frame → <sensor frame_id>` | both | full 6-DoF `Isometry3d`, looked up at the message stamp; *not* a topic |
| `/tf`, `/tf_static` | `tf2_msgs/TFMessage` | both | the external pose source (e.g. `prism_loc`) must supply this chain |

There is **no `/initialpose`** — `prism_map` does not initialize a filter.

### Outputs
| Name | Type | Backend | Notes |
|---|---|---|---|
| `~/map` (`/prism_map/map`) | `nav_msgs/OccupancyGrid` (transient_local) | grid2d | static→100, periodic→75, transient→50, unknown→-1 |
| `~/map_points` (`/prism_map/map_points`) | `sensor_msgs/PointCloud2` | voxel3d | centers of graduated static voxels, in `global_frame` |
| `~/save_map` (`/prism_map/save_map`) | `std_srvs/srv/Trigger` (service) | both | grid2d → PGM + map_server YAML; voxel3d → PCD |

### Frames (REP-105)
The node looks up `T_global_sensor` (`global_frame → sensor frame_id`) at each
message stamp and transforms every endpoint into `global_frame` before
integrating. The sensor origin (ray start, for clearing) is the translation of
that transform. The full 6-DoF transform is used: `voxel3d` keeps z and attitude
volumetrically; `grid2d` projects transformed endpoints onto its 2D plane.

---

## 3. Algorithm

### 3.1 Layered evidence per cell
Each known cell holds `{ log_odds, observations, window_hits, window_misses,
graduated }`. Observations arrive per integration frame as **hits** (an endpoint
fell in the cell) and **misses** (a ray passed through the cell — free-space /
negative information). Frames are grouped into **windows** of `layer_interval`
ticks; the layered update runs once per window (`endWindow`).

### 3.2 Log-odds occupancy
At window close, the cell's window state is `occ = (window_hits >= 1)`,
`free = (!occ && window_misses > 0)`. Then:

```
if occ   : log_odds += l_hit
elif free: log_odds += l_miss
log_odds *= survival_decay          # Persistence-Filter forgetting toward unknown
log_odds  = clamp(log_odds, l_min, l_max)
```

Occupancy probability is `p = sigmoid(log_odds)`. Touched cells increment
`observations`.

### 3.3 Survival decay (Persistence Filter)
The `*= survival_decay` step every window pulls belief back toward unknown
(log-odds 0). A cell that stops being observed decays out; only **refreshed**
evidence persists. This is the discrete forgetting term of Rosen et al.'s
Persistence Filter, and it is what makes transient clutter fade.

### 3.4 Schmitt-trigger graduate / demote
```
graduate: !graduated && p >= graduate_prob && observations >= min_observations  -> graduated = true
demote  :  graduated && p <= demote_prob                                        -> graduated = false
```
The two thresholds (with `demote_prob < graduate_prob`) form the hysteresis band
that prevents flicker. A graduated cell is **Static** — it belongs to the durable
map.

### 3.5 FreMEn periodicity
When `enable_periodicity` is set, every touched cell feeds its per-window state
into `PeriodicityModel`, which keeps incremental Fourier coefficients over a
`period_windows`-long period with `n_harmonics` harmonics (phase
`ω = 2π / period_windows`). It exposes:
- `predict(phase) → P[0,1]` — predicted occupancy at a given window phase
  (`0.5` when the cell is unknown);
- `amplitude` — the dominant-harmonic magnitude (periodicity strength).

A cell whose amplitude reaches `periodic_amplitude_min` is classified
**Periodic**. **The amplitude is only meaningful after a full period has been
observed** — before `period_windows` windows have elapsed the harmonic estimate
is unreliable, so a freshly seen oscillating cell will not yet read as Periodic.

### 3.6 Classification & pruning
```
classify(cell):
  graduated                                     -> Static
  enable_periodicity && amp >= amplitude_min    -> Periodic
  p >= prune_prob                               -> Transient
  else                                          -> Unknown
prune: erase cell if  !graduated && p < prune_prob && amp < periodic_amplitude_min
```
Static cells are never pruned; periodic cells survive on their amplitude even
when momentarily free.

### 3.7 Ray clearing (free-space / negative information)
- **grid2d**: an integer **Bresenham** line from the sensor cell to the endpoint
  cell marks every intermediate cell as a miss, then the endpoint as a hit.
- **voxel3d**: the beam is **sampled** at half-voxel steps from origin to
  endpoint; each sampled voxel is a miss, the endpoint voxel a hit.

Clearing is what demotes a graduated cell that an object has vacated, and what
keeps movers from accumulating.

### 3.8 6-DoF poses
Sensor poses are `Eigen::Isometry3d` end to end. `voxel3d` is fully volumetric
(x, y, z all kept). `grid2d` accepts the same 6-DoF transform but projects
transformed endpoints onto its plane (drops z), which is exact for small
roll/pitch and approximate for large attitude (documented in §8).

---

## 4. Module / file structure

Two ament packages in one repo. **All algorithmic code lives in `prism_map_core`,
which has NO ROS and NO PCL dependency** — Eigen3 + gtest only — so it builds and
unit-tests with the plain system toolchain. The ROS/PCL surface is confined to
`prism_map`.

```
prism_map_core/                         # pure C++17 + Eigen, no ROS, no PCL
  include/prism_map_core/
    version.hpp           # PRISM_MAP_CORE_VERSION "0.1.0"
    types.hpp             # Pose2D, Pose3D(=Isometry3d), GridMeta, GridMap, CellId, world<->grid, flatten
    periodicity.hpp       # PeriodicityModel (FreMEn-lite incremental Fourier)
    layered_map.hpp       # LayeredMap + LayeredMapParams + CellClass + CellEvidence (THE HEART)
    map_backend.hpp       # MapBackend interface + Observation
    grid2d_backend.hpp    # Grid2DBackend (2D occupancy, Bresenham clearing)
    voxel3d_backend.hpp   # Voxel3DBackend (voxel-hash, ray-sample clearing)
  src/                    # types, periodicity, layered_map, grid2d_backend, voxel3d_backend
  test/                   # test_smoke, test_types, test_periodicity, test_layered_map,
                          # test_grid2d_backend, test_voxel3d_backend, test_integration

prism_map/                              # ROS 2 Humble node
  include/prism_map/mapping_node.hpp
  src/mapping_node.cpp    # subs/pub/TF, backend select, save service, calls core
  src/scan_adapter.cpp    # LaserScan + 6-DoF Pose3D -> Observation (map frame)
  src/cloud_adapter.cpp   # PointCloud2 + 6-DoF Pose3D -> Observation (PCL)
  src/main.cpp
  launch/grid2d.launch.py
  launch/voxel3d.launch.py
  params/grid2d.yaml
  params/voxel3d.yaml
  rviz/prism_map.rviz
  test/test_grid_math.cpp     # world<->grid roundtrip (gtest, no rclcpp)
  test/test_scan_adapter.cpp  # 6-DoF beam transform (gtest)
```

---

## 5. Parameters (ROS params, namespaced)

Defaults below are the actual `params/grid2d.yaml` / `params/voxel3d.yaml` values
and the `LayeredMapParams` / `PeriodicityParams` struct defaults — they agree.

### Layered persistence engine (`LayeredMapParams`)
| Param | Default | Meaning |
|---|---|---|
| `layer_interval` | `10` | integration ticks (frames) per window |
| `l_hit` | `0.85` | log-odds added on an occupied window |
| `l_miss` | `-0.4` | log-odds added on a free window |
| `l_min` | `-5.0` | log-odds clamp lower bound |
| `l_max` | `5.0` | log-odds clamp upper bound |
| `survival_decay` | `0.97` | per-window multiply (Persistence-Filter forgetting) |
| `graduate_prob` | `0.8` | P(occ) to promote a cell to Static |
| `demote_prob` | `0.45` | P(occ) below which a Static cell demotes (hysteresis gap) |
| `min_observations` | `3` | minimum touches before a cell may graduate |
| `prune_prob` | `0.05` | erase a non-static, non-periodic cell below this P(occ) |
| `enable_periodicity` | `true` | run the FreMEn model |
| `periodic_amplitude_min` | `0.3` | dominant-harmonic amplitude to classify Periodic |

### Periodicity model (`PeriodicityParams`)
| Param | Default | Meaning |
|---|---|---|
| `period_windows` | `24` | windows per period (FreMEn base period) |
| `n_harmonics` | `2` | number of Fourier harmonics tracked per cell |

### grid2d geometry
| Param | Default | Meaning |
|---|---|---|
| `grid_width` | `400` | grid cells in x |
| `grid_height` | `400` | grid cells in y |
| `grid_resolution` | `0.05` | m per cell |
| `grid_origin_x` | `-10.0` | grid origin x (m, world) |
| `grid_origin_y` | `-10.0` | grid origin y (m, world) |

### voxel3d geometry
| Param | Default | Meaning |
|---|---|---|
| `voxel_size` | `0.2` | voxel edge length (m) |

### Node
| Param | Default | Meaning |
|---|---|---|
| `backend` | `grid2d` / `voxel3d` | selects `Grid2DBackend` or `Voxel3DBackend` |
| `global_frame` | `map` | frame the map and TF lookups are expressed in |
| `scan_topic` | `/scan` | grid2d input topic |
| `points_topic` | `/points` | voxel3d input topic |
| `publish_period` | `1.0` | seconds between map publications |
| `save_path` | `/tmp/prism_map_2d` (grid2d) / `/tmp/prism_map_3d` (voxel3d) | save-service output path stem |

---

## 6. Test strategy

Every algorithmic claim has a deterministic gtest (injected window index, no
wall-clock, no `rand()`) in `prism_map_core/test`, runnable with **no ROS** —
**22 gtests across 7 suites**:

- **Smoke** (1): version macro is defined.
- **Types** (4): world↔grid round-trip; out-of-bounds rejection; cell-id
  uniqueness; `flatten` of a 6-DoF transform recovers x, y, yaw.
- **Periodicity** (4): constant-occupied → high mean, low amplitude;
  constant-free → low prediction; square wave → detected and phase-predicted;
  unknown cell → `0.5`.
- **LayeredMap** (5): graduates only when P(occ) ≥ threshold AND observed ≥
  `min_observations`; a moving obstacle (each cell hit once) never graduates;
  Schmitt hysteresis demotes only after sustained free; a square-wave cell is
  classified Periodic, not Static; `layer_interval` groups ticks into windows.
- **Grid2DBackend** (4): a hit marks the endpoint and clears the ray; repeated
  hits graduate; occupancy-grid render (100/75/50/-1); a 6-DoF (elevated)
  endpoint projects to the plane.
- **Voxel3DBackend** (3): same world point → same voxel id; repeated hits
  graduate a voxel with z preserved; a moving point never graduates.
- **Integration** (1): a deterministic room — a fixed **wall** cell, a **mover**
  occupying a new cell each window, and a **door** cell occupied for the first
  half of each period — asserts wall → **Static**, mover → **never static**,
  door → **Periodic**.

Plus **2 node gtests** in `prism_map/test`: `test_grid_math` (world↔grid
round-trip, no rclcpp) and `test_scan_adapter` (a single beam under a 6-DoF
yaw+translation transform lands at the expected map point, z preserved).

CI-equivalent gates: `prism_map_core` builds + all ctest green with the system
toolchain; both packages build clean and test green under colcon in the
`ros2_humble` env.

---

## 7. Build & run

**Core only (no ROS):**
```bash
cmake -S /home/cona/kangj/prism_map/prism_map_core -B /home/cona/kangj/prism_map/build/core -DPRISM_MAP_CORE_BUILD_TESTS=ON
cmake --build /home/cona/kangj/prism_map/build/core -j
( cd /home/cona/kangj/prism_map/build/core && ctest --output-on-failure )
```

**Full ROS 2 build & test (Humble via micromamba; clean env to avoid distro leak):**
```bash
env -u ROS_DISTRO -u ROS_VERSION -u ROS_PACKAGE_PATH \
  /home/cona/.local/bin/micromamba run -n ros2_humble bash -c '
    cd /home/cona/kangj/prism_map_ws &&
    colcon build --symlink-install &&
    colcon test --packages-select prism_map_core prism_map &&
    colcon test-result --verbose'
```

**Run (a pose source — e.g. prism_loc — must already publish `map → sensor` TF):**
```bash
ros2 launch prism_map grid2d.launch.py      # 2D occupancy mapping
ros2 launch prism_map voxel3d.launch.py     # 3D voxel mapping
ros2 service call /prism_map/save_map std_srvs/srv/Trigger   # persist static map
```

---

## 8. Constraints & non-goals

- ROS 2 **Humble**, **C++17**, **Apache-2.0**.
- `prism_map_core`: **no rclcpp, no PCL** — Eigen3 + gtest only; builds with
  system gcc/cmake outside any ROS env.
- Deterministic core: no wall-clock, no `rand()`/`random_device`. "Time" is an
  injected integration tick; FreMEn phase is the window index. Reproducible.
- **Not SLAM.** `prism_map` does not estimate the robot's pose and has no loop
  closure. It **requires an external pose source** publishing `map → sensor` TF
  (e.g. the sibling `prism_loc`, another localizer, or an odometry chain). Drift
  in that pose source corrupts the map; there is no correction here.
- **3D is a voxel-hash, not TSDF.** The voxel backend stores occupancy per voxel,
  not a signed-distance surface — fast and simple, but no sub-voxel surface
  reconstruction. TSDF is roadmap.
- **Large roll/pitch makes the 2D-grid projection approximate.** `grid2d` drops
  z from 6-DoF endpoints; this is exact only for near-planar sensor attitude.
  Use `voxel3d` when the sensor tilts significantly.
- **No multi-session merge or loop closure in v0.1.** Each run builds one map;
  merging maps across sessions and closing loops are roadmap items, alongside
  TSDF surfaces and learned ephemerality.
- No "Generated with Claude" / Co-Authored-By footers anywhere.

---

## 9. Key references

- **ELite** — efficient lifelong/ephemerality-aware mapping (arXiv 2502.13452).
- **Persistence Filter** — Rosen, Mason & Leonard, *Towards Lifelong Feature-Based
  Mapping in Semi-Static Environments* (ICRA 2016) — the survival-decay forgetting
  model.
- **FreMEn** — Krajník et al., *Spatio-Temporal Representation of Dynamic
  Environments* (IEEE T-RO 2017) — the incremental-Fourier periodicity model.
- **Nav2 STVL** — Spatio-Temporal Voxel Layer — decaying voxel occupancy in a
  costmap (lineage for the voxel backend's decay idea).
- **Removert** — Kim & Kim, removing-then-reverting dynamic points from 3D maps
  (lineage for static-map distillation).
- **Occupancy log-odds** — Thrun/Burgard/Fox, *Probabilistic Robotics* ch. 9
  (occupancy grid mapping, inverse sensor model, log-odds accumulation).

Research note (137-source survey, the design's grounding):
`~/kangj/general_vault/Work/ROS2 & AMR Research/평생 매핑 모듈 리서치 자료 (선택형 2D·3D LiDAR, 137선).md`.
