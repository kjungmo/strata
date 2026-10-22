# strata test inventory (paper evidence catalog)

Total test files: 9 (7 in `strata_core/test`, 2 in `strata/test`).
Total `TEST(...)` cases counted: 22
(strata_core: 4 + 1 + 5 + 4 + 1 + 4 + 3 = 22; strata: 3 + 1 = 4 — note strata's
`test_grid_math.cpp`/`test_types.cpp` overlap in content (both testing
`worldToGrid`/`gridToWorld`/`gridCellId`), so combined unique-behavior count is
lower than raw TEST-macro count. Raw TEST-macro total across all 9 files = 26.)

## strata_core/test/test_smoke.cpp (1 test)
- `Smoke.VersionDefined`: asserts `STRATA_CORE_VERSION == "0.1.0"`. Pure build/link sanity check, no logic under test.

## strata_core/test/test_types.cpp (4 tests)
API surface: `worldToGrid`, `gridToWorld`, `gridCellId`, `flatten` (Pose3D->Pose2D), `Pose3D`/`Pose2D`, `GridMeta`.
- `Types.WorldGridRoundTrip`: grid 10x10 res 0.5, origin (0,0). world(1.25,2.75) -> grid(2,5) -> back to world (1.25,2.75) within 1e-9.
- `Types.OutOfBounds`: grid 4x4 res 1.0; world x=-0.1 and x=4.0 (== width*res, off-by-one edge) both rejected by `worldToGrid`.
- `Types.CellId`: grid 10x10 res 1.0; `gridCellId(m,3,4)==43` (row-major: gy*width+gx = 4*10+3=43); confirms (3,4)!=(4,3) id.
- `Types.FlattenYaw`: Pose3D translation (1,2,3), rotation = 90 degree yaw about Z. `flatten()` gives Pose2D x=1,y=2,yaw=pi/2 (within 1e-9) — verifies 6-DoF->2D projection drops z and keeps yaw.

## strata_core/test/test_layered_map.cpp (5 tests)
Core log-odds / Schmitt-trigger / periodicity state machine, tested directly on `LayeredMap` (no grid geometry).
Shared params `P()`: `layer_interval=1` (1 tick=1 window), `l_hit=1.0`, `l_miss=-1.0`, `l_min=-6`, `l_max=6`, `survival_decay=1.0` (forgetting disabled for determinism), `graduate_prob=0.85`, `demote_prob=0.4`, `min_observations=3`, `prune_prob=0.05`, `enable_periodicity=true`, `periodic_amplitude_min=0.3`, `periodicity.period_windows=8`, `periodicity.n_harmonics=2`.
- `LayeredMap.GraduatesWhenProbExceedsThresholdAndObserved`: cell hit each of 2 windows -> log_odds=2.0, still `isStatic==false` (obs<3). 3rd hit -> log_odds=3.0 (sigmoid ~0.95 >= 0.85 threshold) and observations=3 -> `isStatic==true`, `classify==Static`, `occupancyProb>0.85`.
- `LayeredMap.MovingObstacleNeverGraduates`: 12 distinct cells each hit exactly once (obs=1 < min_observations=3) -> none ever static.
- `LayeredMap.SchmittHysteresisDemotesOnlyAfterSustainedFree`: cell hit 4x -> log_odds=4, graduates to Static. One miss -> log_odds=3 (p~0.95, still > demote_prob=0.4) -> stays Static (hysteresis holds). 4 more misses drive log_odds/prob below demote_prob=0.4 -> `isStatic` becomes false.
- `LayeredMap.PeriodicCellClassifiedPeriodicNotStatic`: `graduate_prob` raised to 0.99 (deliberately hard to reach) so periodicity can be distinguished from static. 32 ticks, square-wave pattern hit/miss `(t%8)<4` (period 8, 4-on/4-off) -> `classify==Periodic`, `isStatic==false`.
- `LayeredMap.WindowIntervalGroupsTicks`: `layer_interval=5`; 4 `tick()` calls after hits return false (not yet a full window), 5th returns true; `windowCount()==1` after one full 5-tick window. Verifies tick-batching into windows.

