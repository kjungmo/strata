# Related Work — strata

Organized by theme. Each entry: claim (1-2 sentences) + exact relation to
strata's design (SPEC.md §1, §3). Citation keys match `citations[]` returned
alongside this file.

## (a) Lifelong / persistent mapping systems

- **ELite** (Gil et al., ICRA 2025) [`gil2025elite`] — a two-timescale
  *ephemerality* score (within-session = dynamic, across-session = transient),
  Bayesian-updated per point, drives a **Lifelong / Static / Delta** map triad.
  **Closest published twin.** strata's Static/Periodic/Transient/Unknown
  classification (§3.6) is the same "graduate durable structure, keep the rest
  legible" idea, but collapsed into one per-cell state machine inside a single
  ROS-free engine, with no multi-session point-cloud registration, place
  recognition, or object-level delta map — strata is the persistence-and-
  classification core ELite's triad wraps, not a competing full system.
- **LT-mapper** (Kim & Kim, ICRA 2022) [`kim2022ltmapper`] — modular
  live/meta/delta map management (LT-SLAM alignment, LT-removert removal,
  LT-map graduation) built on the same static-vs-transient split. **Closest
  twin, architecture-confirmation role.** Validates that a live-accumulation →
  graduate-to-static pipeline is a sound decomposition; strata takes only the
  graduation half and drops the multi-session SLAM/alignment half, trading
  LT-mapper's completeness for a small, unit-testable, SLAM-free module.
- **Khronos** (Schmid et al., MIT SPARK, RSS 2024) [`schmid2024khronos`] —
  factorizes mapping into a fast short-term-dynamics process over an active
  window plus long-term static/semi-static reconstruction, with semantics and
  a scene graph. **Related architecture, generalization strata deliberately
  does not chase.** Same two-timescale idea as strata's window/decay split,
  but at the object/scene-graph level; strata stays at the raw geometric cell
  level with no semantic layer — a narrower, cheaper instance of the same
  principle.
- **Biber & Duckett** (RSS 2005 / IJRR 2009) [`biber2005dynamicmaps`] —
  represents the environment at multiple timescales simultaneously, samples
  fading at different rates (the stability-plasticity dilemma). **Foundation.**
  Direct ancestor of strata's coexistence of a fast-fading log-odds signal, a
  hysteresis-gated Static class, and a separately-timed Periodic class — three
  timescales in one engine rather than one map at one rate.
- **Meyer-Delius et al.** (IROS 2010) [`meyerdelius2010temporary`] — a static
  reference map for localization plus short-lived temporary local maps
  absorbing movable-object observations. **Foundation.** The earliest clean
  static/temporary split; strata's `classify()` (Static / Periodic / Transient
  / Unknown, §3.6) is a finer-grained descendant of exactly this two-way
  split, adding a periodic third class FreMEn motivates (theme c).
- **RTAB-Map** (Labbé & Michaud, J. Field Robotics 2019) [`labbe2019rtabmap`]
  — one framework spanning 2D, 3D, and visual SLAM, with STM/WM/LTM memory
  bounding the active map by processing time. **Closest engineering
  precedent for "one module, selectable 2D/3D."** strata narrows this to just
  the mapping/persistence layer (no SLAM, no loop closure, no memory-tiering)
  behind a `MapBackend` interface selected by one string parameter — RTAB-Map
  proves the selectable-geometry idea works in production; strata is what's
  left after removing everything RTAB-Map does that strata explicitly
  delegates to an external localizer.
- **Berrio et al.** (T-IV 2021) [`berrio2021longterm`] — a practitioner
  pipeline continuously purging transient features and promoting stable ones
  into an AV localization map, validated over 18 months. **Production
  baseline.** Field-scale evidence that the purge/promote pattern strata
  implements as a Schmitt trigger (§3.4) survives real deployments; strata has
  no equivalent field validation yet (see novelty assessment).
- **Yang et al.** (Huawei, RA-L 2025) [`yang2025lifelong3d`] — keeps one base
  map plus positive/negative version deltas so any past session can be
  reconstructed and inter-session change queried. **Related lineage, baseline
  for an alternative history model.** strata does not version sessions at
  all — a graduated cell simply IS the current static map, and its history is
  implicit in log-odds/observations, not explicitly reconstructible; simpler,
  but loses Yang et al.'s "replay any past clean session" capability.

## (b) Persistence / forgetting models

- **Persistence Filter** (Rosen, Mason, Leonard, ICRA 2016)
  [`rosen2016persistence`] — a recursive Bayesian estimator of each feature's
  survival probability, replacing ad hoc "seen N times" counters. **Direct
  foundation for strata's decay term.** strata's `log_odds *= survival_decay`
  per window (§3.2–3.3) is a discretized, single-scalar simplification of this
  survival-probability idea: instead of a full survival-time posterior, one
  tunable multiplicative factor pulls belief toward unknown each window.
  Cheaper, less rigorous, directly attributable.
