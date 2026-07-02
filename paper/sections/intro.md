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

A robot that maps the same building for weeks cannot bake every LiDAR hit
permanently into one grid: parked carts, passing people, and doors that open on
a schedule all burn in, and the map slowly ossifies into a record of everything
that was ever seen rather than of what is actually there now. Lifelong mapping
must instead hold three properties in tension at once — durability (keep the
walls), plasticity (forget the movers), and periodicity (treat a cyclic door as
signal, not noise). We present STRATA, a small, geometry-free
persistence-and-periodicity engine that resolves this tension in one shared
per-cell state machine and drives two pluggable geometry backends — a
fixed-array 2D occupancy grid and an unbounded 3D voxel-hash — behind a single
interface selected by one runtime string parameter. The only per-backend code
is point-to-id mapping and the free-space ray walk; the classifier that
graduates, demotes, prunes, and periodicity-labels every cell is shared by
composition, not duplicated per dimension. The engine is a plain C++17 + Eigen
library with no ROS, DDS, or PCL dependency, exercised by 22 behavior-level
tests, so its scientific logic builds and runs deterministically in a
second-scale CI loop. A seeded synthetic characterization suite reports that
both backends graduate a static wall by the third window and hold recall 1.0
thereafter (their only divergence, a clutter-induced static-precision gap of
0.9253 vs. 0.9758 under 100 movers per window, is geometric, not algorithmic);
that 50%-duty periodic doors are detected at true-positive rate 2/3 and
false-positive rate 1/5; that removing hysteresis inflates flicker from 52 to
574 toggles on identical replayed noise; and that the unified engine costs a
flat ~56 B per cell with no per-dimension penalty. Evaluation is synthetic and
the tool performs no SLAM; STRATA is the persistence core meant to sit beneath
an external localizer, released open-source as a ROS 2 package.

# Introduction

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

The systems that come closest to solving this are heavier than the problem needs
to be. ELite [@gil2025elite] and LT-mapper [@kim2022ltmapper] — the closest
published twins in intent — resolve durability versus plasticity, but as parts
of full lifelong-**SLAM** pipelines, coupled to multi-session point-cloud
registration, place recognition, and alignment. RTAB-Map [@labbe2019rtabmap] is
the closest engineering precedent for offering selectable 2D and 3D geometry
under one framework, but it too is a complete SLAM system with loop closure and
memory tiering. Each carries the persistence-and-classification logic a robot
needs, but welded inside a much larger apparatus and to a specific map
geometry. What is missing from the surveyed prior art is the small piece by
itself: a standalone, SLAM-free, dependency-light engine that does only the
graduation, forgetting, and periodicity classification, that runs under either
2D or 3D geometry, and that a practitioner can drop beneath an external
localizer or an existing SLAM front end.

STRATA is that piece. The name is literal: a cell in the map carries temporal
strata — Static, Periodic, and Transient layers over an Unknown baseline —
coexisting inside one per-cell state machine that log-odds occupancy, survival
decay, Schmitt-trigger hysteresis, and an incremental Fourier periodicity test
all read and write. That state machine is geometry-free. Both shipped
backends translate world points into `int64` cell keys and walk a free-space
ray to clear it; everything downstream of the key — accumulation, graduation,
demotion, pruning, periodicity labeling — happens once, in the shared engine.
The thesis is deliberately narrow: *the only per-backend code is point-to-id
mapping and the free-space ray walk; the classifier is shared by composition,
not duplicated per dimension* (Fig. 1). Selecting 2D versus 3D is a single
runtime string parameter resolved at construction, not a plugin-loading
mechanism, and the engine itself (`strata_core`) is a plain C++17 + Eigen
library with no `rclcpp`, `tf2`, PCL, or DDS dependency, so its scientific logic
builds and unit-tests without a ROS install.

We are explicit about scope up front (§7 consolidates the non-claims). STRATA is
**not SLAM**: it estimates no pose, closes no loops, and aligns no sessions,
consuming instead an external `map → sensor` transform. Its evaluation is
**synthetic only** — every number below comes from deterministic seeded
harnesses and unit tests, not a field deployment, in contrast to the multi-day
validations of ELite, LT-mapper, and Berrio et al. [@berrio2021longterm]. And it
models **single-session** temporal persistence, not multi-session remapping: a
graduated cell simply *is* the current static map, with no session versioning.
These are the boundaries of the tool, stated as scope rather than buried as
gaps.

**Contributions.** This paper makes five, each backed only by the shipped code
and the synthetic suite:

1. **One geometry-free engine, two geometries, one switch (by construction).**
   A single `int64`-keyed persistence-and-periodicity engine (`LayeredMap` +
   `PeriodicityModel`) drives both a fixed-array 2D occupancy grid and an
   unbounded 3D voxel-hash behind one `MapBackend` interface, chosen by a single
   runtime string parameter, with no state-machine logic duplicated across
   dimensions (Fig. 1; §3–4).
2. **A ROS-free, deterministically testable core, shown backend-equivalent
   (design goal + measured).** The engine builds and unit-tests with a plain
   C++17 + Eigen toolchain and is exercised by 22 behavior-level tests; both
   backends graduate the static wall by window 3 and hold recall 1.0 through
   window 39, differing only by a clutter-induced static-precision gap (0.9253
   grid2d vs. 0.9758 voxel3d at 100 movers per window) that traces to 2D
   z-collapse geometry, not to the classifier (§5–6).
3. **A specific minimal combination not previously packaged standalone.**
   Persistence-Filter survival decay [@rosen2016persistence], Removert-motivated
   Schmitt hysteresis [@kim2020removert], ReFusion-style negative-evidence ray
   clearing [@palazzolo2019refusion], and a parallel FreMEn-lite periodicity
   classifier [@krajnik2017fremen], emitting a four-class label from one
   dependency-light, SLAM-free module — a combination absent, in this form, from
   the surveyed prior art (§2, §4).
4. **A mechanistic characterization of the shared classifier (measured).**
   50%-duty periodicity is detected cleanly (dominant-harmonic amplitude
   0.653/0.707, well above the 0.3 threshold) at true-positive rate 2/3 and
   false-positive rate 1/5, with the single miss diagnosed as a prune/maturity
   race; and hysteresis is the dominant stabilizer — removing it yields 574
   flicker toggles at F1 0.810 versus 52 toggles at F1 1.000 on identical
   replayed noise (§6).
5. **A unified engine that adds no per-dimension cost, released as a
   reproducible tool (measured).** Memory is a flat ~56 B per cell and
   per-window cost tracks live-cell count linearly; the 4–8× per-`integrate()`
   gap between backends is entirely 3D free-space voxel proliferation (6–10×
   more live cells), geometry rather than the engine. The whole system ships as
   an open-source ROS 2 package with a documented I/O contract and a seeded
   harness whose figures regenerate from measured CSVs (§5–6).

STRATA is available at `github.com/kjungmo/strata` as a ROS 2 Humble package
under an open-source license, with a thin ROS adapter node and a reusable
characterization harness; it is designed to plug beneath an external localizer
such as `prism_loc`. The remainder of the paper positions STRATA against the
prior art (§2), specifies its two-band architecture (§3), gives the as-shipped
engine mathematics including three honest implementation-versus-specification
differences (§4), documents the testability and reproducibility design (§5),
reports the synthetic evaluation (§6), and states the limitations and boundaries
of the tool (§7–8).
