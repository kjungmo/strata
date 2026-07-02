# System section notes: strata mapping backends and ROS 2 node

Source files read: `strata_core/include/strata_core/{map_backend,grid2d_backend,voxel3d_backend,layered_map,types,periodicity}.hpp`,
`strata_core/src/{grid2d_backend,voxel3d_backend}.cpp`, `strata/include/strata/mapping_node.hpp`,
`strata/src/{mapping_node,scan_adapter,cloud_adapter,main}.cpp`, `strata/params/{grid2d,voxel3d}.yaml`,
`strata/launch/{grid2d,voxel3d}.launch.py`, `strata/{CMakeLists.txt,package.xml}`, `strata_core/{CMakeLists.txt,package.xml}`, `README.md` (Architecture section).

## 1. MapBackend interface contract

`strata_core::MapBackend` (`map_backend.hpp`) is a pure abstract base with four methods:

```cpp
struct Observation { std::vector<Eigen::Vector3d> hits; };  // endpoints, MAP frame
class MapBackend {
  virtual void integrate(const Observation& obs, const Eigen::Vector3d& sensor_origin_map) = 0;
  virtual bool tick() = 0;
  virtual std::size_t staticCellCount() const = 0;
  virtual std::size_t transientCellCount() const = 0;
};
```

Contract:
- `Observation::hits` is a flat list of 3D endpoints already expressed in the **map frame** (dimensionality-agnostic — grid2d simply ignores z when it buckets to a 2D cell id; voxel3d uses all three axes). Producing this list from a sensor message plus a `sensor_to_map` transform is the caller's (ROS adapter's) job, not the backend's — the backend never touches TF.
- `integrate()` is one discrete update of the map from one sweep/cloud: for each hit it (a) clears the free-space cells/voxels between `sensor_origin_map` and the hit via a backend-specific ray operation, then (b) registers the hit itself as an occupied observation. Both clearing and hitting are delegated to a shared `LayeredMap` (the log-odds + persistence + periodicity classifier that both backends own by composition, not inheritance).
- `tick()` advances the `LayeredMap`'s temporal window (called once per integration in the node, i.e. once per scan/cloud) and returns a bool (whether a window boundary was crossed — drives periodicity gathering and Schmitt-trigger reclassification inside `LayeredMap`).
- `staticCellCount()` / `transientCellCount()` are read-only introspection, each forwarding to `layered_.staticCells().size()` / `.transientCells().size()`.

Both concrete backends hold a `LayeredMap layered_` member and satisfy the interface purely by translating world-frame geometry into `CellId` (`std::int64_t`) keys that `LayeredMap` accumulates evidence for — the backend split is *only* about how points map to integer cell ids and how the free-space ray is walked; the occupancy/persistence/periodicity state machine itself is backend-agnostic and lives once, in `LayeredMap`.

## 2. grid2d integrate path

`Grid2DBackend(GridMeta meta, LayeredMapParams params)` — `meta` = `{width, height, resolution, origin_x, origin_y}` (a fixed-size, fixed-origin 2D array, no rehashing/growth).

`integrate()` (`grid2d_backend.cpp`):
1. Endpoint projection: each `Observation::hits[i]` is a full 6-DoF point already transformed into the map frame (see §4 — the ROS adapter applies the complete `sensor_to_map` isometry, not a flattened yaw-only pose). `Grid2DBackend` collapses it to 2D by discarding z implicitly: `worldToGrid(meta_, h.x(), h.y(), gx, gy)` buckets the (x,y) pair into an integer grid cell; points outside `[0,width)×[0,height)` are dropped (`if(!worldToGrid(...)) continue`).
2. The sensor origin is projected the same way: `worldToGrid(meta_, sensor_origin_map.x(), sensor_origin_map.y(), sx, sy)`.
3. Ray clearing: `raycastClear(sx,sy,gx,gy)` walks the line from sensor cell to hit cell with integer **Bresenham's algorithm** (the classic `dx/dy/err` decision-variable form, not a supercover/DDA variant) and calls `layered_.observeMiss(gridCellId(meta_,x,y))` on every intermediate cell strictly between endpoints (loop stops when `x==gx1 && y==gy1`, so the hit cell itself is excluded from the miss pass and only gets `observeHit`). Cells outside grid bounds are bounds-checked and skipped, but do not abort the walk.
4. The hit cell itself always gets `layered_.observeHit(gridCellId(meta_,gx,gy))`, and only runs the raycast at all `if(ok)` (i.e. only if the sensor origin itself projected onto the grid).
5. `tick()` simply forwards to `layered_.tick()`.

