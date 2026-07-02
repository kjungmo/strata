# STRATA — Final Outline Contract (binding for all section writers)

> **Judge's decision.** Chosen spine = **Proposal A (systems / tool paper)**.
> Rationale: given synthetic-only evaluation with no field data, the most
> defensible posture is a *tool/systems* contribution (architecture + ROS-free
> testability + a reproducible characterization suite), where the benchmarks
> **characterize** the shipped engine rather than claim accuracy SOTA. Proposal B's
> spine — "backend equivalence, measured" — leans its weight on E1/E4, but that
> equivalence is largely true *by construction* (it is literally one object), so a
> reviewer can read the empirical claim as near-tautological. We therefore adopt A's
> broader, sturdier contribution set and **graft B's strongest parts**:
> (i) the thesis sentence "the only per-backend code is point→id + ray-walk; the
> classifier is shared by composition"; (ii) the "cross-backend divergence is
> localized to, and quantified at, the geometry layer" analytical lens, used *as an
> interpretation of E1/E4*, not as the paper's spine; (iii) B's sharper E2
> prune/maturity-race diagnosis and per-cell 56 B footprint decomposition;
> (iv) B's determinism convention (`survival_decay=1.0` in unit tests).
>
> **Honesty guardrails, non-negotiable for every writer.**
> - Only **five** figures exist on disk (`fig_architecture`, `fig_e1_f1_vs_time`,
>   `fig_e2_periodicity`, `fig_e3_sensitivity`, `fig_e4_throughput`). Do **not**
>   promise any figure that is not in `paper/figures/`. The cell-class state machine
>   is a **table** (transition table from `core_math.md` §1.9), not a figure.
> - Every number must trace to `experiments_summary.md` / a CSV in
>   `paper/experiments/results/` or to `core_math.md` / `tests.md`. Never invent.
> - The three **SPEC-DIFFs** are surfaced, not hidden. The paper documents the code
>   that ships, not the SPEC prose.

---

## Title (final)

**Primary:**
> **STRATA: One Geometry-Free Persistence-and-Periodicity Engine Driving Selectable 2D/3D Lifelong LiDAR Mapping in ROS 2**

**Alternate (testability-forward, if a shorter title is needed):**
> *STRATA: A ROS-Free, Unit-Testable Persistence-and-Periodicity Core for Two Pluggable 2D/3D Lifelong-Mapping Backends*

**Authors line:**
> Jungmo Kang — Independent Researcher — github.com/kjungmo — kangjmo91@gmail.com

**Naming convention (all writers obey).** `STRATA` (all caps) = the tool/system in
prose. The two shipped ament packages stay monospace lowercase: `strata_core` (the
engine) and `strata` (the ROS 2 node). Class/param names always monospace:
`LayeredMap`, `PeriodicityModel`, `MapBackend`, `Grid2DBackend`, `Voxel3DBackend`.
Introduce the name once via the geological metaphor (temporal strata of the map —
Static / Periodic / Transient — coexisting as one cell state machine). No backronym.

**Keywords:** lifelong mapping, occupancy grids, voxel mapping, persistence
filtering, FreMEn periodicity, ROS 2, open-source robotics software.

---

## Global length target

~11–12 pages, arXiv cs.RO single-column preprint (or IEEE two-column if compiled
that way). Per-section budgets below are guidance, not hard limits.

---

## Section-by-section content contract

Each section lists: **MUST-cover** bullets · **Source note(s)** · **Owns
(figures/tables)** · **Target length**. Writers may not introduce facts absent from
the cited source note.

### §1 Introduction
**MUST-cover**
- Motivate the three-way tension of lifelong mapping: durability (keep walls),
  plasticity (forget movers), periodicity (a cyclic door is signal, not noise) —
  the stability–plasticity dilemma (`biber2005dynamicmaps`).
- State the gap the *tool* fills: the closest-intent systems (ELite, LT-mapper) are
  full lifelong-**SLAM** pipelines; there is no standalone, SLAM-free,
  dependency-light persistence+periodicity core to sit *under* them.
- Contributions as a numbered list C1–C5 (mirror §Claims): one geometry-free engine
  → two geometries via a one-parameter switch; ROS-free deterministic testability
  as a first-class design goal; the specific minimal *combination* (decay +
  hysteresis + negative-evidence + FreMEn-lite) not present as a standalone module
  in prior art; a reproducible synthetic characterization suite.
