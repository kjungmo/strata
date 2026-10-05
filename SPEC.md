# strata — Specification

A real, open-source ROS 2 **Humble** lifelong-mapping tool for mobile robots
that takes **either a 2D LiDAR scan or a 3D LiDAR point cloud** as input and
incrementally builds and maintains a map, coping with dynamic AND semi-static
environments. One Bayesian-persistence **layered engine** drives **two pluggable
geometry backends** (2D occupancy grid or 3D voxel), selected at runtime by a
single parameter. Sensor poses are full 6-DoF.

`strata` is **not SLAM**: it does not estimate pose. It consumes an external
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

`strata` resolves this with a single layered engine that accumulates evidence,
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
  with a strong, statistically significant harmonic is classified **Periodic**
  and predicted per phase, rather than frozen into the static layer.

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

Modeled on ROS 2 mapping conventions and **REP-105**. `strata` is a map
*consumer of pose*, not a localizer: the robot pose enters as a TF lookup, never
as a topic, and there is no `map → odom` output.

### Inputs (subscriptions / TF / params)
| Name | Type | Backend | Notes |
|---|---|---|---|
| `/scan` (`scan_topic`) | `sensor_msgs/LaserScan` | grid2d | planar scan |
| `/points` (`points_topic`) | `sensor_msgs/PointCloud2` | voxel3d | 3D cloud |
| pose | **TF** `global_frame → <sensor frame_id>` | both | full 6-DoF `Isometry3d`, looked up at the message stamp; *not* a topic |
| `/tf`, `/tf_static` | `tf2_msgs/TFMessage` | both | the external pose source (e.g. `prism_loc`) must supply this chain |

There is **no `/initialpose`** — `strata` does not initialize a filter.

### Outputs
| Name | Type | Backend | Notes |
|---|---|---|---|
| `~/map` (`/strata/map`) | `nav_msgs/OccupancyGrid` (transient_local) | grid2d | static→100, periodic→75, transient→50 (last observed as a hit, or leans occupied), free→0 (last observed free, kept after pruning), unknown→-1 |
| `~/map_points` (`/strata/map_points`) | `sensor_msgs/PointCloud2` | voxel3d | centers of graduated static voxels, in `global_frame` |
| `~/save_map` (`/strata/save_map`) | `std_srvs/srv/Trigger` (service) | both | grid2d → PGM + map_server YAML (`image:` relative to the YAML, resolution and origin written to read back exactly); voxel3d → PCD. `success: false` with the path and reason when a file cannot be written (e.g. a missing directory) or voxel3d has no static voxel |
| `/diagnostics` | `diagnostic_msgs/DiagnosticArray` | both | once a second (wall timer), status `<node name>: input rate` (hardware_id: namespace and input topic): window_mode, sensors, scans_in_window, empty_windows, window_duration_s, input_rate_hz, nominal_rate_hz, dropped_in_window, tf_failures_in_window, recent_drop_fraction, mean_window_duration_s, window_duration_cv, effective_period_s (from the last window close; one rate monitor per sensor frame, at most 16, a frame with no message over 10 window closes is forgotten), seconds_since_last_message, windows_closed_total. From the third window: WARN in `scans` mode on loss above `rate_warn_drop_fraction`, duration cv > 0.2, or (with `expected_scan_rate_hz` > 0, per sensor) a mean window more than 10 % off `layer_interval / (expected_scan_rate_hz x sensors)`; in `time` mode on empty windows or a window with under half the expected messages; in both on more TF failures than integrated messages or more than 16 frames. Also WARN on zero stamps in the last second (dropped in `time` mode) or, in `time` mode, when no window has closed for 10 window periods (and 2 s; ROS clock under `use_sim_time`) although messages arrive; WARN when `use_sim_time` is true and the ROS clock has not advanced for 3 s while messages arrive (no `/clock`); WARN (ERROR once something was integrated) when messages arrive but TF lookups fail and nothing was integrated for `startup_timeout_s` (`input_timeout_s` once something was); ERROR after `input_timeout_s` without input, or `startup_timeout_s` before the first message |

