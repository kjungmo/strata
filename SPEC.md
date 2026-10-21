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
- **Graduation by a sequential test** (`evidence_model = sprt`, the default since
  0.2.0) — a cell promotes to **Static** when a Wald probability-ratio test on its
  per-window hit rate accepts "structure" (rate at or above `m1`) against "clutter"
  (rate at or below `m0`), at stated error rates `alpha` and `beta` (§3.2a). Three
  outcomes are possible: Static, rejected, and undecided. The v0.1 rule is still
  available as `evidence_model = legacy`.
- **Schmitt-trigger graduation** (`evidence_model = legacy`, the v0.1 rule) — a cell
  promotes to **Static** when its occupancy probability rises above `graduate_prob`
  (and it has been observed at least `min_observations` times), and demotes only
  when it falls below the lower `demote_prob`. The `[demote_prob, graduate_prob]`
  gap is hysteresis: it stops a cell flickering between map and not-map on
  borderline evidence.
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
| `~/save_map` (`/strata/save_map`) | `std_srvs/srv/Trigger` (service) | both | grid2d → PGM + map_server YAML (`image:` single-quoted and relative to the YAML, resolution and origin written to read back exactly, "C" locale); voxel3d → PCD. Written to `.tmp` files and fsynced, then renamed and the directory fsynced: a save that fails before the renames leaves the previous pair untouched; the PGM is renamed first and the YAML second, so a crash or failure between the two renames can pair the new PGM with the previous YAML (map_server takes the image name from the YAML, so no rename order avoids this with fixed names). To make that pair detectable the YAML also carries `strata_image_bytes` and `strata_image_sha256` (size and SHA-256 of the exact PGM bytes written; map_server ignores them and does not check them, `check_saved_map.py` does, see §6.1). `success: false` with the path and reason when a file cannot be written (e.g. a missing directory), `save_path` has no file name (empty or ending in `/`), or voxel3d has no static voxel |
| `/diagnostics` | `diagnostic_msgs/DiagnosticArray` | both | once a second (wall timer), status `<node name>: input rate` (hardware_id: namespace and input topic): window_mode, sensors, scans_in_window, empty_windows, window_duration_s, input_rate_hz, nominal_rate_hz, dropped_in_window, tf_failures_in_window, recent_drop_fraction, mean_window_duration_s, window_duration_cv, effective_period_s (from the last window close; one rate monitor per sensor frame, at most 16, a frame with no message over 10 window closes is forgotten), seconds_since_last_message, windows_closed_total, and, in every status from the first one, messages_received_total, messages_integrated_total, tf_failures_total, tf_failures_since_integrate, seconds_since_last_integrate, last_tf_error (see "Input states on `/diagnostics`" below). From the third window: WARN in `scans` mode on loss above `rate_warn_drop_fraction`, duration cv > 0.2, or (with `expected_scan_rate_hz` > 0, per sensor) a mean window more than 10 % off `layer_interval / (expected_scan_rate_hz x sensors)`; in `time` mode on empty windows or a window with under half the expected messages; in both on more TF failures than integrated messages or more than 16 frames. Also WARN on zero stamps in the last second (dropped in `time` mode) or, in `time` mode, when no window has closed for 10 window periods (and 2 s; ROS clock under `use_sim_time`) although messages arrive; WARN when `use_sim_time` is true and the ROS clock has not advanced for 3 s while messages arrive (no `/clock`); WARN (ERROR once something was integrated) when messages arrive but TF lookups fail and nothing was integrated for `startup_timeout_s` (`input_timeout_s` once something was), and before that the message names the failing lookup at the level the other checks give; ERROR after `input_timeout_s` without input, or `startup_timeout_s` before the first message |

### Input states on `/diagnostics`

Six values are in **every** `<node name>: input rate` status, from the first one,
after the values listed above and in this order (the window values need a closed
window, and no window closes while nothing integrates):

| Key | Value |
|---|---|
| `messages_received_total` | messages the input callback has handled since the node started: TF failed or resolved (messages the middleware dropped before the callback are not in it) |
| `messages_integrated_total` | those that went into the map |
| `tf_failures_total` | those dropped because the TF lookup `global_frame → <frame_id of the message>` failed |
| `tf_failures_since_integrate` | the running failure streak: TF failures since the last integrated message, `0` again as soon as one integrates |
| `seconds_since_last_integrate` | steady seconds since the last integrated message; until the first one, since the node started |
| `last_tf_error` | why the last lookup of the running streak failed: the tf2 exception text as one line (control characters and line breaks become one space, bytes that are not well-formed UTF-8 become `?`, at most 120 bytes, ending in `...` when cut). Set exactly while `tf_failures_since_integrate` is above 0; the empty string otherwise |

`last_tf_error` is cleared when a message integrates and does not keep the last
text: a status that reads `ok` beside an old error would have to be read
together with the streak to be understood, and this way the value is non-empty
if and only if lookups are failing now. The cost is that the cause of a failure
between two integrated messages is only in the node log (`scan TF: ...` /
`points TF: ...`, at most every 2 s) and counted in `tf_failures_total`.

