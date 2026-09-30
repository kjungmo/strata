---
title: "STRATA: One Geometry-Free Persistence-and-Periodicity Engine Driving Selectable 2D/3D Lifelong LiDAR Mapping in ROS 2"
author:
  - name: Jungmo Kang
    affiliation: Independent Researcher
    email: kangjmo91@gmail.com
    url: https://github.com/kjungmo
keywords:
  - lifelong mapping
  - occupancy grids
  - voxel mapping
  - persistence filtering
  - FreMEn periodicity
  - ROS 2
  - open-source robotics software
---

# Abstract {-}

A robot that maps one building for weeks must keep walls (durability), forget
movers (plasticity) and treat a scheduled door as signal, not noise
(periodicity). Per-cell persistence and periodicity models already exist; the
closest precedent is Frequency Map Enhancement (FreMEn), in the 2D grid of
Krajník et al. (2016) and in FROctomap, while several recent LiDAR
lifelong-mapping systems (e.g. ELite, LT-mapper) embed persistence logic inside
full lifelong-SLAM pipelines. We present STRATA, a small, SLAM-free engine that
combines known ingredients in one per-cell state machine — windowed log-odds
with survival decay, a Schmitt-trigger graduation band, pruning, and a
FreMEn-style Fourier test at one configured base period and its harmonics,
without period selection — shared unchanged by a fixed-array 2D grid and a
sparse 3D voxel hash that differ only in the point-to-key map and the ray walk.
Standard inequalities give the periodic label a false-alarm bound for iid
clutter, with the level spent over the touch count; it holds per cell history,
so only for unpruned cells, and pruned clutter is measured. All experiments use
hand-authored synthetic hit and miss patterns (fed directly to the engine, or as
synthetic hit points to the two ray-casting backends); no real or simulated
LiDAR scans are processed, and no external baseline, FreMEn included, is run.
Both backends graduate a static wall by the third window and keep recall 1.0;
removing hysteresis raises flicker from 54 to 574 toggles. With the level fixed
on calibration seeds, at most 2 of 2000 Bernoulli clutter cells are ever
labelled Periodic within 1024 windows in every held-out configuration (Wilson
95% upper limit 0.0036), and 50%-duty doors are detected from 25 windows (8
without the test). With periodicity off, the estimated memory is 56 B per live
cell (evidence record and hash node only). Code:
<https://github.com/kjungmo/strata>.

# 1 Introduction

An occupancy grid built the textbook way accumulates evidence and never lets go
of it. For a robot that drives a route once, that is fine. For one that maps the
same warehouse or hospital corridor for weeks, it is a slow failure: every hit
is folded into the same static belief, so a cart parked for an afternoon, a
person walking past, and a load-bearing wall all leave the same kind of mark.
Over enough passes the map stops describing the building and starts describing
the union of everything the sensor ever touched. Suppressing that failure is the
whole problem of lifelong mapping, and it forces three requirements that pull in
different directions: the map must be *durable* enough to keep structure that is
genuinely permanent, *plastic* enough to forget structure that has moved on, and
must do both without a single global forgetting rate that is simultaneously too
slow to erase movers and too fast to trust a wall. This is the
stability–plasticity dilemma that Biber and Duckett framed for dynamic maps
[@biber2005dynamicmaps]: represent the environment at more than one timescale at
once, or lose either the walls or the ability to adapt.

A third requirement complicates the split further. Some structure is neither
permanent nor transient but *cyclic* — a door open every morning and shut every
night, a shutter that tracks business hours. Krajník et al. show that such a
cell's occupancy is a periodic temporal signal, and that a flat decay law
erases exactly the regularity that makes it predictable, discarding information
a robot could have exploited [@krajnik2017fremen; @krajnik2014spectral]. A
lifelong map therefore needs three coexisting timescales, not two: a fast-fading
occupancy signal, a slowly-graduated durable class, and a separately-timed
periodic class that a decay term alone would flatten into noise.

Per-cell models of these requirements are not new. Temporal occupancy grids
[@arbuckle2002temporal] classify cells by their occupancy over several
timescales and use the result to locate doors, and Mitsou and Tzafestas
[@mitsou2007temporal] keep each cell's occupancy history to separate static,
low-dynamic and high-dynamic cells. The closest prior work is the FreMEn line.
Krajník et al. [@krajnik2016persistent] maintain a 2D spatio-temporal occupancy
grid that represents the persistence and periodicity of individual cells; it
integrates maps that gmapping builds with AMCL pose estimates as odometry,
replaces the map server of the ROS navigation stack, and was evaluated on data
from several days of routine autonomous patrols of an open-plan office.
FROctomap [@krajnik2014froctomap] attaches the same spectral model to the voxels
of a 3D octree. Several recent LiDAR lifelong-mapping systems instead place
persistence logic inside larger pipelines. ELite [@gil2025elite] and LT-mapper
[@kim2022ltmapper] resolve durability versus plasticity as parts of full
lifelong-**SLAM** pipelines, coupled to multi-session point-cloud registration,
place recognition, and alignment. RTAB-Map [@labbe2019rtabmap] offers
selectable 2D and 3D geometry under one framework, as a complete SLAM system
with loop closure and memory tiering. Khronos [@schmid2024khronos] generalizes
the two-timescale idea to a metric-semantic scene graph, and MTD-Map
[@kim2026mtdmap], concurrent with this work, maintains static, dynamic and
transition maps from per-voxel statistics of occupancy transitions. Every
ingredient STRATA uses therefore has precedent. What we did not find in this
literature is their particular combination: one small, SLAM-free
implementation that assigns each cell a discrete label through hysteresis
graduation and pruning, is shared unchanged by a 2D grid and a 3D voxel hash,
and bounds the false-alarm probability of its periodic label in finite samples.

We present STRATA. The name is literal: a cell in the map carries temporal
strata — Static, Periodic, and Transient layers over an Unknown baseline —
coexisting inside one per-cell state machine, read and written by log-odds
occupancy, survival decay, Schmitt-trigger hysteresis, and an incremental
Fourier periodicity test. That state machine is geometry-free. Both shipped
backends translate world points into `int64` cell keys and walk a free-space
ray to clear it; everything downstream of the key — accumulation, graduation,
demotion, pruning, periodicity labeling — happens once, in the shared engine.
The thesis is deliberately narrow: *the only per-backend code is point-to-id
mapping and the free-space ray walk; the classifier is shared by composition,
not duplicated per dimension* ([@fig:architecture]). Selecting 2D versus 3D is a single
runtime string parameter resolved at construction, not a plugin-loading
mechanism, and the engine itself (`strata_core`) is a plain C++17 + Eigen
library with no `rclcpp`, `tf2`, PCL, or DDS dependency, so its scientific logic
builds and unit-tests without a ROS install.

We are explicit about scope up front (§6 consolidates the non-claims). STRATA is
**not SLAM**: it estimates no pose, closes no loops, and aligns no sessions,
consuming instead an external `map → sensor` transform. Its evaluation is
**synthetic only** — every number below comes from deterministic seeded
harnesses and unit tests, not a field deployment, in contrast to the multi-day
validations of ELite, LT-mapper, and Berrio et al. [@berrio2021longterm] — and
it runs **no external baseline**. It models **single-session** temporal
persistence, not multi-session remapping: a graduated cell simply *is* the
current static map, with no session versioning. And its periodicity test checks
**one configured base period** and its harmonics rather than selecting periods
from a spectrum as FreMEn does. These are the boundaries of the tool, stated as scope rather than buried as
gaps.

**Contributions.** This paper makes six, each backed only by the shipped code
and the synthetic suite:

1. **One geometry-free engine, two geometries, one switch (by construction).**
   A single `int64`-keyed persistence-and-periodicity engine (`LayeredMap` +
   `PeriodicityModel`) drives both a fixed-array 2D occupancy grid and a
   sparse 3D voxel hash behind one `MapBackend` interface, chosen by a single
   runtime string parameter, with no state-machine logic duplicated across
   dimensions ([@fig:architecture]; §3–4). Per-cell periodic grids already
   exist in 2D and in 3D [@krajnik2016persistent; @krajnik2014froctomap], as
   separate implementations [@fremenrepo]; what is added here is one
   implementation that serves both, and the geometry-free part is the whole
   persistence, graduation, pruning and classification state machine together
   with the periodicity test, not per-identifier periodicity modelling alone,
   which the FreMEn server already provides.
2. **A ROS-free, deterministically testable core, shown backend-equivalent
   (design goal + measured).** The engine builds and unit-tests with a plain
   C++17 + Eigen toolchain and is exercised by 49 behavior-level tests; both
   backends graduate the static wall by window 3 and hold recall 1.0 through
   window 39, differing in static-layer quality only by a clutter-induced
   static-precision gap (0.9253 grid2d vs. 0.9758 voxel3d at 100 movers per
   window) that traces to the backends' native-geometry sampling, not to the
   classifier; a
   separate per-`integrate()` cost gap between the backends is characterized in
   contribution 6 (§3, §5).
3. **A minimal lifelong classifier assembled from known parts.**
   Persistence-Filter survival decay [@rosen2016persistence], Removert-motivated
   Schmitt hysteresis [@kim2020removert], ReFusion-style negative-evidence ray
   clearing [@palazzolo2019refusion], and a parallel FreMEn-lite periodicity
   test at a fixed period [@krajnik2017fremen], emitting a four-class label from
   one dependency-light, SLAM-free module, with every equation transcribed from
   the shipped code and the implementation-versus-specification differences
   stated explicitly (§4.8).
4. **A calibrated periodic label.** A known moment-generating-function bound
   for quadratic forms of sub-Gaussian vectors [@hsu2012tail, Remark 2],
   applied to the floating-mean periodogram [@zechmeister2009gls], bounds the
   probability that the periodic label fires on iid clutter under
   occupancy-independent sampling, for any noise rate, at one read-out (§4.4);
   spending the level over the touch count, a Bonferroni-type allocation in the
   spirit of alpha spending [@lan1983discrete], extends the bound to every
   read-out of an unpruned history ([@sec:spend]). The inequalities are
   standard; the contribution is their use for per-cell occupancy periodicity
   and the measurement that repeated read-outs inflate false Periodic labels
   without such a correction.
5. **A mechanistic characterization of the shared classifier (measured,
   synthetic).**
   50%-duty periodicity is detected cleanly (dominant-harmonic amplitude
   0.653/0.707) at true-positive rate 2/3 from 25 windows on, with the single
   miss diagnosed as a prune/maturity race; after calibration, random clutter
   is never labelled Periodic at any read-out length (before: 0 to 4 of 5
   per length), and over whole 1024-window runs of the live, pruned detector at
   most 2 of 2000 held-out clutter cells are ever labelled Periodic, against
   953 of 2000 at a constant level (E5); and hysteresis is the dominant stabilizer — removing it yields 574
   flicker toggles at F1 0.810 versus 54 toggles at F1 1.000 on identical
   replayed noise at the same decay ($\lambda=0.90$) (§5).
6. **A unified engine that adds no per-dimension cost, released as a
   reproducible tool (measured).** Estimated memory is a fixed ~56 B per live
   cell with periodicity off, and per-window cost tracks live-cell count
   linearly; the 5.5–15.0× per-`integrate()` gap between backends is explained
   by 3D free-space voxel proliferation (4.4–10.0× more live cells), geometry
   rather than the engine. The whole system ships as
   an open-source ROS 2 package with a documented I/O contract and a seeded
   harness whose figures regenerate from measured CSVs (§3, §5).

On this synthetic suite, the results are consistent with a lifelong-mapping
core that is written once and shared across dimensions, with the per-dimension
cost arising in each backend's geometry rather than in the shared engine.
Whether the calibrated test improves on FreMEn's own component selection, and
how the classifier behaves on real LiDAR data, remain to be evaluated (§6).

STRATA is available at `github.com/kjungmo/strata` as a ROS 2 Humble package
under an open-source license, with a thin ROS adapter node and a reusable
characterization harness; it is designed to plug beneath an external localizer
such as `prism_loc`. The remainder of the paper positions STRATA against the
prior art (§2), specifies its two-band, testable architecture (§3), gives the
as-shipped engine mathematics including three honest implementation-versus-
specification differences (§4), reports the synthetic evaluation (§5), and
states the limitations, boundaries, and reproducibility of the tool (§6–7).

# 2 Related Work

We organize prior work into seven themes and, for each entry, state STRATA's
exact relation rather than a generic contrast.