### Frames (REP-105)
The node looks up `T_global_sensor` (`global_frame → sensor frame_id`) at each
message stamp and transforms every endpoint into `global_frame` before
integrating. It waits up to 0.1 s for a transform that is not there yet,
counted on the steady clock and polling the buffer, so a frozen sim clock (no
`/clock`) cannot block the callback; a lookup still missing after 0.1 s counts
as a TF failure and the message is dropped. The sensor origin (ray start, for clearing) is the translation of
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
graduate: !graduated && !periodic && p >= graduate_prob && observations >= min_observations  -> graduated = true
demote  :  graduated && (p <= demote_prob || periodic)                                     -> graduated = false
```
The two thresholds (with `demote_prob < graduate_prob`) form the hysteresis band
that prevents flicker. A graduated cell is **Static** — it belongs to the durable
map. `periodic` (§3.5) keeps a semi-static cell out of the static layer; it has
no band of its own, but because the amplitude is mean-centred it can only fire on
a cell that has been observed free in a sizeable fraction of its touched windows
(`amplitude <= 4 m (1 - m)` for touched-window occupancy mean `m`, so the default
`periodic_amplitude_min = 0.3` needs roughly 8–92 % occupancy), and since the
predicate also requires noise-calibrated significance (§3.5) it fires on an
iid-noisy wall only with small, bounded probability per window.

### 3.5 FreMEn periodicity
When `enable_periodicity` is set, every touched cell feeds its per-window state
into `PeriodicityModel`, which keeps incremental Fourier coefficients over a
`period_windows`-long period with `n_harmonics` harmonics (phase
`ω = 2π / period_windows`). It exposes:
- `predict(phase) → P[0,1]` — predicted occupancy at a given window phase
  (`0.5` when the cell is unknown);
- `amplitude` — the dominant-harmonic magnitude (periodicity strength).

The coefficients are **mean-centred** as in FreMEn:
`γ_k = (1/n) Σ_w (o_w − ō) e^{i(k+1)ωw}` over the `n` touched windows, computed
incrementally by also accumulating `Σ_w e^{i(k+1)ωw}`; the amplitude is
`max_k 2|γ_k|`. A cell that is occupied (or free) in every touched window
therefore has amplitude exactly 0 regardless of *when* it is observed — without
centring, the amplitude measures the phase coverage of the observations, and a
wall seen on a revisit loop commensurate with the period reads as Periodic.

**Noise-calibrated periodic test.** The amplitude alone is not a test: for a
cell whose occupancy is pure noise, `o_w ~ Bernoulli(m)` independent of phase,
each centred coefficient has standard deviation `sqrt(2 m (1 - m) / n)` (0.25 at
`m = 0.5`, `n = 8`), so a fixed `periodic_amplitude_min` is crossed by chance at
short observation lengths. The predicate is therefore

```
periodic(cell) := n >= period_windows  AND  exists harmonic k:
                    amplitude_k >= periodic_amplitude_min            (effect size)
                AND n_harmonics * B(dchi_k) <= periodic_false_alarm  (significance)