- **Thrun, Burgard, Fox — *Probabilistic Robotics* ch. 9**
  [`thrun2005probabilistic`] — the counting-model / log-odds inverse-sensor-
  model derivation (credited in the literature to Hähnel), with hit/miss
  increments and clamping bounds. **Direct algorithmic foundation.** strata's
  entire per-window occupancy update — `log_odds += l_hit` on a hit-window,
  `+= l_miss` on a miss-window, `clamp(l_min, l_max)` (§3.2) — is this model
  verbatim, with the Persistence-Filter decay and Schmitt-trigger hysteresis
  layered on top.
- **Tipaldi, Meyer-Delius, Burgard** (IJRR 2013) [`tipaldi2013lifelong`] and
  **Saarinen, Andreasson, Lilienthal — iMac** (IROS 2012)
  [`saarinen2012imac`] — each grid cell as a two-state (occupied/free) Markov
  process with recency-weighted / online-learned transition rates. **Foundation,
  and the honest limitation strata accepts.** These give the principled,
  per-cell-learned form of "how fast should a cell forget"; strata's
  `survival_decay` is a single global constant, i.e. a constant-rate special
  case of this Markov formalism, not a per-cell-learned rate. Documented
  simplification, not an omission strata is unaware of.

## (c) Periodicity

- **FreMEn** (Krajník, Fentanes, Santos, Duckett, IEEE T-RO 2017)
  [`krajnik2017fremen`] — models each cell's occupancy as a periodic temporal
  signal via frequency analysis; periodic structure (a door open mornings,
  shut at night) is not noise and a flat decay erases it. **Counter-argument
  strata directly answers.** strata's `PeriodicityModel` (§3.5) runs FreMEn's
  incremental-Fourier idea in parallel with the decay/graduation engine, so an
  oscillating cell is classified **Periodic** rather than being mis-graduated
  to Static or mis-pruned as Transient — the third class exists specifically
  because of this counter-argument.
- **Krajník et al. — Spectral Analysis for Long-Term Robotic Mapping**
  (ICRA 2014) [`krajnik2014spectral`] — the earlier, more general origin of
  the frequency-domain lineage FreMEn (2017) later packaged as a mapping
  method. **Foundation.** Cited alongside FreMEn as the earlier form of the
  same idea strata's `PeriodicityModel` operationalizes with a bounded
  incremental-Fourier approximation (`n_harmonics`, `period_windows`) rather
  than the full spectral-analysis machinery.

## (d) Dynamic-object removal

- **Removert** (Kim & Kim, IROS 2020) [`kim2020removert`] — conservatively
  removes dynamic points, then iteratively reverts wrongly-removed static
  ones, because aggressive transient removal erases real structure.
  **Counter-argument strata directly answers.** This is the exact
  justification for strata's hysteresis band: a graduated (Static) cell only
  demotes when probability falls below `demote_prob < graduate_prob` (§3.4),
  i.e. sustained contradicting evidence across windows, never a single
  contradicting frame — the same failure mode Removert's revert pass patches
  after the fact, strata prevents online by never letting the cell flip in
  the first place.
- **ReFusion** (Palazzolo, Behley et al., PRBonn, IROS 2019)
  [`palazzolo2019refusion`] — a voxel reliably observed empty cannot hold a
  static object; free-space residuals are evidence, not just hits.
  **Foundation for strata's clearing design.** strata's ray-clearing — Bresenham
  for `grid2d`, half-voxel-step sampling for `voxel3d` (§3.7) — applies exactly
  this negative-information logic online: misses accumulate as `l_miss`
  evidence, which is what demotes a graduated cell once an object vacates it.
- **ERASOR** (Lim, Hwang, Myung, RA-L/ICRA 2021) [`lim2021erasor`] — removes
  dynamic points from an already-accumulated 3D map via per-bin pseudo-
  occupancy ratios, offline. **Baseline, alternative approach.** ERASOR
  reasons over a batch scan-ratio comparison after the fact; strata never
  batches — every cell's class is a running online per-window hit/miss +
  decay state, with no scene-level or object-level reasoning. Baseline for
  what strata is not attempting (no object-level, no offline distillation
  pass).

## (e) Production stacks

- **OctoMap** (Hornung, Wurm, Bennewitz, Stachniss, Burgard, Autonomous
  Robots 2013) [`hornung2013octomap`] — probabilistic volumetric mapping with
  explicit free/occupied/unknown, log-odds with clamping, octree compression.
  **Production baseline strata's voxel3d backend departs from.** strata shares
  the log-odds+clamp core but deliberately uses a flat voxel-hash (below)
  instead of an octree — trading OctoMap's multi-resolution compression for
  O(1) insert/lookup and a simpler, more directly testable structure.
- **Nießner, Zollhöfer, Izadi, Stamminger — Voxel Hashing** (ACM TOG 2013)
  [`niessner2013hashing`] — spatial hashing for sparse, dynamically-allocated
  voxel storage with O(1) update. **Direct data-structure foundation** for
  `Voxel3DBackend`'s voxel-hash map.
