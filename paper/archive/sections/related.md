# Related Work

We organize prior work into six themes and, for each entry, state STRATA's
exact relation rather than a generic contrast.

**Lifelong and persistent mapping.** ELite [@gil2025elite] and LT-mapper
[@kim2022ltmapper] are STRATA's closest published twins. ELite computes a
two-timescale ephemerality score per point (within-session dynamic vs.
across-session transient) and drives a Lifelong/Static/Delta map triad;
LT-mapper composes a live map, a meta map, and a delta map through explicit
LT-SLAM alignment, LT-removert removal, and LT-map graduation stages. Both
are full lifelong-SLAM pipelines with multi-session point-cloud registration
and place recognition. STRATA implements only the graduation half of that
pattern — one per-cell state machine (Static/Periodic/Transient/Unknown,
§4.5) inside a ROS-free engine, with no scan matching, no loop closure, and
no cross-session alignment. STRATA is not a competing full system; it is the
persistence-and-classification core that ELite's triad and LT-mapper's
live-to-static pipeline wrap around, and its measured evaluation (§6) is
correspondingly narrower — a synthetic, single-session characterization, not
the field-scale validation ELite and LT-mapper report. Khronos
[@schmid2024khronos] generalizes the same two-timescale idea to an
active-window/long-term-reconstruction split with semantics and a scene
graph; STRATA stays at the raw per-cell level with no object or scene-graph
layer, a narrower and cheaper instance of the same principle. Biber and
Duckett [@biber2005dynamicmaps] and Meyer-Delius et al.
[@meyerdelius2010temporary] are the foundational multi-timescale and
static/temporary-split ideas STRATA's window/decay/hysteresis/periodicity
combination descends from. RTAB-Map [@labbe2019rtabmap] is the closest
engineering precedent for selectable 2D/3D geometry inside one shipped
framework, coupled to full SLAM and STM/WM/LTM memory management; STRATA
narrows this to the mapping/persistence layer alone, behind a `MapBackend`
interface selected by a single string parameter, consuming an externally
supplied pose rather than estimating one. Berrio et al. [@berrio2021longterm]
report an 18-month field deployment of the same purge/promote pattern STRATA
implements as a Schmitt trigger (§4.3) — field-scale evidence for the pattern
that STRATA currently lacks for itself. Yang et al. [@yang2025lifelong3d]
keep positive/negative version deltas over a base map so any past session can
be reconstructed; STRATA does no session versioning at all — a graduated cell
is simply part of the current static map, its history implicit in log-odds
and observation counts, not explicitly replayable.

**Persistence and forgetting.** The Persistence Filter [@rosen2016persistence]
recursively estimates each feature's survival probability from a Bayesian
survival-time model. STRATA's per-window `ℓ ← λℓ` decay (§4.2) is a
discretized, single-scalar simplification of that idea: one tunable
multiplicative factor pulls belief toward unknown each window in place of a
full survival-time posterior. The log-odds hit/miss update with clamping in
Thrun et al. [@thrun2005probabilistic] is used verbatim as STRATA's per-window
occupancy accumulation, with decay and hysteresis layered on top. Tipaldi et
al. [@tipaldi2013lifelong] and Saarinen et al. (iMac) [@saarinen2012imac]
model each cell as a two-state Markov process with a recency-weighted or
online-learned transition rate — the principled, per-cell-learned form of
"how fast a cell should forget." `survival_decay` is a single global
constant, a constant-rate special case of that formalism; STRATA states this
as a documented simplification (§7), not an unacknowledged gap.

**Periodicity.** FreMEn [@krajnik2017fremen] is the direct counter-argument
STRATA's Periodic class answers: a cell's occupancy can be a periodic
temporal signal, and a flat decay erases exactly that structure. STRATA's
`PeriodicityModel` (§4.4) runs FreMEn's incremental-Fourier accumulation in
parallel with decay and graduation, so an oscillating cell is classified
Periodic rather than mis-graduated to Static or mis-pruned as Transient — the
third class exists specifically to answer this argument, at the cost of a
bounded harmonic count and a validity gate tied to touched-window count
rather than elapsed time (SPEC-DIFF, §4.7). Krajník et al.
[@krajnik2014spectral] is the earlier, more general spectral-analysis
lineage FreMEn later packaged as a mapping method; STRATA's bounded
incremental-Fourier approximation is a further narrowing of that lineage, not
the full spectral machinery.

**Dynamic-object removal.** Removert [@kim2020removert] conservatively
removes dynamic points and then reverts wrongly removed static ones, because
aggressive removal erases real structure. This is the exact justification for
STRATA's hysteresis band (§4.3): a graduated cell demotes only when its
probability falls below `p_dem < p_grad`, i.e. sustained contradicting
evidence across windows rather than a single contradicting frame — the same
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
multi-resolution compression for simplicity and unit-testability (§7). The
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
here as the closest existing shipped system offering selectable 2D/3D
geometry under one framework, narrowed by STRATA to a persistence-only
module with the classifier shared by composition rather than duplicated per
geometry — the only per-backend code is point-to-`CellId` mapping and the
free-space ray walk.

None of the surveyed prior art offers, as a standalone, SLAM-free,
dependency-light module, the specific combination STRATA ships: survival
decay, Schmitt-trigger hysteresis, negative-evidence ray clearing, and
parallel FreMEn-lite periodicity classification, unified behind one
geometry-free engine and selectable between two map geometries at runtime.