B(d) = x e^{1 - x} with x = 2 d   (B = 1 for x <= 1)
```

`dchi_k = y_kᵀ M_k⁻¹ y_k` is the occupancy variance explained by a least-squares
sinusoid at harmonic `k` with a floating mean (the generalised Lomb–Scargle
periodogram): `y_k = Σ (o_w − ō)(cos θ, sin θ)` and `M_k` the centred phase-design
matrix of the touched windows, kept incrementally by also accumulating
`Σ cos 2θ, Σ sin 2θ`. Because the centred phases sum to zero,
`y_k = Σ (o_w − m)(cos θ − c̄, sin θ − s̄)` for *any* `m`, so under the noise null
`dchi_k` is a quadratic form in independent `1/4`-sub-Gaussian variables
(Hoeffding's lemma) whose whitened design is orthonormal; Gaussian decoupling
gives `E exp(λ dchi) ≤ (1 − λ/2)⁻¹` and Chernoff gives `P(dchi ≥ r) ≤ B(r)`, for
every `m`, every `n` and every *non-adaptive* set of touched phases. The
Bonferroni factor `n_harmonics` covers the candidate harmonics. The threshold
thus scales as `n^{-1/2}` in amplitude and needs no knowledge of `m`
(`B` uses the worst case `m = 1/2`). Equivalently, at uniform phase coverage
`dchi ≈ n a² / 2`, so the amplitude threshold is `a*(n) ≈ sqrt(2 r* / n)` with
`B(r*) = α / H`.

The bound is conservative. `periodic_false_alarm = 0.1` (the nominal level) was
chosen on a calibration set disjoint from all evaluation seeds
(`paper/experiments/src/e0_calibration.cpp`, added with the paper in PR #1;
seeds `20260928 + offsets`): it is
the largest candidate in {0.01, 0.02, 0.05, 0.1, 0.2} whose measured
false-alarm rate stayed `<= 0.01` over the whole null grid (both `(T, H)`
configurations, `m ∈ [0.05, 0.95]`, `n ≥ T`) and in the live pipeline with
pruning. Pruning makes the observed sample adaptive (a cell survives only if its
log-odds did not fall), so there the rate is measured, not guaranteed. The price
of calibration is detection delay: a 50 %-duty door needs about two periods of
touches instead of one. Setting `periodic_false_alarm >= 1` disables the
significance term and restores the amplitude-only rule.

**The amplitude is only meaningful after a full period has been observed** —
before `period_windows` touched windows the harmonic estimate is withheld, so a
freshly seen oscillating cell will not yet read as Periodic.

### 3.6 Classification & pruning
```
classify(cell):
  graduated                                     -> Static
  enable_periodicity && periodic(cell)          -> Periodic   (§3.5)
  p >= prune_prob                               -> Transient
  else                                          -> Unknown
prune: erase cell if  !graduated && p < prune_prob && amp < periodic_amplitude_min
```
Static cells are never pruned; periodic cells survive on their amplitude even
when momentarily free.

The prune guard deliberately uses the lenient effect-size screen
(`amp < periodic_amplitude_min`), not the significance test: a *candidate*
periodic cell keeps its Fourier history, and so its growing `n`, until the test
resolves it. Pruning still discards the Fourier history of every other cell.
Keeping history after erasure was considered and rejected: the purpose of
pruning (§1, §3.3) is that clutter does not accumulate, and a per-cell Fourier
record for every cell ever touched would grow without bound, most of all in
`voxel3d`, where every sampled free voxel becomes a cell. A re-created cell starts
from `n = 0`; the significance test is calibrated at every `n`, so a short
history raises the threshold instead of producing false detections.

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

Two ament packages in one repo. **All algorithmic code lives in `strata_core`,
which has NO ROS and NO PCL dependency** — Eigen3 + gtest only — so it builds and
unit-tests with the plain system toolchain. The ROS/PCL surface is confined to
`strata`.

```
strata_core/                         # pure C++17 + Eigen, no ROS, no PCL
  include/strata_core/
    version.hpp           # STRATA_CORE_VERSION "0.1.0"
    types.hpp             # Pose2D, Pose3D(=Isometry3d), GridMeta, GridMap, CellId, world<->grid, flatten
    periodicity.hpp       # PeriodicityModel (FreMEn-lite incremental Fourier)
    layered_map.hpp       # LayeredMap + LayeredMapParams + CellClass + CellEvidence (THE HEART)
    map_backend.hpp       # MapBackend interface + Observation
    grid2d_backend.hpp    # Grid2DBackend (2D occupancy, Bresenham clearing)
    voxel3d_backend.hpp   # Voxel3DBackend (voxel-hash, ray-sample clearing)
  src/                    # types, periodicity, layered_map, grid2d_backend, voxel3d_backend
  test/                   # test_smoke, test_types, test_periodicity, test_layered_map,
                          # test_grid2d_backend, test_voxel3d_backend, test_integration