`toOccupancyGrid()` renders a `nav_msgs`-shaped `GridMap` by initializing every cell to `-1` (unknown) then overwriting, in ascending confidence order, transient→`50`, periodic→`75`, static→`100` (so a cell that is somehow in more than one set ends up at the highest-confidence label — static wins).

## 3. voxel3d integrate path

`Voxel3DBackend(double voxel_size, LayeredMapParams params)` — no fixed extent; the map grows unbounded via a hash map (`LayeredMap`'s `std::unordered_map<CellId, CellEvidence>`), not a preallocated array.

**Voxel hashing** (`voxelId`/`voxelCenter`, `voxel3d_backend.cpp`):
- Each axis is floor-divided by `voxel_size_` (`vc(w,s) = floor(w/s)`), then offset by `kOff = 1<<20` to make the per-axis index non-negative.
- The three 21-bit (`kBits=21`) fields are packed into one 64-bit `CellId` by bit-shifting: `id = (vx<<42) | (vy<<21) | vz`. This gives a signed 21-bit range per axis (≈ ±1,048,576 voxels, i.e. ±~209 km at 0.2 m resolution — not a practical limit) and O(1) hash/unhash with no run-time collision handling beyond `unordered_map`'s own.
- `voxelCenter(id)` is the exact inverse (mask + shift + subtract `kOff`), returning the voxel's center point `(v+0.5)*voxel_size_` per axis.

`integrate()`:
1. For every hit `h`, compute the ray vector `d = h - sensor_origin_map` and its length.
2. **Ray-sample clearing** (not Bresenham/DDA/amanatides-woo traversal): the ray is subdivided into `steps = len / (voxel_size_ * 0.5)` samples (half-voxel spacing) and each interior sample point `sensor_origin_map + st*i` for `i in [1, steps)` is voxel-hashed and marked `observeMiss` — i.e. clearing is done by densely sampling points along the ray and hashing each into its containing voxel, rather than enumerating traversed voxels geometrically. This is simpler than voxel traversal but samples every voxel along a long ray up to (voxel_size/2) redundancy and can skip a thin voxel if `steps` rounds down (guarded only by `std::max(steps,1)` against division by zero, not against under-sampling).
3. The hit itself is always `observeHit(voxelId(h))`, unconditionally (no bounds check — the hash grows to fit).
4. `tick()` forwards to `layered_.tick()`.

`staticPoints()` maps `layered_.staticCells()` (the list of `CellId`s classified static) through `voxelCenter()` to produce a point cloud for publishing/saving.

### grid2d vs voxel3d — side by side

| | Grid2DBackend | Voxel3DBackend |
|---|---|---|
| Cell addressing | 2D array index via `GridMeta` (fixed W×H, fixed origin) | 3D spatial hash, unbounded, `int64` packed key |
| Dimensionality of hits used | x,y only (z implicitly dropped by projection) | x,y,z (full volumetric) |
| Free-space ray algorithm | Bresenham's line algorithm (exact, on-grid) | Ray-sample marching at 0.5-voxel step (approximate, off-grid) |
| Growth | none — pre-sized at construction | unbounded — `unordered_map` grows with exploration |
| Output shape | `nav_msgs/OccupancyGrid`-ready `GridMap` (dense array, `-1/50/75/100`) | list of static voxel-center points → `PointCloud2` |

## 4. Backend-selection mechanism

Selection is a single string ROS parameter, `backend` (`"grid2d"` default, or `"voxel3d"`), read once in `MappingNode`'s constructor (`mapping_node.cpp`). It is **not** polymorphic dispatch over `MapBackend*` inside the node — the node holds *both* `std::unique_ptr<Grid2DBackend> grid_` and `std::unique_ptr<Voxel3DBackend> voxel_` as separate members and an `if (backend_ == "grid2d") { ... } else { /* voxel3d */ ... }` branch at construction time instantiates exactly one of them and wires up backend-specific parameters, publisher, and subscriber:

- `grid2d`: declares `grid_width/height/resolution/origin_x/origin_y`, constructs `Grid2DBackend`, creates the `~/map` `OccupancyGrid` publisher (transient-local QoS) and the `/scan` `LaserScan` subscriber.
- `voxel3d`: declares `voxel_size`, constructs `Voxel3DBackend`, creates the `~/map_points` `PointCloud2` publisher and the `/points` subscriber.

The other backend's `unique_ptr` stays null; `onScan`/`onPoints`/`onPublish`/`onSave` each branch on `backend_` (or implicitly on which subscription fired — `onScan` only exists wired if `grid2d`) to reach the live backend. Two separate launch files (`grid2d.launch.py`, `voxel3d.launch.py`) each load a matching params YAML (`grid2d.yaml` / `voxel3d.yaml`) that sets `backend:` accordingly — so in practice selection happens at launch/config time, and a single running node instance is permanently one backend or the other (no runtime hot-swap).

Both branches share every `LayeredMapParams` field (`layer_interval, l_hit, l_miss, l_min, l_max, survival_decay, graduate_prob, demote_prob, min_observations, prune_prob, enable_periodicity, periodic_amplitude_min, period_windows, n_harmonics`) via the common `readLayerParams()` helper — the persistence/periodicity tuning surface is backend-independent by construction.

## 5. ROS I/O contract

Node: `strata::MappingNode`, executable `strata_node_main`, default node name `"strata"` (so default-namespaced topics/services resolve under `/strata/...` via the `~/` relative names below).

| Direction | Name | Type | QoS | Condition |
|---|---|---|---|---|
| Sub | `scan_topic` param (default `/scan`) | `sensor_msgs/msg/LaserScan` | `rclcpp::SensorDataQoS()` | grid2d only |
| Sub | `points_topic` param (default `/points`) | `sensor_msgs/msg/PointCloud2` | `rclcpp::SensorDataQoS()` | voxel3d only |
| Pub | `~/map` | `nav_msgs/msg/OccupancyGrid` | `QoS(1).transient_local().reliable()` | grid2d only |
| Pub | `~/map_points` | `sensor_msgs/msg/PointCloud2` | `rclcpp::QoS(1)` (volatile, default reliability) | voxel3d only |
| Srv | `~/save_map` | `std_srvs/srv/Trigger` | default | both (writes `.pgm`+`.yaml` for grid2d, `.pcd` for voxel3d, to `save_path` param) |
| Timer | internal, period = `publish_period` param (default 1.0 s) | — | — | drives `onPublish()` for both backends |

TF: the node looks up `lookupTransform(global_frame_, msg->header.frame_id, msg->header.stamp, 0.1s tolerance)` on every incoming scan/cloud — i.e. **`global_frame` (default `"map"`) ← sensor frame**, at the message's own stamp, via `tf2_ros::Buffer` + `TransformListener`. This is a full 6-DoF `tf2::transformToEigen` isometry (`strata_core::Pose3D = Eigen::Isometry3d`), passed whole into `scanToObservation`/`cloudToObservation` — there is no yaw-only flattening on the sensor-input side (flattening, via `flatten(Pose3D)`, exists in `strata_core::types` but is not invoked in this path). Lookup failures are caught (`tf2::TransformException`) and rate-throttled (`RCLCPP_WARN_THROTTLE`, 2 s) rather than crashing the node; that scan/cloud is simply dropped.

Per README: **no** `/initialpose` subscription and **no** `map→odom` broadcast — `strata` is mapping-only; pose must already be published on TF by an external localizer/odometry source (e.g. `prism_loc`). Concurrency: a single `std::mutex mtx_` serializes `onScan`/`onPoints` against `onPublish`/`onSave`, so integration and publish/save cannot interleave.

Occupancy value convention on `~/map` / saved files: unknown = `-1`, transient = `50`, periodic = `75`, static = `100` (grid2d `toOccupancyGrid()`); the same static-cell set becomes points on `~/map_points` for voxel3d (no periodic/transient point output over ROS for voxel3d — those states only affect what does *not* appear yet).

## 6. strata_core vs strata dependency split

`strata_core/package.xml` + `CMakeLists.txt`: only `ament_cmake` (buildtool) + `eigen` (`Eigen3`), plus `ament_cmake_gtest` as a `test_depend`. No `rclcpp`, no `tf2*`, no `sensor_msgs`/`nav_msgs`, no PCL anywhere in this package.

`strata/package.xml` + `CMakeLists.txt`: depends on `strata_core` plus the full ROS 2 surface — `rclcpp`, `rclcpp_components`, `tf2`, `tf2_ros`, `tf2_eigen`, `tf2_geometry_msgs`, `sensor_msgs`, `nav_msgs`, `geometry_msgs`, `std_msgs`, `std_srvs`, `pcl_conversions`, and PCL itself (`find_package(PCL REQUIRED COMPONENTS common io)`).

Why this matters: `strata_core` (types, `LayeredMap`, `PeriodicityModel`, `Grid2DBackend`, `Voxel3DBackend`) is buildable and unit-testable with a **plain system C++17 toolchain** — `cmake -S strata_core -B build/core -DSTRATA_CORE_BUILD_TESTS=ON && ctest` — with no ROS 2 install, no `colcon`, no PCL, and no simulated time/executor/DDS machinery in the loop. This is what the README calls the "gtest-tested… without ROS or PCL" claim, and it's what lets the log-odds/persistence/periodicity/backend-hashing logic (the actual scientific content: Schmitt-trigger graduation, FreMEn-lite periodicity, Bresenham vs ray-sample clearing) be tested deterministically and fast, independent of DDS discovery timing, TF buffer warm-up, or PCL point-type plumbing. The `strata` package is a thin adapter shell around that engine: it owns exactly the ROS-message ↔ `Observation`/`Eigen` conversions (`scan_adapter.cpp`, `cloud_adapter.cpp`), TF lookups, parameter declarations, publishers/subscribers/services/timer, and file I/O (PGM/YAML/PCD) — none of which the core engine's correctness depends on. Practically: CI / development iteration on the mapping algorithm itself never needs a ROS 2 install; only the integration layer does.

## 7. ASCII architecture diagram spec

```
+=====================================================================+
| strata_core  (pure C++17 + Eigen — no ROS, no PCL — gtest-tested)   |
|                                                                      |
|   +-------------------------------------------------------------+  |
|   | LayeredMap  (per-cell evidence, keyed by int64 CellId)       |  |
|   |   log-odds occupancy: observeHit/observeMiss (l_hit/l_miss,  |  |
|   |     clamp [l_min,l_max])                                     |  |
|   |   survival decay (toward "unknown" each window)              |  |
|   |   Schmitt trigger: graduate_prob / demote_prob thresholds    |  |
|   |     --> CellClass: Unknown | Transient | Periodic | Static   |  |
|   |   PeriodicityModel (FreMEn-lite incremental Fourier fit,     |  |
|   |     period_windows, n_harmonics) --> amplitude >= threshold  |  |
|   |     reclassifies Transient -> Periodic                      |  |
|   +-------------------------------^-------------------^---------+  |
|                                   | integrate()/tick() |            |
|            +----------------------+                    +---------+ |
|            |                                                     | |
|   +--------+---------+                              +------------+-+
|   | Grid2DBackend     |                              | Voxel3DBackend|
|   | GridMeta (W,H,res,|                              | voxel_size    |
|   |  origin_x,origin_y|                              | int64 hash id |
|   |  fixed-size array)|                              | (unbounded    |
|   | hit -> worldToGrid|                              |  unordered_map|
|   | clear -> Bresenham|                              | hit -> voxelId|
|   |  line (exact,     |                              | clear -> ray- |
|   |  on-grid ints)    |                              |  sample march |
|   |                   |                              |  (0.5-voxel   |
|   | toOccupancyGrid():|                              |  step points) |
|   |  -1/50/75/100     |                              | staticPoints():|
|   +--------+----------+                              | voxel centers |
|            |                                          +------+-------+
+============|=================================================|=======+
             |  implements MapBackend interface                |
             |  integrate(Observation hits[MAP frame], origin)  |
             |  tick() / staticCellCount() / transientCellCount()
+============|=================================================|=======+
| strata  (ROS 2 node package — rclcpp/tf2/PCL live only here)         |
|            |                                                  |      |
|   +--------v----------------------------------------+         |      |
|   |  MappingNode  (param "backend": grid2d | voxel3d) |         |     |
|   |    selects ONE backend at construction time       |         |     |
|   |                                                    |        |     |
|   |  /scan (LaserScan, SensorDataQoS) --scan_adapter--> Observation
|   |         [full 6-DoF sensor_to_map from TF]         v         |    |
|   |  /points (PointCloud2, SensorDataQoS) --cloud_adapter--------+    |
|   |         [TF lookup: global_frame("map") <- sensor frame]          |
|   |                                                                    |
|   |  onPublish() [timer, publish_period] ------------------------->   |
|   |    grid2d  -> ~/map (OccupancyGrid, transient_local+reliable)     |
|   |    voxel3d -> ~/map_points (PointCloud2, QoS(1))                  |
|   |                                                                    |
|   |  ~/save_map (Trigger) -> grid2d: .pgm+.yaml | voxel3d: .pcd       |
|   +--------------------------------------------------------------+   |
+=======================================================================+

    TF tree (external):  map --(localizer/odom, not strata)--> sensor_frame
    strata reads this TF; it never publishes map->odom or subscribes /initialpose.
```

Figure caption guidance: box the diagram into two horizontal bands (strata_core / strata) split by the `MapBackend` interface line, with the ROS-only dependencies (rclcpp, tf2, PCL) annotated as living exclusively in the lower band; show the two backend boxes as siblings both feeding into/out of one shared `LayeredMap` box; show the external TF arrow (map → sensor frame) entering the node from outside the diagram to make explicit that strata consumes but never produces localization.