The counts are decimal strings (64-bit counters; the seconds with two decimals),
taken over all sensor frames on the input topic, for both backends. They are
written by the input callback and read by the 1 Hz diagnostics wall timer under
the node's one mutex. The times are steady-clock times, so `use_sim_time`, a
slow bag or a missing `/clock` does not change them.

The status then says which input state the node is in. `startup_timeout_s`
(default 30 s) is how long the node may run before its first message **and**
before its first integrated message without the level rising; `input_timeout_s`
(default 5 s) is the same allowance once messages were arriving or integrating.
Each limit is exclusive: a state changes when the time is greater than it.

| # | State | Level | Message |
|---|---|---|---|
| 1 | no message yet, up to `startup_timeout_s` after start | OK | `waiting for input on <topic>` |
| 2 | no message yet, after `startup_timeout_s` | ERROR | `no input on <topic> yet after N s` |
| 3 | messages arrived, then none for more than `input_timeout_s` | ERROR | `no input on <topic> for N s (sensor, driver or link down?)` |
| 4 | messages arrive, TF lookups fail, nothing integrated yet, up to `startup_timeout_s` after start | OK (unchanged) | `no usable TF <global_frame> -> <frame> yet: N message(s) received on <topic>, 0 integrated, N TF lookup(s) failed (WARN after startup_timeout_s = 30 s)` |
| 5 | the same after `startup_timeout_s` | WARN | `messages arrive on <topic> but the TF lookup <global_frame> -> <frame> has failed N times; nothing integrated for N s` |
| 6 | integrating (no running streak) | from the window and stamp checks | `ok`, or those warnings |
| 7 | was integrating; a streak runs, and something integrated within the last 1 s | unchanged | unchanged (the streak shows in `tf_failures_since_integrate` and `last_tf_error` only) |
| 8 | ... nothing integrated for more than 1 s, up to `input_timeout_s` | unchanged | `TF lookup <global_frame> -> <frame> failing: last N message(s) on <topic> not integrated, nothing integrated for N s (ERROR after input_timeout_s = 5 s)` |
| 9 | ... nothing integrated for more than `input_timeout_s` | ERROR | as row 5 |

`<frame>` is the `frame_id` of the last message whose lookup failed (as one line
of at most 64 bytes; `(empty frame_id)` when it is empty), and the numbers in
the parentheses are the configured timeouts. The messages name the lookup that
fails and not its cause: the transform may be absent, too old or too new for the
stamp, or the frame unknown. `last_tf_error` gives the cause.

The 1 s of rows 7-8 runs from the last integrated message, not from the first
failure. One failed lookup leaves a gap of two message intervals, so with input
faster than 2 Hz a single late transform stays in row 7 and does not change the
message; at 2 Hz or slower one failed lookup is already named (row 8) until the
next message integrates. Timing the streak from its first failure would need one
more time stamp and would still name a single failure at 1 Hz or slower, so the
existing time since the last integrated message is used, which is also the one
the ERROR of row 9 has always used.

Rows 1-3, 5 and 9 replace the level and message of the window and stamp checks.
Rows 4 and 8 are notes: they leave the level alone, replace a bare `ok` and are
appended (after `; `) to any other warning. Under `use_sim_time` without
`/clock`, the stalled-clock WARN is put in front of the note, so the status
reads `use_sim_time is true but ...; no usable TF ...` at WARN. Messages whose
TF resolved but which were dropped for a zero stamp (`time` windows) count as
received, not as integrated and not as TF failures, and do not end a streak; the
zero-stamp WARN reports them. Once a message integrates, rows 4-5 never return:
the node is in rows 6-9.

Why row 4 stays OK: at bring-up the mapper is commonly up before the localizer
publishes `global_frame`, and `startup_timeout_s` is the parameter that says for
how long that is expected. A WARN on every launch would be routine noise, and
the levels an aggregator or an existing consumer sees are the same as before;
what changed is that the message and the values no longer say `ok` with nothing
behind it. The node also logs the note every 30 s (`input rate: no usable TF
...`), where it logged `input rate: ok` before.

### Frames (REP-105)
The node looks up `T_global_sensor` (`global_frame → sensor frame_id`) at each
message stamp and transforms every endpoint into `global_frame` before
integrating. It polls the buffer for a transform that is not there yet
(`TfWaitRule`, `TfWait`): 0.1 s of ROS time, capped at 1 s of steady time; a
clock unchanged for 1 s is treated as frozen and no longer waited on (the
callback where it stops can wait up to the cap). "Unchanged" is observed across
calls (`RosClockWatch`, fed by every wait and the diagnostics timer), so a coarse
sim clock stepping less often than every 0.1 s of wall time is still waited on,
as long as it steps at least once per second of wall time; a sparser clock
counts as frozen. When a clock steps past the budget, the lookup is retried
for 20 ms more of steady time, for a transform published with that step: a
clock that moves less than 10 ms between two polls (2 ms apart: a live robot,
or playback below about 5x) stops at the budget; otherwise the extra 20 ms
applies. A clock that jumps back restarts the budget. The tolerance is the same at
playback rates down to 0.1x; below that the cap shortens it. A failed lookup
therefore holds the sensor callback for up to that long; it counts as a TF
failure and the message is dropped. The `/diagnostics` stalled-clock WARN uses
the same observation, after 3 s unchanged while messages arrive. The sensor origin (ray start, for clearing) is the translation of
that transform. The full 6-DoF transform is used: `voxel3d` keeps z and attitude
volumetrically; `grid2d` projects transformed endpoints onto its 2D plane.