## strata_core/test/test_periodicity.cpp (4 tests)
Direct unit tests of `PeriodicityModel({period_windows, n_harmonics})` Fourier-based occupancy predictor.
- `Periodicity.ConstantOccupiedHasHighMeanLowAmplitude`: period=8, harmonics=2; cell gathered `true` for all 32 timesteps -> `predict(c,100) > 0.9`, `amplitude(c) < 0.2` (no periodic signal, just constant high mean).
- `Periodicity.ConstantFreeHasLowPrediction`: same params, cell gathered `false` x32 -> `predict(c,5) < 0.1`.
- `Periodicity.SquareWaveIsDetectedAndPredicted`: period=8, harmonics=3; 32 timesteps, occupied for `(t%8)<4` (4 periods total) -> `amplitude(c) > 0.3` (clearly periodic), `predict(c, 8+1) > 0.5` (phase 1, in occupied half), `predict(c, 8+6) < 0.5` (phase 6, in free half). This is the deterministic door-periodicity scenario building block reused in `test_integration.cpp`.
- `Periodicity.UnknownCellReturnsHalf`: ungathered cell id 999 -> `predict==0.5` exactly, `has(999)==false`.

## strata_core/test/test_grid2d_backend.cpp (4 tests)
`Grid2DBackend(GridMeta, LayeredMapParams)` — combines ray casting + LayeredMap per grid cell. Grid: 20x20, resolution 1.0, origin (0,0). Params `P()`: `layer_interval=1, l_hit=1.0, l_miss=-1.0, survival_decay=1.0, graduate_prob=0.85, demote_prob=0.4, min_observations=3, prune_prob=0.05, enable_periodicity=false`.
- `Grid2DBackend.HitMarksEndpointAndClearsRay`: single `Observation` hit at world (5.5,0.5,0) from sensor origin (0.5,0.5,0) -> endpoint cell (5,0) classified != Unknown; a cell along the ray but before the endpoint, cell (2,0), is not static (cleared by ray, not marked occupied).
- `Grid2DBackend.RepeatedHitGraduates`: same hit repeated 3x with `tick()` after each -> endpoint cell (5,0) becomes static (`isStatic==true`); `staticCellCount()==1`.
- `Grid2DBackend.RenderOccupancyGrid`: after 3 repeated hits, `toOccupancyGrid()` produces `GridMap` with `data.size()==width*height` (400) and `data[cellId(5,0)]==100` (fully occupied value).
- `Grid2DBackend.SixDofEndpointProjectsToPlane`: hit at elevated z=3.7 from sensor at z=1.2 -> still lands in cell (5,0), i.e. the 2D grid backend ignores/flattens z (6-DoF input, 2D-projected output).

## strata_core/test/test_voxel3d_backend.cpp (3 tests)
`Voxel3DBackend(voxel_size=0.5, LayeredMapParams)` — 3D analog of Grid2DBackend, using same disabled-periodicity `P()` params as test_grid2d_backend.
- `Voxel3DBackend.SamePointSameVoxel`: points (1.1,2.2,3.3) and (1.2,2.1,3.4) (within 0.5 voxel) map to the same `voxelId`; a distant point (9.9,2.2,3.3) maps to a different id.
- `Voxel3DBackend.RepeatedHitGraduatesVoxel`: point (3.0,0.0,0.5) hit 3x (with tick() each) from sensor (0.0,0.0,0.5) -> `staticCellCount()==1`, `staticPoints().size()==1`, and the returned static point's z is near 0.75 within tolerance 0.5 — confirms 6-DoF (z) is preserved in voxel output, unlike the 2D backend.
- `Voxel3DBackend.MovingPointDoesNotGraduate`: 6 distinct points along x=0..5 (one new voxel each integrate+tick) -> `staticCellCount()==0` (never repeated enough at one voxel to graduate).

## strata_core/test/test_integration.cpp (1 test) — key end-to-end scenario
`Integration.WallStaticMoverTransientDoorPeriodic` — the canonical multi-class scenario cited for the paper.
- Grid: `width=40, height=40, resolution=1.0, origin=(0,0)`.
- Params: `layer_interval=1, l_hit=1.0, l_miss=-1.0, survival_decay=1.0, graduate_prob=0.9, demote_prob=0.4, min_observations=5, prune_prob=0.05, enable_periodicity=true, periodic_amplitude_min=0.3, periodicity.period_windows=8, periodicity.n_harmonics=3`.
- Sensor origin fixed at (0.5, 0.5, 0.0).
- Three simultaneous phenomena over 24 windows (`w=0..23`, one `integrate()` + `tick()` per window):
  - **Wall** (static): hit at world (25.5, 0.5, 0.0) -> grid cell (25,0), hit every single window (24/24).
  - **Door** (periodic): hit at world (10.5, 0.5, 0.0) -> grid cell (10,0), hit only when `(w % 8) < 4` (occupied first half of each period-8 cycle, matching the square-wave pattern from test_periodicity.cpp).
  - **Mover** (transient): hit at world (5.5, 5.0+w, 0.0), a distinct grid cell every window (never repeats).