**Lifelong and persistent mapping.** ELite [@gil2025elite] and LT-mapper
[@kim2022ltmapper] are recent LiDAR lifelong-mapping systems with the same aim
as STRATA. ELite computes a
two-timescale ephemerality score per point (within-session dynamic vs.
across-session transient) and drives a Lifelong/Static/Delta map triad;
LT-mapper composes a live map, a meta map, and a delta map through explicit
LT-SLAM alignment, LT-removert removal, and LT-map graduation stages. Both
are full lifelong-SLAM pipelines with multi-session point-cloud registration
and place recognition. STRATA implements only the graduation half of that
pattern — one per-cell state machine (Static/Periodic/Transient/Unknown,
§4.5) inside a ROS-free engine, with no scan matching, no loop closure, no
cross-session alignment, and no semantics. STRATA is not a competing full
system; it is the persistence-and-classification core that ELite's triad and
LT-mapper's live-to-static pipeline wrap around, and its measured evaluation
(§5) is correspondingly narrower — a synthetic, single-session
characterization, not the real-data and field-scale validation ELite and
LT-mapper report. Khronos
[@schmid2024khronos] generalizes the same two-timescale idea to an
active-window/long-term-reconstruction split with semantics and a scene
graph; STRATA stays at the raw per-cell level with no object or scene-graph
layer, a narrower and cheaper instance of the same principle. Classifying
cells by their temporal behaviour predates these systems. Temporal occupancy
grids [@arbuckle2002temporal] classify cells by occupancy over several
timescales with planar laser range-finders and locate doors in a real-world
setting; Mitsou and Tzafestas [@mitsou2007temporal] index every cell's
occupancy history over time and separate static, low-dynamic and high-dynamic
cells, in simulation with known poses. Biber and Duckett
[@biber2005dynamicmaps] and Meyer-Delius et al. [@meyerdelius2010temporary]
contribute the multi-timescale and static/temporary-split ideas STRATA's
window/decay/hysteresis/periodicity combination descends from. RTAB-Map
[@labbe2019rtabmap] offers selectable 2D/3D geometry inside one shipped
framework, coupled to full SLAM and STM/WM/LTM memory management; STRATA
narrows this to the mapping/persistence layer alone, behind a `MapBackend`
interface selected by a single string parameter, consuming an externally
supplied pose rather than estimating one. Berrio et al. [@berrio2021longterm]
report an 18-month field deployment of the same purge/promote pattern STRATA
implements as a Schmitt trigger (§4.3) — field-scale evidence for the pattern
that STRATA currently lacks for itself, and Pomerleau et al.
[@pomerleau2014longterm] infer online whether each 3D point is static or
dynamic from repeated observations, over seven months of data. Yang et al.
[@yang2025lifelong3d]
keep positive/negative version deltas over a base map so any past session can
be reconstructed; STRATA does no session versioning at all — a graduated cell
is simply part of the current static map, its history implicit in log-odds
and observation counts, not explicitly replayable. For 2D occupancy grids,
Stefanini et al. [@stefanini2022efficient; @stefanini2023safe] update a LiDAR
occupancy map for long-term operation while accounting for localisation error,
with the aim of keeping the map current rather than modelling periodic change.
Two 2026 works, concurrent with this one, produce layered outputs: MTD-Map
[@kim2026mtdmap] encodes the direction and duration of occupancy transitions
per voxel in one stage and outputs static, dynamic and transition maps,
evaluated on real datasets against established baselines, and Chen and Sun
[@chen2026layer] assign map content to long-term static, potentially dynamic
and removed observed-dynamic layers from geometric and semantic evidence.
Neither describes an explicit periodic class.

**Persistence and forgetting.** The Persistence Filter [@rosen2016persistence]
recursively estimates each feature's survival probability from a Bayesian
survival-time model. STRATA's per-window survival-decay multiplier $\lambda$
(§4.2) is a discretized, single-scalar simplification of that idea: one tunable
multiplicative factor pulls belief toward unknown each window in place of a
full survival-time posterior. The log-odds hit/miss update with clamping in
Thrun et al. [@thrun2005probabilistic] is used verbatim as STRATA's per-window
occupancy accumulation, with decay and hysteresis layered on top. Tipaldi et
al. [@tipaldi2013lifelong] and Saarinen et al. (iMac) [@saarinen2012imac]
model each cell as a two-state Markov process with a recency-weighted or
online-learned transition rate — the principled, per-cell-learned form of
"how fast a cell should forget." Perpetua [@saavedraruiz2025perpetua] combines
persistence and emergence filters under multiple hypotheses for features that
disappear and reappear. `survival_decay` is a single global
constant, a constant-rate special case of that formalism; STRATA states this
as a documented simplification (§6), not an unacknowledged gap.

**Periodicity.** FreMEn [@krajnik2017fremen] is the direct counter-argument
STRATA's Periodic class answers: a cell's occupancy can be a periodic
temporal signal, and a flat decay erases exactly that structure. FreMEn models
cyclic occupancy with a few Fourier components selected by amplitude from a
set of candidate periods, and it is the closest prior work to STRATA. Krajník
et al. [@krajnik2016persistent] use it in a 2D spatio-temporal occupancy grid
in which each cell represents both persistence (through the mean time between
state changes) and periodicity; the grid integrates one map per patrol,
replaces the ROS map server, and predicts time-specific maps for localisation
and planning. FROctomap [@krajnik2014froctomap] applies the same model to 3D
octree voxels, the exploration work of Krajník et al.
[@krajnik2015exploration] displays the static and daily-periodic cells of such
a grid, and the open-source FreMEn repository [@fremenrepo] provides separate
2D-grid and octree packages, and also a generic server that maintains FreMEn
models of arbitrary binary states keyed by an identifier. Warped hypertime
[@krajnik2019warped] extends the idea to pseudo-periodic variation. STRATA is
narrower than this family in period modelling: it tests one configured base
period and its harmonics and does not select periods. It differs from the
family in two respects: its primary output is a discrete class, reached
through a hysteresis band and pruning, rather than a predicted occupancy
probability; and it attaches a finite-sample false-alarm bound to the periodic
label. It also updates once per window, whereas the grid of Krajník et al.
[@krajnik2016persistent] integrates one map per patrol. STRATA's
`PeriodicityModel` (§4.4) runs FreMEn's incremental-Fourier accumulation in
parallel with decay and graduation, so an oscillating cell is classified
Periodic rather than mis-graduated to Static or mis-pruned as Transient — the
third class exists specifically to answer this argument, at the cost of a
bounded harmonic count and a validity gate tied to touched-window count
rather than elapsed time (a documented implementation-versus-specification
divergence, §4.8). Krajník et al.
[@krajnik2014spectral] is the earlier, more general spectral-analysis
lineage FreMEn later packaged as a mapping method; STRATA's bounded
incremental-Fourier approximation is a further narrowing of that lineage, not
the full spectral machinery.

**Dynamic-object removal.** Removert [@kim2020removert] conservatively
removes dynamic points and then reverts wrongly removed static ones, because
aggressive removal erases real structure. This is the exact justification for
STRATA's hysteresis band (§4.3): on the occupancy path a graduated cell
demotes only when its probability falls below `p_dem < p_grad`, i.e. sustained
contradicting evidence across windows rather than a single contradicting frame
(the periodic guard of §4.3 has no band of its own) — the same
failure mode Removert's revert pass patches after the fact, STRATA prevents
online by construction. ReFusion [@palazzolo2019refusion] treats a reliably
observed empty voxel as evidence, not merely an absence of evidence; STRATA's
ray-clearing — exact Bresenham for `grid2d`, sub-voxel-step sampling for
`voxel3d` (§4.6) — applies the same negative-information logic online, with
misses accumulating as `l_miss` and eventually demoting a graduated cell once
an object vacates it. ERASOR [@lim2021erasor] removes dynamic points from an
already-accumulated 3D map offline via per-bin pseudo-occupancy ratios; STRATA
never batches and performs no object-level reasoning — every cell's class is
a running online per-window hit/miss-and-decay state, the baseline ERASOR's
batch, object-level approach is not attempting to be.

**Production stacks.** OctoMap [@hornung2013octomap] shares STRATA's
log-odds-with-clamping core but compresses storage with an octree; `voxel3d`
instead uses a flat `int64`-keyed hash for O(1) insert/lookup, trading
multi-resolution compression for simplicity and unit-testability (§6). The
voxel-hashing scheme of Nießner et al. [@niessner2013hashing] is the direct
data-structure this hash follows. UFOMap [@duberg2020ufomap] makes "never
observed" an explicit third per-voxel state; STRATA gets the same distinction
implicitly from the `observations` counter and log-odds value rather than a
dedicated state, cheaper to implement and less explicit to query. Nav2 STVL
[@macenski2020stvl] decays costmap obstacle evidence for short-horizon
planning using the same decay-rate mechanism family STRATA's
`survival_decay` uses to decay mapping evidence toward unknown — same knob,
different consumer. Layered Costmaps [@lu2014layered] composite independently
maintained plugin layers per cell; STRATA's output value convention
(`static→100, periodic→75, transient→50, unknown→-1`) reproduces the same
layered mental model as the output of one cell-level state machine rather
than as multiple composited layers. SLAM Toolbox [@macenski2021slamtoolbox]
bounds compute by adding and removing pose-graph nodes while performing its
own 2D SLAM; STRATA has no pose graph and no SLAM, consuming an externally
supplied transform and pruning at the per-cell evidence level (§4.5) instead
— a different layer of the stack, included as the standard 2D lifelong-mapping
comparison point.

**Pluggable-backend architecture.** ROS 2 Pluginlib [@ros2pluginlib] is the
canonical swappable-backend mechanism — abstract base class, XML plugin
description, runtime `dlopen`-based loading — and the pattern STRATA
deliberately rejects, not adopts. With exactly two backends, STRATA resolves
`MapBackend` to one of two `unique_ptr`s at construction from a single
runtime string parameter (§3); no plugin XML, no dynamic loading, no
`ament_index` registry. This is the right choice at two backends; pluginlib
becomes the right choice only once backend count grows past what a single
`if`/`else` factory reads clearly. RTAB-Map [@labbe2019rtabmap] is re-cited
here as a shipped system offering selectable 2D/3D geometry under one
framework, narrowed by STRATA to a persistence-only
module with the classifier shared by composition rather than duplicated per
geometry — the only per-backend code is point-to-`CellId` mapping and the
free-space ray walk.

Relative to the works above, the structural difference is narrow.
Periodicity modelling that does not depend on geometry already exists, since
the FreMEn server models any binary state that has an identifier
[@fremenrepo]. What STRATA shares, unchanged, between its 2D and 3D
ray-casting backends is the full cell-level state machine: windowed log-odds
with survival decay, graduation and demotion hysteresis, pruning, layer
classification and the periodicity test.

**Periodicity tests and error control.** The periodic test uses the
generalised Lomb–Scargle periodogram [@zechmeister2009gls]. Analytic
false-alarm probabilities for periodogram peaks, including the search over
frequencies, are derived under white Gaussian noise [@baluev2008]. For binary
series, Schmidtke and Vetter [@schmidtke2026binary] test a constant success
probability against a periodic one of unspecified period, with a level that
holds asymptotically. STRATA addresses a narrower question: a finite-sample,
distribution-free bound for independent Bernoulli occupancy at one configured
period and its harmonics, under any sampling pattern fixed independently of
the occupancy. The calibrated false-alarm bound of §4.4 obtains it from the
moment-generating-function bound for quadratic forms of sub-Gaussian vectors of
Hsu et al. [@hsu2012tail, Remark 2], followed by Markov's inequality and a
union bound, with the variance proxy of Hoeffding's lemma [@hoeffding1963]; the
result is an application of that bound, not a new inequality. Spending the
level over the touch count ([@sec:spend]) is a Bonferroni-type allocation over
repeated looks, related to the alpha-spending functions of group-sequential
trials [@lan1983discrete]; time-uniform bounds from nonnegative
supermartingales [@howard2020timeuniform] are the modern alternative for
repeated testing, which we do not use.