- Disclaim SLAM and field validation *up front* (forward-ref §Non-claims).
- Availability pointer (open-source repo `kjungmo/strata`, ROS 2 Humble).

**Source:** `related_work.md` (novelty assessment). **Owns:** none.
**Target:** ~1 page.

### §2 Related Work
**MUST-cover** (six themes, each entry states the *exact* STRATA relation)
- (a) Lifelong/persistent mapping — `gil2025elite`, `kim2022ltmapper` as closest
  twins (STRATA = the graduation/decay core they wrap, minus SLAM/multi-session);
  `schmid2024khronos`, `biber2005dynamicmaps`, `meyerdelius2010temporary` as
  multi-timescale foundations; `labbe2019rtabmap` as the closest "selectable 2D/3D
  under one framework" precedent; `berrio2021longterm` (field baseline STRATA lacks),
  `yang2025lifelong3d` (session-versioning STRATA does not do).
- (b) Persistence/forgetting — `rosen2016persistence` (direct decay ancestor),
  `thrun2005probabilistic` (log-odds inverse-sensor model verbatim),
  `tipaldi2013lifelong` + `saarinen2012imac` (per-cell-learned rate = the
  simplification STRATA openly accepts).
- (c) Periodicity — `krajnik2017fremen` (the counter-argument the Periodic class
  answers) + `krajnik2014spectral`.
- (d) Dynamic-object removal — `kim2020removert` (hysteresis motivation),
  `palazzolo2019refusion` (misses = negative evidence), `lim2021erasor` (offline
  contrast, what STRATA is not).
- (e) Production stacks — `hornung2013octomap`, `niessner2013hashing`,
  `duberg2020ufomap`, `macenski2020stvl`, `lu2014layered`, `macenski2021slamtoolbox`.
- (f) Pluggable-backend prior art — `ros2pluginlib` as the pattern STRATA
  deliberately **rejects** at two backends; `labbe2019rtabmap` re-cited.
- Close with the one-sentence gap statement (no standalone, dependency-light,
  geometry-free persistence+periodicity module in the surveyed prior art).

**Source:** `related_work.md`. **Owns:** none (prose only).
**Target:** ~1.5 pages.

### §3 System Overview and Architecture — *core of the systems framing*
**MUST-cover**
- The two-band, two-package split: `strata_core` (pure C++17 + Eigen, **no**
  `rclcpp`/`tf2`/PCL) vs `strata` (thin ROS 2 adapter owning message↔`Observation`
  conversion, TF lookups, publishers/services, PGM/YAML/PCD I/O).
- The `MapBackend` interface contract (`integrate`, `tick`, `staticCellCount`,
  `transientCellCount`) and the geometry-free invariant: both backends translate
  world geometry into `int64` `CellId` keys; the occupancy/persistence/periodicity
  state machine lives once, in `LayeredMap`.
- **Grafted thesis sentence (from Proposal B):** "the only per-backend code is
  point→id mapping and the free-space ray walk; the classifier is shared by
  composition, not duplicated per dimension."
- Backend selection = a single runtime string param resolved at construction (two
  `unique_ptr`s + if/else, **not** `dlopen`/pluginlib); argue compile-time interface
  + one switch is right at two backends, deferring pluginlib to backend-count growth.
- ROS I/O contract: `~/map` (OccupancyGrid, transient-local), `~/map_points`
  (PointCloud2), `~/save_map` (Trigger); mapping-only — no `/initialpose`, no
  `map→odom` broadcast (consumes external localization).

**Source:** `system.md`. **Owns:** **Fig. 1** (`fig_architecture`); **ROS I/O
contract table**. **Target:** ~1.5 pages.

### §4 The Layered Map Engine (as implemented) — *Method; math is verbatim from `core_math.md`*
**MUST-cover**
- 4.1 Per-frame accumulation + windowing: lazy cell creation, window counters,
  `tick()`/`endWindow()` on the `layer_interval` boundary; window occupancy decision
  (`touched`/`occ`/`free`; one hit outweighs any misses).
