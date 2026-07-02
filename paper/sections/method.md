# System Overview and Architecture {#sec:system}

STRATA is delivered as two ament packages with a deliberate dependency
boundary between them. `strata_core` is the scientific engine: pure C++17 with
Eigen as its only third-party dependency, holding the entire
occupancy/persistence/periodicity state machine and both geometry backends.
`strata` is a thin ROS 2 node that wraps the engine, owning message
conversion, TF lookups, publishers, subscribers, services, and file I/O. Every
ROS-only dependency — `rclcpp`, `tf2`, `sensor_msgs`, `nav_msgs`, PCL — lives
exclusively in `strata`; `strata_core` links none of them. This split is the
reason the engine builds and unit-tests with a plain system toolchain
(`cmake -S strata_core -B build -DSTRATA_CORE_BUILD_TESTS=ON && ctest`), with
no ROS install, no colcon, no DDS discovery, and no TF buffer warm-up in the
loop. The architecture is shown in [@fig:architecture].

![Two-band architecture of STRATA. The upper band is `strata_core` (pure C++17
+ Eigen, no ROS or PCL, gtest-tested): one shared `LayeredMap`
persistence-and-periodicity engine, keyed by `int64` `CellId`, feeds two
sibling backend boxes `Grid2DBackend` and `Voxel3DBackend`. The lower band is
the `strata` ROS 2 node, where `MappingNode` selects one backend at
construction, its scan/cloud adapters convert messages to `Observation`s in the
map frame, and its timer publishes `~/map` / `~/map_points` and services
`~/save_map`. The two bands are split by the `MapBackend` interface line. The
external TF arrow (`map` &rarr; sensor frame) enters the node from outside the
diagram: STRATA consumes localization but never produces
it.](../figures/fig_architecture.pdf){#fig:architecture}

## The `MapBackend` interface and the geometry-free invariant {#sec:mapbackend}

`strata_core::MapBackend` is a pure abstract base with four methods:

```cpp
struct Observation { std::vector<Eigen::Vector3d> hits; };  // endpoints, MAP frame
class MapBackend {
  virtual void integrate(const Observation& obs,
                         const Eigen::Vector3d& sensor_origin_map) = 0;
  virtual bool tick() = 0;
  virtual std::size_t staticCellCount() const = 0;
  virtual std::size_t transientCellCount() const = 0;
};
```

`Observation::hits` is a flat list of 3D endpoints already expressed in the map
frame; producing it from a sensor message and a `sensor_to_map` transform is
the caller's job, so the backend never touches TF. `integrate()` performs one
discrete map update from one sweep or cloud: for each hit it clears the
free-space cells between `sensor_origin_map` and the hit via a backend-specific
ray operation, then registers the hit itself as an occupied observation.
`tick()` advances the temporal window and returns whether a window boundary was
crossed. `staticCellCount()` and `transientCellCount()` are read-only
introspection forwarded to the shared classifier.

Both concrete backends hold a `LayeredMap` member and satisfy the interface
purely by translating world-frame geometry into `int64` `CellId` keys that
`LayeredMap` accumulates evidence for. This is the load-bearing design
invariant: **the only per-backend code is point&rarr;id mapping and the
free-space ray walk; the occupancy/persistence/periodicity classifier is shared
by composition, not duplicated per dimension.** `LayeredMap` and
`PeriodicityModel` hold no coordinates, no resolution, and no origin — only
integer ids — so the identical engine drives both 2D and 3D. The consequence,
which the evaluation returns to, is that any cross-backend behavioral
divergence or cost gap must originate in the geometry layer, because that is
the only layer that differs.

## Backend selection {#sec:selection}

Selection is a single string ROS parameter, `backend` (`"grid2d"` or
`"voxel3d"`), read once in the node constructor. It is not polymorphic dispatch
over a `MapBackend*`: the node holds both `std::unique_ptr<Grid2DBackend>` and
`std::unique_ptr<Voxel3DBackend>` as separate members, and an `if`/`else` at
construction instantiates exactly one of them, leaving the other null, and
wires up the backend-specific parameters, publisher, and subscriber. We use a
compile-time interface plus one runtime switch rather than `dlopen`-based
plugin loading. At two backends, `pluginlib` [@ros2pluginlib] would add a
registration and discovery mechanism whose cost is not repaid until the backend
count grows; we defer that machinery until it is. Both branches read every
`LayeredMapParams` field through a common helper, so the persistence and
periodicity tuning surface is backend-independent by construction.

## ROS 2 I/O contract {#sec:io}

The node is `strata::MappingNode`, default node name `strata`. It looks up
`global_frame` (default `map`) &larr; sensor frame on every incoming
scan or cloud, at the message stamp, as a full 6-DoF `Eigen::Isometry3d`; there
is no yaw-only flattening on the input side. Lookup failures are caught and
rate-throttled rather than fatal, and that scan or cloud is dropped. STRATA is
mapping-only: it neither subscribes to `/initialpose` nor broadcasts
`map`&rarr;`odom`. Pose must already be published on TF by an external
localizer. A single mutex serializes integration against publish and save.

| Direction | Name | Type | QoS | Condition |
|---|---|---|---|---|
| Sub | `scan_topic` (default `/scan`) | `sensor_msgs/LaserScan` | `SensorDataQoS` | grid2d only |
| Sub | `points_topic` (default `/points`) | `sensor_msgs/PointCloud2` | `SensorDataQoS` | voxel3d only |
| Pub | `~/map` | `nav_msgs/OccupancyGrid` | `QoS(1)`, transient-local, reliable | grid2d only |
| Pub | `~/map_points` | `sensor_msgs/PointCloud2` | `QoS(1)`, volatile | voxel3d only |
| Srv | `~/save_map` | `std_srvs/Trigger` | default | both (`.pgm`+`.yaml` / `.pcd`) |
| Timer | `publish_period` (default 1.0 s) | — | — | drives publishing for both |

Table: ROS 2 input/output contract of the `strata` node. {#tbl:io}

The occupancy-value convention on `~/map` and saved files is: unknown `-1`,
transient `50`, periodic `75`, static `100`. For voxel3d, only the static-cell
set is emitted as points on `~/map_points`.

---

# The Layered Map Engine {#sec:method}

This section specifies the engine as implemented in `strata_core`. All
equations are transcribed from the source, not from the design specification;
where the two disagree, [@sec:specdiff] documents the difference. Symbols follow
the notation used throughout the paper: $\ell$ is a cell's log-odds occupancy
evidence, $p=\sigma(\ell)$ its occupancy probability, $t$ the window (phase)
index, $H$ the harmonic count, $T$ the base period, and $\lambda$ the survival
decay.

## Per-frame accumulation and windowing {#sec:windowing}

Each `observeHit(id)` / `observeMiss(id)` only increments the cell's window
counters, `window_hits` or `window_misses`; it performs no log-odds
arithmetic. Cells are created lazily on first touch, with $\ell=0$,
`observations=0`, and `graduated=false`. `tick()` advances the integration
counter and closes a window on the interval boundary:

$$\text{closeWindow} \iff \big(\texttt{layer\_interval}\le 1\big)\ \lor\
\big(\texttt{integration\_count} \bmod \texttt{layer\_interval} = 0\big).$$

The layered update, `endWindow()`, then runs once over **all** live cells. With
window counts $h_w$ and $m_w$, each cell's window is scored:

$$\text{touched}=[\,h_w>0 \lor m_w>0\,],\quad
\text{occ}=[\,h_w>0\,],\quad
\text{free}=[\,h_w=0 \land m_w>0\,].$$

A single hit outweighs any number of misses in the same window.

## Log-odds persistence with survival decay {#sec:logodds}

For **touched** cells only, the engine applies one log-odds increment
(an inverse-sensor-model update in the sense of [@thrun2005probabilistic]),
multiplies by the survival decay $\lambda$, and clamps:

$$\ell \leftarrow \operatorname{clamp}\!\Big(\lambda\big(\ell +
\underbrace{[\text{occ}]\,l_{\text{hit}} + [\text{free}]\,l_{\text{miss}}}_{\text{one increment}}\big),\;
l_{\min},\, l_{\max}\Big),\qquad
\operatorname{clamp}(x,a,b)=\min\!\big(b,\max(a,x)\big),$$

with occupancy probability $p=\sigma(\ell)=1/(1+e^{-\ell})$. The survival
multiplier is a constant-rate special case of the Persistence Filter forgetting
model [@rosen2016persistence].

**Ordering note (load-bearing).** The decay multiplies the *already-incremented*
value, so the fresh hit or miss is itself attenuated by $\lambda$ in the same
window. This is not the classic decay-then-add order. One consequence is the
decayed fixed point of repeated hits,
$\ell^\star=\lambda\,l_{\text{hit}}/(1-\lambda)\approx 27.5$, which the clamp
caps at $l_{\max}=5$, giving $p_{\max}=\sigma(5)\approx 0.9933$. With the
defaults, $p_{\text{grad}}=0.8$ corresponds to $\ell\ge\ln 4\approx 1.386$ and
$p_{\text{prune}}=0.05$ to $\ell<\ln(1/19)\approx -2.944$.

## Schmitt-trigger graduation and demotion {#sec:schmitt}

After the log-odds update, $p$ and the periodicity amplitude $a$
([@sec:periodicity]) are recomputed for **every** cell, touched or not, and the
`graduated` flag $g$ is updated by a two-sided Schmitt trigger:

$$\textbf{graduate:}\quad \lnot g \ \land\ \lnot\text{periodic}\ \land\
p \ge p_{\text{grad}}\ \land\ \text{observations}\ge N_{\min}\
\Rightarrow\ g\leftarrow\text{true},$$

$$\textbf{demote:}\quad g \ \land\ \big(p \le p_{\text{dem}}\ \lor\
\text{periodic}\big)\ \Rightarrow\ g\leftarrow\text{false}.$$

The interval $[p_{\text{dem}},\,p_{\text{grad}}]$ is the hysteresis band that
suppresses flicker at the static boundary; the motivation is the same
observation-consistency argument as in dynamic-object removal by
reverting [@kim2020removert]. Two periodicity guards go beyond a bare
threshold rule: `!periodic` blocks a strongly periodic cell from ever
graduating to Static, and `|| periodic` force-demotes a graduated cell that
later reveals periodicity. The two rules cannot fight in one window, because
graduation requires $\lnot\text{periodic}$ and
$p\ge p_{\text{grad}}>p_{\text{dem}}$.

## FreMEn-lite periodicity {#sec:periodicity}

In parallel with occupancy, each touched cell (when `enable_periodicity`) feeds
an incremental Fourier model in the spirit of FreMEn [@krajnik2017fremen]. With
the per-window occupancy sample $v=[\text{occ}]\in\{0,1\}$ and phase index $t$,
`gather` accumulates:

$$n \mathrel{+}= 1,\quad S_0 \mathrel{+}= v,\quad
C_k \mathrel{+}= v\cos\!\big((k{+}1)\omega t\big),\quad
S_k \mathrel{+}= v\sin\!\big((k{+}1)\omega t\big),\quad k=0..H{-}1,$$
$$\omega = \frac{2\pi}{\max(1,\,T)}.$$

Here $n$ counts **touched windows fed to gather**, not elapsed windows. The
phase prediction at window $t$ is

$$\hat p(t)=\operatorname{clip}_{[0,1]}\!\left(\frac{S_0}{n}+\sum_{k=0}^{H-1}
\Big[\underbrace{\tfrac{2C_k}{n}}_{a_k}\cos\!\big((k{+}1)\omega t\big)+
\underbrace{\tfrac{2S_k}{n}}_{b_k}\sin\!\big((k{+}1)\omega t\big)\Big]\right),$$

with DC term equal to the empirical mean $S_0/n$ and harmonic coefficients
$a_k=2C_k/n$, $b_k=2S_k/n$. The dominant-harmonic amplitude and the periodicity
test are

$$a=\begin{cases}0, & n < T\\[4pt]
\displaystyle\max_{0\le k<H}\sqrt{a_k^2+b_k^2}, & n \ge T\end{cases}
\qquad
\text{periodic}=\texttt{enable\_periodicity}\ \land\ a \ge a_{\min}.$$

The $n\ge T$ gate means a sparsely observed cell needs $T$ touches before its
amplitude is trusted, which can span far more than $T$ elapsed windows.

## Cell-class state machine and pruning {#sec:states}

At the end of each window, cells below confidence are erased:

$$\text{erase cell} \iff \lnot g\ \land\ p < p_{\text{prune}}\ \land\
a < a_{\min}.$$

Static cells ($g$) and periodic cells ($a\ge a_{\min}$) are never pruned;
pruning a cell also erases its FreMEn coefficients. A snapshot classifier reads
out one of four states by a priority ladder:

$$\text{absent}\to\text{Unknown};\quad g\to\text{Static};\quad
(\texttt{enable}\land a\ge a_{\min})\to\text{Periodic};\quad
p\ge p_{\text{prune}}\to\text{Transient};\quad \text{else}\to\text{Unknown}.$$

The full transition table is [@tbl:states]. Note that a present cell classifies
Unknown when $p<p_{\text{prune}}$ and it is neither periodic nor graduated — the
same condition under which it is generally pruned in the same window.

| From | To | Trigger |
|---|---|---|
| (implicit) Unknown | Transient | first `observeHit/Miss`: cell created, $\ell{=}0\Rightarrow p{=}0.5\ge p_{\text{prune}}$ |
| Transient | Static | $p\ge p_{\text{grad}}\land\text{obs}\ge N_{\min}\land\lnot\text{periodic}$ (graduate) |
| Transient | Periodic | $a\ge a_{\min}$ (needs $n\ge T$ touched windows) |
| Transient | Unknown (erased) | $p<p_{\text{prune}}\land a<a_{\min}\land\lnot g$ (prune) |
| Static | Transient / Periodic / Unknown | demote ($p\le p_{\text{dem}}\lor\text{periodic}$), then re-classified by ladder |
| Periodic | Transient / Unknown | $a$ falls below $a_{\min}$ (then subject to prune) |
| Periodic | — | never graduates (`!periodic` guard), never pruned while $a\ge a_{\min}$ |
| Static | — | never pruned while $g$ |

Table: Cell-class transitions, all evidence-driven and evaluated at window
close. {#tbl:states}

The complete per-window update is Algorithm&nbsp;1.

```
Algorithm 1  endWindow(): one layered update per closed window, over all live cells

  for each live cell c:                       # main update pass
      touched <- [h_w>0 or m_w>0]
      occ     <- [h_w>0]
      free    <- [h_w=0 and m_w>0]
      if touched:
          l  <- l + [occ]*l_hit + [free]*l_miss   # one log-odds increment
          l  <- lambda * l                        # decay attenuates the fresh increment
          l  <- clamp(l, l_min, l_max)
          observations <- observations + 1
          if enable_periodicity:
              gather(c, occ, t)                   # n+=1; S0+=v; Ck+=v cos((k+1)w t); Sk+=v sin((k+1)w t)
      p <- sigma(l);  a <- amplitude(c)           # recomputed for ALL cells, touched or not
      periodic <- enable_periodicity and a >= a_min
      if not g and not periodic and p >= p_grad and observations >= N_min:
          g <- true                               # graduate -> Static
      else if g and (p <= p_dem or periodic):
          g <- false                              # demote
      reset h_w <- 0, m_w <- 0
  for each live cell c:                       # prune pass
      if not g and p < p_prune and a < a_min:
          erase c and its FreMEn coefficients
  t <- t + 1
```

The window update is $O(N\cdot H)$ for $N$ live cells and $H$ constant, hence
$O(N)$; it iterates all cells, including untouched ones, because the Schmitt
and prune decisions read the recomputed $p$ and $a$ of every cell. Between
window closes a frame is $O(1)$ per endpoint plus the backend ray cost.

## Geometry backends {#sec:backends}

The two backends differ only in how a world point becomes a `CellId` and how the
free-space ray is walked; both delegate all evidence updates to the shared
`LayeredMap`.

`Grid2DBackend` addresses a fixed-size, fixed-origin 2D array described by
`GridMeta` $=\{$`width, height, resolution, origin_x, origin_y`$\}$. It buckets
each hit's $(x,y)$ (dropping $z$) into a row-major id
$\texttt{gridCellId}(m,g_x,g_y)=g_y\cdot\texttt{width}+g_x$; points outside the
array are dropped. Free space is cleared with integer Bresenham's line algorithm
from the sensor cell to the hit cell, calling `observeMiss` on every
intermediate cell strictly between the endpoints, so the hit cell receives only
`observeHit`. Rendering to `nav_msgs/OccupancyGrid` initializes every cell to
`-1`, then overwrites in ascending confidence order transient&rarr;`50`,
periodic&rarr;`75`, static&rarr;`100`, so static wins any tie.

`Voxel3DBackend` has no fixed extent; the map grows through the hash map. Each
axis is floor-divided by `voxel_size` and offset by $\texttt{kOff}=1\ll 20$ to
keep the per-axis index non-negative, and the three 21-bit
($\texttt{kBits}=21$) fields are packed into one 64-bit key,
$\text{id}=(v_x\ll 42)\,|\,(v_y\ll 21)\,|\,v_z$; `voxelCenter` is the exact
inverse, returning $(v+0.5)\cdot\texttt{voxel\_size}$ per axis. This gives O(1)
hashing and a per-axis range far larger than any practical map. Free space is
cleared by ray-sample marching, not geometric voxel traversal: the ray is
subdivided into $\lceil\text{len}/(0.5\cdot\texttt{voxel\_size})\rceil$
half-voxel steps, and each interior sample is hashed to its containing voxel and
marked `observeMiss`. This is simpler than exact traversal but can over-sample a
long ray and can skip a thin voxel when the step count rounds down. The hit is
always `observeHit`, with no bounds check because the hash grows to fit.
`staticPoints()` maps the static-cell set through `voxelCenter` to a point
cloud. [@tbl:backends] contrasts the two.

| | `Grid2DBackend` | `Voxel3DBackend` |
|---|---|---|
| Cell addressing | 2D array index via `GridMeta` (fixed $W\times H$, fixed origin) | 3D spatial hash, unbounded, `int64` packed key |
| Hit dimensionality | $x,y$ (z dropped by projection) | $x,y,z$ (full volumetric) |
| Free-space ray | Bresenham line (exact, on-grid) | ray-sample march at 0.5-voxel step (approximate, off-grid) |
| Growth | none — pre-sized at construction | unbounded — hash grows with exploration |
| Output | `nav_msgs/OccupancyGrid` (dense `-1/50/75/100`) | static voxel centers &rarr; `PointCloud2` |

Table: The two geometry backends. All persistence and periodicity logic is
shared; only these two rows of behavior differ. {#tbl:backends}

## Parameters {#sec:params}

[@tbl:params] lists the engine's tunable parameters with their defaults, which
are identical between the struct definition and both shipped YAML files. Geometry
and node parameters (`grid_width` 400, `grid_height` 400, `grid_resolution`
0.05 m, `grid_origin_{x,y}` $-10.0$ m for grid2d; `voxel_size` 0.2 m for voxel3d;
frame names, topics, `publish_period` 1.0 s) are not part of the engine math.

| Name | Default | Units | Meaning |
|---|---|---|---|
| `layer_interval` | 10 | frames/window | integration ticks per layered-update window |
| `l_hit` | 0.85 | log-odds | increment on an occupied window |
| `l_miss` | $-0.4$ | log-odds | increment on a free window |
| `l_min` | $-5.0$ | log-odds | clamp lower bound |
| `l_max` | 5.0 | log-odds | clamp upper bound |
| `survival_decay` ($\lambda$) | 0.97 | $\times$/window | Persistence-Filter forgetting multiplier |
| `graduate_prob` ($p_{\text{grad}}$) | 0.8 | probability | $p$ to promote &rarr; Static |
| `demote_prob` ($p_{\text{dem}}$) | 0.45 | probability | $p$ at/below which Static demotes (hysteresis floor) |
| `min_observations` ($N_{\min}$) | 3 | touch count | min touched windows before a cell may graduate |
| `prune_prob` ($p_{\text{prune}}$) | 0.05 | probability | erase non-static, non-periodic cell below this $p$ |
| `enable_periodicity` | true | bool | run the FreMEn model |
| `periodic_amplitude_min` ($a_{\min}$) | 0.3 | amplitude | dominant-harmonic magnitude to classify Periodic |
| `period_windows` ($T$) | 24 | windows | FreMEn base period; also amplitude-validity gate ($n\ge T$) |
| `n_harmonics` ($H$) | 2 | count | Fourier harmonics tracked per cell |

Table: Engine parameters and defaults (struct and `params/*.yaml` agree).
{#tbl:params}

Per-cell state is compact: `CellEvidence` is 24 bytes, plus roughly 32 bytes of
`unordered_map` node overhead, giving the ~56 B/cell footprint reported in the
evaluation when periodicity storage is excluded. A FreMEn-tracked cell adds a
`Coeff` record (~96 B payload at $H=2$), allocated only for cells touched at
least once with periodicity enabled.

## Implementation versus specification {#sec:specdiff}

We document the code that ships, not the design prose. Three points where the
implementation diverges from `SPEC.md` are surfaced here rather than smoothed
over, because each changes the operational semantics.

**SPEC-DIFF #1 (forgetting is coupled to re-observation).** The specification
states that decay happens "every window" and that "a cell that stops being
observed decays out." In code, the entire log-odds-and-clamp block of
[@sec:logodds] is inside `if (touched)`. An untouched cell — out of the sensor
field of view, with neither a hit nor a miss that window — does not decay; its
$\ell$ freezes. Forgetting therefore requires being re-observed as free (a miss
is a touch), so clutter fades only while it stays in the field of view, and
anything that leaves the field of view persists indefinitely. This is the most
consequential difference and is restated as an operational limitation in the
discussion.

**SPEC-DIFF #2 (periodicity guards absent from the formal block).** The
specification's §3.4 pseudocode omits the periodicity guards entirely, listing
graduate as `!graduated && p >= graduate_prob && observations >= min_observations`
and demote as `graduated && p <= demote_prob`. The `!periodic` and `|| periodic`
terms actually in the code ([@sec:schmitt]) appear only in prose and comments. A
method transcribed verbatim from that block would be wrong; the equations above
follow the code.

**SPEC-DIFF #3 (amplitude gate counts touched windows, not elapsed time).** The
specification says amplitude is zero before `period_windows` windows have
*elapsed*. The code gates on $n<T$, where $n$ is the count of touched windows
(calls to `gather`), not elapsed wall or window time. A sparsely observed cell
needs $T$ touches, which can span far more than $T$ windows — the mechanism
behind the low-duty periodicity miss reported in the evaluation.