- Assertions after 24 windows:
  - Wall cell (25,0): `isStatic()==true` (graduated after >=5 observations, well past `min_observations=5`).
  - Door cell (10,0): `classify()==CellClass::Periodic` (not Static) and `isStatic()==false` — despite being hit 12/24 times, its alternating pattern keeps it below the static graduate threshold and gets flagged periodic instead.
  - Mover cells: for every `w`, the corresponding grid cell (via `worldToGrid`) is asserted `isStatic()==false` — a moving obstacle never accumulates enough repeat observations at one cell to graduate.
- This is the single deterministic scenario that jointly exercises static/moving/periodic classification through the full `Grid2DBackend` pipeline (ray integration -> LayeredMap per-cell log-odds -> Schmitt hysteresis -> periodicity model), and is the most directly reusable reference for an eval harness (same grid size/params/hit pattern can be replayed against a new evaluator).

## strata/test/test_grid_math.cpp (3 tests) — ROS2 package layer, duplicate of core geometry checks
- `GridMath.WorldGridRoundTrip`: same as `Types.WorldGridRoundTrip` above (grid 10x10 res 0.5).
- `GridMath.OutOfBoundsRejected`: same as `Types.OutOfBounds` (grid 4x4 res 1.0, boundary rejection at x=4.0).
- `GridMath.CellIdRowMajor`: same as `Types.CellId` (grid 10x10 res 1.0, id 43 for (3,4)).
(These duplicate strata_core's own type tests, presumably to confirm the ROS2-wrapped package sees identical behavior through its own include path/build.)

## strata/test/test_scan_adapter.cpp (1 test)
Tests `strata::scanToObservation(sensor_msgs::msg::LaserScan, Pose3D, Eigen::Vector3d& origin_out)` — the ROS2 LaserScan-to-core-Observation adapter.
- `ScanAdapter.SingleBeamUnderYawTranslation`: single-beam scan, `angle_min=0.0`, `angle_increment=pi/2`, `range_min=0.1`, `range_max=30.0`, `ranges={2.0}` (beam along sensor's local +x at 2m). Sensor pose: translation (1.0, 0.0, 0.5), rotation = 90-degree yaw about Z (local +x maps to world +y). Result: `obs.hits[0]` = (1.0, 2.0, ...) i.e. x unchanged at 1.0 (translation only, since local +x becomes world +y and beam length is added there), y = 2.0 (2m beam rotated into +y and added to translation y=0), both within 1e-6; `origin.z()==0.5` within 1e-6, confirming the 6-DoF sensor origin (including z) is passed through/preserved by the adapter even though the 2D grid backend later flattens it.

## Reusable-API notes for a new eval harness (from constants used above)
- `LayeredMapParams` fields exercised: `layer_interval, l_hit, l_miss, l_min, l_max, survival_decay, graduate_prob, demote_prob, min_observations, prune_prob, enable_periodicity, periodic_amplitude_min, periodicity.period_windows, periodicity.n_harmonics`.
- Typical "deterministic" test config sets `survival_decay=1.0` to disable forgetting/decay so log-odds accumulate predictably per hit/miss.
- `Grid2DBackend`/`Voxel3DBackend` API: constructed from `(GridMeta or voxel_size, LayeredMapParams)`; driven via `integrate(Observation, sensor_origin)` then `tick()`; queried via `.layered().classify(id)`, `.layered().isStatic(id)`, `.staticCellCount()`, `.toOccupancyGrid()` (2D only), `.staticPoints()` (3D only), `.voxelId(point)` (3D only).
- `worldToGrid`/`gridToWorld`/`gridCellId` take a `GridMeta{width,height,resolution,origin_x,origin_y}`.
- The integration test's wall/door/mover scenario (grid 40x40 res 1.0, 24 windows, door period 8 with 4-on/4-off, min_observations=5, graduate_prob=0.9) is the best template to replay against a new/alternative evaluator implementation for apples-to-apples comparison.