- 4.2 Log-odds persistence + survival decay + clamp: the
  `ℓ ← clamp(λ(ℓ + one-increment), l_min, l_max)` update; the **load-bearing ordering
  note** (decay attenuates the *fresh* increment — not classic decay-then-add);
  derived fixed point `ℓ⋆≈27.5` clamped to 5.
- 4.3 Schmitt-trigger graduate/demote: `p_grad`/`p_dem` hysteresis band,
  `min_observations` gate, `graduated` flag; the `!periodic`/`||periodic` guards.
- 4.4 FreMEn-lite periodicity: incremental Fourier accumulators (`S_0,C_k,S_k`),
  phase prediction `p̂(t)`, dominant-harmonic amplitude `a`, the `n≥T` validity gate,
  `a≥a_min` test.
- 4.5 Cell-class state machine + pruning: four-state ladder
  (Unknown/Transient/Periodic/Static), prune rule `¬g ∧ p<p_prune ∧ a<a_min`.
- 4.6 Geometry backends: grid2d exact Bresenham clear on a fixed array vs voxel3d
  0.5-voxel ray-sample march into an unbounded `int64`-packed hash;
  occupancy-value convention (`-1/50/75/100`).
- 4.7 **Implementation-vs-specification (honesty subsection):** flag the three
  **SPEC-DIFFs** — (1) untouched cells do *not* decay (forgetting coupled to in-FOV
  re-observation), (2) amplitude gate on *touched-window* count not elapsed time,
  (3) periodic guards absent from SPEC §3.4 pseudocode. Frame: "the paper documents
  the code that ships."

**Source:** `core_math.md`. **Owns:** **Algorithm listing** (per-window `endWindow`:
accumulate → log-odds+decay+clamp → FreMEn gather → amplitude → Schmitt
graduate/demote → prune); **Cell-class transition table** (from §1.9 — this is a
table, NOT a figure); **backend side-by-side table** (grid2d vs voxel3d). **Target:**
~2.5 pages.

### §5 Testability, Determinism, and Reproducibility — *second pillar; may fold into §3 if page-tight*
**MUST-cover**
- Why the core/adapter split buys deterministic, fast CI: the scientific logic
  builds and tests with `cmake -S strata_core -B build -DSTRATA_CORE_BUILD_TESTS=ON
  && ctest` — no ROS install, no DDS discovery, no TF warm-up.
- Test inventory: **22 behavior-level `TEST` cases across 9 files** (26 raw macros),
  spanning types/geometry, `LayeredMap` state machine, `PeriodicityModel`, both
  backends, and the ROS adapters.
- The canonical `Integration.WallStaticMoverTransientDoorPeriodic` scenario (40×40
  grid, 24 windows, door period 8) as the reusable reference eval — one deterministic
  scene jointly exercising all three classes; a template a third party can replay
  against an alternative implementation.
- **Determinism convention (grafted from B):** unit tests set `survival_decay=1.0`
  so log-odds accumulate predictably; the E1–E4 harness instead seeds a single
  `mt19937` from `kSeed=12345`, recorded in each CSV `#seed` row; figures regenerate
  from CSVs via `run_all_plots.sh`; `strata_core` `ctest` 1/1 gates the harness.

**Source:** `tests.md` (+ `experiments_summary.md` preamble). **Owns:** none
(optionally a compact test-inventory table). **Target:** ~0.75 page.

### §6 Evaluation (synthetic)
**MUST-cover**
- 6.1 Setup: harness compiled directly against `strata_core`, no ROS/ament;
  deterministic seeds; only E4 reads the wall clock (absolute µs machine-dependent,
  ratios/cell-counts deterministic). Map each experiment to the thesis: E1+E4 probe
  backend-behavior equivalence and the **geometric localization of cost/divergence**;
  E2+E3 characterize the shared engine once (valid for both backends by construction).
- 6.2 **E1** static-layer quality vs time → both backends graduate walls by window 3,
  recall→1.0 held; the only errors are false-positive statics under 100-mover/window
  clutter (precision@w39 **0.9253 grid2d / 0.9758 voxel3d**), attributed to 2D
  z-collapse geometry, not the classifier.