strata/                              # ROS 2 Humble node
  include/strata/mapping_node.hpp
  src/mapping_node.cpp    # subs/pub/TF, backend select, save service, calls core
  src/scan_adapter.cpp    # LaserScan + 6-DoF Pose3D -> Observation (map frame)
  src/cloud_adapter.cpp   # PointCloud2 + 6-DoF Pose3D -> Observation (PCL)
  src/main.cpp
  launch/grid2d.launch.py
  launch/voxel3d.launch.py
  params/grid2d.yaml
  params/voxel3d.yaml
  rviz/strata.rviz
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
| `periodic_amplitude_min` | `0.3` | minimum harmonic amplitude (effect size) to classify Periodic |
| `periodic_false_alarm` | `0.1` | nominal level of the Chernoff significance test (§3.5); `>= 1` disables it |

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
| `window_mode` | `scans` (code); `time` in the shipped YAMLs | `scans`: close a window every `layer_interval` integrated messages (the engine rule); `time`: every `window_period_s` of message time (integer ns; re-anchors on a stamp more than half a window behind the newest or 10^6 windows ahead, closing the open window first; zero stamps are dropped), robust to lost messages; the periodic period is `period_windows x window_period_s` |
| `window_period_s` | `1.0` | window length in seconds when `window_mode` is `time` |
| `expected_scan_rate_hz` | `0.0` | per-sensor rate for the loss estimate and the scan-window check; `0` estimates it from stamps |
| `rate_warn_drop_fraction` | `0.05` | `/diagnostics` warns above this share of lost messages (scan windows) |
| `input_timeout_s` | `5.0` | `/diagnostics` turns ERROR after this long without input, once input was seen |
| `startup_timeout_s` | `30.0` | ... and after this long if no input has arrived since start |
| `save_path` | `/tmp/strata_2d` (grid2d) / `/tmp/strata_3d` (voxel3d) | save-service output path stem |

---

## 6. Test strategy

Every algorithmic claim has a deterministic gtest (injected window index, no
wall-clock, no `rand()`; the statistical tests use fixed-seed `std::mt19937`
streams) in `strata_core/test`, runnable with **no ROS** — **56 gtests across 9
suites**:

- **Smoke** (1): version macro is defined.
- **Types** (4): world↔grid round-trip; out-of-bounds rejection; cell-id
  uniqueness; `flatten` of a 6-DoF transform recovers x, y, yaw.
- **Periodicity** (8): constant-occupied → high mean, low amplitude;
  constant-free → low prediction; square wave → detected and phase-predicted;
  unknown cell → `0.5`; centred amplitude of an always-occupied cell is 0 at
  every length (T=8 and T=24) and under a revisit loop; square wave detected at
  non-multiples of T.
- **PeriodicitySignificance** (7): the Chernoff bound is a valid, monotone tail
  bound; Bernoulli(m) noise is Periodic at rate `<= periodic_false_alarm` and
  `<= 0.01` over m ∈ [0.1, 0.9] and n up to 240 (T=8/H=3 and T=24/H=2); the
  amplitude-only rule would fire on > 20 % of such cells; square-wave doors are
  still detected (50 %-duty from n = 16, 25 %-duty from n = 32); a constant cell is
  never Periodic under any sampling; nothing is Periodic below the n ≥ T gate.
- **LayeredMap** (18): graduates only when P(occ) ≥ threshold AND observed ≥
  `min_observations`; a moving obstacle (each cell hit once) never graduates;
  Schmitt hysteresis demotes only after sustained free; a square-wave cell is
  classified Periodic, not Static; `layer_interval` groups ticks into windows;
  an always-hit wall is never Periodic (T=8, and shipped T=24); a revisit-loop
  wall stays Static; the E2 door is Periodic from window 16 on; periodic
  demotion needs contradicting free evidence; a graduated door is demoted once
  Periodic; Bernoulli clutter through pruning is Periodic at rate `<= 0.01`;
  `periodic_false_alarm >= 1` restores the amplitude-only rule; alpha spending
  is the shipped rule; with spending, a graduated door is still demoted once
  Periodic, a noisy wall is rarely ever demoted by the periodic path, and pruned
  clutter is rarely ever Periodic; `closeWindows(k)` equals k `endWindow()` calls.
- **PeriodicitySpending** (4): the spent levels are zero below the n ≥ T gate
  and sum to alpha; the per-read-out rule is not trajectory-valid but spending
  is; doors are still detected, later; the amplitude-only rule is unaffected.
- **Grid2DBackend** (10): a hit marks the endpoint and clears the ray; repeated
  hits graduate; occupancy-grid render (100 static, 50 transient, 0 free, -1
  unknown); a cleared cell stays free after pruning; a freed transient reads
  free once a window observes it free; a new obstacle on cleared ground reads
  50 from its first hit; an obstacle last seen hit stays 50 out of view until
  observed free; `closeWindows` closes windows whatever the tick count, and a
  silent gap leaves evidence untouched; a 6-DoF (elevated) endpoint projects to the plane.