---

## 3. Algorithm

### 3.1 Layered evidence per cell
Each known cell holds `{ log_odds, observations, window_hits, window_misses,
graduated }` (24 bytes in both evidence models). Observations arrive per integration frame as **hits** (an endpoint
fell in the cell) and **misses** (a ray passed through the cell — free-space /
negative information). Frames are grouped into **windows** of `layer_interval`
ticks; the layered update runs once per window (`endWindow`).

### 3.2 Log-odds occupancy (`evidence_model = legacy`)
This section and §3.3–§3.4 describe the v0.1 rule. The default rule is §3.2a.
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

### 3.2a Sequential test (`evidence_model = sprt`, default since 0.2.0)

**What you state.** `m0`: the per-window hit rate at or below which a cell is clutter.
`m1`: the rate at or above which it is structure. `alpha`: the chance that clutter at
`m0` graduates. `beta`: the chance that structure at `m1` is rejected. Everything else
is derived once, in `deriveSprt()`:

```
l_hit  = ln(m1 / m0)                 added in a hit window
l_miss = ln((1 - m1) / (1 - m0))     added in a miss window (negative)
A      = ln((1 - beta) / alpha)      graduate at l >= A
B      = ln(beta / (1 - alpha))      reject at l <= B, once n >= T
ceiling = A + 2 l_hit                 upper clamp (sprt_ceiling overrides it)
```

**Update at window close**, for a touched cell (`n` counts the touched windows of the current
test, and a reverse-test demotion restarts it at 0 with `l`; `T` is `period_windows`):

```
l += l_hit (occ) or l_miss (free)        no multiplier: exp(l) is a supermartingale for every rate <= m0, up to the first graduation or floor window
l  = min(l, ceiling)
n += 1
if n >= T: l = max(l, B)                 no lower clamp before the periodicity gate
graduate : !graduated && !periodic && l >= A          -> graduated, l := ceiling
demote   : graduated && (periodic || l <= ceiling - A) -> not graduated; after the reverse test l := 0, n := 0
reject   : !graduated && n >= T && l <= B && amp < periodic_amplitude_min -> erase the cell
```

If the periodic predicate and the reverse test both fire in the same window, the periodic path
wins: the cell stops being Static and keeps `l` and `n`; the restart `l := 0, n := 0` runs only
when the reverse test fires alone. (Test `Sprt.PeriodicPathWinsWhenTheReverseTestFiresInTheSameWindow`:
a door open 17 and closed 7 windows of 24, tie at window 45.)

A rejected cell is erased. A later observation (hit or miss) creates a new cell, which starts a new history
at `n = 0`. A cell that is still a periodic candidate (`amp >= periodic_amplitude_min`)
is kept until the periodicity test decides.

**Why no lower clamp before the gate.** The periodicity test withholds its answer
until `n >= T` windows. If a cell could be rejected earlier, a door with a long closed
phase would be erased before it can be read as Periodic (the prune/maturity race of
v0.1). Clamping at `B` before the gate would also raise the false-Static rate on clutter
from 2.8 % to 3.5 % and halve the structure rejects (2.4 % to 1.3 %), at the pair
(0.15, 0.65) on LILocBench with 0.67 s windows: rows 2 and 3 of
`paper/experiments/realdata/results_p2/replay_advisory_table.txt` state this trade.

**Demotion is a one-sided reverse test.** A graduated cell sits at the ceiling. It is demoted
when `l` falls by `A` from there: `ceil(A / |l_miss|)` all-miss windows (5 at the
default pair, 6 at the pair (0.15, 0.65)). One miss never demotes. The test is CUSUM-like
(the ceiling reflects the statistic from above). It is not the SPRT in the opposite direction
and has no error bound per history; a wall at `m1` is demoted after a finite mean time that
this repository does not bound. After a demotion by
the reverse test the forward test restarts from `l = 0` and `n = 0`: the restarted test
has the same no-clamp grace before `T`, and the same `n_max_undecided` read-out, as a new
cell. A cell demoted after 100 hit windows is therefore not `Intermittent` until its new test
has run `n_max_undecided` windows.

**Truncation.** An undecided, non-periodic cell is read out `Intermittent` once
`n >= n_max_undecided` (`0`: never), where `n` is the window count of the current test
(restarted by a reverse-test demotion). It is a read-out only; the test continues, and the
cell can still graduate or be rejected. `n_max_undecided = 80` is the smallest multiple
of 16 for which the share still undecided at the zero-drift rate `m*` is at most 3 %
(1.5 % at 80; `PREREG.md`).

**`occupancyProb(id)`** is `sigmoid(l)`: the posterior probability of "structure" for
equal priors on the two hypotheses. It is not a calibrated occupancy.