- 6.3 **E2** periodicity → **TPR 2/3, FPR 1/5**; 50%-duty doors clear cleanly
  (amplitude 0.653 / 0.707 ≫ a_min 0.3); the 25%-duty miss is a **prune/maturity
  race** (pruned in ~10/64 vacant windows before Fourier amplitude matures; the
  `ref_amplitude` non-pruning column confirms); one aperiodic false positive at 0.329.
- 6.4 **E3** hysteresis/decay sensitivity → removing hysteresis is catastrophic
  (**574 flickers, F1 0.810**) vs **52 flickers, F1 1.000** on the identical replayed
  noise with a wide band; lower `survival_decay` amplifies flicker.
- 6.5 **E4** throughput/memory → grid2d **4–8×** cheaper per `integrate()`; flat
  **~56 B/cell**; ≤37 MB even at the largest 3D case; `O(N·H)`/window. The 4–8× gap is
  entirely attributable to 3D free-space voxel proliferation (**6–10× more live
  cells**) — geometry, not the engine.
- 6.6 Synthesis: tie the four back — the engine is one object, so E1's near-identical
  behavior and E4's flat-56 B/cell-tracks-count are the empirical face of "no
  per-dimension logic or cost"; the only behavioral divergence (E1 high-clutter
  precision) and the only cost gap (E4 4–8×) are both **geometric**.

**Source:** `experiments_summary.md` + CSVs in `paper/experiments/results/`.
**Owns:** **Fig. 2** (`fig_e1_f1_vs_time`, §6.2), **Fig. 3** (`fig_e2_periodicity`,
§6.3), **Fig. 4** (`fig_e3_sensitivity`, §6.4), **Fig. 5** (`fig_e4_throughput`,
§6.5); one measured results table per subsection (verbatim numbers). **Target:**
~2.5 pages.

### §7 Discussion and Limitations
**MUST-cover**
- Consolidated non-claims (see §Non-claims); the SPEC-DIFF #1 forgetting caveat
  (out-of-FOV clutter persists) restated as an operational limitation.
- Design trade-offs owned honestly: global scalar `survival_decay` vs
  per-cell-learned rate (`saarinen2012imac`/`tipaldi2013lifelong`); flat hash vs
  octree compression (`hornung2013octomap`/`duberg2020ufomap`); sim-only cost of
  honesty (no field validation yet).
- When *not* to use STRATA (needs SLAM / multi-session replay / semantics) → the
  tool's stated boundaries.
- Future work: field + multi-session validation; per-cell-learned decay rate;
  decouple the prune-vs-amplitude race behind the low-duty periodicity miss; decouple
  forgetting from re-observation so out-of-FOV clutter can fade.

**Source:** `related_work.md` (novelty assessment) + `core_math.md` (SPEC-DIFFs).
**Owns:** none. **Target:** ~1 page.

### §8 Conclusion and Availability
**MUST-cover**
- Recap: one small engine, two geometries, one switch, ROS-free testable core,
  reproducible synthetic evidence.
- Open-source availability (`kjungmo/strata`), license, ROS 2 distro, invitation to
  plug onto an external localizer (e.g. `prism_loc`).

**Source:** `related_work.md` + `system.md`. **Owns:** none. **Target:** ~0.4 page.

---

## Canonical notation table (ALL writers must use these symbols exactly)