# 3 System Overview and Architecture {#sec:system}

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
it.](figures/fig_architecture.pdf){#fig:architecture}

## 3.1 The `MapBackend` interface and the geometry-free invariant {#sec:mapbackend}

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

## 3.2 Backend selection {#sec:selection}

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

## 3.3 ROS 2 I/O contract {#sec:io}

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

# 4 The Layered Map Engine {#sec:method}

This section specifies the engine as implemented in `strata_core`. All
equations are transcribed from the source, not from the design specification;
where the two disagree, [@sec:specdiff] documents the difference. Symbols follow
the notation used throughout the paper: $\ell$ is a cell's log-odds occupancy
evidence, $p=\sigma(\ell)$ its occupancy probability, $t$ the window (phase)
index, $H$ the harmonic count, $T$ the base period, and $\lambda$ the survival
decay.

## 4.1 Per-frame accumulation and windowing {#sec:windowing}

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

## 4.2 Log-odds persistence with survival decay {#sec:logodds}

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

## 4.3 Schmitt-trigger graduation and demotion {#sec:schmitt}

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
later reveals periodicity. That second guard has no band of its own, but with
the centred amplitude of [@sec:periodicity] it can only fire on a cell that has
been observed free in a substantial share of its touched windows ($a\le
4\bar v(1-\bar v)$), never on a wall that is occupied whenever seen, and the
significance test below, with its level spent over the touch count, bounds the
chance that it ever fires on a merely noisy wall: a graduated cell is never
pruned, so its history is uninterrupted and, on iid noise, it is demoted by this
term at some point of the run with probability at most $\delta$. The two rules cannot fight in one window, because
graduation requires $\lnot\text{periodic}$ and
$p\ge p_{\text{grad}}>p_{\text{dem}}$.

## 4.4 FreMEn-lite periodicity {#sec:periodicity}

In parallel with occupancy, each touched cell (when `enable_periodicity`) feeds
an incremental Fourier model in the spirit of FreMEn [@krajnik2017fremen]. The
model is restricted to one configured base period and its first harmonics;
unlike FreMEn, it does not select periods from a candidate set, so a cycle
whose frequency is not a harmonic of the base frequency is outside its scope.
With
the per-window occupancy sample $v=[\text{occ}]\in\{0,1\}$ and phase index $t$,
`gather` accumulates:

$$n \mathrel{+}= 1,\quad S_0 \mathrel{+}= v,\quad
C_k \mathrel{+}= v\cos\theta_k,\quad S_k \mathrel{+}= v\sin\theta_k,\quad
E^c_k \mathrel{+}= \cos\theta_k,\quad E^s_k \mathrel{+}= \sin\theta_k,\quad k=0..H{-}1,$$
$$\theta_k=(k{+}1)\omega t,\qquad \omega = \frac{2\pi}{\max(1,\,T)}.$$

Here $n$ counts **touched windows fed to gather**, not elapsed windows, and
$E^c_k,E^s_k$ record the phases at which the cell was touched. With the
touched-window mean $\bar v=S_0/n$, the coefficients are **mean-centred** as in
FreMEn:

$$a_k=\tfrac{2}{n}\big(C_k-\bar v\,E^c_k\big),\qquad
b_k=\tfrac{2}{n}\big(S_k-\bar v\,E^s_k\big),$$

i.e. $a_k+\mathrm{i}b_k=\tfrac{2}{n}\sum_{t}(v_t-\bar v)e^{\mathrm{i}\theta_k}$
over the touched windows. The phase prediction at window $t$ is

$$\hat p(t)=\operatorname{clip}_{[0,1]}\!\left(\bar v+\sum_{k=0}^{H-1}
\big[a_k\cos\theta_k+b_k\sin\theta_k\big]\right),$$

and the harmonic and dominant amplitudes are

$$a_{(k)}=\sqrt{a_k^2+b_k^2},\qquad a=\begin{cases}0, & n < T\\[4pt]
\displaystyle\max_{0\le k<H}a_{(k)}, & n \ge T.\end{cases}$$

The periodicity test, which adds a significance condition to the amplitude
threshold, is given below.

The $n\ge T$ gate means a sparsely observed cell needs $T$ touches before its
amplitude is trusted, which can span far more than $T$ elapsed windows.

**Centring, and the defect it corrects.** Because $|v_t-\bar v|$ averages
$2\bar v(1-\bar v)$, the triangle inequality gives $a\le 4\bar v(1-\bar v)$
under any sampling: a cell occupied (or free) in every touched window has
$a=0$, and $a_{\min}=0.3$ requires $0.081<\bar v<0.919$. The code as first
released (v0.1.0) omitted $E^c,E^s$, so its coefficients were uncentred and,
for an always-occupied cell, $a$ was twice the mean phase vector of the touched
windows — it measured *when* the cell was seen, not how it varied. For $n$
consecutive touches $a=\max_k 2|\sin(n(k{+}1)\pi/T)|/(n|\sin((k{+}1)\pi/T)|)$,
which exceeds 0.3 at $n=10$–13 for $T=8$ (peak 0.439 at $n=11$) and at
$n=29$–40 for the shipped $T=24$, $H=2$; through `|| periodic` a wall hit every
window left the Static layer for 120 integration ticks at $L=10$, and a wall
seen 8 of every 48 windows had $a\approx1.66$ and was never Static again. Ten
regression tests cover both failures and genuine detection; seven fail on
v0.1.0 and all pass after the fix; @tbl:fix
compares E1–E3 before and after.

**Calibrating the periodic test against noise.** Centring removes the bias of
the amplitude but not its variance, and $a\ge a_{\min}$ alone is not a test.
If a cell's occupancy is pure noise, $v_t\sim\text{Bernoulli}(m)$ independent
of the phase, each centred coefficient has variance $2m(1-m)/n$ under uniform
phase coverage, a standard deviation of $\sqrt{0.5/8}=0.25$ at $m=\tfrac12$,
$n=T=8$. A fixed $a_{\min}$ is then crossed by chance at short lengths, and
pruning ([@sec:states]) keeps returning cells to short lengths. For harmonic
$k$ let $\tilde z_t=(\cos\theta_k-\bar c,\ \sin\theta_k-\bar s)^\top$ be the
centred phase vector over the touched windows,
$y_k=\sum_t(v_t-\bar v)\tilde z_t=\tfrac n2(a_k,b_k)^\top$ and
$M_k=\sum_t\tilde z_t\tilde z_t^\top$. Then

$$d_k=y_k^\top M_k^{-1}y_k,\qquad B(r)=\begin{cases}2r\,e^{1-2r}, & r>\tfrac12\\ 1, & r\le\tfrac12,\end{cases}$$

where $d_k$ is the drop in squared residual when a sinusoid at harmonic $k$ is
fitted with a floating mean, the generalised Lomb–Scargle periodogram
[@zechmeister2009gls]. $M_k$ needs two more accumulators per harmonic,
$\sum\cos2\theta_k$ and $\sum\sin2\theta_k$. The test requires, for some
harmonic, both an effect size and significance at a nominal level $\delta$
(`periodic_false_alarm`):

$$\text{periodic}=\texttt{enable\_periodicity}\land n\ge T\land\exists k:\
\big[a_{(k)}\ge a_{\min}\land H\,B(d_k)\le\delta\big].$$

The proposition below bounds the false-alarm probability of this test. It is
an application of known results, not a new inequality: the
moment-generating-function step is the bound of Hsu et al.
[@hsu2012tail, Remark 2] for a quadratic form of a sub-Gaussian vector, here
with the identity matrix and Hoeffding's variance proxy $\tfrac14$, and the
rest is Markov's inequality and a union bound over the harmonics. We state it
with the explicit constants that the shipped test uses.

*Proposition (calibrated false-alarm bound).* Let the $n$ touched windows be
fixed independently of the occupancy, and let the $v_t$ be independent
Bernoulli($m$) for one unknown $m\in[0,1]$. If $M_k$ is non-singular, then
$\Pr(d_k\ge r)\le B(r)$ for every $r>0$, and hence, for every $a_{\min}\ge0$
and $0<\delta<H$, the test is true with probability at most $\delta$.

*Proof.* Because $\sum_t\tilde z_t=0$, the unknown mean drops out:
$y_k=\sum_t\xi_t\tilde z_t$ with $\xi_t=v_t-m$. Put $b_t=M_k^{-1/2}\tilde z_t$
and $u=\sum_t\xi_tb_t$, so $d_k=\|u\|^2$ and $\sum_tb_tb_t^\top=I_2$. Each
$\xi_t$ has mean zero and lies in an interval of length 1, so Hoeffding's lemma
[@hoeffding1963] gives $\mathbb E e^{s\xi_t}\le e^{s^2/8}$, and by independence
$\mathbb E e^{w^\top u}\le e^{\|w\|^2/8}$ for every $w\in\mathbb R^2$. Following
Hsu et al. [@hsu2012tail], let $g\sim\mathcal N(0,I_2)$ be independent of $u$;
for $0\le\eta<2$,
$\mathbb E_g e^{\sqrt{2\eta}g^\top u}=e^{\eta\|u\|^2}$, so
$\mathbb E e^{\eta d_k}\le\mathbb E_g e^{\eta\|g\|^2/4}=(1-\eta/2)^{-1}$.
Markov's inequality with $\eta=2-1/r$ gives $2re^{1-2r}$ for $r>\tfrac12$.
$B$ decreases strictly on $(\tfrac12,\infty)$, so $H\,B(d_k)\le\delta$ exactly
when $d_k\ge r^\star$ with $B(r^\star)=\delta/H$, and the union bound over the
$H$ harmonics gives probability at most $\delta$. $\square$

The bound needs no knowledge of $m$, since the constant $\tfrac14$ is the
Bernoulli worst case $m=\tfrac12$. When $M_k$ is singular the code projects
onto its range, where the same argument gives a smaller bound. Under uniform
phase coverage at a harmonic with $2(k+1)\not\equiv0 \pmod T$ (so that the sine
column does not vanish; the defaults satisfy it) $M_k=\tfrac n2 I_2$ and $d_k=n\,a_{(k)}^2/2$, so the test
becomes an amplitude threshold that shrinks with $n$:
$a_{(k)}\ge\max(a_{\min},a^\star(n))$ with $a^\star(n)=\sqrt{2r^\star/n}$. For
$\delta=0.1$ and $H=3$, $r^\star=3.115$, so $a^\star(8)=0.883$ and
$a^\star(64)=0.312$. The bound is conservative: in the Gaussian limit at
$m=\tfrac12$, $\Pr(d_k\ge r)\to e^{-2r}$, a factor $2er$ below $B(r)$, or about
17 at $r^\star$. We therefore first treated $\delta$ as a nominal level for one
read-out and chose it on a calibration set disjoint from every evaluation seed
(E0). The pre-stated rule took the largest
$\delta\in\{0.01,0.02,0.05,0.1,0.2\}$ whose measured per-read-out false-alarm
rate stays at or below 0.01, both over a null grid and in the live pipeline,
and it selected $\delta=0.1$ ([@tbl:e0], §5.1). That rule compares point
estimates: the worst pipeline rate at $\delta=0.1$, 7 of 1000 cells, has a 95%
Wilson upper limit of 0.0144 [@wilson1927], so the calibration does not establish the 0.01
target with confidence. Only $\delta$ is held out; the test itself was
introduced after the amplitude-only rule produced false positives on the E2
seed, so E2 is not a blind evaluation of its design. More importantly, one
read-out is not what the map does.

### 4.4.1 Spending the level over repeated read-outs {#sec:spend}

The map evaluates the periodic test after every window, so a clutter cell gets
one chance per read-out, and pruning ([@sec:states]) erases its history and
starts another. E5 measures the consequence: at a single-read-out level of 0.1,
up to 953 of 2000 Bernoulli clutter cells are labelled Periodic at some window
within 1024 windows ([@sec:e5]). The shipped rule (`periodic_alpha_spending`)
therefore replaces the constant level $\delta$ by a level that decreases with
the touch count,

$$\delta_n=\delta\,\frac{T}{n(n+1)}\quad(n\ge T),\qquad \sum_{n\ge T}\delta_n=\delta,$$

where the sum telescopes because $T/(n(n+1))=T/n-T/(n+1)$. This is a
Bonferroni-type allocation of the level over the sequence of read-outs, in the
spirit of the alpha-spending functions used for repeated significance tests in
group-sequential trials [@lan1983discrete]; the trajectory-level proposition
below is the union bound over it.

*Proposition (trajectory-level false-alarm bound).* Call a *history* of a cell
its touched windows from its creation until it is pruned or the run ends.
Suppose that, given everything observed before the history begins, its touched
windows are fixed independently of its occupancy and its occupancy samples are
independent Bernoulli($m$). With the level $\delta_n$ in the periodic test, the
probability that the test holds at some read-out of the history is at most
$\delta$, whatever the length of the run. If $L_N$ histories of the cell begin
in windows $1,\dots,N$, the probability that the cell is labelled Periodic at
some window up to $N$ is at most $\delta\,\mathbb E[L_N]$.

*Proof.* Condition on everything observed before the history begins; under the
hypothesis the history is then a fixed-design sample. For $n\ge T$ let $D_n$ be
the event that the statistic of its first $n$ samples, computed as if the cell
were never pruned, passes the test at level $\delta_n$. The calibrated
false-alarm bound with $\delta_n$ in place of $\delta$ gives
$\Pr(D_n)\le\delta_n$. A read-out at touch count $n$ evaluates the test on
exactly these $n$ samples (read-outs between two touches repeat the last
value), and it happens only if the history survived to $n$, so the event that
the test ever holds is contained in $\bigcup_{n\ge T}D_n$. The union bound gives
at most $\sum_{n\ge T}\delta_n=\delta$; pruning only removes read-outs. For the
second claim, the $\ell$-th history begins at a window determined by the past,
so the probability that it begins by $N$ and fires is at most $\delta$ times the
probability that it begins by $N$; summing over $\ell$ gives
$\delta\,\mathbb E[L_N]$. $\square$

The proposition is strongest where pruning does not act. A graduated cell is
exempt from pruning, so a Static wall has one history, and the probability that
the `|| periodic` demotion ever fires on iid noise is at most $\delta$ for the
whole run. For clutter that is pruned and re-created, the bound grows with the
number of histories, which depends on the data: a Bernoulli(0.4) cell under the
E2 parameters begins about 62 histories in 1024 windows, and
$\delta\,\mathbb E[L_N]$ is then vacuous. That regime is measured (E5), not
guaranteed. The null is still that of the calibrated bound: temporally
correlated clutter violates it, and E5 shows that the test flags such clutter.
The shipped level is chosen on separate calibration seeds, now at trajectory
level. The pre-stated rule takes the largest
$\delta\in\{0.01,0.02,0.05,0.1,0.2\}$ for which the rate of ever labelling a
Bernoulli clutter cell Periodic within 1024 windows has a Wilson 95% upper
limit [@wilson1927] of at most 0.01 in every calibration configuration. It selects
$\delta=0.2$, the largest candidate ([@tbl:e5choice]), and this is the shipped
default. For the shipped rule, then, what is proved is the per-history bound
0.2; the 0.01 target is a measurement. Under uniform coverage the threshold
becomes $a^\star(n)=\sqrt{2r^\star_n/n}$ with $B(r^\star_n)=\delta_n/H$, which
falls roughly like $\sqrt{\log n/n}$ rather than $1/\sqrt n$. The detection
delay is the price: in E2 a 50%-duty door with $T=8$ is detected from 25
windows, against 15 with a single-read-out level of 0.1 and 8 without the test.

## 4.5 Cell-class state machine and pruning {#sec:states}

At the end of each window, cells below confidence are erased:

$$\text{erase cell} \iff \lnot g\ \land\ p < p_{\text{prune}}\ \land\
a < a_{\min}.$$

Static cells ($g$) and periodic cells (which have $a\ge a_{\min}$) are never
pruned; pruning a cell also erases its FreMEn coefficients. The guard uses the
lenient effect-size screen $a<a_{\min}$, not the full test, so a *candidate*
periodic cell keeps its coefficients, and its growing $n$, until the test
resolves it. We considered keeping the Fourier history of erased cells and
rejected it: pruning exists so that clutter does not accumulate, and a record
for every cell ever touched would grow without bound, fastest in `voxel3d`. A
re-created cell restarts at $n=0$ with a new history and, under the spent
level of [@sec:spend], a new level budget; this is why the trajectory bound
scales with the number of histories. A snapshot classifier reads
out one of four states by a priority ladder:

$$\text{absent}\to\text{Unknown};\quad g\to\text{Static};\quad
\text{periodic}\to\text{Periodic};\quad
p\ge p_{\text{prune}}\to\text{Transient};\quad \text{else}\to\text{Unknown}.$$

The full transition table is [@tbl:states]. Note that a present cell classifies
Unknown when $p<p_{\text{prune}}$ and it is neither periodic nor graduated — the
same condition under which it is generally pruned in the same window.

| From | To | Trigger |
|---|---|---|
| (implicit) Unknown | Transient | first `observeHit/Miss`: cell created, $\ell{=}0\Rightarrow p{=}0.5\ge p_{\text{prune}}$ |
| Transient | Static | $p\ge p_{\text{grad}}\land\text{obs}\ge N_{\min}\land\lnot\text{periodic}$ (graduate) |
| Transient | Periodic | periodic: $n\ge T$ and, for some $k$, $a_{(k)}\ge a_{\min}$ and $H\,B(d_k)\le\delta_n$ |
| Transient | Unknown (erased) | $p<p_{\text{prune}}\land a<a_{\min}\land\lnot g$ (prune) |
| Static | Transient / Periodic / Unknown | demote ($p\le p_{\text{dem}}\lor\text{periodic}$), then re-classified by ladder |
| Periodic | Transient / Unknown | periodic becomes false (pruned only once also $a<a_{\min}$) |
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
      periodic <- enable_periodicity and n >= T and
                  exists k: a_k >= a_min and H * B(d_k) <= delta * T / (n (n + 1))
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

## 4.6 Geometry backends {#sec:backends}

The two backends differ only in how a world point becomes a `CellId` and how the
free-space ray is walked; both delegate all evidence updates to the shared
`LayeredMap`.

`Grid2DBackend` addresses a fixed-size, fixed-origin 2D array described by
`GridMeta` $=\{$`width, height, resolution, origin_x, origin_y`$\}$. It buckets
each hit's $(x,y)$ (dropping $z$) into a row-major id
$\texttt{gridCellId}(m,g_x,g_y)=g_y\cdot\texttt{width}+g_x$; points outside the
array are dropped. Free space is cleared with integer Bresenham's line algorithm
[@bresenham1965] from the sensor cell up to but excluding the hit cell, calling `observeMiss`
on every cell in that half-open span — including the sensor-origin cell — so
the hit cell receives only `observeHit`. Rendering to `nav_msgs/OccupancyGrid` initializes every cell to
`-1`, then overwrites in ascending confidence order transient&rarr;`50`,
periodic&rarr;`75`, static&rarr;`100`, so static wins any tie.

`Voxel3DBackend` is a sparse hash: the map grows through the hash map and needs
no preallocated extent. Each
axis is floor-divided by `voxel_size` and offset by $\texttt{kOff}=1\ll 20$ to
keep the per-axis index non-negative, and the three 21-bit
($\texttt{kBits}=21$) fields are packed into one 64-bit key,
$\text{id}=(v_x\ll 42)\,|\,(v_y\ll 21)\,|\,v_z$; `voxelCenter` is the exact
inverse, returning $(v+0.5)\cdot\texttt{voxel\_size}$ per axis. This gives O(1)
hashing. The key space is nevertheless finite: each 21-bit field represents the
indices $-2^{20}$ to $2^{20}-1$ around the origin, far beyond the extent of a
building at the default voxel size, and the code does not check this range, so
a point outside it would receive a wrong key rather than being dropped. Free space is
cleared by ray-sample marching, not geometric voxel traversal: the ray is
subdivided into $\lfloor\text{len}/(0.5\cdot\texttt{voxel\_size})\rfloor$
half-voxel steps, and each interior sample is hashed to its containing voxel and
marked `observeMiss`. This is simpler than exact traversal but can over-sample a
long ray and can skip a thin voxel when the step count rounds down. The hit is
always `observeHit`, with no bounds check: the hash grows to fit, and the key
range is not checked (above).
`staticPoints()` maps the static-cell set through `voxelCenter` to a point
cloud. [@tbl:backends] contrasts the two.

| | `Grid2DBackend` | `Voxel3DBackend` |
|---|---|---|
| Cell addressing | 2D array index via `GridMeta` (fixed $W\times H$, fixed origin) | 3D spatial hash, sparse, `int64` packed key (21 bits per axis) |
| Hit dimensionality | $x,y$ (z dropped by projection) | $x,y,z$ (full volumetric) |
| Free-space ray | Bresenham line (exact, on-grid) | ray-sample march at 0.5-voxel step (approximate, off-grid) |
| Growth | none — pre-sized at construction | sparse — hash grows with exploration; per-axis index range fixed by the 21-bit key |
| Output | `nav_msgs/OccupancyGrid` (dense `-1/50/75/100`) | static voxel centers &rarr; `PointCloud2` |

Table: The two geometry backends. All persistence and periodicity logic is
shared; only these two rows of behavior differ. {#tbl:backends}

## 4.7 Parameters {#sec:params}

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
| `periodic_amplitude_min` ($a_{\min}$) | 0.3 | amplitude | minimum harmonic amplitude (effect size) to classify Periodic |
| `periodic_false_alarm` ($\delta$) | 0.2 | probability | level of the significance test |
| `periodic_alpha_spending` ($\delta_n$) | true | bool | spend $\delta$ over the touch count ([@sec:spend]) |
| `period_windows` ($T$) | 24 | windows | FreMEn base period; also amplitude-validity gate ($n\ge T$) |
| `n_harmonics` ($H$) | 2 | count | Fourier harmonics tracked per cell |

Table: Engine parameters and defaults (struct and `params/*.yaml` agree).
{#tbl:params}

Per-cell state is compact: `CellEvidence` is 24 bytes, plus roughly 32 bytes of
`unordered_map` node overhead, giving the ~56 B/cell footprint reported in the
evaluation. That figure is an estimate for the periodicity-off path, not a
measurement. A FreMEn-tracked cell adds a `Coeff` record, with two scalar
accumulators and six heap-allocated vectors of $H$ values in a second hash map,
allocated only for cells touched at least once with periodicity enabled; it is
not included in the estimate, nor are the hash tables' bucket arrays.

## 4.8 Implementation versus specification {#sec:specdiff}

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

**SPEC-DIFF #2 (periodicity guards; resolved).** The specification's §3.4
pseudocode originally omitted the `!periodic` and `|| periodic` guards that the
code applies ([@sec:schmitt]). The specification was updated together with the
amplitude-centring fix and now states both guards and the centred amplitude.

**SPEC-DIFF #3 (amplitude gate counts touched windows, not elapsed time).** The
specification says amplitude is zero before `period_windows` windows have
*elapsed*. The code gates on $n<T$, where $n$ is the count of touched windows
(calls to `gather`), not elapsed wall or window time. A sparsely observed cell
needs $T$ touches, which can span far more than $T$ windows — the mechanism
behind the low-duty periodicity miss reported in the evaluation.

# 5 Evaluation (synthetic)

## 5.1 Setup

All experiments (E1–E5), and the calibration run E0, are driven by a standalone harness in
`paper/experiments/` that links directly against the `strata_core` sources —
no ROS 2, no ament, no colcon. Reproduction from the repository root is a
single call, `bash paper/experiments/run_all.sh`, which configures and builds
`strata_core` and the six harness executables, runs each one, and writes the
CSVs in `paper/experiments/results/` from which every number and figure in
this section is taken; `strata_core`'s own `ctest` suite (1/1 target,
aggregating the 49 gtest cases: ten regression tests for the amplitude defect
of [@sec:periodicity], nine for its significance test and eight for the spent
level of [@sec:spend]) is checked first, so the harness is only
trusted once the unit-level behavior it builds on is confirmed. Every stochastic experiment (E1–E3) derives its `std::mt19937` generator
deterministically from the fixed constant `kSeed = 12345` (with
per-backend/per-configuration offsets recorded in code), and the seed is
logged in each CSV's `#seed` row (E1–E4 use offsets 0–1500 from 12345; the
calibration E0 uses seeds from 20260928 upward, and E5 draws its calibration
seeds from 30260928 and its evaluation seeds from 40260928 upward, so no
calibration stream coincides with an evaluation stream), so the reported
precision/recall/F1/flicker/TPR/FPR numbers are exactly reproducible. E1–E4
magnitudes are single-seed (12345) point estimates: no cross-seed variance is
characterized for them, so figures such as 0.9253 vs. 0.9758 should be
read as illustrative of a mechanism rather than as robust distributional
estimates. E5 is multi-seed and reports Wilson 95% intervals: Bernoulli($m$)
clutter, $m=0.1,\dots,0.9$, plus always-occupied, always-free and temporally
correlated cells (a two-state Markov chain with mean 0.5 and mean dwell 10
windows), 2000 cells per configuration (10 seeds of 200), through the live
`LayeredMap` with the E2 and with the shipped parameters, touched every window
or independently with probability $q=0.5$, over 1024 windows, labelled after
every window; four door types for detection delay; and 20 E2-style scenes for
a four-class confusion. The calibration half of E5 fixes the shipped level on
its own seeds before the evaluation half runs. All E1–E4 results are from the
current engine (centred coefficients and the test at $\delta=0.2$ spent over
the touch count); the same harness run against three earlier engines (v0.1.0,
the centred amplitude-only rule, and the test at the single-read-out level
$\delta=0.1$, archived in `results/pre_spending_2026-09-28/`) is kept for
comparison. E4 additionally reads the wall clock to
time `integrate()`/`endWindow()` calls; its absolute microsecond figures are
therefore machine-dependent, while the live-cell counts it also reports are
deterministic (they follow only from ray geometry, not timing).

**What is compared.** There is no external baseline. The lifelong-SLAM systems
of §2 do not expose a persistence core that this harness could drive. The
FreMEn per-cell model [@krajnik2017fremen; @krajnik2016persistent;
@fremenrepo], with its amplitude-ranked component selection, could be driven by
the same streams, but we have not run it, so every comparison below is
internal. The centred amplitude-only rule kept in @tbl:fix thresholds
FreMEn-style coefficients at one fixed period; it is STRATA's own earlier rule,
not an implementation of FreMEn.

The four experiments map onto the two thesis claims of §3–4 (C2, C5)
differently. E1 and E4 probe *backend-behavior equivalence*: each backend is
driven, in its native geometry, by its own deterministically seeded hit
stream — `Grid2DBackend` on the 2D integer cell lattice, `Voxel3DBackend` in
continuous 3D, with per-run seeds `kSeed`+offset recorded in code — and we
compare the outcomes. Because the classifier both backends call is the same
`LayeredMap` type, any divergence arises in the geometry layer
(point&rarr;`CellId` mapping and the free-space ray walk) together with its
native input sampling; this is a native-geometry comparison, not a controlled
same-input isolation. E2 and E3, by contrast, drive `LayeredMap`/`PeriodicityModel`
directly, with no backend in the loop at all — a choice that is itself an
argument for C1: because persistence, hysteresis, and periodicity are
implemented once, independent of `CellId` provenance, characterizing them
against `LayeredMap` directly is valid for whichever backend supplies the
cell ids in production. Each harness constructs its own `LayeredMapParams`
in code rather than loading the shipped YAML, so several values deviate from
the [@tbl:params] production defaults; [@tbl:eparams] lists every deviation,
per experiment, so the runs are reproducible without reading the harness
source. Three deviations are common to interpreting the results below: all
four harnesses run at `layer_interval` 1 (one window per integration tick,
against the production default of 10), so "window" and "frame" coincide in
every reported count; E1 and E3 are each run twice, with periodicity off to
isolate the persistence layer and on at the shipped defaults, E4 runs with it
off, and E2 exercises the FreMEn model at $T=8$; and
E4 and E2 both set `survival_decay` 1.0 (no forgetting). E2 in particular
raises all six of the log-odds/graduation defaults it touches
($l_{\text{hit}}$ 1.0, $l_{\text{miss}}$ $-1.0$, $\lambda$ 1.0,
$p_{\text{grad}}$ 0.9, $p_{\text{dem}}$ 0.4, $N_{\min}$ 5) to drive its
detected-versus-pruned classification, and E3 lowers `prune_prob` to 0.01 to
keep noisy walls alive; these are the values actually compiled into each
harness.

| Parameter (default) | E1 | E2 | E3 | E4 |
|---|---|---|---|---|
| `layer_interval` (10) | 1 | 1 | 1 | 1 |
| `enable_periodicity` (true) | false / true | true | false / true | false |
| `l_hit` (0.85) | — | 1.0 | — | — |
| `l_miss` ($-0.4$) | — | $-1.0$ | — | — |
| `survival_decay` (0.97) | — | 1.0 | swept | 1.0 |
| `graduate_prob` (0.8) | — | 0.9 | swept | — |
| `demote_prob` (0.45) | — | 0.4 | swept | — |
| `min_observations` (3) | — | 5 | — | — |
| `prune_prob` (0.05) | — | — | 0.01 | — |
| `period_windows` (24) | — | 8 | — | n/a |
| `n_harmonics` (2) | — | 3 | — | n/a |
| `periodic_false_alarm` (0.2, spent) | — | — | — | n/a |

Table: Per-experiment deviations from the [@tbl:params] production defaults.
"—" = default unchanged; "swept" = varied across the E3 configuration grid
($\lambda\in\{0.90,0.97,1.0\}$, `graduate_prob` $\in\{0.6,0.7,0.8,0.9\}$,
`demote_prob` $\in\{0.3,0.5,\text{graduate\_prob}\}$); "n/a" = inert because
periodicity is disabled in that run; "false / true" = run once each way. {#tbl:eparams}

## 5.2 E1 — Static-layer map quality vs. time

**Setup.** A ground-truth static cross of 161 cells (segment $y{=}60,
x\in[20,100]$ and $x{=}60, y\in[20,100]$) is hit every window, alongside
transient movers at three densities — low/med/high = 5/25/100 fresh random
cells per window — over 40 windows, run against both a $120\times120$
(resolution 1.0) `Grid2DBackend` and a $1.0$-voxel `Voxel3DBackend`, once with
periodicity disabled to isolate the persistence layer and once with it enabled
at the shipped defaults ($T=24$, $H=2$, $\delta=0.2$ spent over the touch
count; `results/e1_static_quality_periodic.csv`). Static-set
precision/recall/F1 against the 161-cell wall set is logged per window
(`results/e1_static_quality.csv`).

| backend | density | recall $=1$ from | precision @ $w{=}39$ | F1 @ $w{=}39$ |
|---|---|---|---|---|
| grid2d | low (5/window) | window 3 | 1.0000 | 1.0000 |
| grid2d | med (25/window) | window 3 | 0.9938 | 0.9969 |
| grid2d | high (100/window) | window 3 | 0.9253 | 0.9612 |
| voxel3d | low (5/window) | window 3 | 1.0000 | 1.0000 |
| voxel3d | med (25/window) | window 3 | 0.9938 | 0.9969 |
| voxel3d | high (100/window) | window 3 | 0.9758 | 0.9877 |

Table: E1 — final static-set precision/recall/F1, both backends, all three
clutter densities. {#tbl:e1}

![E1: static-set F1 vs. window index, one panel per backend, one line per
clutter density. Both backends recover F1$\approx$1.0 within three windows;
the 100-movers/window regime is the only one with a visible
precision/F1 dip, and the dip is larger for
grid2d.](figures/fig_e1_f1_vs_time.pdf){#fig:e1}

With $N_{\min}=3$, a cell needs three touched windows before it is eligible
to graduate; the CSV confirms recall jumps from 0 to 1.0 exactly at the
third window (window index $t{=}2$) for every one of the six
backend$\times$density combinations, and holds at 1.0 through $w{=}39$ in
every case — no wall cell is ever demoted by clutter. The only errors are a
handful of false-positive statics that appear when a random mover happens to
re-hit the same cell often enough to graduate on its own; at low density
(5 movers/window) this never happens at all, and precision stays exactly
1.0 through $w{=}39$ for both backends. At medium density it saturates at a
single spurious cell (precision floors at 0.9938 and never falls further),
but at high density (100 movers/window) false statics keep accumulating
window over window — from 162 predicted cells at $w{=}17$ (grid2d) /
$w{=}25$ (voxel3d) to 174 (grid2d) / 165 (voxel3d) by $w{=}39$ — giving the
only material gap between backends:
precision 0.9253 (grid2d) vs. 0.9758 (voxel3d) at the final window. Both
backends run the identical `LayeredMap` graduation rule, each on its own
independently seeded native mover stream — `Grid2DBackend` drawing movers on
the integer cell lattice, `Voxel3DBackend` drawing them continuously, both
confined to a single $z$-plane here — so the small precision gap reflects
native quantization and sampling differences rather than the classifier. We
do not claim it isolates a single geometric mechanism.

With periodicity enabled the run is identical window for window: the wall is
occupied whenever it is touched, so its centred amplitude is 0 and it explains
no variance ($d_k=0$). The v0.1.0
engine (uncentred amplitude) instead lost the whole wall from the Static layer
in windows 29–40 ($t=28$–39) in all six runs, ending at Static recall 0
(@tbl:fix).

| measure | periodicity off | on, v0.1.0 | on, centred | on, single | on, spent |
|---|---|---|---|---|---|
| E1: windows $t\ge3$ with Static recall $<1$ | 0 | 12 ($t=28$–39) | 0 | 0 | 0 |
| E1: Static recall at $t=39$ | 1 | 0 | 1 | 1 | 1 |
| E2 at $n=64$: TPR / FPR | — | 2/3 / 1/5 | 2/3 / 2/5 | 2/3 / 0/5 | 2/3 / 0/5 |
| E2, $n=8$–100: mean FPR | — | 0.363 | 0.331 | 0.000 | 0.000 |
| E2, $n=8$–100: FP count range (of 5) | — | 0–4 | 0–4 | 0–0 | 0–0 |
| E2, $n=8$–100: lengths with no FP | — | 10 of 93 | 7 of 93 | 93 of 93 | 93 of 93 |
| E2, $n=8$–100: lengths with constant wall Periodic | — | 4 of 93 | 0 of 93 | 0 of 93 | 0 of 93 |
| E2, $n=8$–100: mean TPR | — | 0.667 | 0.667 | 0.624 | 0.563 |
| E2: TPR 2/3 at every $n\ge$ | — | 8 | 8 | 15 | 25 |
| E2, $n=8$–100: lengths with FP, no pruning | — | — | — | 25 of 93 | 0 of 93 |
| E3: total flicker, 36 configurations | 3672 | 7132 | 5472 | 3792 | 3672 |
| E3: flicker at 0.9 / 0.3 / $\lambda=0.90$ | 54 | 138 | 102 | 58 | 54 |
| E3: flicker at 0.9 / 0.9 / $\lambda=0.90$ | 574 | 582 | 596 | 576 | 574 |

Table: Effect of the three corrections to the periodicity test: same harness,
seeds and parameters run against the v0.1.0 engine
(`results/pre_fix_2026-09-28/`), the centred engine with the amplitude-only
rule (`results/pre_calibration_2026-09-28/`), the calibrated test at the
single-read-out level $\delta=0.1$ (`results/pre_spending_2026-09-28/`) and the
current engine ($\delta=0.2$ spent over the touch count). {#tbl:fix}

## 5.3 E2 — Periodicity detection

**Setup.** Three periodic doors (`p8` 4-on/4-off, `p8` 2-on/6-off, `p4`
2-on/2-off, all with periods commensurate with the FreMEn base period
$T{=}8$ using $H{=}3$ harmonics), a constant wall, and four deterministic
Bernoulli(0.5)
aperiodic movers as negative controls, are driven through the real
`LayeredMap` pipeline for 64 windows, and, in a sweep, read out after every
length $n=8$–100 with a fresh map per length (`results/e2_rates_vs_length.csv`). Reported: per-cell final class against
ground truth (Periodic TPR/FPR), and FreMEn dominant-harmonic amplitude vs.
observation length (`results/e2_classification.csv`,
`results/e2_amplitude_vs_length.csv`, `results/e2_summary.csv`).

| cell | ground truth | final class | ref. amplitude | ref. $H\,B$ | note |
|---|---|---|---|---|---|
| `door_p8_4on4off` (50% duty) | periodic | Periodic | 0.653 | $<10^{-3}$ | correct |
| `door_p4_2on2off` (50% duty) | periodic | Periodic | 0.707 | $<10^{-3}$ | correct |
| `door_p8_2on6off` (25% duty) | periodic | Transient | 0.462 | $<10^{-3}$ | **miss** |
| `wall_constant` | non-periodic | Static | 0 | 1 | correct |
| `aperiodic_0` | non-periodic | Static | 0.159 | 1 | **false Static** (not a Periodic FP) |
| `aperiodic_1` | non-periodic | Unknown | 0.329 | 0.056 | correct |
| `aperiodic_2`, `_3` | non-periodic | Transient | 0.14–0.25 | 1 / 0.585 | correct |

Table: E2 — per-cell classification against ground truth with the shipped
test ($a_{\min}=0.3$, $\delta=0.2$ spent over the touch count) at the 64-window read-out; reference values
come from a non-pruning model fed the identical stream. Periodic TPR $=2/3$,
FPR $=0/5$. {#tbl:e2}

![E2: (left) Periodic-class TPR/FPR bars; (right) FreMEn dominant-harmonic
amplitude vs. observation length for all 8 probe cells, with the
$a_{\min}=0.3$ threshold, the calibrated-test amplitude $a^\star(n)$
($\delta=0.1$ at a single read-out, $H=3$, uniform phase coverage; the shipped
spent level requires more) and the $n\ge T$ validity gate
marked — a cell is Periodic only above both thresholds; the two 50%-duty doors
clear them, the 25%-duty door is pruned before it matures, and the constant
wall stays at exactly 0.](figures/fig_e2_periodicity.pdf){#fig:e2}

**Single-read-out calibration (E0).** The single-read-out level $\delta$ (constant in $n$) is chosen first, on held-out
seeds, by the rule of [@sec:periodicity] (@tbl:e0). The null grid covers iid
Bernoulli($m$) streams at $T/H\in\{8/3,24/2\}$, $m$ from 0.05 to 0.95 and 12
read-out lengths in $8$–100 ($T=8$) or 8 in $24$–240 ($T=24$), with 20,000 streams per point; the pipeline check runs
3000 clutter cells through the live `LayeredMap` with the E2 parameters and
reads them out at every $n=8$–100; the door check runs four doors in 40 runs each at random phase offsets.
The Chernoff bound is conservative at every candidate level. At $\delta=0.1$ the
worst null rate is 0.0050 and the worst pipeline rate 0.007, both within the
0.01 target, whereas $\delta=0.2$ exceeds it (0.0149 and 0.014), so $\delta=0.1$
is selected as the single-read-out level; the shipped rule replaced it (E5). Relaxing $\delta$ from 0.01 to 0.1 shortens the median read-out
length at which a 4-on/4-off door becomes stably Periodic from 22 to 15
windows. The Gaussian-residual approximation of the periodogram, the textbook
false-alarm formula [@zechmeister2009gls], fails the same check: at a level of
0.01 it reaches a null rate of 0.037 for $n$ near $T$.

| $\delta$ | null rate | pipeline rate | door, median first $n$ | selected |
|---|---|---|---|---|
| 0.01 | 0.0004 | 0.001 | 22 | no |
| 0.02 | 0.0007 | 0.002 | 20 | no |
| 0.05 | 0.0022 | 0.003 | 17 | no |
| **0.1** | **0.0050** | **0.007** | **15** | **yes** |
| 0.2 | 0.0149 | 0.014 | 14 | no (rate $>0.01$) |

Table: E0 — calibration of $\delta$ on seeds from 20260928, disjoint from
E1–E4 (`results/e0_calibration_choice.csv`). {#tbl:e0}

**Results.** Both 50%-duty doors are detected (reference amplitudes 0.653 and
0.707, bounds below $10^{-3}$). No aperiodic control is Periodic, so at the
$n=64$ read-out the Periodic class reaches a true-positive rate of 2/3 and a
false-positive rate of 0/5; the two earliest engines scored 1/5 (v0.1.0) and 2/5
(centred only) at this length. This is not a favourable read-out: over all
lengths $n=8$–100 the pipeline has no false positive at any length, as with the
single-read-out level, against a mean false-positive rate of 0.363 (v0.1.0) and
0.331 (centred), with 0–4 of 5 per length in both (@tbl:fix). The gain is paid
for in detection delay. The true-positive rate is 0 for $n=8$–19 and 1/3 at
$n=20$–24, and it reaches 2/3 at $n=25$; the single-read-out level reached it at
$n=15$ and the two earliest engines from $n=8$. The mean true-positive rate over
the sweep falls from 0.667 to 0.624 (single read-out) and 0.563 (spent).

Two observations qualify the zero. First, it is no longer owed to pruning. With
the single-read-out level, a non-pruning reference model fed the same streams
and read with the same predicate called `aperiodic_1` Periodic at 25 of 93
lengths ($n=29$–31, 45–50 and 52–67), a mean false-positive rate of 0.054: at
$n=64$ this cell's amplitude is 0.329 and its bound $H\,B=0.056<0.1$, a genuine
tail event of this seed, roughly 3.7 null standard deviations. With the spent
level the same reference model has no false positive at any length, because
$\delta_{64}=0.2\cdot8/(64\cdot65)\approx3.8\times10^{-4}$. Second, `aperiodic_0`
ends Static. With $\lambda=1$ its log-odds is a random walk that reached
$p_{\text{grad}}$, and it was previously masked by a spurious Periodic label
(centred engine) that demoted it. E2 does not score the Static class; E5 does,
over 20 scenes, and finds this failure common (@tbl:e5conf).

The one miss, the 25%-duty door (`door_p8_2on6off`), is a **prune/maturity
race**, not a failure of the test: the real pipeline erases the cell within at
most 7 touched windows of each re-creation, before the $n\ge T$ gate is
reached, and each erasure discards its accumulators. The reference model
detects it at every length from $n=57$ with the spent level (from $n=26$ at a
single read-out), with amplitude 0.462 at $n=64$; the miss is
specifically an interaction between pruning and the touched-window amplitude
gate. The amplitude-vs-length data also confirms the gate: for
`door_p8_4on4off`, amplitude is exactly 0 at observation length 6
($n<T{=}8$), 0.653 at every sampled multiple of $T$, and 0.581 at length 12,
where the uncentred v0.1.0 amplitude had overshot to 0.871.

## 5.4 E3 — Sensitivity: hysteresis band and decay

**Setup.** 50 ground-truth wall cells are observed occupied with probability
0.6 each window (noisy) alongside 10 fresh movers/window, over 80 windows,
with the identical noise sequence replayed for every configuration in the
sweep. The sweep spans `graduate_prob` $\in\{0.6,0.7,0.8,0.9\}$
$\times$ `demote_prob` (a low floor 0.3, a mid floor 0.5, and the degenerate
`demote_prob == graduate_prob`, i.e. zero hysteresis band)
$\times$ `survival_decay` $\in\{0.90,0.97,1.0\}$ — 36 configurations total.
Reported: final Static-layer F1 and the count of per-window `isStatic`
flicker transitions (`results/e3_sensitivity.csv`).

| `graduate_prob` | `demote_prob` | hysteresis band | `survival_decay` | flicker | final F1 |
|---|---|---|---|---|---|
| 0.9 | 0.9 (degenerate) | 0 | 0.90 | **574** | **0.810** |
| 0.8 | 0.8 (degenerate) | 0 | 0.90 | 343 | 0.947 |
| 0.7 | 0.7 (degenerate) | 0 | 0.90 | 250 | 0.980 |
| 0.9 | 0.9 (degenerate) | 0 | 0.97 | 150 | 1.000 |
| 0.9 | 0.9 (degenerate) | 0 | 1.00 | 122 | 1.000 |
| 0.9 | 0.3 | 0.6 | 0.90 | 54 | 1.000 |
| 0.9 | 0.3 | 0.6 | 0.97 | 52 | 1.000 |
| 0.9 | 0.3 | 0.6 | 1.00 | **52** | **1.000** |

Table: E3 — selected sweep configurations (full 36-row sweep in the CSV);
identical replayed noise throughout. {#tbl:e3}

![E3: three heatmap panels (one per `survival_decay`); rows =
`graduate_prob`, columns = hysteresis-band width, cells colored by
flicker-transition count (log scale) and annotated with the raw count and
final F1 — the degenerate (zero-band) column drives both the worst flicker
and the worst F1 at every decay
setting.](figures/fig_e3_sensitivity.pdf){#fig:e3}

Removing hysteresis is the single largest effect in the sweep: at
`graduate_prob = demote_prob = 0.9`, `survival_decay = 0.90` (the worst
degenerate configuration), the wall layer flickers 574 times and loses
19% of its final F1 (0.810, recall collapses to 0.68), while a
same-noise, same-decay run with the hysteresis band widened to 0.6
(`demote_prob = 0.3`) flickers only 54 times at F1 1.000 — the band alone
accounts for a $>10\times$ reduction in flicker. The degenerate
(zero-band) configurations are also internally monotonic in threshold: at
`survival_decay = 0.90`, flicker rises with the coincident threshold value
(250 at 0.7, 343 at 0.8, 574 at 0.9) even though a higher threshold is
ordinarily the more conservative, more "static" setting — a threshold pair
with no gap between graduate and demote is not a substitute for a
hysteresis band, regardless of where that pair sits.
`survival_decay` is the second-order effect: holding the worst degenerate
threshold pair (0.9/0.9) fixed, flicker falls monotonically as forgetting
slows — 574 at $\lambda{=}0.90$, 150 at $\lambda{=}0.97$, 122 at
$\lambda{=}1.0$ — because faster decay lets noisy log-odds cross the
(here, coincident) threshold more often. With a wide hysteresis band,
`survival_decay` barely matters (52–54 flicker across all three decay
settings at the best band): hysteresis, not decay, is what keeps the
static layer stable.

**Periodicity on.** Rerunning the sweep with periodicity enabled at the
shipped defaults (`results/e3_sensitivity_periodic.csv`) leaves final F1
unchanged in all 36 configurations, and flicker is now identical to that with periodicity off
in every configuration: 3672 in total in both cases, against 3792 with the
single-read-out level, 5472 with the centred amplitude-only rule and 7132 with
v0.1.0, and 54 at the widest band and $\lambda=0.90$ (single read-out: 58,
centred: 102, v0.1.0: 138). These walls are free in 40% of their windows, so
their centred amplitude is sampling noise; under the amplitude-only rule it
crossed $a_{\min}$ often and the unbanded `|| periodic` demotion toggled the
wall out of the Static layer. With the single-read-out level, 1 or 2 such
demotions remained per configuration, because each wall was re-tested in every
window. With the spent level a graduated wall has one history, and by the
trajectory-level bound ([@sec:spend]) the probability that it is ever demoted by
the periodic predicate is at most $\delta$; none is demoted in any
configuration.

## 5.5 E5 — Trajectory-level false alarms and four-class confusion {#sec:e5}

**What a deployed map does.** E0 and E2 read a cell once per run. The map
labels every cell after every window, so the event that matters for a deployed
map is that a non-periodic cell is labelled Periodic at *some* window of the
run. E5 measures this event directly on held-out seeds, for the live detector
with pruning, re-creation and re-testing, over horizons of 64, 256 and 1024
windows (@tbl:e5; `results/e5_trajectory.csv`).

| $\delta$ | worst ever-Periodic (of 2000) | Wilson upper | door, $T=8$ / $T=24$ | selected |
|---|---|---|---|---|
| 0.01 | 0 | 0.0019 | 34 / 33 | no |
| 0.02 | 0 | 0.0019 | 32 / 30 | no |
| 0.05 | 0 | 0.0019 | 30 / 26 | no |
| 0.1 | 1 | 0.0028 | 27 / 24 | no |
| **0.2** | **2** | **0.0036** | **25 / 24** | **yes** |

Table: E5 — choice of the spent level $\delta$ on calibration seeds (from
30260928) disjoint from every evaluation seed. Worst ever-Periodic: over 36
configurations (E2 and shipped parameters, touched every window or with
probability 0.5, Bernoulli($m$) clutter, $m=0.1,\dots,0.9$, 2000 cells, 1024
windows), the largest count of cells labelled Periodic at some window, with the
Wilson 95% upper limit of that rate. Door: median first window from which a
4-on/4-off door (E2 parameters) or a 12-on/12-off door (shipped) stays
Periodic. Rule, fixed before the run: the largest $\delta$ whose upper limit is
$\le 0.01$ in every configuration (`results/e5_calibration_choice.csv`). {#tbl:e5choice}

| parameters, touches | rule | $N=64$ | $N=256$ | $N=1024$ | 95% interval, $N=1024$ | per window | Markov, $N=1024$ |
|---|---|---|---|---|---|---|---|
| E2, every window | single | 84 | 280 | 953 ($m=0.4$) | [0.4547, 0.4984] | 0.0045 | 349 |
| | spent | 0 | 1 | 2 ($m=0.4$) | [0.0003, 0.0036] | $3.9\times10^{-6}$ | 2 |
| E2, $q=0.5$ | single | 20 | 127 | 543 ($m=0.4$) | [0.2525, 0.2914] | 0.0047 | 79 |
| | spent | 0 | 0 | 2 ($m=0.4$) | [0.0003, 0.0036] | $3.9\times10^{-6}$ | 0 |
| shipped, every window | single | 64 | 66 | 202 ($m=0.3$) | [0.0885, 0.1150] | 0.0008 | 1998 |
| | spent | 2 | 2 | 2 ($m=0.4$) | [0.0003, 0.0036] | $1.0\times10^{-6}$ | 1908 |
| shipped, $q=0.5$ | single | 18 | 52 | 102 ($m=0.3$) | [0.0422, 0.0615] | 0.0009 | 1331 |
| | spent | 1 | 1 | 1 ($m=0.3$) | [0.0001, 0.0028] | $3.4\times10^{-6}$ | 409 |

Table: E5 — trajectory-level false alarms of the live detector on held-out
seeds (from 40260928). Each entry is the worst Bernoulli($m$) clutter
configuration over $m=0.1,\dots,0.9$: the number of 2000 cells (10 seeds of 200)
labelled Periodic at some window up to $N$, with its Wilson 95% interval for
$N=1024$, and the worst per-window labelling rate. $q$: probability that a cell
is touched in a window, independent of its occupancy. Single: $\delta=0.1$ at
every read-out; spent: $\delta=0.2$ spent over the touch count. Markov:
temporally correlated clutter, outside the null of the calibrated bound.
Constant cells were never labelled Periodic. {#tbl:e5}

**The single-read-out level does not control it.** With $\delta=0.1$ applied at
every read-out, up to 84 of 2000 Bernoulli clutter cells are labelled Periodic
within 64 windows and 953 of 2000 within 1024 (E2 parameters, $m=0.4$, 95%
interval [0.4547, 0.4984]). The per-window labelling rate stays small, at most
0.0047, but it accumulates over the run. The shipped parameters are affected
less, because their slower log-odds walk prunes Bernoulli clutter less often,
yet 202 of 2000 cells still flip at some window ($m=0.3$).

**The spent level does.** With $\delta=0.2$ spent over the touch count, chosen on
the calibration seeds (@tbl:e5choice), at most 2 of 2000 Bernoulli clutter cells
are ever labelled Periodic within 1024 windows in every held-out configuration,
a Wilson 95% upper limit of 0.0036, and the per-window rate is at most
$3.9\times10^{-6}$. Always-occupied and always-free cells are never labelled
Periodic under either rule. The proved bound of [@sec:spend] covers the
never-pruned case at 0.2. For pruned clutter the proved bound
$\delta\,\mathbb E[L_N]$ is vacuous (a Bernoulli(0.4) cell under the E2
parameters begins about 62 histories), and the measured rate sits far below it:
the Chernoff bound is conservative, and successive read-outs of one history are
strongly correlated, so the union bound over them is loose.

**Correlated clutter is flagged.** The Markov cells violate the independence the
bound assumes: their occupancy has real power at low frequencies. Under the
shipped $T=24$ the spent test labels 1908 of 2000 of them Periodic within 1024
windows (1998 at a single read-out); at $T=8$ it labels 2. The periodogram test
cannot tell a slow random process from a period of 24 windows; this is a
limitation of the null, not a failure of the bound.

**What it costs.** On held-out seeds the median window from which a noiseless
4-on/4-off door stays Periodic moves from 15 to 25, a 2-on/2-off door from 13 to
20, a 3-on/5-off door from 21 to 31, and a 4-on/4-off door with 10% flipped
windows from 26 to 48; at the shipped $T=24$ a 12-on/12-off door stays at 24
windows, and with 10% flips it moves from 25 to 40. No door run fails to become
Periodic (`results/e5_doors.csv`).

| ground truth (cells) | Static | Periodic | Transient | Unknown | single P | ever P, $N=256$ (single / spent) |
|---|---|---|---|---|---|---|
| wall (1000) | 1000 | 0 | 0 | 0 | 0 | 0 / 0 |
| door (1200) | 0 | 800 | 400 | 0 | 800 | 800 / 800 |
| aperiodic (2000) | 1221 | 0 | 672 | 107 | 12 | 219 / 1 |
| dynamic (1000) | 1 | 0 | 705 | 294 | 0 | 1 / 0 |

Table: E5 — four-class confusion on 20 held-out E2-style scenes (E2 parameters,
touched every window), read out after 64 windows with the shipped test. Per
scene: 50 walls, 20 doors of each E2 type at random phase, 100 aperiodic
Bernoulli(0.5) cells, and 50 dynamic cells crossed by a mover with probability
0.1 per window. Single P: Periodic count under the single-read-out rule.
Precision / recall with Wilson 95% intervals: Static 0.450 [0.429, 0.471] /
1.000 [0.996, 1.000]; Periodic 1.000 [0.995, 1.000] / 0.667 [0.640, 0.693];
not persistent (Transient or Unknown) 0.816 [0.800, 0.832] / 0.593 [0.575,
0.610] (`results/e5_confusion.csv`). {#tbl:e5conf}

**Four-class confusion.** @tbl:e5conf scores all four labels over 20 held-out
E2-style scenes. Walls are Static and dynamic cells Transient or Unknown, with
one exception in 1000. The Periodic label has precision 1.000 [0.995, 1.000]
and recall 0.667 [0.640, 0.693]; the 400 misses are exactly the 400
25%-duty doors, which the prune/maturity race proves are never Periodic. Within
256 windows the single-read-out level labels 219 of 2000 aperiodic cells
Periodic at some window, the spent level 1. The dominant error is elsewhere:
1221 of 2000 aperiodic Bernoulli(0.5) cells end falsely Static, so Static
precision is only 0.450 [0.429, 0.471]. Under the E2 parameters ($\lambda=1$,
unit increments) the log-odds of such a cell is a random walk reflected at
$l_{\max}=5$; it often reaches $p_{\text{grad}}=0.9$, and a Static cell is
demoted only when the walk falls back to $p_{\text{dem}}=0.4$. The periodicity
test is not what decides this; the graduation band is, and E2's parameters
without decay are the adversarial case for it.

## 5.6 E4 — Throughput and memory

**Setup.** 150 random hits per window over 20 windows, for `Grid2DBackend`
vs. `Voxel3DBackend` at three nominal extents (100/250/500, resolution/voxel
size 1.0), periodicity disabled. Reported: mean per-`integrate()` call
latency, mean per-`endWindow()` cell-update rate, final live-cell count, and
estimated memory at the fixed 56 B/cell footprint documented in §4
(`CellEvidence` 24 B + $\approx$32 B `unordered_map` node overhead, with
FreMEn storage excluded since periodicity is off in this run)
(`results/e4_throughput.csv`).

| backend | extent | µs/`integrate()` | endWindow updates/s | live cells | est. memory |
|---|---|---|---|---|---|
| grid2d | $100^2$ | 320.8 | $9.41\times10^{6}$ | 8,553 | 479 KB |
| voxel3d | $100^2$ | 4,796.8 | $7.06\times10^{6}$ | 85,399 | 4.78 MB |
| grid2d | $250^2$ | 1,702.6 | $1.07\times10^{7}$ | 45,657 | 2.56 MB |
| voxel3d | $250^2$ | 18,469.5 | $4.46\times10^{6}$ | 295,300 | 16.5 MB |
| grid2d | $500^2$ | 6,548.9 | $8.64\times10^{6}$ | 150,096 | 8.41 MB |
| voxel3d | $500^2$ | 35,958.3 | $4.45\times10^{6}$ | 661,410 | 37.0 MB |

Table: E4 — per-call latency, throughput, live-cell count, and estimated
memory, both backends, all three extents. Absolute µs are single-machine
wall-clock samples; live-cell counts (and the ratios derived from them) are
geometry-determined and reproducible. {#tbl:e4}

![E4: grouped bars by backend $\times$ extent — per-`integrate()` latency in
µs (log scale, left) and `endWindow()` cell-update throughput in
$10^6$ updates/s (right). grid2d is cheaper per call than voxel3d at every
tested extent; the gap tracks voxel3d's larger live-cell
count.](figures/fig_e4_throughput.pdf){#fig:e4}

At every tested extent, `Grid2DBackend`'s `integrate()` call is cheaper than
`Voxel3DBackend`'s at the identical nominal extent and hit rate — by
$4{,}796.8/320.8\approx15.0\times$ at $100^2$, $18{,}469.5/1{,}702.6\approx
10.8\times$ at $250^2$, and $35{,}958.3/6{,}548.9\approx5.5\times$ at
$500^2$, the multiple shrinking as extent grows. The live-cell counts
explain why: `Voxel3DBackend` ends each run holding
$85{,}399/8{,}553\approx10.0\times$, $295{,}300/45{,}657\approx6.5\times$,
and $661{,}410/150{,}096\approx4.4\times$ as many cells as `Grid2DBackend`
at the same extent, because its free-space ray-sample march (§4.6) walks a
full 3D volume and spawns far more traversed voxels than `Grid2DBackend`'s
exact 2D Bresenham clear over the same nominal footprint — the same
geometric asymmetry that drives the latency gap also drives the cell-count
gap, and both narrow together as the extent grows and the ray paths (and
hence the per-ray voxel counts) become a smaller fraction of the total
volume. Memory is not measured: the harness multiplies the live-cell count by
a fixed 56 B/cell (evidence and hash node with periodicity off; table and
backend overhead excluded), so the estimate tracks live-cell count by
construction, topping out at 37.0 MB for the largest 3D
configuration tested (661,410 cells $\times$ 56 B) — inexpensive even at
the largest map in this sweep. Absolute microsecond values here are
single-run wall-clock measurements on one machine and are not claimed to be
portable; the live-cell counts, and the ratios and memory figures derived
from them, follow only from ray/hash geometry and reproduce under the fixed
seed regardless of machine.

## 5.7 Synthesis

Across E1–E4, the two backends behave as one engine wearing two geometries,
exactly as C1 asserts by construction: E1's near-identical F1 trajectories
(both backends graduate the wall by the third window and hold recall 1.0
through $w{=}39$) and E4's live-cell counts are the
empirical face of "no per-dimension logic or cost," since `LayeredMap` and
`PeriodicityModel` — exercised directly and identically in E2 and E3 — never
see which backend produced the `CellId` they are updating. The two places
where the backends *do* diverge trace to native geometry rather than to the
shared classifier — which E2 and E3 exercise directly, backend-free, so it is
provably identical for both backends:
E1's high-clutter precision gap (0.9253 vs. 0.9758) reflects the backends'
different native mover sampling (grid2d on the integer cell lattice, voxel3d
continuously, both at a single $z$-plane in E1) rather than a demonstrated
$z$-separation mechanism, and E4's per-call latency gap
(5.5–15.0$\times$, narrowing with extent) follows from `Voxel3DBackend`'s
free-space ray-sample march spawning 4.4–10.0$\times$ more live voxels than
`Grid2DBackend`'s exact line clear at the same nominal extent. E2 and E3,
run once against `LayeredMap` with no backend involved at all, characterize
the shared engine's own behavior — 50%-duty periodicity detection from 25
windows on with no false positive on clutter once the test is calibrated and
its level spent (E5 extends this to whole runs: at most 2 of 2000 held-out
clutter cells are ever labelled Periodic within 1024 windows), a
diagnosed prune/maturity-race failure mode at low duty cycle, and
hysteresis as the dominant flicker suppressor, decay a secondary one — and
that characterization is, by the same construction argument, valid for
whichever backend feeds it cell ids in production.

# 6 Discussion and Limitations

## 6.1 What has and has not been shown

The evaluation in §5 characterizes a shipped engine; it does not validate a
deployed system. Every number in this paper — E1's precision/recall curves,
E2's TPR/FPR, E3's flicker counts, E4's throughput and memory — comes from a
deterministic, seeded synthetic harness compiled directly against
`strata_core`, driving hand-authored hit/miss patterns through the real
`LayeredMap`/`PeriodicityModel` code. No real sensor, no real robot motion,
and no field deployment enters any of these results. This is a statement of
present scope, not a hidden gap: ELite [@gil2025elite], LT-mapper
[@kim2022ltmapper], the long field deployment of Berrio et al.
[@berrio2021longterm] and the FreMEn grid of Krajník et al.
[@krajnik2016persistent] all report long-term real-world evidence that STRATA
does not yet have. No external baseline is run, not even a FreMEn model driven
by the same streams, so this paper does not show that the calibrated test
labels periodic cells better than FreMEn's amplitude-ranked component
selection, or that the classifier is useful on real sensor data. The evidence
establishes a transparent state machine on synthetic streams, not a reliable
four-class scene classifier: many aperiodic cells end Static under the E2
parameters, low-duty doors are lost to pruning, and at the shipped period
correlated clutter is labelled Periodic (below). STRATA is, so far, validated
the way a library is validated — by unit and characterization tests against
known ground truth — not the way a robot behavior is validated, by miles
driven.

A second, narrower scope limit sits inside the "lifelong" framing itself.
STRATA implements single-session temporal persistence: a cell's class is a
running function of its own log-odds, observation count, and Fourier
accumulators, updated window by window within one continuous mapping run.
There is no session boundary, no re-localization into a prior map, and no
cross-session registration — a graduated Static cell simply *is* the current
map, with its history implicit in accumulator state rather than explicitly
stored. This is a strictly smaller claim than LT-mapper's live/meta/delta
session management or Yang et al.'s versioned base-map-plus-deltas
[@yang2025lifelong3d], both of which let a robot query or reconstruct a past
session. STRATA cannot do either; it only ever has "now."

Finally, the paper does not claim bit-identical cross-backend behavior.
Section 5.7 already frames grid2d and voxel3d as *equivalent to within
geometry- and native-input-induced differences* rather than identical, and
E1's clutter-driven precision gap (0.9253 vs 0.9758 at 100 movers/window) and
E4's 5.5–15.0× per-`integrate()` cost gap are exactly those differences,
measured rather than glossed over.

Periodicity detection is the least mature part. The first release shipped an
uncentred Fourier amplitude that could demote constant walls, and an
uncalibrated amplitude threshold that labelled Bernoulli clutter Periodic at up
to 4 of 5 cells per read-out length ([@sec:periodicity]). Both are fixed and
covered by tests, but the test has limits of its own. What is proved (the
calibrated bound and its trajectory-level extension, [@sec:spend]) is a
per-history bound under an iid null and occupancy-independent touches: at the
shipped level, a cell that is never pruned is labelled Periodic at some window
of the run with probability at most 0.2. The 0.01 target is a measurement: at
most 2 of 2000 clutter cells within 1024 windows on held-out seeds (E5), and for
pruned clutter the proved bound grows with the number of re-created histories.
Temporally correlated clutter violates the null, and at the shipped $T=24$ the
test labels most slowly switching cells Periodic (1908 of 2000 in E5). Spending
costs detection delay: a noiseless 50%-duty door with $T=8$ needs 25 windows
instead of 15, and one with 10% flipped windows 48 instead of 26. Finally, the
four-class confusion shows that the dominant labelling error under the E2
parameters is not periodic at all: 1221 of 2000 aperiodic cells end falsely
Static, which is a property of the graduation band without decay. E1–E4 are
single-seed; only E5 is multi-seed.

## 6.2 The out-of-FOV forgetting caveat is an operational limitation, not a footnote

§4.8 documents SPEC-DIFF #1 as an implementation fact: decay is applied only
inside the `touched` branch of `endWindow()`, so a cell that leaves the
sensor's field of view stops decaying entirely — its log-odds value freezes
at whatever it last reached, rather than relaxing toward the unknown prior.
Restated as an operational consequence: a transient object that is observed
long enough to accumulate strong occupied evidence, and then permanently
exits the sensor's coverage (a box dragged out of a corridor the robot no
longer revisits, a door propped open just outside the LiDAR's usual sweep),
retains its evidence indefinitely. STRATA's forgetting is coupled to
re-observation, not to elapsed time. In a bounded, frequently re-swept
environment — the setting E1–E4 implicitly assume, and the setting the
canonical `Integration.WallStaticMoverTransientDoorPeriodic` scenario exercises
— this rarely matters, because clutter that matters is clutter the robot
keeps driving past. In a sparsely-revisited environment it is a real failure
mode the current implementation does not address, and it is one of the
concrete items in the future-work list below rather than an incidental
detail.

## 6.3 Design trade-offs owned honestly

STRATA makes three simplifications relative to richer prior formalisms,
each adopted deliberately and each with a known cost.

**A global scalar decay rate, not a per-cell-learned one.** `survival_decay`
is a single tunable constant shared by every cell, applied as in §4.2. The
Persistence Filter's survival-time posterior [@rosen2016persistence] and the
Markov-chain occupancy models of Tipaldi et al. and Saarinen et al.
[@tipaldi2013lifelong; @saarinen2012imac] instead let each cell (or region)
learn its own forgetting rate from observed transition statistics — a wall
in a rarely-disturbed alcove and a doormat in a busy doorway would decay at
different, empirically fit rates. STRATA's one-parameter simplification is
what E3 characterizes: a single `survival_decay` value, uniformly applied,
already buys most of the achievable stability once paired with hysteresis
(F1 1.000 for the wide-band configuration at both `decay=0.90` and
`decay=1.0`), so the paper does not claim the uniform rate is a limitation
in the tested regime — only that it is a strictly less expressive model
than the per-cell-learned alternative, and a scene with sharply
heterogeneous dynamics per cell is untested.

**A flat voxel hash, not an octree.** `Voxel3DBackend` stores live voxels in
an `unordered_map<CellId, CellEvidence>` keyed by a packed `int64`, with no
multi-resolution compression. OctoMap [@hornung2013octomap] and UFOMap
[@duberg2020ufomap] instead exploit spatial coherence — large free or
occupied regions collapse into single octree nodes — trading a more complex
tree structure for asymptotically better memory scaling on sparse or
large-extent maps. E4 measures the cost of this choice directly: voxel3d
holds 4.4–10.0× the live-cell count of grid2d at matched scene extent because
every sampled free-space point along a ray becomes its own hash entry, and
the estimated per-cell footprint is a flat 56 B with periodicity off,
regardless of spatial redundancy. The trade
is deliberate — O(1) insert/lookup with no tree-rebalancing logic keeps the
backend simple enough to unit-test the way §3 describes — but it means
STRATA's voxel3d backend will not scale as gracefully as an octree-backed
map to very large, sparsely-occupied 3D extents.

**Two backends behind an `if`/`else`, not a plugin registry.** §3 argues,
and the Related Work rejection of pluginlib [@ros2pluginlib] restates, that
a compile-time `MapBackend` interface with two concrete `unique_ptr` members
selected by one runtime string parameter is the right amount of mechanism
for exactly two backends. This is a design bet, not a measured result: it
costs nothing today, but it does not generalize — a third backend (e.g. a
signed-distance or hybrid representation) would need either a third branch
bolted onto the same `if`/`else` (tolerable once, ugly repeated) or a
migration to `pluginlib`-style dynamic loading. The paper takes no position
on which is correct beyond two backends; it only argues the current
structure is not premature generalization at the current backend count.

**Honesty has a cost the paper pays openly.** Choosing the systems/tool
framing over an accuracy-SOTA framing means every claim in §5 is scoped to
"what the shipped code measurably does," not "how well STRATA localizes or
maps relative to a competing system." No baseline comparison against
OctoMap, ELite, LT-mapper, or FreMEn is run in this paper. The lifelong-SLAM
systems do not expose a persistence core that this harness could drive; the
FreMEn per-cell model could be driven by the same streams, but we have not run
it (§5.1). The E1–E4 harness is offered instead as a replayable
protocol (§7) a future study could run against an alternative
implementation, rather than a leaderboard result against one run today.

## 6.4 When not to use STRATA

STRATA's boundaries, stated plainly: it is not a substitute for SLAM — it
performs no pose estimation, scan matching, or loop closure, and it consumes
rather than produces the `map → sensor` transform, so a deployment without an
external localizer (`prism_loc` or otherwise already publishing that TF) has
no path to a usable map. It is not a substitute for multi-session mapping —
a robot that needs to compare Monday's map against Friday's, or replay a
specific past session, needs LT-mapper- or Yang et al.-style session
versioning [@kim2022ltmapper; @yang2025lifelong3d] that STRATA does not
provide. And it is not a substitute for semantic or object-level reasoning —
the cell-class state machine of §4.5 has no notion of "this cluster of cells
is one dynamic agent," unlike Khronos's scene graph [@schmid2024khronos] or
ERASOR's per-object removal [@lim2021erasor]. And it does not discover
periods: where the relevant cycles are unknown or are not harmonics of one base
period, FreMEn's selection among candidate periods [@krajnik2017fremen] or
warped hypertime [@krajnik2019warped] is the better tool. Within those
boundaries — a
single continuous mapping session behind an external localizer, at the raw
occupancy-cell level, in 2D or 3D — STRATA is the intended fit.

## 6.5 Future work

Five items follow directly from the limitations above, in the order they
would be tackled: (0) a noise null that allows temporal correlation, for example by testing
the harmonic against a local noise floor rather than against white noise, and
an anytime-valid test that also covers re-created histories, for which
time-uniform supermartingale bounds [@howard2020timeuniform] are the natural
tool, addressing the independence assumption of the calibrated test and the
pruned-clutter regime that E5 measures but the trajectory bound does not cover;
(1) a FreMEn baseline on the same synthetic streams and a four-class confusion
under the shipped defaults, then validation on at least one real 2D and one
real 3D LiDAR sequence, and real-robot and multi-session field validation,
closing the synthetic-only gap of §6.1, on both a 2D wheeled platform and a
3D-LiDAR platform to exercise both backends under real sensor noise; (2) a
per-cell-learned decay rate along the lines of iMac/Tipaldi
[@saarinen2012imac; @tipaldi2013lifelong], replacing the global
`survival_decay` scalar characterized in §6.3 with region- or cell-adaptive
forgetting; (3) resolving the prune/maturity race identified in E2 — the
25%-duty periodic door is pruned during its own vacant stretches before its
FreMEn amplitude has enough touched-window support to mature (§5.3) — by
either exempting recently-active cells from pruning or feeding a running
amplitude estimate into the prune decision itself; and (4) decoupling
forgetting from re-observation, i.e. fixing SPEC-DIFF #1 (§6.2) so an
untouched cell's log-odds relaxes toward the unknown prior on a wall-clock
or tick basis rather than only ever decaying when re-touched, closing the
out-of-FOV persistence gap without changing the touched-cell update path E1–E4
already characterize.

# 7 Conclusion

STRATA packages known ingredients of lifelong occupancy mapping — log-odds
accumulation, Persistence-Filter-style survival decay, Removert-motivated
Schmitt-trigger hysteresis, ReFusion-style negative evidence from free-space
ray clearing, and a FreMEn-lite periodicity test at one configured period,
with a significance bound obtained from standard concentration inequalities —
into one geometry-free `LayeredMap`/`PeriodicityModel` engine
that drives two pluggable geometries, a 2D occupancy grid and a 3D voxel
hash, behind a single `MapBackend` interface and a one-parameter runtime
switch. The only code that differs per backend is point-to-cell-id mapping
and the free-space ray walk (§3–§4); the classifier that decides Static,
Periodic, Transient, or Unknown is written once and shared by composition.
That engine builds and unit-tests with a plain C++17 + Eigen toolchain,
independent of ROS 2, DDS, or PCL (§3), and its behavior is characterized —
not merely asserted — by a seeded, reproducible synthetic harness: near-
identical static-layer quality across backends up to a clutter-induced,
geometrically-explained precision gap (E1), detection of 50%-duty
periodicity from 25 windows on, with the probability of ever labelling iid
clutter Periodic during an unpruned history bounded by the spent level and
measured at no more than 2 of 2000 cells over 1024 windows on held-out seeds,
and a diagnosed failure mode at low duty cycles (E0, E2, E5), walls that stay Static with periodicity on once the Fourier
coefficients are centred (E1, E3),
hysteresis as the dominant stabilizer against flicker (E3), and a flat
estimated per-cell memory footprint with periodicity off, with a 5.5–15.0×
backend cost gap attributable to 3D free-space voxel proliferation, not to the
shared engine (E4). Its closest prior work, the FreMEn spatio-temporal grid of
Krajník et al. [@krajnik2016persistent], has long-term real-world evidence that
STRATA lacks; a FreMEn baseline on the same streams and real LiDAR sequences
are the next evaluation steps. None of this replaces field validation, a
comparison with an external baseline, multi-session mapping, period discovery,
or semantic reasoning — §6 states those boundaries explicitly — but within them (synthetic
evidence, no external baseline, a single session, a fixed base period, an
external localizer), STRATA is offered as a small, dependency-light module other systems can sit on top
of rather than a competing full lifelong-SLAM pipeline.

## 7.1 Reproducibility

STRATA is open source under the Apache-2.0 license at
`github.com/kjungmo/strata`, targeting ROS 2 Humble. The `strata_core`
package alone builds and unit-tests without ROS installed
(`cmake -S strata_core -B build -DSTRATA_CORE_BUILD_TESTS=ON && ctest`, §3);
the continuous-integration workflow builds and tests this core with a C++17
toolchain (g++, Eigen3) on `ubuntu-latest`. All figures and tables in §5
regenerate from the CSVs the harness itself
writes: `bash paper/experiments/run_all.sh` configures and builds the
calibration E0, the four E1–E4 executables and E5 against `strata_core`, runs
them (E0 with its own seeds from 20260928, E1–E4 with the fixed harness seed
`kSeed = 12345`, E5 with calibration seeds from 30260928 and evaluation seeds
from 40260928), and writes `paper/experiments/results/*.csv`; a
follow-up `paper/experiments/plot/run_all_plots.sh` regenerates every figure
from those CSVs. The comparison columns of [@tbl:fix] are archived in
`results/pre_fix_2026-09-28/`, `results/pre_calibration_2026-09-28/` and
`results/pre_spending_2026-09-28/`. To reproduce or audit the parameterization, the engine
parameters and production defaults are in [@tbl:params] (§4.7) and the
per-experiment deviations from those defaults are in [@tbl:eparams] (§5.1).
`strata_core`'s own `ctest` suite (1/1) is checked before
the harness output is trusted. We invite integration reports, alternative
backend implementations replayed against the
`Integration.WallStaticMoverTransientDoorPeriodic` reference scenario,
and pairing with an external localizer such as `prism_loc` for end-to-end
deployment.