**Default pair and what follows from it** (`m0 = 0.20`, `m1 = 0.70`, `alpha = beta = 0.01`):

| Quantity | Value |
|---|---|
| `l_hit`, `l_miss` | 1.2528, -0.9808 |
| `A = -B`, ceiling | 4.5951, 7.1006 |
| windows for an always-hit cell to graduate | **4** (never fewer; v0.1: 3) |
| all-miss windows to demote a graduated cell | 5 |
| zero-drift rate `m*` | 0.4391 |

The pair is the one measured on LILocBench (2D, 0.05 m, 1.0 s windows = 15 scans at
15 Hz) by `paper/experiments/realdata`; `PREREG.md` marks it **not frozen** (the wall
thickness gate G1 failed on all three calibration sequences), so the defaults are
provisional. The node says so on `/diagnostics` ("provisional (PREREG not frozen: G1 failed)" in the
message of the evidence status and in the value of `evidence_calibration`). The CTest `defaults_match_yaml_and_prereg` (`strata_core/test/check_defaults.py`) fails if
`PREREG.json`, the library defaults and both shipped YAMLs disagree.

**What the error rates mean, and what they do not.**
- *They hold for independent windows, for the first test of a history.* Under independent
  windows with hit rate at most `m0`, the first test of a cell history (from its creation to
  its first graduation or its floor window, the first window with `n >= T` and `l <= B`)
  graduates with probability at most `exp(-A) = alpha/(1-beta)`, about
  `alpha` (0.0101 at the default pair; Ville's inequality). A restart after a demotion, and a
  graduation after a floor window, are not covered. The exact value for the default pair at `m0`
  is 0.0055 (`results/e7_oc.csv`, Periodic veto off). Real windows are autocorrelated (lag-1 0.2 to 0.5 on the
  calibration sequences). With lag-1 autocorrelation 0.23 at exactly `m0`, the
  rate per forward test (a history split at each demotion, within 1024 windows) on the engine is 0.035, 3.5 times `alpha` (0.0056 at 0, 0.014 at 0.10,
  0.061 at 0.35; `results/e7_autocorr.csv`); a bare random walk floored at `B` without
  rejection, the first reading, gives 13 times at the pair (0.15, 0.65).
- *Realised rates on real clutter: the per-cell rate is above the nominal level.*
  The nominal level `alpha` is per first test of a history under independent windows. The
  first-history rate below is low because most clutter lies far below `m0`; we make no claim
  from it that the realised rate is below the bound. Two units give two numbers
  (`paper/experiments/realdata/results_p2/replay_shipped_pair_W15.txt`, W = 15 calibration
  sequences; clutter cells: n = 24,096, of which n = 22,273 at or below `m0` and n = 1,823 above):
  - *First history* (the unit of the bound; the replay stops at the first Static or Rejected
    verdict of each cell). The shipped rule graduates 0.40 % of the clutter cells whose
    observed rate is at most `m0`, 22.3 % of those above `m0`, and 2.06 % of all; it graduates
    89.6 % of the structure cells and rejects 1.4 %. The population is mostly far below `m0`, so
    this figure does not test the bound.
  - *Per cell* (the replay runs the whole sequence of each cell; the engine erases a rejected
    cell, and the next observation creates it again with a new history; ever-Static means the
    cell was Static at any window). With periodicity off: 1.64 % at or below `m0`, 33.8 % above,
    4.07 % of all clutter cells. With periodicity on, as the node ships it: 1.51 %, 30.5 %,
    3.71 %. Structure cells, periodicity on: 90.7 % ever-Static. **The per-cell figure at or below
    `m0` is above `alpha = 0.01`**: every new history is a new chance to graduate.
  - Read the per-cell figure when you count wrongly graduated cells on a map. A comparison with
    the bound is per first test of a history, not per cell.
  Walking people who linger belong to the cells above `m0`: no hit-rate test tells them from a
  wall.
- *Wall thickening.* Free cells next to a wall graduate at 6 % to 10 % (edge noise; pair
  (0.15, 0.65), `replay_advisory_table.txt`, controls). A localiser matching against the
  map feels it.
- *Rejected structure costs latency, not a hole.* A rejected wall cell is re-created by
  its next hit and graduates again.
- *The pair belongs to a window length, a resolution and a sensor class.* Re-derive it
  with `hitrate_probe` when you change any of the three.

**3D (`voxel3d`) is not calibrated.** No 3D measurement passed the pre-registered rule,
so the 2D pair is the shipped 3D default (`evidence_calibration: "fallback_2d_pair"`).
On single-pass driving data (KITTI 00, 0.2 m, 4 windows per structure voxel, replay
with the pair (0.15, 0.65)) 73 % of structure voxels stay undecided
(`paper/experiments/realdata/results_p2/replay_kitti.txt`). A lifelong mapper needs revisits. The node logs a
warning at start and publishes the limitation on `/diagnostics` in the status
`<node>: evidence model`.

### 3.3 Survival decay (Persistence Filter)
The `*= survival_decay` step every window pulls belief back toward unknown
(log-odds 0). A cell that stops being observed decays out; only **refreshed**
evidence persists. This is the discrete forgetting term of Rosen et al.'s
Persistence Filter, and it is what makes transient clutter fade.

### 3.4 Schmitt-trigger graduate / demote (`evidence_model = legacy`)
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
`legacy`:
```
classify(cell):
  graduated                                     -> Static
  enable_periodicity && periodic(cell)          -> Periodic   (§3.5)
  p >= prune_prob                               -> Transient
  else                                          -> Unknown
prune: erase cell if  !graduated && p < prune_prob && amp < periodic_amplitude_min
```
`sprt` (n = touched windows, T = `period_windows`):

| State of the cell | Read-out (`classify`) | Grid2D draws |
|---|---|---|
| graduated | Static | 100 |
| not graduated, Periodic | Periodic | 75 |
| n >= T, l <= B, kept only as a periodic candidate | Unknown | free or last-seen value |
| n >= `n_max_undecided` > 0, otherwise undecided | Intermittent | 50 if `p > 0.5`, as Transient |
| otherwise (undecided, or n < T) | Transient | 50 if `p > 0.5` |
| erased (rejected, or never seen) | Unknown | free or last-seen value |

A rejected cell is erased in the window in which `l <= B` and `n >= T` hold, unless it
is a periodic candidate. Static cells are never erased.

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
    version.hpp           # STRATA_CORE_VERSION "0.2.0"
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
  scripts/check_saved_map.py  # saved-pair check, installed to lib/strata (ros2 run), §6.1
  test/test_grid_math.cpp     # world<->grid roundtrip (gtest, no rclcpp)
  test/test_scan_adapter.cpp  # 6-DoF beam transform (gtest)
```

---

## 5. Parameters (ROS params, namespaced)

Defaults below are the actual `params/grid2d.yaml` / `params/voxel3d.yaml` values
and the `LayeredMapParams` / `PeriodicityParams` struct defaults — they agree.

### Evidence model (`LayeredMapParams`, version 0.2.0)
| Param | Default | Meaning |
|---|---|---|
| `evidence_model` | `sprt` | `sprt`: sequential test (§3.2a); `legacy`: the v0.1 rule (§3.2–§3.4). In the library `EvidenceModel::Sprt` / `Legacy` |
| `m0` | `0.20` | per-window hit rate at or below which a cell is clutter |
| `m1` | `0.70` | per-window hit rate at or above which a cell is structure |
| `alpha` | `0.01` | P(clutter at `m0` graduates), independent windows |
| `beta` | `0.01` | P(structure at `m1` is rejected), independent windows |
| `n_max_undecided` | `80` | touched windows after which an undecided, non-periodic cell reads Intermittent; `0` never |
| `sprt_ceiling` | derived, `A + 2 l_hit` | library only: upper clamp of the statistic (NaN derives it); not a node parameter |
| `evidence_calibration` | `measured_2d` for `grid2d`, `fallback_2d_pair` for `voxel3d` (the shipped YAMLs say so, and so does the node when the key is missing; `measured_2d` on another backend is reported as `fallback_2d_pair`) | node only: states where `(m0, m1)` come from: `measured_2d`, `fallback_2d_pair` (not calibrated for this backend; logs a warning and says so on `/diagnostics`) or `user` ; while `PREREG.md` marks the 2D pair not frozen, the message and the value of `evidence_calibration` on `/diagnostics` also carry `provisional (PREREG not frozen: G1 failed)` (every value except `user`; `evidence_model: legacy` states no pair); if `m0`, `m1`, `alpha` or `beta` differs from the library default, the node logs a warning and sets the label to `user` (the pair is then the user's), so the source on `/diagnostics` is never the measured one for a changed pair |

With `sprt` the nine keys `l_hit` to `prune_prob` below are ignored, and the node logs a warning that names any of them set away from its v0.1 default. With `legacy`
`m0` to `n_max_undecided` are ignored (and not validated), with the same kind of warning. The node refuses to start
on an unusable `sprt` configuration (`0 < m0 < m1 < 1`, `alpha, beta > 0`,
`alpha + beta < 1`, `n_max_undecided >= 0`) and names the parameter.

**API and ABI note for library users (0.2.0).**
1. Rebuild against 0.2.0. `MapBackend` gains a virtual (`intermittentCellCount`), and `LayeredMapParams` and `LayeredMap` change layout.
2. If you set the v0.1 keys (`l_hit`, `graduate_prob` and the others), set `evidence_model = EvidenceModel::Legacy` too. `LayeredMapParams{}` now defaults to `EvidenceModel::Sprt`, which ignores those keys without an error.
3. Handle `CellClass::Intermittent` in every `switch` over `CellClass`. With `Legacy` it never occurs.

### Layered persistence engine (`LayeredMapParams`, legacy keys)
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
| `periodic_false_alarm` | `0.2` | nominal level of the Chernoff significance test (§3.5); `>= 1` disables it |

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
| `startup_timeout_s` | `30.0` | ... and after this long if no input has arrived since start; also how long messages may arrive with a failing TF lookup and nothing integrated before `/diagnostics` turns WARN (until then: OK, `no usable TF ... yet`) |
| `save_path` | `/tmp/strata_2d` (grid2d) / `/tmp/strata_3d` (voxel3d) | save-service output path stem |

---

## 6. Test strategy

Every algorithmic claim has a deterministic gtest (injected window index, no
wall-clock, no `rand()`; the statistical tests use fixed-seed `std::mt19937`
streams, or `std::mt19937_64` raw output where the standard library must not matter) in
`strata_core/test`, runnable with **no ROS** — **113 gtests across 12 suites** (7 of
them, `ReplayP1`, need the phase 1 cell tables and skip without them), plus the
CTest `defaults_match_yaml_and_prereg`:

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
- **LayeredMap** (20; the tests that state the v0.1 arithmetic name
  `evidence_model = legacy`; the shipped-defaults tests exist twice, once for the
  default `sprt` — static from the derived window 4 — and once for `legacy` — window 3):
  graduates only when P(occ) ≥ threshold AND observed ≥
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
- **WindowObserver** (10): the read-only window hook reports exactly the touched
  cells with the hit-wins rule, changes no state, and leaves the map's read-outs
  byte-identical with and without it.
- **Sprt** (38): derived quantities (default pair, the advisory pairs); validation of
  every bad `(m0, m1, alpha, beta, n_max_undecided, sprt_ceiling)` on both sides of
  each bound; an always-hit wall graduates at exactly the derived window and never
  earlier (7 parameter sets, periodicity on and off); a long miss run before the gate is
  not clamped; Ville (first test of a history, iid at `m0`, 1,024 windows, Wilson upper limit
  ≤ 0.01) with a negative control that autocorrelation breaks it; operating
  characteristic against the exact lattice computation (gate 1 and gate 24) and Wald's
  closed form; the periodicity-gate race (an all-miss cell survives to exactly `n = T`;
  a 25 %-duty door survives the gate and becomes Periodic); restart after rejection;
  demotion by the reverse test after exactly `demote_misses` windows and never by one
  miss; Intermittent at `n_max_undecided`, never when it is 0, Periodic wins over it;
  `occupancyProb = sigmoid(l)`; `closeWindows(k)` equals k `endWindow()` calls; the
  window observer sees the same touches as under `legacy`.
- **ReplayP1** (7): the advisor's derived table, Wald operating characteristic and
  truncation table; the advisory empirical table replayed through `LayeredMap` on the
  committed phase 1 run sequences to ±0.002 (`paper/experiments/realdata/run_replay_p2.sh`);
  the in-sample KITTI 00 replay; the realised rates of the shipped pair on the 1.0 s
  sequences, per first history and per cell (whole sequence, three engine settings).
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

Plus **71 node gtests** in `strata/test`: `test_grid_math` (3; world↔grid
round-trip, no rclcpp), `test_scan_adapter` (1; a single beam under a 6-DoF
yaw+translation transform lands at the expected map point, z preserved),
`test_window_clock` (6; anchoring, gaps close every skipped window, epoch
nanosecond stamps close exactly every period, reorder kept and a bag loop
re-anchors, a short bag loop re-anchors, a huge forward jump re-anchors) and
`test_rate_monitor` (4; a steady stream has no loss, 40 % loss is estimated, the
sensor rate overrides the estimate, a back jump restarts the intervals),
`test_map_writer` (25; PGM rows top down with the expected shades and a YAML
naming the image relatively, resolution and origin read back within 1e-12,
numbers ignore a global locale with grouping and a decimal comma, the image
name is single-quoted with quotes doubled, a save_path without a file name is
refused, a moved pair still resolves its image, a missing directory is reported
as a failure naming the path, a failed YAML write leaves the previous pair
byte-identical with no `.tmp` left, a failed second rename reports that the
PGM was already replaced, a grid whose data does not match its size
is refused; SHA-256 against the FIPS 180-4 vectors (empty, "abc", the 56-byte
two-block message, a million "a") and against hashlib at the padding
boundaries (55, 56, 63, 64, 65, 120 bytes) and over all 256 byte values; the
YAML records the size and SHA-256 of the PGM written; a saved pair verifies
and still does after a move; another save's PGM beside the previous YAML fails
verification, with a different grid size and with the same size; the pair a
failed second rename leaves fails it too; one changed or one extra image byte
fails it; a YAML without the keys is unverifiable, not OK, and one with a
single key is invalid; a missing YAML or image is invalid; an image or YAML
path that is a directory is invalid, with or without the keys; a YAML with
CRLF line ends gives the same three results as the LF one; hand-edited keys
verify as the checker reads them (leading zeros, upper-case, double-quoted or
unquoted hash, quoted count, trailing blanks or a comment, two spaces or a tab
after the colon, a quoted key, lone CR line ends) and fail again beside another
image; a key given twice is invalid even with the same value; a count over 18
digits, signed or not whole, a hash that is not 64 hex digits and an unclosed
quote are invalid; a YAML line the checker's reader refuses is invalid, with or
without the keys; each of these YAMLs is also given to `check_saved_map.py`,
whose exit status must match) and `test_tf_wait` (7; a live clock waits 0.1 s, a 0.2x / 0.1x sim
clock still gets 0.1 s of ROS time, a coarse 10 Hz sim clock at 0.5x and at 0.2x
gets its full 0.1 s, a clock frozen from the start is waited on at most once to
the cap and then not at all, a clock that stops is recognised within 1 s, a
clock that jumps back counts the budget from the jump) and `test_input_status`
(18; the tally counts received, integrated and the failure streak, a resolved
but dropped message does not end a streak, and `last_tf_error` and the failed
frame are set while the streak runs and cleared when a message integrates;
`oneLine` turns control characters and line breaks into single spaces, keeps
well-formed UTF-8 and replaces every other byte (overlong forms, surrogates,
past U+10FFFF, cut-short sequences), and cuts at 120 bytes between characters;
one test per row of the input-state table above with the exact message and each
limit tested at the limit and at the next double above it: rows 1, 2, 3
(whatever TF did), 4 (with received and failed counts that differ), 5, 6 (no
stale text after a late localizer, a slow sensor), 7-9 together; a single
failed lookup is named at 1 Hz and not at 10 Hz; the message carries the frame
of the failed message and no cause, for a missing, a stale and an unconnected
transform; resolved but dropped messages are not a TF state; the configured
timeouts and names are used; a note keeps the level, replaces a bare `ok` and
is appended to another warning, an overriding state replaces both). 98 mutants
of `input_status.hpp` (every comparison, every count, time and frame used in a
message, the tally updates and the text rules) were run against it: 96 fail
it, 2 are equivalent (the streak and the failure total are equal while nothing
has integrated; a space is already a space).

`test_evidence_status` (7) covers the `/diagnostics` status of the evidence model
(`evidence_status.hpp`), one test per row of the table in that header: `grid2d` with
`measured_2d` starts the message with `provisional (PREREG not frozen: G1 failed)` and
carries it in the value of `evidence_calibration`; `voxel3d` with `fallback_2d_pair`
says `NOT calibrated for voxel3d` first and then the provisional note; the fallback
label on `grid2d` is provisional but not called uncalibrated; `user` and `legacy` carry
no note; every value (`m0`, `m1`, `alpha`, `beta`, the two derived window counts,
`n_max_undecided`) and the order of the values are pinned. 10 mutants of the header
were run against it and all fail it; `check_defaults.py` fails if the note is present
while `PREREG.json` says the 2D pair is frozen, or absent while it says it is not.

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
across the save. The next step runs the installed checker
(`ros2 run strata check_saved_map.py`, §6.1) on a
copy of that pair (`--keep-map`): exit 0 on the pair as saved, with the recorded
size and hash also compared against `stat` and `sha256sum`; exit 1 with the
mismatch message on that YAML beside the image with one pixel changed (same
size) and beside a 2 × 2 image; exit 0 on that changed image once both keys are
rewritten from `stat` and `sha256sum`, and on the YAML with CRLF line ends;
exit 3 on the YAML with the two keys removed;
exit 1 on a zero resolution, a two-element origin, a truncated PGM, an ASCII
(`P2`) PGM, a PNG, a missing image, a directory as the image, and a 5000-digit
run in `strata_image_bytes` or in the PGM width (one line each, no traceback). A later step launches grid2d with `use_sim_time:=true`, feeds
it scans with no `/clock` and no TF (`scripts/check_sim_clock_stall.py`) and
requires `ros2 param get` to answer within 8 s, at least 9 `/diagnostics`
statuses over 11 s of scans, and a WARN that the ROS clock is not advancing.
A second launch runs a sim clock at 0.5x in 0.1 s steps with scans stamped 30 ms
ahead of a dynamic `map → sensor` TF (`scripts/check_coarse_clock.py`) and
requires zero TF failures over at least 3 closed windows and at least 60 % of
the sent scans integrated in them. The stalled-clock run also requires every
status while scans arrive to carry the input counts (received > 0, integrated 0,
a growing failure streak, `last_tf_error` set) and the WARN to keep the `no
usable TF` note.
A further step walks each backend, on the wall clock with `startup_timeout_s`
shortened to 15 s, through the input states (`scripts/check_no_localizer.py`):
nothing published (row 1), messages without any transform and then with one
stamped 5 s behind them (row 4 with the exact message built from the status's
own counts, then row 5; `last_tf_error` is not an extrapolation in the first
case and is one in the second, and the message does not say the transform is
missing), messages with a transform stamped at each message (row 6: `ok`,
streak 0, the total kept, no TF text left, `last_tf_error` empty), the
transform stopped again (row 8, then row 9), and nothing published (row 3). In
every status of the run it requires the earlier values in their order (3 before
a window close, 15 after) followed by the six new ones, and `last_tf_error` to
be one line of at most 120 bytes that is set exactly while the streak is above
0. Rows 2 and 7 are covered by the gtests only. Verified on Humble; no Jazzy
job exists.

### 6.1 Saved-pair check

`writeMapPair` cannot make the two renames one atomic step, so a crash or
failure between them leaves the new PGM beside the previous YAML, and
`nav2_map_server` loads that pair silently with the previous resolution and
origin (and, if the grid size changed, with an image of another size). The
YAML therefore records which image it belongs to:

```yaml
strata_image_bytes: 160015
strata_image_sha256: '97e081439e8b6331dbec5cd55d7fd4cb25b2c6c1c84614d72671451735f2de53'
```

Both are computed over the exact bytes written to the PGM (header and pixels)
and follow the keys map_server reads. The hash is single-quoted so that it is
a string whatever its digits. SHA-256 is implemented in
`strata/include/strata/sha256.hpp` (no new dependency). `nav2_map_server`
(yaml-cpp) ignores unknown keys, which the round-trip scenario confirms on
every CI run, and it does **not** check them. CI verifies this on Humble only:
it has no Jazzy job. One manual load of a saved pair in Jazzy's
`nav2_map_server` 1.3.11 succeeded (160000 cells, 0 mismatches); the script, the
pair and the recorded output are in `paper/deploy_checks/jazzy_map_server/`. A
deployer must run the check
before trusting a saved pair after an unclean shutdown or a failed `save_map`.
The checker is `strata/scripts/check_saved_map.py`, installed to `lib/strata`:

```bash
ros2 run strata check_saved_map.py /path/to/map.yaml
# as a gate before the launch that loads the map:
ros2 run strata check_saved_map.py map.yaml && ros2 launch nav2_bringup bringup_launch.py map:=map.yaml
```

Standard library only (no ROS, no PyYAML), so the file can also be copied to a
machine without the package and run with `python3`. It reads the flat mapping a map
YAML is (`key: value` lines, quoted or plain scalars, `[a, b, c]` or `- a`
lists, comments; anything else is a failure), resolves a relative `image:`
next to the YAML as map_server does, and prints one line.

| Exit status | Line | When |
|---|---|---|
| 0 | `OK: ...` | size and SHA-256 of the image equal the YAML's keys; the image is a binary PGM (`P5`, maxval 1..65535) whose width × height × bytes per sample is exactly its payload; `resolution` is finite and > 0; `origin` is three finite numbers |
| 1 | `FAIL: ...` | `image does not match this YAML: interrupted save, or the image was edited or replaced` (size or hash differs), or the YAML or the image is missing, unreadable, not a regular file or malformed, or the image is not a binary PGM (PNG, BMP and ASCII `P2` fail here, with or without the keys), or only one of the two keys is present, or a size field has more than 18 digits |
| 2 | usage text | wrong command line (argparse) |
| 3 | `UNVERIFIABLE: ...` | neither key is present (an older save, or another tool's YAML) and the image is a well-formed binary PGM (`P5`); the other checks passed, but nothing ties the image to the YAML |

Any edit of the image fails by design, including routine clean-up in an image
editor, because the keys cover the exact PGM bytes. After an intended edit,
either delete both keys (the pair is then `UNVERIFIABLE`, exit 3) or rewrite
them: `strata_image_bytes` from `stat -c %s map.pgm` and `strata_image_sha256`
(single-quoted) from the first field of `sha256sum map.pgm`.

`OK` goes to stdout, the other lines to stderr; after a non-zero exit
`ros2 run` adds its own `[ros2run]: Process exited with failure N` line. `verifyMapPair(yaml_path)` in
`map_writer.hpp` is the same size-and-hash comparison in ROS-free C++
(`Ok`, `Mismatch`, `Unverifiable`, `Invalid`). It reads the YAML by the
checker's rules, so a hand-edited YAML gets the same verdict from both: a
value may be plain, single-quoted or double-quoted, with any blanks after the
colon and a trailing ` # comment`; the byte count is 1 to 18 digits, leading
zeros allowed; the hash is 64 hex digits in either case; a key given twice, a
nested line or another line the checker refuses is `Invalid`, as is a path
that is not a regular file or an image name that holds a NUL byte; CRLF line ends are read like LF. `Ok` is exit 0,
`Unverifiable` exit 3, `Mismatch` and `Invalid` exit 1, and the gtests run the
checker on each hand-edited YAML to hold the two together. Two differences
remain: the C++ treats only ASCII blanks as whitespace and does not reject
text that is not UTF-8. It does not validate the PGM header,
resolution or origin, and the node does not call it. Limits: the keys tie the YAML to the image bytes only,
so two saves with byte-identical images (the same cells under a different
`grid_origin_*`) are not told apart; and they are an integrity check against a
mixed-up or damaged pair, not a signature. voxel3d saves a single PCD, so it
has no pair to mismatch.

---

## 7. Build & run

**Core only (no ROS):**
```bash
cmake -S strata_core -B build/core -DSTRATA_CORE_BUILD_TESTS=ON
cmake --build build/core -j
( cd build/core && ctest --output-on-failure )     # the 113 engine gtests and the defaults check
```

**Full ROS 2 build & test (Humble; this repository at `<ws>/src/strata`, run from `<ws>`):**
```bash
colcon build --symlink-install
source install/setup.bash
colcon test --packages-select strata      # the 71 node gtests (the engine gtests run in the step above)
colcon test-result --verbose
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