| Symbol | Meaning | Default |
|---|---|---|
| `CellId` | raw `int64` cell key | — |
| `gridCellId(m,gx,gy) = gy·width + gx` | 2D row-major cell id | — |
| voxel id `= (vx≪42)\|(vy≪21)\|vz` | 3D packed key; `kOff = 1≪20`, `kBits = 21` | — |
| `ℓ` | per-cell log-odds occupancy evidence | init 0 |
| `p = σ(ℓ) = 1/(1+e^{−ℓ})` | occupancy probability; `σ` = logistic sigmoid | — |
| `t` | window (phase) index (`window_count_`) | — |
| `h_w`, `m_w` | window hits / window misses in the current window | — |
| `touched` | `[h_w>0 ∨ m_w>0]` — cell observed this window | — |
| `occ` | `[h_w>0]` — window scored occupied (one hit outweighs any misses) | — |
| `free` | `[h_w=0 ∧ m_w>0]` — window scored free | — |
| `l_hit`, `l_miss` | log-odds increment on an occupied / free window | 0.85, −0.4 |
| `l_min`, `l_max` | log-odds clamp bounds (`p_max=σ(5)≈0.9933`) | −5.0, 5.0 |
| `clamp(x,a,b)` | `min(b, max(a, x))` | — |
| `λ` | `survival_decay` — per-window forgetting multiplier (multiplies the already-incremented `ℓ`) | 0.97 |
| `observations` | per-cell count of touched windows | — |
| `N_min` | `min_observations` — min touched windows before graduation | 3 |
| `p_grad`, `p_dem` | `graduate_prob` / `demote_prob` Schmitt thresholds; `[p_dem,p_grad]` = hysteresis band | 0.8, 0.45 |
| `g` | `graduated` (Static) boolean flag | false |
| `p_prune` | `prune_prob` — erase non-static, non-periodic cell below this `p` | 0.05 |
| `H` | `n_harmonics` — Fourier harmonics tracked per cell | 2 |
| `T` | `period_windows` — FreMEn base period; also amplitude-validity gate (`n≥T`) | 24 |
| `ω = 2π/max(1,T)` | FreMEn fundamental angular frequency | — |
| `v = [occ] ∈ {0,1}` | per-window occupancy sample fed to FreMEn | — |
| `n` | count of touched windows fed to `gather` (≠ elapsed windows) | — |
| `S_0` | DC accumulator, `Σ v` | — |
| `C_k`, `S_k` | cosine / sine Fourier accumulators for harmonic `k` (`k=0..H−1`) | — |
| `a_k = 2C_k/n`, `b_k = 2S_k/n` | harmonic cosine / sine coefficients; DC mean `= S_0/n` | — |
| `p̂(t)` | FreMEn occupancy prediction at phase `t`, clipped to [0,1] | — |
| `a` | dominant-harmonic amplitude `max_k √(a_k²+b_k²)`; `0` when `n<T` | — |
| `a_min` | `periodic_amplitude_min` — Periodic if `a ≥ a_min` | 0.3 |
| `periodic` | `[enable_periodicity ∧ a ≥ a_min]` | — |
| `layer_interval` | integration ticks per window | 10 |
| `N` | number of live cells (complexity parameter; window update `O(N·H)`) | — |
| `ℓ⋆ = λ·l_hit/(1−λ) ≈ 27.5` | decayed fixed point of repeated hits (clamped to `l_max`) | — |
| `GridMeta` | 2D `{width,height,resolution,origin_x,origin_y}` | 400,400,0.05,−10,−10 |
| `voxel_size` | 3D voxel edge | 0.2 m |
| per-cell footprint | `CellEvidence` 24 B + ~32 B map node = **56 B** (periodicity off) | — |
| harness seed | `mt19937` `kSeed` | 12345 |

**Derived thresholds to cite inline:** `p_grad=0.8 ⇒ ℓ≥ln4≈1.386`;
`p_prune=0.05 ⇒ ℓ<ln(1/19)≈−2.944`; clamped `p_max=σ(5)≈0.9933`.

---

## CLAIMS (each backed by notes/experiments only)

- **C1 (by construction).** One geometry-free, `int64`-keyed persistence+periodicity
  engine (`LayeredMap` + `PeriodicityModel`) drives both a fixed-array 2D occupancy
  grid and an unbounded 3D voxel-hash behind one `MapBackend` interface, selected by a
  single runtime string parameter; the only per-backend code is point→id mapping and
  the free-space ray walk — no duplicated state-machine logic across dimensions.
  *(Fig. 1; §3–4.)*
- **C2 (design goal + measured, E1).** The ROS-free core builds and unit-tests with a
  plain C++17 + Eigen toolchain (no `rclcpp`/`tf2`/PCL/colcon/DDS), exercised by 22
  behavior-level gtest cases; its observable behavior is backend-equivalent — both
  backends graduate the static wall by window 3 and hold recall 1.0 through window 39,
  F1 matching except a clutter-induced precision gap (0.9253 grid2d vs 0.9758 voxel3d
  at 100 movers/window) attributable to 2D z-collapse geometry, not the classifier.
- **C3 (new standalone combination).** Persistence-Filter survival decay +
  Removert-motivated Schmitt hysteresis + ReFusion-style negative-evidence ray
  clearing + parallel FreMEn-lite periodicity, emitting a four-class label, packaged
  as one dependency-light, SLAM-free engine — a combination not available as a
  standalone module in the surveyed prior art.