- **UFOMap** (Duberg & Jensfelt, RA-L 2020) [`duberg2020ufomap`] — an octree
  representing unknown space explicitly alongside free/occupied, faster
  insert/query than OctoMap. **Production baseline, alternative to strata's
  implicit approach.** UFOMap gives "never observed" a first-class per-voxel
  state; strata gets the same distinction implicitly through the
  `observations` counter plus log-odds rather than a third explicit state —
  cheaper to implement, less explicit to query.
- **Nav2 STVL** (Macenski, Tsai, Feinberg, IJARS 2020)
  [`macenski2020stvl`] — a sparse 3D voxel costmap layer with explicit voxel
  decay + decay acceleration, the production reference for a decaying layer
  plugged into a ROS 2 stack. **Production baseline, same mechanism family,
  different purpose.** strata's window-based `survival_decay` mirrors STVL's
  decay-rate idea, but strata decays log-odds *mapping* evidence toward
  unknown (§3.3), whereas STVL decays *costmap obstacle* evidence for
  short-horizon planning — same knob, different consumer.
- **Layered Costmaps** (Lu, Hershberger, Smart, IROS 2014 concept, as shipped
  in Nav2 `costmap_2d`) [`lu2014layered`] — static layer + obstacle layer +
  inflation, composited per-cell, the canonical pluggable-layer pattern.
  **Production baseline / mental-model source.** strata's per-cell output
  values (`static→100, periodic→75, transient→50, unknown→-1`, §I/O contract)
  reproduce this same layered mental model, but as one cell-level state
  machine's classification output rather than multiple independently
  composited plugin layers.
- **SLAM Toolbox** (Macenski & Jambrecic, JOSS 2021)
  [`macenski2021slamtoolbox`] — lifelong 2D mapping via adding *and removing*
  pose-graph nodes to bound compute, plus serialization/continued mapping.
  **Baseline, alternative 2D lifelong-mapping approach.** SLAM Toolbox prunes
  at the pose-graph level and performs its own SLAM; strata has no pose graph
  and no SLAM — it consumes an externally-supplied pose and prunes at the
  per-cell evidence level (§3.6) instead. Different layer of the stack
  entirely, included as the standard 2D lifelong-mapping comparison point.

## (f) 2D/3D pluggable-backend architecture prior art

- **ROS 2 Pluginlib** (ROS 2 official docs, Jazzy, 2024)
  [`ros2pluginlib`] — the canonical swappable-backend mechanism: abstract
  base class, no-arg constructor + `initialize()`, `PLUGINLIB_EXPORT_CLASS`,
  XML plugin-description files, runtime `dlopen`-based selection.
  **Counter-pattern strata deliberately rejects, not adopts.** With exactly
  two backends (`grid2d`, `voxel3d`), strata uses a compile-time `MapBackend`
  interface selected by a single runtime string parameter — no plugin XML, no
  dynamic loading, no `ament_index` plugin registry. Simpler and fully
  statically typed for a two-backend module; pluginlib is the right choice
  only once backend count grows past what a single `if/else`-style factory
  can read clearly.
- **RTAB-Map** [`labbe2019rtabmap`] — see theme (a); re-cited here as the
  closest *existing shipped system* offering selectable 2D/3D geometry under
  one framework, which strata narrows to a persistence-only module.

---

## Novelty assessment

**What strata does not claim:**
- It is **not SLAM** — it estimates no pose, performs no scan matching, loop
  closure, or multi-session alignment. It consumes an external `map → sensor`
  transform (from `prism_loc` or any other localizer/odometry chain).
- It has **no real-robot evaluation yet**. All current results are synthetic
  (deterministic core tests: a static wall that must graduate after N layers,
  a moving obstacle that must not). Field-scale validation of the kind ELite,
  LT-mapper, and Berrio et al. report (multi-day, multi-session, real
  hardware) is future work, not a present claim.
- It does not do object-level or semantic reasoning (unlike ERASOR's
  per-object removal, Khronos's scene graph, or Yang et al.'s session
  versioning), no full survival-time posterior (unlike the Persistence
  Filter's Bayesian estimator), and no per-cell-learned transition rate
  (unlike iMac/Tipaldi) — strata substitutes single tunable scalars
  (`survival_decay`, `graduate_prob`, `demote_prob`) for each of these richer
  formalisms.

**What the defensible contribution is:**
one small, geometry-free persistence-and-periodicity engine (`LayeredMap` +
`PeriodicityModel`) that drives two pluggable geometry backends (2D occupancy
grid, 3D voxel-hash) behind a single `MapBackend` interface and a one-parameter
runtime switch — combining, in one open-source, ROS-free, unit-testable core:
(1) Persistence-Filter-style survival decay, (2) Schmitt-trigger graduation
with Removert-motivated hysteresis, (3) ray-clearing as ReFusion-style
negative evidence, and (4) a parallel FreMEn-lite periodicity classifier —
none of which, in this specific minimal combination, exists as a standalone,
SLAM-free, dependency-light module in the surveyed prior art. The prior
systems that share the closest intent (ELite, LT-mapper) are full lifelong-
SLAM pipelines; strata is deliberately just the graduation/decay/periodicity
core those pipelines could sit on top of, validated so far only by
deterministic synthetic tests, not a field deployment.
