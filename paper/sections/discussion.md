# 7 Discussion and Limitations

## 7.1 What has and has not been shown

The evaluation in §6 characterizes a shipped engine; it does not validate a
deployed system. Every number in this paper — E1's precision/recall curves,
E2's TPR/FPR, E3's flicker counts, E4's throughput and memory — comes from a
deterministic, seeded synthetic harness compiled directly against
`strata_core`, driving hand-authored hit/miss patterns through the real
`LayeredMap`/`PeriodicityModel` code. No real sensor, no real robot motion,
and no field deployment enters any of these results. This is a statement of
present scope, not a hidden gap: the closest published systems in intent
(ELite [@gil2025elite], LT-mapper [@kim2022ltmapper]) and the closest
production baseline (Berrio et al. [@berrio2021longterm]) all report
multi-day or multi-session field evidence that STRATA does not yet have.
STRATA is, so far, validated the way a library is validated — by unit and
characterization tests against known ground truth — not the way a robot
behavior is validated, by miles driven.

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
Section 6.6 already frames grid2d and voxel3d as *equivalent to within
geometry-induced differences* rather than identical, and E1's clutter-driven
precision gap (0.9253 vs 0.9758 at 100 movers/window) and E4's 4–8×
per-`integrate()` cost gap are exactly those differences, measured rather
than glossed over.

## 7.2 The out-of-FOV forgetting caveat is an operational limitation, not a footnote

§4.7 documents SPEC-DIFF #1 as an implementation fact: decay is applied only
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

## 7.3 Design trade-offs owned honestly

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
holds 6–10× the live-cell count of grid2d at matched scene extent because
every sampled free-space point along a ray becomes its own hash entry, and
per-cell footprint is a flat 56 B regardless of spatial redundancy. The trade
is deliberate — O(1) insert/lookup with no tree-rebalancing logic keeps the
backend simple enough to unit-test the way §5 describes — but it means
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
framing over an accuracy-SOTA framing means every claim in §6 is scoped to
"what the shipped code measurably does," not "how well STRATA localizes or
maps relative to a competing system." No baseline comparison against
OctoMap, ELite, or LT-mapper is run in this paper — none is designed as an
apples-to-apples ROS-free unit-testable core, so no fair single-number
comparison exists yet. The E1–E4 harness is offered instead as a replayable
protocol (§5) a future study could run against an alternative
implementation, rather than a leaderboard result against one run today.

## 7.4 When not to use STRATA

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
ERASOR's per-object removal [@lim2021erasor]. Within those boundaries — a
single continuous mapping session behind an external localizer, at the raw
occupancy-cell level, in 2D or 3D — STRATA is the intended fit.

## 7.5 Future work

Four items follow directly from the limitations above, in the order they
would be tackled: (1) real-robot and multi-session field validation, closing
the synthetic-only gap of §7.1, on both a 2D wheeled platform and a 3D-LiDAR
platform to exercise both backends under real sensor noise; (2) a
per-cell-learned decay rate along the lines of iMac/Tipaldi
[@saarinen2012imac; @tipaldi2013lifelong], replacing the global
`survival_decay` scalar characterized in §7.3 with region- or cell-adaptive
forgetting; (3) resolving the prune/maturity race identified in E2 — the
25%-duty periodic door is pruned during its own vacant stretches before its
FreMEn amplitude has enough touched-window support to mature (§6.3) — by
either exempting recently-active cells from pruning or feeding a running
amplitude estimate into the prune decision itself; and (4) decoupling
forgetting from re-observation, i.e. fixing SPEC-DIFF #1 (§7.2) so an
untouched cell's log-odds relaxes toward the unknown prior on a wall-clock
or tick basis rather than only ever decaying when re-touched, closing the
out-of-FOV persistence gap without changing the touched-cell update path E1–E4
already characterize.

# 8 Conclusion

STRATA packages lifelong occupancy mapping's persistence-and-periodicity
logic — log-odds accumulation, Persistence-Filter-style survival decay,
Removert-motivated Schmitt-trigger hysteresis, ReFusion-style negative
evidence from free-space ray clearing, and FreMEn-lite periodicity
detection — into one geometry-free `LayeredMap`/`PeriodicityModel` engine
that drives two pluggable geometries, a 2D occupancy grid and a 3D voxel
hash, behind a single `MapBackend` interface and a one-parameter runtime
switch. The only code that differs per backend is point-to-cell-id mapping
and the free-space ray walk (§3–§4); the classifier that decides Static,
Periodic, Transient, or Unknown is written once and shared by composition.
That engine builds and unit-tests with a plain C++17 + Eigen toolchain,
independent of ROS 2, DDS, or PCL (§5), and its behavior is characterized —
not merely asserted — by a seeded, reproducible synthetic harness: near-
identical static-layer quality across backends up to a clutter-induced,
geometrically-explained precision gap (E1), clean detection of 50%-duty
periodicity with a diagnosed failure mode at low duty cycles (E2),
hysteresis as the dominant stabilizer against flicker (E3), and a flat
per-cell memory footprint with a 4–8× backend cost gap attributable entirely
to 3D free-space voxel proliferation, not to the shared engine (E4). None of
this replaces field validation, multi-session mapping, or semantic
reasoning — §7 states those boundaries explicitly — but within them, STRATA
is offered as a small, dependency-light module other systems can sit on top
of rather than a competing full lifelong-SLAM pipeline.

## Reproducibility

STRATA is open source under the Apache-2.0 license at
`github.com/kjungmo/strata`, targeting ROS 2 Humble. The `strata_core`
package alone builds and unit-tests without ROS installed
(`cmake -S strata_core -B build -DSTRATA_CORE_BUILD_TESTS=ON && ctest`, §5).
All figures and tables in §6 regenerate from the CSVs the harness itself
writes: `bash paper/experiments/run_all.sh` configures and builds the four
E1–E4 executables against `strata_core`, runs them with the fixed harness
seed `kSeed = 12345`, and writes `paper/experiments/results/*.csv`; a
follow-up `paper/experiments/plot/run_all_plots.sh` regenerates every figure
from those CSVs. `strata_core`'s own `ctest` suite (1/1) is checked before
the harness output is trusted. We invite integration reports, alternative
backend implementations replayed against the
`Integration.WallStaticMoverTransientDoorPeriodic` reference scenario (§5),
and pairing with an external localizer such as `prism_loc` for end-to-end
deployment.