- **C4 (measured, E2/E3).** On the shared engine, 50%-duty periodicity is detected
  cleanly (amplitude 0.653/0.707 ≫ a_min 0.3) at TPR 2/3, FPR 1/5, with the single
  miss mechanistically diagnosed as a prune/maturity race; and hysteresis is the
  dominant stabilizer — removing it yields 574 flicker toggles and F1 0.810 versus 52
  toggles at F1 1.000 on identical replayed noise.
- **C5 (measured, E4 + reproducible tool).** The unified engine adds no per-dimension
  cost: flat ~56 B/cell, cost tracks live-cell count linearly, and the 4–8×
  per-`integrate()` grid2d-vs-voxel3d gap is entirely 3D free-space voxel
  proliferation (6–10× more live cells) — geometry, not the engine; delivered as an
  open-source ROS 2 tool with a documented I/O contract and a seeded harness whose
  figures regenerate from measured CSVs.

## NON-CLAIMS (explicit)

- **Not SLAM.** No pose estimation, scan matching, loop closure, or multi-session
  alignment; STRATA consumes an external `map→sensor` transform and never publishes
  `map→odom` or subscribes `/initialpose`.
- **Synthetic evaluation only.** All numbers come from deterministic seeded synthetic
  harnesses and unit tests — no real robot, no real LiDAR, no field deployment
  (contrast the field validation of ELite, LT-mapper, Berrio et al.). Present scope,
  not a hidden gap.
- **Single-session temporal persistence, not multi-session remapping.** No session
  versioning, cross-session registration, place recognition, or past-session
  reconstruction; a graduated cell simply *is* the current static map, history
  implicit in log-odds/observations (contrast LT-mapper, Yang et al., ELite).
- **Not bit-identical across backends.** Behavior is *equivalent to within
  geometry-induced differences*, which E1 measures rather than hides.
- **No object-level or semantic reasoning.** Cell-level state machine only — no scene
  graph (unlike Khronos), no per-object removal (unlike ERASOR).
- **Not a full Bayesian survival posterior, not a per-cell-learned rate.**
  `survival_decay` is a single global constant — a constant-rate special case of the
  Persistence Filter / iMac / Tipaldi formalisms.
- **No multi-resolution compression.** `voxel3d` is a flat O(1) hash, trading away
  OctoMap/UFOMap octree compression for simplicity and testability.
- **Periodicity is FreMEn-lite** (bounded `H`, base period `T`); low-duty (≤25%)
  periodic cells can be missed under pruning — documented (E2), not swept aside.
- **Honest as-implemented semantics (SPEC-DIFFs).** Out-of-FOV cells do *not* decay
  (forgetting coupled to in-FOV re-observation, #1); the amplitude gate counts
  *touched* windows, not elapsed time (#3); the periodic graduate/demote guards exist
  in code but not in SPEC §3.4 (#2). The paper documents the shipped code.
- **Absolute E4 timings are machine-dependent** — only cross-backend/size ratios and
  cell counts are deterministic.

---

## Citation keys available (from `paper/references.bib` — 24 keys)

**(a) Lifelong / persistent mapping:** `gil2025elite`, `kim2022ltmapper`,
`schmid2024khronos`, `biber2005dynamicmaps`, `meyerdelius2010temporary`,
`labbe2019rtabmap`, `berrio2021longterm`, `yang2025lifelong3d`.
**(b) Persistence / forgetting:** `rosen2016persistence`, `thrun2005probabilistic`,
`tipaldi2013lifelong`, `saarinen2012imac`.
**(c) Periodicity:** `krajnik2017fremen`, `krajnik2014spectral`.
**(d) Dynamic-object removal:** `kim2020removert`, `palazzolo2019refusion`,
`lim2021erasor`.
**(e) Production stacks:** `hornung2013octomap`, `niessner2013hashing`,
`duberg2020ufomap`, `macenski2020stvl`, `lu2014layered`, `macenski2021slamtoolbox`.
**(f) Pluggable-backend prior art:** `ros2pluginlib` (+ `labbe2019rtabmap` re-cited).