- **Voxel3DBackend** (3): same world point → same voxel id; repeated hits
  graduate a voxel with z preserved; a moving point never graduates.
- **Integration** (1): a deterministic room — a fixed **wall** cell, a **mover**
  occupying a new cell each window, and a **door** cell occupied for the first
  half of each period — asserts wall → **Static**, mover → **never static**,
  door → **Periodic**.

Plus **19 node gtests** in `strata/test`: `test_grid_math` (3; world↔grid
round-trip, no rclcpp), `test_scan_adapter` (1; a single beam under a 6-DoF
yaw+translation transform lands at the expected map point, z preserved),
`test_window_clock` (6; anchoring, gaps close every skipped window, epoch
nanosecond stamps close exactly every period, reorder kept and a bag loop
re-anchors, a short bag loop re-anchors, a huge forward jump re-anchors) and
`test_rate_monitor` (4; a steady stream has no loss, 40 % loss is estimated, the
sensor rate overrides the estimate, a back jump restarts the intervals) and
`test_map_writer` (5; PGM rows top down with the expected shades and a YAML
naming the image relatively, resolution and origin read back within 1e-12, a
moved pair still resolves its image, a missing directory is reported as a
failure naming the path, a grid whose data does not match its size is refused).

CI-equivalent gates: `strata_core` builds + all ctest green with the system
toolchain; both packages build clean and test green under colcon in the
`ros2_humble` env. On Humble, CI also launches each backend with its shipped
YAML and checks every parameter is applied (`scripts/check_param_binding.py`),
and feeds five synthetic scenarios through the real topics and TF
(`scripts/synthetic_e2e.py`). One grid2d scenario adds `--check-map-server` on a
grid origin of (-10.000001234567891, -9.999998765432109): after `~/save_map` it
moves the saved pair to a fresh directory and loads the YAML there in
`nav2_map_server` (own namespace,
lifecycle configure + activate) and requires the map it serves to equal the last
published `/strata/map` in width, height, resolution and origin (within 1e-9)
and cell by cell under 0→0, 100→100, 50/75/-1→-1 (zero mismatches; served
values only -1, 0, 100), after asserting the published map did not change
across the save. A last step launches grid2d with `use_sim_time:=true`, feeds
it scans with no `/clock` and no TF (`scripts/check_sim_clock_stall.py`) and
requires `ros2 param get` to answer within 8 s, at least 9 `/diagnostics`
statuses over 11 s of scans, and a WARN that the ROS clock is not advancing.

---

## 7. Build & run

**Core only (no ROS):**
```bash
cmake -S /home/cona/kangj/strata/strata_core -B /home/cona/kangj/strata/build/core -DSTRATA_CORE_BUILD_TESTS=ON
cmake --build /home/cona/kangj/strata/build/core -j
( cd /home/cona/kangj/strata/build/core && ctest --output-on-failure )
```

**Full ROS 2 build & test (Humble via micromamba; clean env to avoid distro leak):**
```bash
env -u ROS_DISTRO -u ROS_VERSION -u ROS_PACKAGE_PATH \
  /home/cona/.local/bin/micromamba run -n ros2_humble bash -c '
    cd /home/cona/kangj/strata_ws &&
    colcon build --symlink-install &&
    colcon test --packages-select strata_core strata &&
    colcon test-result --verbose'
```

**Run (a pose source — e.g. prism_loc — must already publish `map → sensor` TF):**
```bash
ros2 launch strata grid2d.launch.py      # 2D occupancy mapping
ros2 launch strata voxel3d.launch.py     # 3D voxel mapping
ros2 service call /strata/save_map std_srvs/srv/Trigger   # persist static map
```

---

## 8. Constraints & non-goals

- ROS 2 **Humble**, **C++17**, **Apache-2.0**.
- `strata_core`: **no rclcpp, no PCL** — Eigen3 + gtest only; builds with
  system gcc/cmake outside any ROS env.
- Deterministic core: no wall-clock, no `rand()`/`random_device`. "Time" is an
  injected integration tick; FreMEn phase is the window index. Reproducible.
- **Not SLAM.** `strata` does not estimate the robot's pose and has no loop
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
