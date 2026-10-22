---
title: "STRATA: One Geometry-Free Persistence-and-Periodicity Engine for Selectable 2D/3D Lifelong LiDAR Mapping"
author:
  - name: Jungmo Kang
    affiliation: Independent Researcher, Seoul, Republic of Korea
    email: kangjmo91@gmail.com
    orcid: 0009-0002-0394-1861
    url: https://github.com/kjungmo/strata
keywords:
  - lifelong mapping
  - occupancy grids
  - sequential probability ratio test
  - persistence filtering
  - FreMEn periodicity
  - ROS 2
---

> This Markdown file is the companion of the submitted manuscript, whose source is
> `paper/arxiv_ref/` (PDF: `paper/strata_paper.pdf`). It is generated from that source
> by `paper/arxiv_ref/tools/tex2md.py`: the text, numbers, claims and scope are the
> same, section and equation numbers are those of the PDF, figures are not reproduced
> (the PDF draws them), and citations are pandoc keys that `references.bib` resolves.
> Where the wording differs, the manuscript is authoritative.

# Abstract {-}

A robot that maps one building for weeks must keep walls, forget movers and treat a scheduled door as signal. Occupancy mappers make the first two decisions with a log-odds accumulator whose increments were chosen by habit. We show that the OctoMap increments ($+0.85$, $-0.4$) are the increments of Wald's sequential probability ratio test (SPRT) between the hit rates 0.1975 and 0.4621, so that “Static” means “occupied in more than about a third of the windows”, and that a multiplicative leak removes the property on which an error bound rests. STRATA 0.2 makes the test explicit: the user states two hit rates and two error rates, increments and thresholds follow, and a cell ends Static, Rejected or Undecided. Under independent windows, clutter at a hit rate of at most $m_0$ graduates in the first test of one cell history (up to its first graduation, its first rejection or its retention as a periodic candidate) with probability at most 0.0101 (Ville's inequality); restarts after a demotion are not covered. The operating characteristic measured on the engine with the Periodic veto off matches the exact computation of the rule and is approximated by Wald's closed form. At a lag-1 autocorrelation of $0.23$, the low end of the range measured on real calibration cells (0.231 to 0.474), the graduation rate per forward test (a history split at each demotion) is 3.49 times $\alpha$, and at $0.5$ it is 10.87 times $\alpha$. We derived the pair under a pre-registered rule that we amended after the first calibration measurement, and the wall-thickness gate failed on every calibration sequence, so the shipped pair $(0.20,0.70,0.01,0.01)$ is provisional and the 3D pair is not calibrated. On held-out LILocBench, Oxford Spires, KTH-benchmark and Boreas data we compare seven evidence rules, without tuning and without choosing a winner. On LILocBench clutter, 8.81 \% of the cells are ever Static under the sequential test (2.93 \% at an observed hit rate of at most $m_0$) against 21.16 \% under v0.1; both exceed the nominal $\alpha=0.01$, which is per history, not per cell. The sequential test rejects none of the 366 wall-run cells, as does v0.1, but it leaves 14.21 \% of them not Static at the end against 1.09 \% for v0.1, and it needs a median of 33 windows to graduate one, against 10. In the opposite direction, on KITTI 05 (0.2 m voxels, ten frames per window, Periodic veto on) under the policy that exports Static voxels only, it drops more of both static and dynamic points than v0.1 (static accuracy 66.90 \% against 90.20 \%, dynamic accuracy 98.99 \% against 83.19 \%); a dynamic accuracy above that of an oracle map is reached only by dropping static points, and in this setting, under the policy that exports every voxel not Rejected, it removes no dynamic point. The engine is one geometry-free state machine for a 2D grid and a 3D voxel hash, with a FreMEn-style periodic label whose false-alarm bound holds for iid clutter; Periodic is validated on synthetic streams only. Code: <https://github.com/kjungmo/strata>.

# 1 Introduction {#sec:intro}

> **Figure 1.** **Overview of STRATA.** One geometry-free engine (top) owns every persistence and periodicity decision and sees only integer cell keys. The two backends (middle) contribute exactly two things: the point-to-key map $\kappa_b$ and the free-space ray walk. A thin ROS 2 node (bottom) selects one backend from a single string parameter and consumes, but never produces, the sensor pose. *(Drawn in the PDF with TikZ/pgfplots from the data in `paper/arxiv_ref/figures/data/`; not reproduced in this Markdown file.)*

An occupancy grid built the textbook way accumulates evidence and never lets go of it [@thrun2005probabilistic]. For a robot that drives a route once this is harmless; for one that maps the same warehouse or hospital corridor for weeks it is a slow failure. A cart parked for an afternoon, a person walking past and a load-bearing wall all leave the same kind of mark, and after enough passes the map describes the union of everything the sensor ever touched rather than the building. Lifelong mapping has to hold three requirements at once: the map must be *durable* enough to keep structure that is genuinely permanent, *plastic* enough to forget structure that has moved on, and it must do both without one global forgetting rate that is simultaneously too slow to erase movers and too fast to trust a wall—the stability–plasticity dilemma that @biber2005dynamicmaps framed for dynamic maps. A third requirement complicates the split: some structure is *cyclic*, such as a door open every morning and shut every night. Its occupancy is a periodic temporal signal, and a flat decay law erases exactly the regularity that makes it predictable [@krajnik2017fremen; @krajnik2014spectral].

Per-cell models of these requirements are not new. Temporal occupancy grids [@arbuckle2002temporal] classify cells by their occupancy over several timescales, and @mitsou2007temporal keep each cell's occupancy history to separate static, low-dynamic and high-dynamic cells. The closest prior work is the FreMEn line: @krajnik2016persistent maintain a 2D spatio-temporal occupancy grid that represents the persistence and periodicity of individual cells and was evaluated on several days of patrols in an open-plan office, and FROctomap [@krajnik2014froctomap] attaches the same spectral model to the voxels of an octree. Recent LiDAR systems place the persistence decision inside larger pipelines. ELite [@gil2025elite] updates a per-point ephemerality with a Bayes rule; LT-mapper [@kim2022ltmapper] and Khronos [@schmid2024khronos] resolve durability against plasticity as stages of full lifelong-SLAM or metric-semantic systems; and MTD-Map [@kim2026mtdmap], concurrent with this work, maintains static, dynamic and transition maps from per-voxel occupancy statistics. Dynamic-point removal methods decide the same question offline or per scan [@kim2020removert; @lim2021erasor; @duberg2024dufomap; @jia2024beautymap].

Several of these systems keep a number per cell or point that hits push up and misses push down, and compare it with a threshold. As far as we can tell from the papers, none of them states the test that the number performs: which two hit rates it tells apart, and with which error. For the log-odds accumulator of OctoMap [@hornung2013octomap] the answer is a short calculation, and it is not what a user would choose. The increments $(+0.85,-0.4)$ are the increments of Wald's sequential probability ratio test [@wald1947sequential] between the hit rates 0.1975 and 0.4621 (Section 4). A cell whose hit rate is above about a third then drifts toward Static, and a leak that shrinks the statistic every window removes the property on which a bound on the error rests. The first release of STRATA used exactly this rule, and in a synthetic test 1960 of 2000 Bernoulli(0.5) clutter cells ended Static.

We present *STRATA* 0.2, a SLAM-free persistence-and-periodicity engine in which the test is explicit (Figure 1). The user states the hit rate $m_0$ at or below which a cell is clutter, the hit rate $m_1$ at or above which it is structure, and two error rates; increments and thresholds are derived, and every cell ends Static, Rejected or Undecided. The same per-cell state machine serves a fixed-array 2D grid and a sparse 3D voxel hash, which differ only in the point-to-key map and the ray walk: the state machine never sees coordinates, the engine (`strata_core`) links no ROS, DDS, TF or PCL, and choosing 2D or 3D is one runtime string parameter. We state the scope up front: STRATA is not SLAM (it consumes an external `map`$\to$sensor transform), it models single-session persistence rather than multi-session remapping, its periodicity test checks one configured base period and its harmonics rather than selecting periods as FreMEn does, and its default pair is provisional. Our contributions are:

- *The test, made explicit.* The increments of a log-odds accumulator determine a pair of hit rates (Proposition 5). The OctoMap increments, which the first release used, are a sequential probability ratio test between 0.1975 and 0.4621 (Section 4.1), and a multiplicative leak breaks the supermartingale property that an error bound needs (Proposition 6). We did not find an earlier statement of either fact. ELite [@gil2025elite] already accumulates a likelihood ratio per point, symmetric at zero distance from the ray, and MTD-Map [@kim2026mtdmap] already maintains a per-voxel Markov statistic; we cite both, run the factors of ELite as one arm of the evaluation, and did not build an arm for MTD-Map because we found no released code.
- *Defaults derived from stated error rates.* From $(m_0,m_1,\alpha,\beta)$ the engine derives increments, thresholds, a cap and a CUSUM-like reverse test for demotion, with three outcomes instead of two. The periodicity gate is the parameter $T$ (`period_windows`), a fixed default not derived from the pair. The truncation $n_{\max}$ was computed once, offline, for the shipped pair by a pre-registered rule; the engine does not recompute it when the pair changes. Under independent windows the probability that clutter at a rate of at most $m_0$ graduates in the first test of one cell history (up to its first graduation, its first rejection or its retention as a periodic candidate) is at most $\alpha/(1-\beta)$ (Proposition 7, by Ville's inequality); restarts after a demotion are not covered. The operating characteristic measured on the engine with the Periodic veto off matches the exact lattice computation and is approximated by Wald's closed form (E7), and the committed Monte Carlo shows how autocorrelated windows inflate the rate.
- *Calibration and held-out evaluation on real data, reported as measured.* We calibrated the pair on LILocBench under a pre-registered rule with quality gates, amended after the first calibration measurement; the gate failed, nothing froze, and the shipped pair is provisional (Section 6). We then ran seven evidence rules on LILocBench, Oxford Spires, the KTH benchmark and Boreas without tuning and without choosing a winner (Section 7). The realised per-cell false-Static rates are above the nominal per-history level, and we say so.
- *One geometry-free engine for two geometries, with a periodic label that has a proved false-alarm bound for iid clutter.* A single `int64`-keyed engine (`LayeredMap` with `PeriodicityModel`) drives both backends through one `MapBackend` interface and builds with C++17 and Eigen alone under 114 behaviour-level tests. A known moment-generating-function bound [@hsu2012tail], applied to the floating-mean periodogram [@zechmeister2009gls] and spent over the touch count in the manner of alpha spending [@lan1983discrete], bounds the probability that the periodic label fires on iid clutter. The periodic label is validated on synthetic streams only.

In the synthetic suite, which runs in two arms (the first release's arithmetic and the sequential test), the sequential test graduates an always-hit wall at its derived 4th window instead of the third and labels 1120 instead of 1960 of the 2000 Bernoulli(0.5) clutter cells Static, a rate that the pair does not promise to control between its two hypotheses. On real data, the sequential test graduates fewer clutter cells than the first release's rule, waits longer to graduate walls and leaves more structure Undecided; none of the compared rules is shown to be a good removal method on these data (Section 7). Whether the periodic test, whose false-alarm bound holds for iid clutter, improves on FreMEn's own component selection, and whether it finds doors in real data, remain to be evaluated (Section 8).

# 2 Related Work {#sec:related}

**Lifelong and persistent mapping.** ELite [@gil2025elite] scores each point's ephemerality at two timescales and maintains a lifelong/static/delta map triad; its local score is updated by a Bayes rule whose factor depends on the distance of the map point from the ray, and at zero distance the odds factors printed in the paper are $1/9$ for an endpoint and $9$ for a sampled free point, a symmetric likelihood ratio with, as printed, no clamp and no leak (our arithmetic from its Eqs. (5) and (6)); LT-mapper [@kim2022ltmapper] composes live, meta and delta maps through explicit alignment, removal and change-management stages. Khronos [@schmid2024khronos] splits an active window from a long-term reconstruction with semantics, and @yang2025lifelong3d keep versioned deltas over a base map so that past sessions can be rebuilt. Classifying cells by their temporal behaviour predates these systems. Temporal occupancy grids [@arbuckle2002temporal] classify cells by occupancy over several timescales with planar laser range-finders and locate doors in a real-world setting; @mitsou2007temporal index every cell's occupancy history over time and separate static, low-dynamic and high-dynamic cells, in simulation with known poses. Multi-timescale and static/temporary map splits follow in @biber2005dynamicmaps and @meyerdelius2010temporary. @pomerleau2014longterm infer online whether each 3D point is static or dynamic from repeated observations, over seven months of data, and @berrio2021longterm describe a long-term map maintenance pipeline for autonomous vehicles. For 2D occupancy grids, @stefanini2022efficient; @stefanini2023safe update a LiDAR occupancy map for long-term operation while accounting for localisation error, with the aim of keeping the map current rather than modelling periodic change. Two 2026 works, concurrent with this one, produce layered outputs: MTD-Map [@kim2026mtdmap] keeps per voxel an instantaneous occupancy probability, a long-term occupancy probability that is an exponential moving average, and a stability statistic that grows while consecutive observations agree, builds on a mixture transition distribution, and outputs static, dynamic and transition maps from hand-set thresholds on these three quantities, evaluated on real datasets against established baselines (it states no hypothesis test and no error rate), and @chen2026layer assign map content to long-term static, potentially dynamic and removed observed-dynamic layers from geometric and semantic evidence. Neither describes an explicit periodic class. STRATA implements only the graduation half of this pattern—one per-cell state machine with no scan matching, loop closure, session alignment, semantics or object layer—and is intended as the core such systems wrap, not a competitor; correspondingly, it lacks their multi-session and field-scale evidence. Its real-data evidence is limited to the evidence rule on four public datasets (Section 7).

**Evidence rules in occupancy mapping and dynamic-point removal.** OctoMap [@hornung2013octomap] clamps the log-odds of a voxel between a lower and an upper bound so that the map can adapt, with the sensor-model increments $0.85$ and $-0.4$, and the sensor model of the first release of STRATA is that pair; UFOMap [@duberg2020ufomap] adds an explicit unknown state. Removert [@kim2020removert] conservatively retains definite static points and then reverts wrongly removed ones; ReFusion [@palazzolo2019refusion] treats a reliably observed empty voxel as evidence, and STRATA uses free-space misses along each ray as the evidence that demotes a graduated cell, online and per cell; ERASOR [@lim2021erasor] removes dynamic points from a ratio of pseudo-occupancy per polar bin, the ratio of the height ranges of the points in a bin of a scan and of the map, whose threshold of $0.2$ is a ratio of heights and not a hit rate; DUFOMap [@duberg2024dufomap] marks a region as void once it has been observed empty and calls every point later found inside it dynamic, a one-shot rule without a rate; and BeautyMap [@jia2024beautymap] decides on a binary-encoded ground matrix. The KTH benchmark [@zhang2023dynamic] scores such methods on the retained static points (SA) and the removed dynamic points (DA) at point level, and BeautyMap defines the harmonic mean HA beside the geometric mean AA that the benchmark uses. The methods above decide with a threshold or a one-shot rule. STRATA decides with a rate-based sequential test whose hypotheses and error rates are parameters, and its evaluation (Section 7) is not a removal benchmark.

**Sequential analysis.** Wald's sequential probability ratio test [@wald1947sequential] stops as soon as the cumulative log-likelihood ratio crosses one of two thresholds; @wald1948optimum prove that, among tests with the same error probabilities, it needs the fewest observations on average. Page's cumulative-sum procedure [@page1954continuous] uses a reflecting lower barrier at zero; as @lorden1971procedures recalls, Page observed that it is equivalent to repeating an SPRT on successive observations until it decides for the alternative. The engine of Section 4 differs from both: it has a floor only after a gate, and it ends a history when it rejects. Its demotion test is CUSUM-like: the cap reflects the statistic from above, and the test has no error bound per history. Ville's inequality bounds the probability that a nonnegative supermartingale ever exceeds a level; @howard2020timeuniform state it as their Lemma 1 and note that the likelihood ratio, stopped above a threshold only, is an open-ended SPRT with a valid type-I error, and @ramdas2023game survey the resulting anytime-valid tests. We found no result that gives error rates for a Wald test with a multiplicative leak; leaky accumulators appear in models of decision making [@bogacz2006physics].

**Persistence, forgetting and periodicity.** The Persistence Filter [@rosen2016persistence] estimates each feature's survival probability from a Bayesian survival-time model, and the Markov-chain occupancy models of @tipaldi2013lifelong and @saarinen2012imac learn per-cell transition rates. Perpetua [@saavedraruiz2025perpetua] combines persistence and emergence filters under multiple hypotheses for features that disappear and reappear. FreMEn [@krajnik2017fremen], descending from spectral analysis of long-term maps [@krajnik2014spectral], models cyclic occupancy with a few Fourier components selected by amplitude from a set of candidate periods, and it is the closest prior work to STRATA. @krajnik2016persistent use it in a 2D spatio-temporal occupancy grid in which each cell represents both persistence (through the mean time between state changes) and periodicity; the grid integrates one map per patrol, replaces the ROS map server, and predicts time-specific maps for localisation and planning. FROctomap [@krajnik2014froctomap] applies the FreMEn spectral model to 3D octree voxels, the exploration work of @krajnik2015exploration displays the static and daily-periodic cells of such a grid, and the open-source FreMEn repository [@fremenrepo] provides separate 2D-grid and octree packages, and also a generic server that maintains FreMEn models of arbitrary binary states keyed by an identifier. Warped hypertime [@krajnik2019warped] extends the idea to pseudo-periodic variation. STRATA is narrower than this family in period modelling: it tests one configured base period and its harmonics and does not select periods. It differs from the family in two respects: its primary output is a discrete class, reached through a hysteresis band and pruning, rather than a predicted occupancy probability; and it attaches a finite-sample false-alarm bound to the periodic label. It also updates once per window, whereas the grid of @krajnik2016persistent integrates one map per patrol. In its legacy evidence model STRATA uses the log-odds update of @thrun2005probabilistic, clamped as in OctoMap [@hornung2013octomap], and replaces the survival posterior with one global multiplicative decay (a constant-rate special case); the default model drops the decay (Section 4). In both models it runs a bounded FreMEn-style accumulator in parallel with graduation so that an oscillating cell is labelled Periodic instead of being mis-graduated or mis-pruned.

**Map data structures and ROS 2 tooling.** OctoMap [@hornung2013octomap] and UFOMap [@duberg2020ufomap] share the clamped log-odds core but compress space with octrees, the latter with an explicit unknown state; the voxel hash of @niessner2013hashing is the structure `voxel3d` follows. Nav2's spatio-temporal voxel layer [@macenski2020stvl] decays costmap evidence for planning, layered costmaps [@lu2014layered] composite per-cell layers, and SLAM Toolbox [@macenski2021slamtoolbox] is a 2D SLAM library that includes prototype lifelong mapping. For swappable backends, ROS 2 offers `pluginlib` [@ros2pluginlib]; STRATA deliberately uses one interface and a two-way factory instead. Relative to the works above, the structural difference is narrow. Periodicity modelling that does not depend on geometry already exists, since the FreMEn server models any binary state that has an identifier [@fremenrepo]. What STRATA shares, unchanged, between its 2D and 3D ray-casting backends is the full cell-level state machine: the evidence rule (the sequential test of Section 4, the default of version 0.2, or the first release's windowed log-odds with survival decay), graduation and demotion, pruning, layer classification and the periodicity test.

**Periodicity tests and error control.** The periodic test uses the generalised Lomb–Scargle periodogram [@zechmeister2009gls]. Analytic false-alarm probabilities for periodogram peaks, including the search over frequencies, are derived under white Gaussian noise [@baluev2008]. For binary series, @schmidtke2026binary test a constant success probability against a periodic one of unspecified period, with a level that holds asymptotically. STRATA addresses a narrower question: a finite-sample, distribution-free bound for independent Bernoulli occupancy at one configured period and its harmonics, under any sampling pattern fixed independently of the occupancy. Proposition 3 obtains it from the moment-generating-function bound for quadratic forms of sub-Gaussian vectors of @hsu2012tail [Remark 2], followed by Markov's inequality and a union bound, with the variance proxy of Hoeffding's lemma [@hoeffding1963]; the result is an application of that bound, not a new inequality. Spending the level over the touch count (Proposition 4) is a Bonferroni-type allocation over repeated looks, related to the alpha-spending functions of group-sequential trials [@lan1983discrete]; time-uniform bounds from nonnegative supermartingales [@howard2020timeuniform] are the modern alternative for repeated testing. Section 4 uses them for the evidence test; the periodic label keeps the Bonferroni-type allocation.

# 3 STRATA: A Geometry-Free Engine Beneath Two Map Geometries {#sec:method}

STRATA is delivered as two ROS 2 packages with a hard dependency boundary. `strata_core` holds the entire occupancy, persistence and periodicity logic together with both geometry backends, in C++17 with Eigen as its only third-party dependency. `strata` is a thin node that owns message conversion, TF lookups, publishers, a save service, file I/O and input diagnostics (Tables 27 and 28). This section specifies the engine exactly as implemented: every equation is transcribed from `strata_core`, and §3.7 lists where the code departs from the design document. Table 31 collects the symbols. The engine has two evidence models. `evidence_model = legacy` is the rule of the first release, specified in §3.2 and 3.3. `evidence_model = sprt` is the default of version 0.2, a sequential probability ratio test specified in Section 4. Geometry, windows, the periodicity test, pruning and the read-out are shared by both models.

**Problem Setup: One Classifier, Any Geometry.** A sensor sweep at integration tick $k$ delivers an origin $\mathbf{o}_k\in\mathbb{R}^3$ and a set of hit endpoints $\mathcal{P}_k\subset\mathbb{R}^3$, both already in the map frame (the pose comes from an external localizer). A backend $b\in\{\mathrm{2D},\mathrm{3D}\}$ supplies two operators only: a key map $\kappa_b:\mathbb{R}^3\to\mathcal{C}\cup\{\bot\}$ onto integer cell ids $\mathcal{C}\subset\mathbb{Z}$, and a ray walk $\rho_b(\mathbf{o},\mathbf{p})\subset\mathcal{C}$ listing the free cells between origin and endpoint. Ticks are grouped into windows $w=1,2,\dots$. Each live cell $i$ carries a state $\mathbf{x}_i=(\ell_i,N_i,g_i,\boldsymbol{\phi}_i)$—log-odds occupancy, touched-window count, graduated flag and Fourier accumulators—and the map reads out a class $c_i\in\{\mathsf{U},\mathsf{T},\mathsf{P},\mathsf{S}\}$ (Unknown, Transient, Periodic, Static); the sequential model adds a fifth read-out, Intermittent (Section 4). The design objective is that the per-window map update factor as

$$
\mathcal{X}_w=\Phi\big(\mathcal{X}_{w-1},\,\Gamma_b(\{(\mathbf{o}_k,\mathcal{P}_k)\}_{k\in w})\big),\qquad c_i=\Psi(\mathbf{x}_i), \tag{1}
$$

where only the evidence extractor $\Gamma_b$ depends on the backend, while the engine $\Phi$ and the read-out $\Psi$ are written once and see nothing but integer ids. The classes should make walls *durable* ($\mathsf{S}$ persists under clutter), movers *plastic* ($\mathsf{T}$ cells are forgotten), and cyclic structure *periodic* ($\mathsf{P}$ is neither baked into $\mathsf{S}$ nor pruned). The stages below instantiate $\Gamma_b$, $\Phi$ and $\Psi$; Algorithm 1 gives the complete window update.

## 3.1 Keying Observations: Reducing Geometry to Integer Cell Ids {#sec:keying}

For every endpoint $\mathbf{p}\in\mathcal{P}_k$ that the backend accepts ($\kappa_b(\mathbf{p})\ne\bot$) it registers one miss on each cell of $\rho_b(\mathbf{o}_k,\mathbf{p})$ and then one hit on $\kappa_b(\mathbf{p})$; an endpoint it does not accept contributes nothing, not even its ray. The engine keeps only per-window counters,

$$
h_{i,w}=\sum_{k\in w}\sum_{\mathbf{p}\in\mathcal{P}_k}\big[\kappa_b(\mathbf{p})=i\big],\qquad
  m_{i,w}=\sum_{k\in w}\sum_{\mathbf{p}\in\mathcal{P}_k}\big|\{j\in\rho_b(\mathbf{o}_k,\mathbf{p}) : j=i\}\big|, \tag{2}
$$

creating a cell lazily on first touch with $\ell_i=0$, $N_i=0$, $g_i=\text{false}$. This is $\Gamma_b$; it is the only backend-specific code.

**Fixed 2D grid.** `Grid2DBackend` addresses an array of $W$ columns and $R$ rows with resolution $r$ and origin $(x_0,y_0)$, drops $z$, and maps a point to a row-major index,

$$
\kappa_{\mathrm{2D}}(\mathbf{p})=\Big\lfloor\tfrac{p_y-y_0}{r}\Big\rfloor W+\Big\lfloor\tfrac{p_x-x_0}{r}\Big\rfloor, \tag{3}
$$

returning $\bot$ outside the array. An endpoint outside the array is dropped together with its ray: a beam that ends beyond the array registers no hit and clears no cell, including the cells it crosses inside the array. $\rho_{\mathrm{2D}}$ is integer Bresenham [@bresenham1965] from the sensor cell up to but excluding the hit cell, so the sensor cell is included and the hit cell receives only its hit. A sweep whose origin lies outside the array registers its hits but clears no ray. The node adds a filter of its own before the backend: its scan adapter drops every beam whose range is not finite or lies outside $[\texttt{range\_min},\texttt{range\_max}]$, so a beam without a return clears nothing, and a reading equal to `range_max` is kept as a hit.

**Sparse 3D hash.** `Voxel3DBackend` floor-divides each axis by the voxel size $v$, offsets it by $2^{20}$ to keep it non-negative, and packs the three 21-bit fields into one 64-bit key,

$$
\kappa_{\mathrm{3D}}(\mathbf{p})=\big(u_x\ll 42\big)\,\big|\,\big(u_y\ll 21\big)\,\big|\,u_z,\qquad u_a=\lfloor p_a/v\rfloor+2^{20}. \tag{4}
$$

The map grows through the hash and needs no preallocated extent. Its key space is nevertheless finite: each 21-bit field represents the indices $-2^{20}$ to $2^{20}-1$ around the origin, far beyond the extent of a building at the default voxel size, and the code does not check this range, so a point outside it would receive a wrong key rather than being dropped. $\rho_{\mathrm{3D}}$ is a sample march rather than an exact voxel traversal: with $\mathbf{d}=\mathbf{p}-\mathbf{o}$ and $S=\lfloor\|\mathbf{d}\|/(v/2)\rfloor$ half-voxel steps, the interior samples $\mathbf{o}+s\,\mathbf{d}/\max(S,1)$, $s=1,\dots,S{-}1$, are hashed with (4). This is simpler than exact traversal but can visit a voxel more than once along a long ray and can skip a thin one when $S$ rounds down.

## 3.2 Accumulating Evidence: Windowed Log-Odds with Survival Decay (the v0.1 Rule) {#sec:logodds}

This subsection and the next specify the evidence rule of the first release, `evidence_model = legacy`, which the experiments run as the *legacy arm*. The default model replaces Eq. (6) by the sequential test of Section 4; windows, scoring and everything that follows the evidence update are shared.

A window closes on integration tick $k$ when $L\le 1$ or $k \bmod L=0$, with $L$ the `layer_interval`. The ROS node can instead close windows every `window_period_s` seconds of message time (`window_mode` `time`, the shipped setting; the code default and every experiment use $L$): windows are counted from the first header stamp, a message stamped past the open window first closes it and every silent window since, and a message with a zero stamp is dropped. A window that observes nothing changes no cell, so a silent gap of $k$ windows is exactly $k$ updates done in one pass; silent windows advance the phase without a touch, so within one uninterrupted stream of message time, and as long as message loss and TF drops are independent of occupancy, the false-alarm bound of §3.4 holds in either mode and only the period, and with it the power, depends on the window rule. A stamp more than half a window behind the newest one (a bag loop, a clock reset) or more than $10^6$ windows ahead (an unset stamp followed by a real one) closes the open window and re-anchors the window clock, while cell histories carry on; neither the bound nor the persistence evidence holds across such a jump, so restart the node rather than loop a bag. Scan-counted windows last $L$ divided by the rate of messages actually integrated (§8). The update then scores every live cell once, so a single hit outweighs any number of misses in the same window:

$$
\tau_{i}=[h_{i,w}>0\lor m_{i,w}>0],\qquad o_{i}=[h_{i,w}>0],\qquad f_{i}=[h_{i,w}=0\land m_{i,w}>0]. \tag{5}
$$

For *touched* cells only ($\tau_i=1$), one log-odds increment is applied, multiplied by the survival decay $\lambda$, and clamped:

$$
\ell_i\leftarrow\min\!\Big(l_{\max},\max\!\big(l_{\min},\;\lambda\,(\ell_i+o_i\,l_{\mathrm{hit}}+f_i\,l_{\mathrm{miss}})\big)\Big),\qquad N_i\leftarrow N_i+1, \tag{6}
$$

with occupancy probability $p_i=\sigma(\ell_i)=1/(1+e^{-\ell_i})$. The update is the inverse-sensor-model rule of @thrun2005probabilistic with a constant-rate special case of the Persistence Filter's forgetting [@rosen2016persistence]. The order matters: the decay multiplies the *already incremented* value, so the fresh evidence is itself attenuated in the same window.

**Proposition 1 (Saturation under repeated hits).** If a cell is hit in every window, $0<\lambda<1$, $l_{\mathrm{hit}}>0$ and $l_{\min}\le0<l_{\max}$, then $\ell_i$ converges monotonically from $0$ to $\min\!\big(l_{\max},\,\ell^\star\big)$ with $\ell^\star=\lambda l_{\mathrm{hit}}/(1-\lambda)$.

*Proof.* Without the clamp the recursion is $\ell\mapsto\lambda(\ell+l_{\mathrm{hit}})$, an affine contraction with factor $\lambda$ and unique fixed point $\ell^\star$; from $\ell=0<\ell^\star$ the iterates increase monotonically towards it. The clamp is monotone and, since every iterate is non-negative and $l_{\min}\le0$, only the upper bound can bind, so the clamped iterates increase to $\min(l_{\max},\ell^\star)$. $\square$

With the v0.1 defaults (Table 25, legacy group; $\lambda{=}0.97$, $l_{\mathrm{hit}}{=}0.85$) $\ell^\star\approx 27.5$, so the clamp binds at $l_{\max}{=}5$ and $p_{\max}=\sigma(5)\approx 0.9933$. The default thresholds $p_{\mathrm{grad}}{=}0.8$ and $p_{\mathrm{prune}}{=}0.05$ correspond to $\ell\ge\ln 4\approx 1.386$ and $\ell<\ln(1/19)\approx-2.944$.

## 3.3 Gating Graduation: A Schmitt Trigger Against Flicker {#sec:schmitt}

After the log-odds step, $p_i$ and the periodicity amplitude $a_i$ (§3.4) are recomputed for *every* cell, touched or not, and the graduated flag follows a two-sided Schmitt trigger with two periodicity guards, evaluated in this order:

$$
\begin{aligned}
\text{graduate:}\quad & \lnot g_i\land\lnot\pi_i\land p_i\ge p_{\mathrm{grad}}\land N_i\ge N_{\min} \;\Rightarrow\; g_i\leftarrow\text{true},\\
  \text{demote:}\quad & g_i\land\big(p_i\le p_{\mathrm{dem}}\lor\pi_i\big)\;\Rightarrow\; g_i\leftarrow\text{false},
\end{aligned} \tag{7}
$$

where $\pi_i$ is the periodic predicate of (13). The interval $[p_{\mathrm{dem}},p_{\mathrm{grad}}]$ is the hysteresis band: through the occupancy disjunct $p_i\le p_{\mathrm{dem}}$, a Static cell demotes only after sustained contradicting evidence, not after one contradicting window—the failure that Removert's revert pass repairs after the fact [@kim2020removert] is prevented online. The guard $\neg\pi_i$ keeps a periodic cell out of the static map; the term $\vee\,\pi_i$ removes a Static cell that later reveals periodicity. That term lies outside the band and has no hysteresis of its own. It cannot, however, fire without contradicting evidence: with the centred amplitude of §3.4, $\pi_i$ requires a cell to have been observed free in a substantial share of its touched windows (Remark 2), so a wall that is occupied whenever it is seen is never demoted by it. The predicate also requires the noise-calibrated significance of Proposition 3, with its level spent over the touch count. A graduated cell is never pruned, so by Proposition 4 a wall whose occupancy is merely iid noise is demoted by this term with probability at most $\delta$ for as long as its history is not interrupted; a noisy wall that the occupancy disjunct demotes can afterwards be pruned and re-created, and the new history has a budget of its own. The term still acts in a single window when it does fire; E3 measures its cost in flicker with periodicity on (§5.4).

**Remark 1 (Which occupancy rates graduate).** The graduation rule sees a cell only through its log-odds. Take a cell that is touched in every window and occupied independently with probability $m$. While the clamp is inactive, (6) reads $\ell\leftarrow\lambda(\ell+x)$ with $\mathbb{E}\,x=m\,l_{\mathrm{hit}}+(1-m)\,l_{\mathrm{miss}}$. For $\lambda<1$ the mean of $\ell$ converges to $\lambda\,\mathbb{E}\,x/(1-\lambda)$, which lies above the graduation threshold $\operatorname{logit}p_{\mathrm{grad}}$ exactly when

$$
m>m^\dagger=\frac{\tfrac{1-\lambda}{\lambda}\operatorname{logit}p_{\mathrm{grad}}-l_{\mathrm{miss}}}{l_{\mathrm{hit}}-l_{\mathrm{miss}}}. \tag{9}
$$

For $\lambda=1$ the same expression is the rate $-l_{\mathrm{miss}}/(l_{\mathrm{hit}}-l_{\mathrm{miss}})$ above which the walk drifts upward. The v0.1 defaults give $m^\dagger=0.354$ and the E2 parameters $m^\dagger=0.5$. This is a statement about a mean, not a bound: it ignores the clamp and pruning, and fluctuations graduate cells below $m^\dagger$; E6 measures the transition (Table 12). Its consequence is that Static means no more than *occupied in more than roughly $m^\dagger$ of the touched windows*. The engine holds no evidence that separates a wall seen in 60\% of its windows from clutter present in 50\% of them. Section 4.1 gives the reason: the increments are a sequential probability ratio test whose hypotheses nobody chose.

**Proposition 2 (No same-window oscillation).** If $p_{\mathrm{dem}}<p_{\mathrm{grad}}$, a cell that graduates by (7) in a window is not demoted by (8) in the same window.

*Proof.* Graduation requires $\neg\pi_i$ and $p_i\ge p_{\mathrm{grad}}>p_{\mathrm{dem}}$, and $p_i,\pi_i$ are not recomputed between (7) and (8), so both disjuncts of the demotion condition are false. $\square$

## 3.4 Detecting Periodicity: A FreMEn-Lite Amplitude Test {#sec:fremen}

In parallel with occupancy, every touched cell feeds an incremental Fourier model following FreMEn [@krajnik2017fremen]. The model is restricted to one configured base period and its first harmonics; unlike FreMEn, it does not select periods from a candidate set, so a cycle whose frequency is not a harmonic of the base frequency is outside its scope. With window index $w$, base period $T$ (in windows), $H$ harmonics, $\omega=2\pi/\max(1,T)$, occupancy sample $o_i\in\{0,1\}$ and $\theta_k=(k{+}1)\omega w$, the accumulators $\boldsymbol{\phi}_i=(n_i,S_{0,i},C_{k,i},S_{k,i},E^{c}_{k,i},E^{s}_{k,i})$ evolve as

$$
n_i\mathrel{+}=1,\quad S_{0,i}\mathrel{+}=o_i,\quad C_{k,i}\mathrel{+}=o_i\cos\theta_k,\quad S_{k,i}\mathrel{+}=o_i\sin\theta_k,\quad E^{c}_{k,i}\mathrel{+}=\cos\theta_k,\quad E^{s}_{k,i}\mathrel{+}=\sin\theta_k, \tag{10}
$$

for $k=0,\dots,H{-}1$. Here $n_i$ counts *touched* windows, not elapsed ones, and $E^{c},E^{s}$ record the phases at which the cell was touched. With the touched-window mean $\bar{o}_i=S_{0,i}/n_i$, the *centred* coefficients are $u_k=2(C_{k,i}-\bar{o}_iE^{c}_{k,i})/n_i$ and $v_k=2(S_{k,i}-\bar{o}_iE^{s}_{k,i})/n_i$, i.e. $u_k+\mathrm{i}v_k=\tfrac{2}{n_i}\sum_{w\in\mathcal{W}_i}(o_{i,w}-\bar{o}_i)\,e^{\mathrm{i}\theta_k}$ over the touched windows $\mathcal{W}_i$, the mean-removed spectrum of FreMEn. The harmonic amplitudes and the dominant amplitude are

$$
a_{i,k}=\sqrt{u_k^2+v_k^2},\qquad
  a_i=\begin{cases}0, & n_i<T,\\ \max_{0\le k<H}a_{i,k}, & n_i\ge T,\end{cases} \tag{11}
$$

and the periodic predicate $\pi_i$, which adds a significance test to the amplitude threshold, is given in (13) below.
The phase prediction $\hat{p}_i(w)=\mathrm{clip}_{[0,1]}\big(S_{0,i}/n_i+\sum_k[u_k\cos((k{+}1)\omega w)+v_k\sin((k{+}1)\omega w)]\big)$ is exposed for downstream use. The gate $n_i\ge T$ withholds a harmonic estimate until a period's worth of touches exists; a sparsely observed cell needs $T$ *touches*, which can span many more than $T$ windows.

**Remark 2 (Centring, and the defect it corrects).** Centring bounds the amplitude by the occupancy variation. Since $|o_{i,w}-\bar{o}_i|$ averages $2\bar{o}_i(1-\bar{o}_i)$ over $\mathcal{W}_i$, the triangle inequality gives

$$
a_i\;\le\;4\,\bar{o}_i(1-\bar{o}_i)
$$

under any sampling of $\mathcal{W}_i$. A cell that is occupied (or free) in every touched window therefore has $a_i=0$, and $\pi_i$ with $a_{\min}{=}0.3$ requires $0.081<\bar{o}_i<0.919$: at least about 8\% of its touched windows must be free. The code as first released (v0.1.0, commit `a82f935`; the package version string has not been raised since) omitted $E^{c},E^{s}$, so its coefficients were uncentred. For an always-occupied cell, $a_i$ was then twice the mean phase vector of $\mathcal{W}_i$: it measured *when* the cell was observed, not how its occupancy varied. For $n$ consecutive touches this is

$$
a_i=\max_{0\le k<H}\frac{2\,\big|\sin\big(n(k{+}1)\pi/T\big)\big|}{n\,\big|\sin\big((k{+}1)\pi/T\big)\big|}\;\le\;\frac{2}{n\sin(\pi/T)}.
$$

It exceeds $a_{\min}$ at $n=10$–$13$ for $T{=}8$ (peak 0.439 at $n{=}11$; the bound clears only from $n{=}18$), and at $n=29$–$40$ for the shipped $T{=}24$, $H{=}2$ (bound: $n\ge52$). Through the $\vee\,\pi_i$ term of (8), whose predicate was then $a_i\ge a_{\min}$ alone, a wall hit every window therefore left the Static layer for 120 integration ticks at $L{=}10$. Uneven phase coverage made this permanent: a wall seen in 8 of every 48 windows had $a_i\approx1.66$. The corrected engine adds the two phase sums, 16 bytes per harmonic (the significance test below adds two more), and the per-cell cost order is unchanged. Ten regression tests cover both failures and genuine detection; seven of them fail on v0.1.0 and all pass after the fix (§5.1), and Table 6 compares E1–E3 before and after.

**Calibrating the Periodic Test Against Noise.** Centring removes the bias of the amplitude but not its variance, and $a_i\ge a_{\min}$ alone is not a test. If a cell's occupancy is pure noise, $o_{i,w}\sim\mathrm{Bernoulli}(m)$ independently of the phase, each centred coefficient has variance $2m(1{-}m)/n_i$ under uniform phase coverage, a standard deviation of $\sqrt{0.5/8}=0.25$ at $m{=}\tfrac12$, $n_i{=}T{=}8$. A fixed $a_{\min}$ is then crossed by chance at short lengths, and pruning (§3.5) keeps returning cells to short lengths. We therefore test each harmonic for significance. Write $c_w=\cos\theta_k$, $s_w=\sin\theta_k$ over the touched windows, $\tilde{\mathbf{z}}_w=(c_w-\bar c,\,s_w-\bar s)^{\top}$ for the centred phase vectors, $\mathbf{y}_{i,k}=\sum_{w\in\mathcal{W}_i}(o_{i,w}-\bar o_i)\,\tilde{\mathbf{z}}_w=\tfrac{n_i}{2}(u_k,v_k)^{\top}$ and $\mathbf{M}_{i,k}=\sum_{w}\tilde{\mathbf{z}}_w\tilde{\mathbf{z}}_w^{\top}$. Then

$$
d_{i,k}=\mathbf{y}_{i,k}^{\top}\mathbf{M}_{i,k}^{-1}\mathbf{y}_{i,k},\qquad \Theta(r)=\begin{cases}2r\,e^{1-2r}, & r>\tfrac12,\\ 1, & r\le\tfrac12,\end{cases} \tag{12}
$$

where $d_{i,k}$ is the drop in squared residual when a sinusoid at harmonic $k$ is fitted with a floating mean, the generalised Lomb–Scargle periodogram [@zechmeister2009gls]. $\mathbf{M}_{i,k}$ needs two more accumulators per harmonic, $\sum\cos2\theta_k$ and $\sum\sin2\theta_k$, since $\sum c_w^2=\tfrac12(n_i+\sum\cos2\theta_k)$ and $\sum c_ws_w=\tfrac12\sum\sin2\theta_k$. The periodic predicate requires, for some harmonic, both an effect size and significance at a nominal level $\delta$ (`periodic_false_alarm`):

$$
\pi_i=\texttt{enable\_periodicity}\land n_i\ge T\land\exists k:\ \big[a_{i,k}\ge a_{\min}\ \land\ H\,\Theta(d_{i,k})\le\delta\big]. \tag{13}
$$

Proposition 3 bounds the false-alarm probability of this predicate. It is an application of known results, not a new inequality: the moment-generating-function step is the bound of @hsu2012tail [Remark 2] for a quadratic form of a sub-Gaussian vector, here with the identity matrix and Hoeffding's variance proxy $\tfrac14$, and the rest is Markov's inequality and a union bound over the harmonics. We state it with the explicit constants that the shipped test uses.

**Proposition 3 (Calibrated false-alarm bound).** Let the touched windows $\mathcal{W}_i$ ($|\mathcal{W}_i|=n$) be fixed independently of the occupancy, and let $o_{i,w}$, $w\in\mathcal{W}_i$, be independent $\mathrm{Bernoulli}(m)$ for one unknown $m\in[0,1]$. If $\mathbf{M}_{i,k}$ is non-singular, then $\Pr(d_{i,k}\ge r)\le \Theta(r)$ for every $r>0$. Consequently, for every $a_{\min}\ge0$ and $0<\delta<H$, the predicate (13) is true with probability at most $\delta$.

*Proof.* Because $\sum_w\tilde{\mathbf{z}}_w=\mathbf{0}$, the unknown mean drops out: $\mathbf{y}_{i,k}=\sum_w\xi_w\tilde{\mathbf{z}}_w$ with $\xi_w=o_{i,w}-m$. Put $\mathbf{b}_w=\mathbf{M}_{i,k}^{-1/2}\tilde{\mathbf{z}}_w$ and $\mathbf{u}=\sum_w\xi_w\mathbf{b}_w$, so that $d_{i,k}=\|\mathbf{u}\|^2$ and $\sum_w\mathbf{b}_w\mathbf{b}_w^{\top}=\mathbf{I}_2$. Each $\xi_w$ has mean zero and lies in an interval of length 1, so Hoeffding's lemma [@hoeffding1963] gives $\mathbb{E}\,e^{t\xi_w}\le e^{t^2/8}$, and by independence $\mathbb{E}\,e^{\mathbf{v}^{\top}\mathbf{u}}\le\exp\big(\tfrac18\sum_w(\mathbf{v}^{\top}\mathbf{b}_w)^2\big)=e^{\|\mathbf{v}\|^2/8}$ for every $\mathbf{v}\in\mathbb{R}^2$. Following @hsu2012tail, let $\mathbf{g}\sim\mathcal{N}(\mathbf{0},\mathbf{I}_2)$ be independent of $\mathbf{u}$. For $0\le\eta<2$, $\mathbb{E}_{\mathbf{g}}\,e^{\sqrt{2\eta}\,\mathbf{g}^{\top}\mathbf{u}}=e^{\eta\|\mathbf{u}\|^2}$, so exchanging the expectations,

$$
\mathbb{E}\,e^{\eta d_{i,k}}=\mathbb{E}_{\mathbf{g}}\,\mathbb{E}_{\mathbf{u}}\,e^{\sqrt{2\eta}\,\mathbf{g}^{\top}\mathbf{u}}\le\mathbb{E}_{\mathbf{g}}\,e^{\eta\|\mathbf{g}\|^2/4}=(1-\eta/2)^{-1}.
$$

Markov's inequality gives $\Pr(d_{i,k}\ge r)\le(1-\eta/2)^{-1}e^{-\eta r}$, and $\eta=2-1/r$ (admissible for $r>\tfrac12$) yields $2r\,e^{1-2r}$; for $r\le\tfrac12$ the bound 1 is trivial. $\Theta$ decreases strictly from 1 to 0 on $(\tfrac12,\infty)$, so $H\,\Theta(d_{i,k})\le\delta$ holds exactly when $d_{i,k}\ge r^\star$ with $\Theta(r^\star)=\delta/H$. The union bound over the $H$ harmonics then gives $\Pr(\pi_i)\le H\cdot\delta/H=\delta$. $\square$

The bound needs no knowledge of $m$: the sub-Gaussian constant $\tfrac14$ is the Bernoulli worst case $m=\tfrac12$. When $\mathbf{M}_{i,k}$ is singular (for instance at a Nyquist harmonic), the code projects onto its range; the same argument with a one-dimensional $\mathbf{g}$ gives the smaller bound $(1-\eta/2)^{-1/2}e^{-\eta r}$, so $\Theta$ stays valid. Under uniform phase coverage at a harmonic with $2(k{+}1)\not\equiv0\pmod T$ (so that the sine column does not vanish; the defaults $T/H\in\{8/3,24/2\}$ satisfy it) $\mathbf{M}_{i,k}=\tfrac{n}{2}\mathbf{I}_2$ and $d_{i,k}=n\,a_{i,k}^2/2$, so (13) becomes an amplitude threshold that shrinks with $n$,

$$
a_{i,k}\ \ge\ \max\big(a_{\min},\ a^\star(n)\big),\qquad a^\star(n)=\sqrt{2r^\star/n}. \tag{14}
$$

For $\delta{=}0.1$ and $H{=}3$, $r^\star=3.115$, so $a^\star(8)=0.883$ and $a^\star(64)=0.312$. The bound is conservative: in the Gaussian limit at $m{=}\tfrac12$, $\Pr(d_{i,k}\ge r)\to e^{-2r}$, a factor $2e\,r$ below $\Theta(r)$, or about 17 at $r^\star$. We therefore first treated $\delta$ as a nominal level for one read-out and chose it on a calibration set disjoint from every evaluation seed (E0, §5.1). The pre-stated rule took the largest $\delta\in\{0.01,0.02,0.05,0.1,0.2\}$ whose measured per-read-out false-alarm rate stays at or below 0.01, both over a null grid and in the live pipeline, and it selected $\delta{=}0.1$ (Table 7). That rule compares point estimates: the worst pipeline rate at $\delta{=}0.1$, 7 of 1000 cells, has a 95\% Wilson upper limit of 0.0144 [@wilson1927], so the calibration does not establish the 0.01 target with confidence. Only $\delta$ is held out; the test itself was introduced after the amplitude-only rule produced false positives on the E2 seed, so E2 is not a blind evaluation of its design. More importantly, one read-out is not what the map does.

**Spending the Level Over Repeated Read-Outs.** The map evaluates (13) after every window, so a clutter cell gets one chance per read-out, and pruning (16) erases its history and starts another. E5 measures the consequence: at a single-read-out level of 0.1, up to 953 of 2000 Bernoulli clutter cells are labelled Periodic at some window within 1024 windows (§5.3). The shipped rule therefore replaces the constant level $\delta$ in (13) by a level that decreases with the touch count,

$$
\delta_n=\delta\,\frac{T}{n(n+1)}\quad(n\ge T),\qquad \sum_{n\ge T}\delta_n=\delta , \tag{15}
$$

where the sum telescopes because $T/(n(n{+}1))=T/n-T/(n{+}1)$. This is a Bonferroni-type allocation of the level over the sequence of read-outs, in the spirit of the alpha-spending functions used for repeated significance tests in group-sequential trials [@lan1983discrete]; Proposition 4 is the union bound over it.

**Proposition 4 (Trajectory-level false-alarm bound).** Call a *history* of cell $i$ its touched windows from its creation until it is pruned or the run ends. Suppose that, given everything observed before the history begins, its touched windows are fixed independently of its occupancy and its occupancy samples are independent $\mathrm{Bernoulli}(m)$. With the level $\delta_n$ of (15) in (13), the probability that $\pi_i$ holds at some read-out of the history is at most $\delta$, whatever the length of the run. If $L_N$ histories of the cell begin in windows $1,\dots,N$, the probability that the cell is labelled Periodic at some window up to $N$ is at most $\delta\,\mathbb{E}[L_N]$.

*Proof.* Condition on everything observed before the history begins; under the hypothesis the history is then a fixed-design sample. For $n\ge T$ let $D_n$ be the event that the statistic of its first $n$ samples, computed as if the cell were never pruned, satisfies (13) at level $\delta_n$. Proposition 3 with $\delta_n$ in place of $\delta$ gives $\Pr(D_n)\le\delta_n$. A read-out at touch count $n$ evaluates $\pi_i$ on exactly these $n$ samples (read-outs between two touches repeat the last value), and it happens only if the history survived to $n$, so the event that $\pi_i$ ever holds is contained in $\bigcup_{n\ge T}D_n$. The union bound gives at most $\sum_{n\ge T}\delta_n=\delta$; pruning only removes read-outs. For the second claim, the $\ell$-th history begins at a window determined by the past, so the probability that it begins by $N$ and fires is at most $\delta$ times the probability that it begins by $N$; summing over $\ell$ gives $\delta\,\mathbb{E}[L_N]$. $\square$

The proposition is strongest where pruning does not act. A graduated cell is exempt from (16), so while a wall stays Static its history is not interrupted, and the probability that the $\vee\,\pi_i$ term of (8) ever demotes it on iid noise during that history is at most $\delta$. A noisy wall can still be demoted through the occupancy disjunct, pruned and re-created (Bernoulli(0.6) cells begin 5 histories in 1024 windows under the E2 parameters, Table 32), so over a whole run the bound for a wall is $\delta\,\mathbb{E}[L_N]$ as well. For clutter that is pruned and re-created, the bound grows with the number of histories, which depends on the data: a Bernoulli(0.4) cell under the E2 parameters begins about 62 histories in 1024 windows, and $\delta\,\mathbb{E}[L_N]$ is then vacuous. That regime is measured (E5), not guaranteed. The null is still that of Proposition 3: temporally correlated clutter violates it, and E5 shows that the test flags such clutter. The shipped level is chosen on separate calibration seeds, now at trajectory level. The pre-stated rule takes the largest $\delta\in\{0.01,0.02,0.05,0.1,0.2\}$ for which the rate of ever labelling a Bernoulli clutter cell Periodic within 1024 windows has a Wilson 95\% upper limit [@wilson1927] of at most 0.01 in every calibration configuration. It selects $\delta{=}0.2$, the largest candidate (Table 9), and this is the shipped default. For the shipped rule, then, what is proved is the per-history bound 0.2; the 0.01 target is a measurement. Under uniform coverage the threshold of (14) becomes $a^\star(n)=\sqrt{2r^\star_n/n}$ with $\Theta(r^\star_n)=\delta_n/H$, which falls roughly like $\sqrt{\log n/n}$ rather than $1/\sqrt n$. The detection delay is the price: in E2 a 50\%-duty door with $T{=}8$ is detected from 25 windows, against 15 with a single-read-out level of 0.1 and 8 without the test.

## 3.5 Classifying and Pruning: Four Classes from One State {#sec:classify}

At the end of the same per-cell pass, a cell with little evidence is erased together with its Fourier accumulators:

$$
\text{erase } i \iff \lnot g_i\land p_i<p_{\mathrm{prune}}\land a_i<a_{\min} . \tag{16}
$$

Static cells and periodic cells are therefore never pruned. The guard uses the lenient effect-size screen $a_i<a_{\min}$ rather than $\neg\pi_i$, so a *candidate* periodic cell keeps its accumulators, and hence its growing $n_i$, until the test resolves it. Every other erased cell loses $\boldsymbol{\phi}_i$. We considered keeping the Fourier history of erased cells and rejected it: pruning exists so that clutter does not accumulate, and a record for every cell ever touched would grow without bound, fastest in `voxel3d`, where every sampled free voxel is a cell. A re-created cell restarts at $n_i=0$ with a new history and, under (15), a new level budget; this is why the trajectory bound of Proposition 4 scales with the number of histories. The read-out $\Psi$ of (1) is a priority ladder,

$$
c_i=\begin{cases}
    \mathsf{U} & i\text{ absent},\\
    \mathsf{S} & g_i,\\
    \mathsf{P} & \pi_i,\\
    \mathsf{T} & p_i\ge p_{\mathrm{prune}},\\
    \mathsf{U} & \text{otherwise},
  \end{cases} \tag{17}
$$

evaluated top to bottom. In the sequential model Static and Periodic keep their places; below them an undecided cell is Transient, or Intermittent once it has $n_{\max}$ touched windows, and a rejected cell that is kept only as a periodic candidate reads Unknown (Section 4.3). The node renders it with the convention Unknown $-1$, free 0, Transient 50, Periodic 75, Static 100, writing in ascending priority so Static wins any tie: `grid2d` keeps one byte per cell outside the classifier, an observed-free bit set by the ray clear and never cleared, a bit for a hit in the open window and a bit for whether the cell's last observation was a hit; it draws 50 for a cell hit in the open window or last observed as a hit, or a Transient or Intermittent cell whose evidence leans occupied ($p_i>0.5$), and 0 for a cleared cell last observed free, so free means last observed free; `voxel3d` publishes only Static voxel centres. Table 29 lists every transition. The update visits every live cell, including untouched ones, because (7)–(16) read the recomputed $p_i$ and $a_i$; its cost per window is linear in the number of live cells times $H$, and between window closes a sweep costs $O(1)$ per endpoint plus the ray walk. Per-cell state is a 24-byte record plus roughly 32 bytes of hash-node overhead, the ${\sim}56$ B/cell used in §5. That figure is an estimate for the periodicity-off path, not a measurement. A Fourier record, with two scalar accumulators and six heap-allocated vectors of $H$ values in a second hash map, is allocated only for cells touched with periodicity enabled and is not included in it, nor are the hash tables' bucket arrays or the `grid2d` flag byte per grid cell with its list of cells touched in the open window.

**Algorithm 1.** `endWindow` for `evidence_model = legacy`: one layered update per closed window, as in `layered_map.cpp` (Algorithm 2 is the sequential model)

- **Input:** live cells $\mathcal{X}$ with window counters $h_i,m_i$; window index $w$
- $w\leftarrow w+1$
- for each live cell $i$:
  - $\tau\leftarrow[h_i{>}0\vee m_i{>}0]$, $o\leftarrow[h_i{>}0]$, $f\leftarrow[h_i{=}0\wedge m_i{>}0]$  // Eq. (5)
  - if $\tau$:
    - $\ell_i\leftarrow\mathrm{clamp}\big(\lambda(\ell_i+o\,l_{\mathrm{hit}}+f\,l_{\mathrm{miss}}),\,l_{\min},\,l_{\max}\big)$, $N_i\leftarrow N_i+1$  // Eq. (6)
    - if `enable_periodicity`: update $\boldsymbol{\phi}_i$ with $(o,w)$  // Eq. (10)
  - $p_i\leftarrow\sigma(\ell_i)$, $a_i\leftarrow\mathrm{amplitude}(\boldsymbol{\phi}_i)$, $\pi_i\leftarrow$ Eq. (13) on $\boldsymbol{\phi}_i$  // Eqs. (11)–(13)
  - if $\neg g_i\wedge\neg\pi_i\wedge p_i\ge p_{\mathrm{grad}}\wedge N_i\ge N_{\min}$: $g_i\leftarrow\text{true}$  // Eq. (7)
  - if $g_i\wedge(p_i\le p_{\mathrm{dem}}\vee\pi_i)$: $g_i\leftarrow\text{false}$  // Eq. (8)
  - $h_i\leftarrow0$, $m_i\leftarrow0$
  - if $\neg g_i\wedge p_i<p_{\mathrm{prune}}\wedge a_i<a_{\min}$: erase $i$ and $\boldsymbol{\phi}_i$  // Eq. (16)
- **Output:** updated $\mathcal{X}$; classes by Eq. (17) on demand

## 3.6 Selecting a Backend: One Interface, One String {#sec:select}

`MapBackend` exposes six methods: `integrate(obs, origin)` runs $\Gamma_b$ for one sweep, `tick()` advances the integration counter and may close a window, `closeWindows(k)` closes $k$ windows at once for the time-based rule of §3.2, and three read-only counters report Static, Transient and Intermittent cells (the last returns zero unless a backend overrides it). The node reads one string parameter, `backend` $\in\{$`grid2d`, `voxel3d`$\}$, holds one `unique_ptr` per concrete backend and instantiates exactly one at construction (the string is not validated: any value other than `grid2d` selects `voxel3d`); all engine parameters are read through one shared helper, so the tuning surface is backend-independent by construction. At two backends a runtime `dlopen` registry such as `pluginlib` [@ros2pluginlib] would add discovery machinery whose cost is not yet repaid. Because $\Phi$ and $\Psi$ in (1) never see coordinates, any behavioural or cost difference between the backends must originate in $\kappa_b$, $\rho_b$ or their input sampling—the property the experiments test.

## 3.7 Reconciling Code and Specification {#sec:specdiff}

We document the code that ships. Two places diverge from the design document (`SPEC.md`), and each changes operational semantics.

**Remark 3 (Forgetting is coupled to re-observation).** The design document states that every cell decays every window. In the code, (6) runs only for touched cells, so a cell outside the sensor's field of view neither decays nor relaxes toward the prior: its $\ell_i$ freezes. Clutter fades only while it is re-observed as free, and anything that leaves the field of view persists (§8).

**Remark (The amplitude gate counts touches).** The design document gates the amplitude on *elapsed* windows; the code gates on the touched-window count $n_i$ of (13). This is the mechanism behind the low-duty-cycle miss in §5.2.

# 4 Evidence as a Sequential Test {#sec:seq}

For each cell, STRATA decides between two stories: its occupancy is clutter, or it is structure. The state of that decision is one number, the log-odds $\ell$ of Eq. (6). This section shows what that number is a test of, why the leak of Eq. (6) removes the property on which an error bound rests, and how engine version 0.2 replaces the rule of the first release with a test whose hypotheses and error rates the user states. The first release is still in the code as `evidence_model = legacy`; the experiments run both. Every number of this section that comes from a run is generated from the committed results (Appendix A).

## 4.1 The v0.1 increments are a sequential probability ratio test {#sec:seqv01}

Let a cell be touched in a sequence of windows, and let $x_t\in\{0,1\}$ record whether window $t$ held a hit ($x_t=1$) or only misses ($x_t=0$). Let $m$ be the probability of a hit in a touched window, the *hit rate* of the cell. Wald's sequential probability ratio test (SPRT) [@wald1947sequential] of $H_0\!:m=m_0$ (clutter) against $H_1\!:m=m_1$ (structure), with $m_0<m_1$, adds the log-likelihood ratio of each observation to a statistic,

$$
\ell_t=\ell_{t-1}+\begin{cases}\ln\dfrac{m_1}{m_0}, & x_t=1,\\[4pt] \ln\dfrac{1-m_1}{1-m_0}, & x_t=0,\end{cases}\qquad \ell_0=0, \tag{18}
$$

decides for $H_1$ when $\ell_t\ge A$, for $H_0$ when $\ell_t\le B$, and otherwise takes one more observation. For the error rates $\alpha$ (decide $H_1$ under $H_0$) and $\beta$ (decide $H_0$ under $H_1$), Wald's thresholds are $A=\ln\frac{1-\beta}{\alpha}$ and $B=\ln\frac{\beta}{1-\alpha}$. Compare Eq. (18) with Eq. (6): the first release adds $l_{\mathrm{hit}}$ in a hit window and $l_{\mathrm{miss}}$ in a miss window. The two pairs of increments determine each other.

**Proposition 5 (Increments determine the hypotheses).** For every $l_{\mathrm{hit}}>0>l_{\mathrm{miss}}$ there is exactly one pair $0<m_0<m_1<1$ with $\ln(m_1/m_0)=l_{\mathrm{hit}}$ and $\ln\big((1-m_1)/(1-m_0)\big)=l_{\mathrm{miss}}$, namely

$$
m_0=\frac{1-e^{l_{\mathrm{miss}}}}{e^{l_{\mathrm{hit}}}-e^{l_{\mathrm{miss}}}},\qquad m_1=m_0\,e^{l_{\mathrm{hit}}}. \tag{19}
$$

*Proof.* The two conditions read $m_1=m_0e^{l_{\mathrm{hit}}}$ and $1-m_1=(1-m_0)e^{l_{\mathrm{miss}}}$. Substituting the first into the second gives $m_0\,(e^{l_{\mathrm{hit}}}-e^{l_{\mathrm{miss}}})=1-e^{l_{\mathrm{miss}}}$, which is Eq. (19). Since $l_{\mathrm{miss}}<0<l_{\mathrm{hit}}$, the numerator and the denominator are positive, so $m_0>0$, and $m_1>m_0$ follows from $l_{\mathrm{hit}}>0$. Finally $m_1<1$ is equivalent to $e^{l_{\mathrm{miss}}}<e^{l_{\mathrm{hit}}+l_{\mathrm{miss}}}$, which holds because $l_{\mathrm{hit}}>0$. $\square$

The shipped values of the first release are the sensor model of OctoMap, $l_{\mathrm{hit}}=0.85$ and $l_{\mathrm{miss}}=-0.4$ [@hornung2013octomap], which the engine applies once per touched window. By Eq. (19) they are the increments of an SPRT between the hit rates $m_0=0.1975$ and $m_1=0.4621$. The graduation probability $p_{\mathrm{grad}}{=}0.8$ is the threshold $A=\operatorname{logit}p_{\mathrm{grad}}=1.386$, and without the leak a cell with the hit rate $m^\ast=-l_{\mathrm{miss}}/(l_{\mathrm{hit}}-l_{\mathrm{miss}})=0.32$ has zero drift. In the first release of STRATA these hypotheses were not chosen; the two increments imply them. In this reading Static means that the evidence favours a hit rate near $0.4621$ over one near $0.1975$ by a factor of four, and a cell whose hit rate is above $m^\ast$ drifts toward Static. This is the reason for the false Static labels that E6 measures (Remark 1 gives the same threshold in the form that includes the leak).

## 4.2 A leak breaks the premise of the error bound {#sec:leak}

An error statement for a sequential test rests on the fact that $e^{\ell_t}$ is a nonnegative supermartingale when $H_0$ is true. Then Ville's maximal inequality bounds the probability that the statistic ever reaches a threshold [@howard2020timeuniform, Lemma 1]; @howard2020timeuniform [Section 4.6] state that this is an open-ended SPRT with a valid type-I error. The recursion of the first release, $\ell\leftarrow\lambda(\ell+x)$ with $\lambda=0.97$, breaks the property.

**Proposition 6 (A leak breaks the supermartingale).** Let $Z=\ln(m_1/m_0)$ with probability $m_0$ and $Z=\ln\frac{1-m_1}{1-m_0}$ otherwise, so that $\mathbb{E}\,e^{Z}=1$. For $\lambda\in(0,1)$ and a level $\ell<0$, the leaky update $\ell'=\lambda(\ell+Z)$ satisfies

$$
\frac{\mathbb{E}\,e^{\ell'}}{e^{\ell}}=e^{(1-\lambda)|\ell|}\;\mathbb{E}\,e^{\lambda Z},
$$

and this ratio exceeds $1$ for every $|\ell|>\ln\!\big(1/\mathbb{E}\,e^{\lambda Z}\big)/(1-\lambda)$. Hence $e^{\ell_t}$ is not a supermartingale under $H_0$.

*Proof.* $\mathbb{E}\,e^{\lambda(\ell+Z)}=e^{\lambda\ell}\,\mathbb{E}\,e^{\lambda Z}$, and $e^{\lambda\ell}/e^{\ell}=e^{(1-\lambda)|\ell|}$ for $\ell<0$. The factor $\mathbb{E}\,e^{\lambda Z}$ is a positive constant, so the product grows without bound in $|\ell|$. $\square$

At the values of the first release ($\lambda=0.97$, increments $(0.85,-0.4)$, hit rate $m_0=0.1975$) the ratio at the level $\ell=-4$ is $1.12$, and the level lies inside the clamp $[-5,5]$ of Table 25. The leak is also what moves the zero-drift rate from $0.32$ to the value of Remark 1. We found no result that gives error rates for a Wald test with a multiplicative leak; leaky accumulators are studied as models of decision making [@bogacz2006physics], not as tests with a stated error. Where forgetting is wanted, version 0.2 provides it by a separate mechanism (the ceiling and the reverse test below), not by shrinking the statistic.

## 4.3 The test of engine version 0.2 {#sec:sprtengine}

**What the user states.** The user states two hit rates and two error rates: $m_0$, the hit rate at or below which a cell is clutter; $m_1$, the hit rate at or above which it is structure; $\alpha$, the probability that clutter at $m_0$ is declared Static; $\beta$, the probability that structure at $m_1$ is rejected. The engine requires $0<m_0<m_1<1$, $\alpha,\beta>0$ and $\alpha+\beta<1$, and derives

$$
\begin{gathered}
  l_{\mathrm{hit}}=\ln\frac{m_1}{m_0},\qquad l_{\mathrm{miss}}=\ln\frac{1-m_1}{1-m_0},\\
  A=\ln\frac{1-\beta}{\alpha},\qquad B=\ln\frac{\beta}{1-\alpha},\qquad \ell_{\max}=A+2l_{\mathrm{hit}} .
  \end{gathered} \tag{20}
$$

The increments, the thresholds and the cap follow from these four numbers. The periodicity gate $T$ (the parameter `period_windows`) is a parameter with a fixed default, not derived from the pair. The truncation $n_{\max}$ depends on the pair through its zero-drift rate, but the engine does not derive it: it was computed once, offline, for the shipped pair by the pre-registered rule of Section 6 (below). The engine does not recompute it when the pair changes, so a user who re-derives the pair should recompute $n_{\max}$. The shipped values are $(m_0,m_1,\alpha,\beta)=(0.20,0.70,0.01,0.01)$. They are *provisional*: they are the pair measured on one calibration set, whose quality gate failed (Section 6), and the node says so on its diagnostics topic unless `evidence_calibration` is `user` or the pair is changed. Table 1 lists what Eq. (20) gives for them.

**Table 1.** Quantities derived from the shipped pair $(m_0,m_1,\alpha,\beta)=(0.20,0.70,0.01,0.01)$ (generated from the library defaults by the rule of the engine). Windows are touched windows. *Always-hit*: windows an always-hit cell needs to graduate; *demote*: all-miss windows that demote a graduated cell.

| $l_{\mathrm{hit}}$ | $l_{\mathrm{miss}}$ | $A=-B$ | $\ell_{\max}$ | $m^\ast$ | Always-hit | Demote | $T$ | $n_{\max}$ |
|---|---|---|---|---|---|---|---|---|
| 1.2528 | $-0.9808$ | 4.5951 | 7.1006 | 0.4391 | 4 | 5 | 24 | 80 |

**Three outcomes.** For each touched window the statistic moves by $l_{\mathrm{hit}}$ (a hit) or $l_{\mathrm{miss}}$ (only misses), with no multiplier, and is capped at $\ell_{\max}$. A cell ends in one of three states:

- *Static*: the cell graduated and has been neither demoted nor vetoed since. It graduates when $\ell\ge A$ while it is not Periodic, and its statistic then restarts at the cap $\ell_{\max}$. After graduation $\ell$ can fall below $A$ (to just above $\ell_{\max}-A$) while the cell stays Static.
- *Rejected*: $n\ge T$ touched windows and $\ell\le B$. From $n\ge T$ the statistic is floored at $B$, and a hit can still raise it. The cell is erased when its amplitude is below $a_{\min}$ (the effect-size screen of the periodicity test), and its next observation (a hit or a miss) starts a new history. A cell with $\ell\le B$ and an amplitude of at least $a_{\min}$ is kept as a periodic candidate: its statistic stays at or above $B$, and the cell is erased once its amplitude falls below $a_{\min}$ while $\ell\le B$.
- *Undecided*: everything else. Undecided cells are never promoted by a timer, and they are kept. The backend interface counts Static, Transient and Intermittent cells, and an Undecided cell that is not Periodic is counted as one of the last two. The node publishes no count of Undecided cells and no memory value.

The *periodicity gate* $n\ge T$ closes the prune/maturity race of Proposition 8: a cell cannot be rejected, and its statistic is not bounded below, before it has $T$ touches for the periodicity test to read. The price is that a clutter cell lives at least $T$ touched windows before it is erased (Table 3). The Periodic veto is unchanged from Section 3.4: a cell for which $\pi_i$ holds does not graduate and is demoted if it is Static.

**Demotion is a one-sided reverse test.** A graduated cell sits at the cap. It is demoted when $\ell\le\ell_{\max}-A$, that is, after $\lceil A/|l_{\mathrm{miss}}|\rceil=5$ all-miss windows at the shipped pair; one miss never demotes. This is a CUSUM-like test [@page1954continuous]: the cap reflects the statistic from above, and the cell is demoted when the statistic falls $A$ below the cap. It is not the SPRT in the opposite direction, and it has no error bound per history. A wall at the hit rate $m_1$ is demoted after a finite mean time, which we do not bound; Section 7.3 counts the real wall cells that were Static and later were not. After a reverse-test demotion the forward test restarts from $\ell=0$ and $n=0$, so a history can hold several forward tests. A demotion by the Periodic veto keeps $\ell$ and $n$, so the cell graduates again as soon as the veto clears while $\ell\ge A$. Proposition 7 covers the first forward test of a history.

**Truncation.** An undecided cell that is not Periodic is read out as *Intermittent* once it has $n_{\max}$ touched windows ($n_{\max}{=}80$, the smallest multiple of 16 for which at most 3\% of cells at the zero-drift rate $m^\ast$ are still undecided; the rule is pre-registered in Section 6). Intermittent is a read-out only: the test keeps running, and the cell can still graduate or be rejected. The 2D grid renders it like Transient. The occupancy probability that the engine reports (`occupancyProb`) is $\sigma(\ell)$, the posterior probability of “structure” for equal priors on the two hypotheses; it is not a calibrated occupancy.

**Algorithm 2.** `endWindow` for `evidence_model = sprt`, as in `layered_map.cpp`

- **Input:** live cells $\mathcal{X}$ with window counters; derived $l_{\mathrm{hit}},l_{\mathrm{miss}},A,B,\ell_{\max}$; gate $T$
- for each live cell $i$:
  - if $i$ was touched:
    - $\ell_i\leftarrow\min\big(\ell_i+o_i\,l_{\mathrm{hit}}+f_i\,l_{\mathrm{miss}},\,\ell_{\max}\big)$, $n_i\leftarrow n_i+1$  // Eq. (5)
    - if $n_i\ge T$: $\ell_i\leftarrow\max(\ell_i,B)$
    - update the Fourier model $\boldsymbol{\phi}_i$ with $(o_i,w)$ and recompute $\pi_i$
  - if $\neg g_i\wedge\neg\pi_i\wedge\ell_i\ge A$:
    - $g_i\leftarrow\text{true}$, $\ell_i\leftarrow\ell_{\max}$  // graduate
  - if $g_i$:
    - if $\pi_i$: $g_i\leftarrow\text{false}$
    - else if $\ell_i\le\ell_{\max}-A$: $g_i\leftarrow\text{false}$, $\ell_i\leftarrow0$, $n_i\leftarrow0$
  - if $\neg g_i\wedge n_i\ge T\wedge\ell_i\le B\wedge a_i<a_{\min}$: erase $i$ and $\boldsymbol{\phi}_i$  // rejected
- **Output:** classes: Static if $g_i$; Periodic if $\pi_i$; otherwise Unknown if rejected and kept, Intermittent if $n_i\ge n_{\max}$, else Transient

## 4.4 What the error rates promise {#sec:promise}

**Proposition 7 (Error bounds under independent windows).** Call a *history* of a cell its touched windows from its creation until it is erased or the run ends. Call its *first test* the windows from its creation up to its first graduation or, if that comes first, up to its *floor window*: the first window with $n\ge T$ in which the updated statistic is at or below $B$ (the engine floors the statistic at $B$ there and either erases the cell or keeps it as a periodic candidate). Suppose that, given everything observed before the history begins, its touched windows are fixed independently of its occupancy and its outcomes $x_1,x_2,\dots$ are independent Bernoulli($p$). If $p\le m_0$, the probability that the history graduates in its first test is at most $e^{-A}=\alpha/(1-\beta)$. If $p\ge m_1$, the probability that the first test ends in its floor window, that is, that the history is rejected or held as a periodic candidate before it has graduated, is at most $e^{B}=\beta/(1-\alpha)$ with the Periodic veto off; with the veto on it is at most $e^{B}$ plus the probability that the statistic reaches $A$ in the first test while $\pi_i$ blocks graduation. If $L_N$ histories of a cell begin in windows $1,\dots,N$, the probability that, for a clutter rate $p\le m_0$, the cell becomes Static at some window up to $N$ by a graduation in a first test is at most $e^{-A}\,\mathbb{E}[L_N]$. The statement does not cover a graduation after a demotion (a reverse-test demotion starts a new forward test; a veto demotion keeps $\ell$ and $n$), nor one after a floor window, from which a kept periodic candidate continues at $B$.

*Proof.* Let $\lambda(1)=m_1/m_0$ and $\lambda(0)=(1-m_1)/(1-m_0)$ and $\Lambda_t=\prod_{s\le t}\lambda(x_s)$, so that $\ell_t=\ln\Lambda_t$ while no clamp has acted. A direct computation gives

$$
\begin{aligned}
\mathbb{E}_p\,\lambda(x)&=1+\frac{(p-m_0)(m_1-m_0)}{m_0(1-m_0)}\le1\quad(p\le m_0),\\
  \mathbb{E}_p\,\frac{1}{\lambda(x)}&=1+\frac{(p-m_1)(m_0-m_1)}{m_1(1-m_1)}\le1\quad(p\ge m_1).
\end{aligned}
$$

Hence for $p\le m_0$ the process $\Lambda_t$ is a nonnegative supermartingale with $\Lambda_0=1$, and Ville's inequality [@howard2020timeuniform, Lemma 1] gives $\Pr(\sup_t\Lambda_t\ge e^{A})\le e^{-A}$. In the first test, up to the first window in which the statistic reaches $A$, the engine's statistic equals $\ln\Lambda_t$: the cap (at least $A$) can act only in a window in which the uncapped statistic exceeds it, that is, not before the statistic reaches $A$, nothing is floored before the periodicity gate, and the floor acts only in the floor window, where it replaces a value at or below $B$. The event that the history graduates in its first test is contained in $\{\sup_t\Lambda_t\ge e^{A}\}$, because the first window with $\ell\ge A$ comes before the floor window and has $\ell=\ln\Lambda_t$; the Periodic veto only removes graduations. For $p\ge m_1$ the process $1/\Lambda_t$ is a supermartingale. If the statistic has not reached $A$ before the floor window, no cap has acted and $\ln\Lambda_t\le B$ there, so the event that the first test ends there is contained in $\{\sup_t\Lambda_t^{-1}\ge e^{-B}\}$. With the veto off, the first window with $\ell\ge A$ is a graduation and ends the first test, so this covers every floor window of a first test. With the veto on, the remaining case is a first test in which the statistic reached $A$ while $\pi_i$ blocked graduation; after that the cap can act, and its probability is the added term. The third statement follows as in Proposition 4: the $\ell$-th history begins at a window determined by the past. $\square$

Four limits of this statement matter for how the numbers of Section 7 are read.

- *The unit is the first test of a history.* The bound is for the first test of one history. A reverse-test demotion restarts the forward test at $\ell=0$, and a rejected cell is erased and its next observation (a hit or a miss) starts a new history. Each restart after a reverse-test demotion, and each continuation of a kept periodic candidate, is a chance to graduate that the proposition does not bound; restarts after a rejection are bounded only through the expected number of histories. The share of cells that are ever Static in a run can therefore exceed the bound (Proposition 7 bounds the graduations in first tests by the bound times the expected number of histories). The per-cell rates on real clutter are reported in Section 7 and are above $\alpha$.
- *Independence is an assumption.* Real windows of one cell are correlated. Table 2 measures the effect on the engine for a cell at exactly $m_0$: the share of forward tests (a cell history, split at each demotion) that graduate within 1024 windows is 0.556 \% for independent windows, 3.492 \% at lag-1 autocorrelation $0.23$ (3.49 times $\alpha$) and 10.870 \% at $0.5$. A bare random walk floored at $B$ and never rejected, which is not the engine, reaches 12.30 times $\alpha$ at $0.23$. The band means of the lag-1 autocorrelation of real LILocBench calibration cells lie between 0.231 and 0.474 (Table 18), so independence does not hold, and no guarantee of the paper is stated for real windows.
- *Nothing is promised between the hypotheses.* A cell with a hit rate between $m_0$ and $m_1$ is neither clutter nor structure in the test, and it graduates with the probability of the operating characteristic below. The sweep of Table 3 shows how steeply that probability rises between $m_0$ and $m_1$.
- *A hit rate cannot say which kind of object produced it.* A wall seen in $60\,\%$ of its windows and clutter present in $60\,\%$ are the same input for this test. A pre-registered rule named a second statistic (run lengths) as a third step. Its trigger fired on the first (ten-scan) calibration measurement with opposite signs in the two datasets that it was read on; the second amendment replaced that step by the fallback of the 3D backend on the 2D pair, flagged not calibrated, and decided not to implement Option C (Section 6). The engine of this paper has no second statistic.

**Table 2.** The guarantee needs independent windows (E7). Share of forward tests (a cell history, split at each demotion) that graduate (Periodic veto off) when the windows of a cell at exactly $m_0$ follow a two-state chain with lag-1 autocorrelation $\rho$ ($\rho=0$: independent). *Engine*: per forward test (CSV row `engine_per_history`), 1024 windows, shipped pair. *Bare walk*: a random walk floored at $B$ and never rejected, 200 windows, the shipped pair; this is not the engine. Ratios are to $\alpha{=}0.01$. The band means of the lag-1 autocorrelation of the window outcomes of real LILocBench calibration cells lie between 0.231 and 0.474 (Table 18).

| $\rho$ | Engine, share | Engine, ratio | Bare walk, share | Bare walk, ratio |
|---|---|---|---|---|
| 0 | 0.556 \% | 0.56 | 0.943 \% | 0.94 |
| 0.1 | 1.358 \% | 1.36 | 3.537 \% | 3.54 |
| 0.23 | 3.492 \% | 3.49 | 12.298 \% | 12.30 |
| 0.35 | 6.089 \% | 6.09 | 28.109 \% | 28.11 |
| 0.5 | 10.870 \% | 10.87 | 55.028 \% | 55.03 |

## 4.5 Operating characteristic and latency {#sec:oc}

For the hit rate $m$ the probability that the test graduates a cell follows from Wald's operating characteristic, which neglects the overshoot of the statistic over the thresholds [@wald1947sequential, Chapter 3]. With $a=e^{A}$ and $b=e^{B}$,

$$
P_m(\text{graduate})\approx\frac{1-b^{h}}{a^{h}-b^{h}},\qquad
  m\Big(\frac{m_1}{m_0}\Big)^{h}+(1-m)\Big(\frac{1-m_1}{1-m_0}\Big)^{h}=1, \tag{21}
$$

where $h=h(m)\ne0$ solves the second equation, and the value at the zero-drift rate $m^\ast$ ($h=0$) is the limit $-B/(A-B)=\ln(1/b)/(\ln a-\ln b)$, the probability that a zero-drift walk from $0$ reaches $A$ before $B$; it equals $1/2$ only when $\alpha=\beta$. The formula gives $\alpha$ at $m_0$ ($h=1$) and $1-\beta$ at $m_1$ ($h=-1$). Experiment E7 compares three quantities on the same pair: Monte Carlo on the real engine with the Periodic veto off (40000 cells per rate, one first test each), the exact value of the same rule computed state by state on the lattice $\ell=h\,l_{\mathrm{hit}}+k\,l_{\mathrm{miss}}$, and Eq. (21) (Table 3). The engine and the exact computation agree: the exact value lies inside the Wilson interval of the Monte Carlo share in 38 of 40 rows, and the mean number of windows to a decision differs by at most 0.16 windows. At $m_0$ the exact graduation probability is 0.0055, below both $\alpha$ and the Ville bound 0.0101, because the statistic overshoots the threshold. Eq. (21) departs from the exact value by at most 0.028 for the rule without a gate (at $m=0.35$) and by at most 0.083 for the shipped rule with the periodicity gate (at $m=0.50$; E7 runs it with the Periodic veto off); the gate keeps cells from being rejected early, which is why the shipped rule graduates 0.9996 of the cells at $m_1$ where the closed form says $1-\beta$. A cell that is clutter lives 24.1 windows on average under the shipped rule ($T=24$) against 9.1 without the gate.

**Table 3.** Operating characteristic of the sequential test on the engine (E7), pair $(m_0,m_1,\alpha,\beta)=(0.20,0.70,0.01,0.01)$, independent windows. $P(\mathrm{Static})$: share of 40000 cells per rate (one first test each) that graduate. *Engine*: Monte Carlo on `LayeredMap` with the periodicity gate $T{=}24$ (the shipped rule with the Periodic veto off) and Wilson 95\% limits; *Exact*: the same rule computed state by state on the lattice $l=h\,l_{\mathrm{hit}}+k\,l_{\mathrm{miss}}$, no sampling; *Wald*: the closed form of Eq. (21), which ignores the overshoot of the statistic over the thresholds. The last column is the mean number of windows to a decision (engine / exact).

| $m$ | Engine, $P(\mathrm{Static})$ | Exact | Wald | Windows |
|---|---|---|---|---|
| 0.05 | 0.0000 [0.0000, 0.0001] | 0.0000 | 0.0000 | 24.0 / 24.0 |
| 0.10 | 0.0002 [0.0001, 0.0004] | 0.0002 | 0.0004 | 24.0 / 24.0 |
| 0.15 | 0.0011 [0.0008, 0.0015] | 0.0012 | 0.0026 | 24.0 / 24.0 |
| 0.20 | 0.0053 [0.0046, 0.0061] | 0.0055 | 0.0100 | 24.1 / 24.1 |
| 0.25 | 0.0195 [0.0182, 0.0209] | 0.0200 | 0.0306 | 24.5 / 24.5 |
| 0.30 | 0.0595 [0.0572, 0.0619] | 0.0615 | 0.0794 | 25.5 / 25.4 |
| 0.35 | 0.1579 [0.1544, 0.1615] | 0.1615 | 0.1782 | 27.1 / 27.1 |
| 0.40 | 0.3476 [0.3429, 0.3523] | 0.3506 | 0.3409 | 28.4 / 28.3 |
| 0.4391 ($m^\ast$) | 0.5481 [0.5432, 0.5530] | 0.5462 | 0.5000 | 27.6 / 27.6 |
| 0.50 | 0.8166 [0.8128, 0.8204] | 0.8166 | 0.7334 | 23.2 / 23.1 |
| 0.60 | 0.9844 [0.9831, 0.9856] | 0.9838 | 0.9377 | 14.0 / 14.0 |
| 0.70 | 0.9997 [0.9995, 0.9998] | 0.9996 | 0.9900 | 9.0 / 9.0 |
| 0.80 | 1.0000 [0.9999, 1.0000] | 1.0000 | 0.9990 | 6.5 / 6.5 |

The latency of an always-hit wall is a consequence of the error rates, not a free constant: it graduates at the window $\lceil A/l_{\mathrm{hit}}\rceil=\lceil\ln\frac{1-\beta}{\alpha}/\ln\frac{m_1}{m_0}\rceil$, which is 4 at the shipped pair, not the third window of the first release. We report the latency; we do not advertise it. A graduated cell is demoted after $\lceil A/|l_{\mathrm{miss}}|\rceil$ all-miss windows. Rejection waits for the $T$ touched windows of the gate, so on a short pass a cell touched fewer than $T$ times can end Static or Undecided but never Rejected; Section 7 reports the Undecided share on real data.

# 5 Synthetic Experiments {#sec:exp}

The synthetic suite uses hand-authored hit and miss patterns, so that the truth of every cell is known. It runs in two arms: the *legacy* arm, the arithmetic of the first release with the explicit parameters of the first release, whose results are those of the first version of this study; and the *sprt* arm, the sequential test at the shipped defaults on the same scenes and seeds (Section 5.5). In the text and tables of the legacy arm, *shipped* means the defaults of the first release (Table 25, group “Legacy evidence model”); in the sprt arm it means the defaults of version 0.2. We ask six questions. Do the two backends behave as one engine wearing two geometries (E1, E4)? Does the shared classifier detect periodic structure, and how does it fail (E2)? Does the live, pruned and repeatedly tested detector control false alarms over a whole run (E5)? Which cells does the graduation rule call Static when they are neither walls nor periodic (E5, E6)? Which mechanism actually stabilises the static layer (E3)? Does the sequential test on the real engine follow Wald's operating characteristic, and how much does autocorrelation of the windows inflate its error (E7)?

## 5.1 Experimental Setup {#sec:setup}

**Harness and protocol.** All experiments are driven by a standalone harness that compiles directly against the `strata_core` sources, with no ROS 2, ament or colcon in the loop; it lives in `paper/experiments/`. One command, `bash paper/experiments/run_all.sh`, builds the core and its unit tests, checks the `ctest` suite (the 114 gtest cases in one target, among them ten regression tests for the amplitude defect of Remark 2, nine for the significance test of Proposition 3 and eight for the spent level of Proposition 4, and a check that the shipped parameter files and `PREREG.json` state the same defaults), builds and runs the calibration E0 and the experiments E1–E7, and writes the CSVs in `paper/experiments/results/`; every table and plot in this paper is regenerated from those files (Appendix A). E0–E3, E5, E6 and E7 and the E4 cell-count file of the sprt arm (`results/e4_cells_sprt.csv`) reproduce their committed files byte for byte with GCC 9.4.0 and libstdc++, and under another standard library the two E1 files of the legacy arm, the E0 door files, the E6 confusion files and the E4 cell counts of the sprt arm change (Appendix A); the cell counts of the legacy E4 file were not compared under the other library; E4's timings are wall-clock and are recorded separately. Every stochastic experiment E1–E4 derives its `std::mt19937` generator from the constant seed 12345 plus a per-run offset recorded in code (offsets 0–1500), and the seed is logged in each CSV. The calibration E0 uses seeds from 20260928 upward, E5 draws its calibration seeds from 30260928 and its evaluation seeds from 40260928 upward, so no calibration stream coincides with an evaluation stream; E6 reuses the scene seeds of E5's confusion and draws its sweep from 50260928 upward, E7 draws from 70261018 upward with `std::mt19937_64`, and the unit tests use a further disjoint range. E1–E4 magnitudes are single-seed point estimates: we report no cross-seed dispersion for them, and gaps such as 0.9253 against 0.9758 illustrate a mechanism rather than a distributional estimate. E5 and E6 are multi-seed and report Wilson 95\% intervals.

**What is compared.** There is no external baseline in the synthetic suite. The lifelong-SLAM systems of §2 do not expose a persistence core that this harness could drive. The FreMEn per-cell model [@krajnik2017fremen; @krajnik2016persistent; @fremenrepo], with its amplitude-ranked component selection, could be driven by the same streams, but we have not run it, so every comparison below is internal; the evaluation on real data (Section 7) compares four other evidence rules. The centred amplitude-only rule kept in Table 6 thresholds FreMEn-style coefficients at one fixed period; it is STRATA's own earlier rule, not an implementation of FreMEn. Instead E1 and E4 compare the two STRATA backends, each driven in its native geometry by its own seeded stream (`grid2d` on the integer lattice, `voxel3d` continuously); because both call the identical engine, any divergence arises in $\kappa_b$, $\rho_b$ or their input sampling. E2 and E3 drive `LayeredMap` directly with no backend at all, which by (1) characterises the classifier for whichever backend supplies the ids. Each harness sets its own parameters in code (Table 26 for E1–E4; E0, E5 and E6 use the E2 parameters or the shipped defaults, as stated with each); in particular every experiment, E0 and E5 included, runs with one window per tick ($L{=}1$), so *window* and *frame* coincide.

**Metrics.** Static-set precision, recall and F1 against a known wall set (E1, E3); per-read-out false-alarm rates on pure-noise cells and the read-out length from which a door stays Periodic (E0); the trajectory-level false-alarm rate, i.e. the share of non-periodic cells labelled Periodic at *some* window up to a horizon $N$, the per-window labelling rate, and four-class precision and recall (E5); Periodic-class true- and false-positive rates against ground truth and the dominant amplitude $a$ (E2); *flicker*, the number of per-window toggles of a wall cell's Static flag (E3); the share of Bernoulli($m$) cells labelled Static (E6); final live-cell count, estimated memory at 56 B per cell and, where it was recorded reliably, latency per `integrate()` call (E4).

**Scenarios and hardware.** E0 runs first and fixes the single-read-out level (§3.4): iid Bernoulli($m$) streams at $T/H\in\{8/3, 24/2\}$, $m\in\{0.05,0.1,0.2,\dots,0.9,0.95\}$ and 12 read-out lengths $n\in\{8,\dots,100\}$ for $T{=}8$ and 8 lengths $n\in\{24,\dots,240\}$ for $T{=}24$ (listed in `e0_calibration.cpp`), with 20,000 streams per point; 3000 Bernoulli($m$) clutter cells ($m\in\{0.3,0.5,0.7\}$) through the live `LayeredMap` with the E2 parameters, read out at every $n=8$–$100$; and four doors (4-on/4-off, 2-on/2-off, 3-on/5-off, and 4-on/4-off with 10\% flipped windows) in 40 runs each at random phase offsets. E1: a 161-cell static cross (the segments $y{=}60$ and $x{=}60$ over $[20,100]$) hit every window, plus 5, 25 or 100 fresh random movers per window, for 40 windows, on a $120{\times}120$ grid and a voxel map, both at resolution 1.0, once with periodicity off and once with it on at the shipped $T{=}24$, $H{=}2$. E2: three periodic doors (period 8 at 4-on/4-off and 2-on/6-off, period 4 at 2-on/2-off), a constant wall and four Bernoulli(0.5) aperiodic controls, with $T{=}8$, $H{=}3$, read out at 64 windows and, in a sweep, after every length $n=8$–$100$ (a fresh map per length). E3: 50 wall cells observed occupied with probability 0.6 per window among 10 movers per window, 80 windows, the same noise replayed for all 36 configurations of $p_{\mathrm{grad}}\in\{0.6,0.7,0.8,0.9\}\times p_{\mathrm{dem}}\in\{0.3,0.5,p_{\mathrm{grad}}\}\times\lambda\in\{0.90,0.97,1.0\}$, with periodicity off and again with it on at the shipped defaults. E5: Bernoulli($m$) clutter, $m=0.1,\dots,0.9$, plus always-occupied, always-free and temporally correlated cells (a two-state Markov chain with mean 0.5 and mean dwell 10 windows), 2000 cells per configuration (10 seeds of 200), through the live `LayeredMap` with the E2 and with the shipped parameters, touched every window or independently with probability $q{=}0.5$, over 1024 windows, labelled after every window; four door types for detection delay; and 20 E2-style scenes for the confusion of Table 11. The calibration half of E5 fixes the shipped level on its own seeds before the evaluation half runs. E6, added after E5 showed many false Static labels, repeats that confusion on the same 20 scene seeds under the E2 parameters with $\lambda{=}0.97$ and under the shipped defaults (whose doors have the same duty cycles at three times the period, since $T{=}24$), and sweeps Bernoulli($m$) cells over $m=0.05,0.10,\dots,0.95$, 2000 cells per point (10 seeds of 200), touched every window for 256 windows, under four parameter sets: E2, E2 with $\lambda{=}0.97$, the shipped defaults, and the shipped defaults with $l_{\mathrm{miss}}=-l_{\mathrm{hit}}$. All E1–E4 results are from the current engine (centred coefficients and the test at $\delta{=}0.2$ spent over the touch count). The same harness run against three earlier engines is kept for comparison (Table 6): v0.1.0, the centred engine with the amplitude-only rule $a\ge a_{\min}$, and the test at the single-read-out level $\delta{=}0.1$. E4: 150 random hits per window for 20 windows at nominal extents $100^2$, $250^2$ and $500^2$, resolution 1.0, periodicity off, each configuration run 15 times on a fresh map after one discarded warm-up run. E4 alone reads the wall clock. Its timings are 3 separate recordings taken on a quiet host with `paper/experiments/measure_e4.sh`: the script refuses to start unless the 1-minute load average is below 0.50, pins the benchmark to one core, discards one warm-up run per configuration, records the load and the largest CPU consumers before and after, and marks a recording invalid when the load afterwards is 1.50 or more (an AMD Ryzen 5 5500GT with 12 logical CPUs, GCC 9.4.0, `-O3`; every repeat and the environment are in `results/e4_recordings/`); they are not claimed to be portable. Its live-cell counts are deterministic for a given standard library.

## 5.2 Main Results {#sec:main}

**Summary of the legacy arm.** Under the v0.1 evidence rule both backends graduate a 161-cell wall by the third window and keep recall at 1.0 through window 39 (with the sequential test of the current engine the same wall graduates at its derived fourth window and no false Static cell appears in any of the six runs); their only quality gap, a final static precision of 0.9253 (`grid2d`) against 0.9758 (`voxel3d`) under 100 movers per window, arises in the backends' native sampling rather than in the shared classifier. We report and correct two defects of the first release's periodicity test, with before-and-after results. An uncentred Fourier amplitude could demote constant walls. An uncalibrated amplitude threshold labelled Bernoulli clutter Periodic at 0 to 4 of 5 cells depending on read-out length, a mean false-positive rate of 0.331 even after centring. Because the map re-tests every window, a constant significance level still let up to 953 of 2000 clutter cells be labelled Periodic at some window of a 1024-window run; with the level spent over the touch count, at most 2 of 2000 are, on held-out seeds, and the E2 false-positive rate is 0 at every read-out length from 8 to 100 windows. Both 50\%-duty doors are still detected, for a true-positive rate of 2/3, but only from 25 windows instead of 8, and the one miss is a provable interaction between pruning and the amplitude gate. Over 20 held-out scenes the dominant labelling error is a false Static label on aperiodic cells, not a false Periodic one: with the v0.1 defaults 1960 of 2000 Bernoulli(0.5) cells end Static, and the graduation rule cannot separate such clutter from a noisy wall of similar occupancy (Remark 1; Section 4.1 gives the reason). Removing the hysteresis band raises flicker from 54 to 574 toggles at the same decay, and the estimated footprint is a flat 56 B per cell with periodicity off; `grid2d` is cheaper per call than `voxel3d` at every tested extent, by 7.7$\times$ to 10.4$\times$ over 3 quiet-host recordings on one machine, alongside 4.4–10.0$\times$ more live 3D cells. On this synthetic suite, the results are consistent with a lifelong-mapping core that is written once and shared across dimensions, with the two backends differing in how many cells their geometry creates. Whether the calibrated test improves on FreMEn's own component selection remains to be evaluated (§8); how the evidence rule behaves on real LiDAR data is the subject of Sections 6 and 7.

**Table 4.** Static-layer quality (E1) against the 161-cell ground-truth wall set. *Recall 1 from* is the first window index $t$ at which recall reaches 1.0 (it never drops afterwards); precision, F1 and the predicted static-set size are at the final window $t{=}39$. Single seed (12345), so no dispersion is reported. Bold marks the better backend under high clutter.

| Backend | Clutter (movers/window) | Recall 1 from | Precision | F1 | $\|\hat{\mathcal{S}}\|$ |
|---|---|---|---|---|---|
| `grid2d` | low (5) | $t{=}2$ | 1.0000 | 1.0000 | 161 |
| `grid2d` | med (25) | $t{=}2$ | 0.9938 | 0.9969 | 162 |
| `grid2d` | high (100) | $t{=}2$ | 0.9253 | 0.9612 | 174 |
| `voxel3d` | low (5) | $t{=}2$ | 1.0000 | 1.0000 | 161 |
| `voxel3d` | med (25) | $t{=}2$ | 0.9938 | 0.9969 | 162 |
| `voxel3d` | high (100) | $t{=}2$ | **0.9758** | **0.9877** | 165 |

> **Figure 2.** **Static-layer F1 over time (E1).** F1 is 0 for $t<2$ (no cell has met $N_{\min}{=}3$; not shown) and reaches 1.0 at $t{=}2$ for every backend and clutter level. Only the 100-movers-per-window regime accumulates false statics, more so on `grid2d`. Plotted directly from `e1_static_quality.csv`. *(Drawn in the PDF with TikZ/pgfplots from the data in `paper/arxiv_ref/figures/data/`; not reproduced in this Markdown file.)*

**Backend equivalence of the static layer (E1).** The numbers of this paragraph are those of the v0.1 evidence rule (`legacy`, the arithmetic of the first release, which the harness names explicitly). The same scenes run with the sequential test of the current engine (`evidence_model = sprt` at the shipped defaults; `results/e1_static_quality_sprt.csv`) give recall 1.0 from the derived fourth window in all six runs and no false Static cell at any clutter density. With $N_{\min}{=}3$ a cell needs three touched windows before it may graduate, and recall jumps from 0 to 1.0 exactly at the third window ($t{=}2$) for all six backend–clutter combinations, then holds at 1.0 through $t{=}39$: clutter never demotes a wall cell (Table 4, Figure 2). Table 4 is the periodicity-off run. With periodicity on at the shipped defaults ($T{=}24$, $H{=}2$, $\delta{=}0.2$ spent over the touch count) the run is identical window for window: the wall is occupied whenever it is touched, so its centred amplitude is 0 (Remark 2) and $d_{i,k}=0$. The v0.1.0 engine instead lost the whole wall from the Static layer in windows 29–40 ($t=28$–$39$), in all six runs, and ended with Static recall 0 (Table 6). The only errors are false Static cells created when a random mover re-hits one cell often enough to graduate on its own. At 5 movers per window this never happens and precision stays at 1.0; at 25 it saturates at one spurious cell (precision 0.9938); at 100 false statics keep accumulating, from the first extra cell at $t{=}17$ (`grid2d`) or $t{=}25$ (`voxel3d`) to 174 or 165 predicted cells at $t{=}39$. This yields the one material gap between backends, precision 0.9253 against 0.9758. Both runs apply the identical graduation rule, each to its own natively sampled mover stream (movers on the integer lattice for `grid2d`, continuous for `voxel3d`, both confined to one $z$-plane), so the gap reflects quantisation and sampling rather than the classifier; we do not claim it isolates a single geometric mechanism.

**Table 5.** Periodicity detection (E2) after 64 windows with the shipped test, $a_{\min}{=}0.3$, $\delta{=}0.2$ spent over the touch count, $T{=}8$, $H{=}3$. *Ref. amplitude* and *Ref. $H\,B$* (the Bonferroni-adjusted Chernoff bound of Proposition 3; Periodic needs $\le\delta_n$) come from a non-pruning reference model fed the identical occupancy stream. Periodic TPR $=2/3$, FPR $=0/5$. *Outcome* scores the Periodic class; a non-periodic cell ending Static is not a Periodic false positive but is a wrong map label (*false Static*).

| Probe cell | Ground truth | Final class | Ref. amplitude | Ref. $H\,B$ | Outcome |
|---|---|---|---|---|---|
| `door_p8_4on4off` (50\% duty) | periodic | Periodic | 0.653 | $<10^{-3}$ | correct |
| `door_p8_2on6off` (25\% duty) | periodic | Transient | 0.462 | $<10^{-3}$ | **miss** |
| `door_p4_2on2off` (50\% duty) | periodic | Periodic | 0.707 | $<10^{-3}$ | correct |
| `wall_constant` | non-periodic | Static | $\approx 0$ | 1 | correct |
| `aperiodic_0` | non-periodic | Static | 0.159 | 1 | **false Static** |
| `aperiodic_1` | non-periodic | Unknown | 0.329 | 0.056 | correct |
| `aperiodic_2` | non-periodic | Transient | 0.139 | 1 | correct |
| `aperiodic_3` | non-periodic | Transient | 0.251 | 0.585 | correct |

> **Figure 3.** **FreMEn-lite amplitude against observation length (E2)**, from a non-pruning reference model of the corrected engine fed each probe cell's occupancy stream ($T{=}8$, $H{=}3$). The $n\ge T$ gate forces $a{=}0$ at $n{=}6$; both 50\%-duty doors settle well above $a_{\min}$, and the 25\%-duty door would too, yet the live pipeline prunes it first (Proposition 8). The constant wall is exactly 0 at every length because the coefficients are centred (Remark 2); the Bernoulli controls hover near $a_{\min}$. The thick dotted curve $a^\star(n)$ is the amplitude the shipped test requires under uniform phase coverage, with $\delta{=}0.2$ spent over the touch count and $H{=}3$ (Eq. (14) with $\delta_n$ of Proposition 4); the thin dotted curve is the single-read-out threshold at $\delta{=}0.1$. A cell is Periodic only above both $a_{\min}$ and $a^\star(n)$. Plotted from `e2_amplitude_vs_length.csv`. *(Drawn in the PDF with TikZ/pgfplots from the data in `paper/arxiv_ref/figures/data/`; not reproduced in this Markdown file.)*

> **Figure 4.** **Periodic false- and true-positive rates against read-out length (E2)**, from the real `LayeredMap`, a fresh map per length, $T{=}8$, $H{=}3$, $a_{\min}{=}0.3$, for the four engine versions of Table 6. The dotted lines apply the single-read-out and the spent predicate to a non-pruning reference model fed the same streams (lower panel: spent only). The dashed vertical marks the $n{=}64$ read-out of Table 5. The v0.1.0 and centred TPR curves coincide at 2/3. Plotted from `e2_rates_vs_length.csv` and its three archived counterparts. *(Drawn in the PDF with TikZ/pgfplots from the data in `paper/arxiv_ref/figures/data/`; not reproduced in this Markdown file.)*

**Table 6.** Effect of the three corrections to the periodicity test, same harness, seeds and parameters. *v0.1.0*: uncentred amplitude and the amplitude-only rule $a\ge a_{\min}$ (results in `results/pre_fix_2026-09-28/`); *Centred*: centred amplitude, amplitude-only rule (`results/pre_calibration_2026-09-28/`); *Single*: the significance test of Proposition 3 at $\delta{=}0.1$ at every read-out (`results/pre_spending_2026-09-28/`); *Spent*: the level $\delta{=}0.2$ spent over the touch count (Proposition 4, current engine). E1 and E3 with periodicity on use the shipped $T{=}24$, $H{=}2$, $a_{\min}{=}0.3$; E2 uses $T{=}8$, $H{=}3$. E1 counts are identical in all six backend–clutter runs.

| Measure | Periodicity off | Periodicity on v0.1.0 | Centred | Single | Spent |
|---|---|---|---|---|---|
| E1: windows $t\ge3$ with Static recall $<1$ | 0 windows | 12 ($t{=}28$–$39$) | 0 windows | 0 windows | 0 windows |
| E1: Static recall at $t{=}39$ | 1 | 0 | 1 | 1 | 1 |
| E2 at $n{=}64$: TPR / FPR | – | 2/3 / 1/5 | 2/3 / 2/5 | 2/3 / 0/5 | 2/3 / 0/5 |
| E2, $n=8$–$100$: mean FPR | – | 0.363 | 0.331 | 0.000 | 0.000 |
| E2, $n=8$–$100$: FP count range (of 5) | – | 0–4 | 0–4 | 0–0 | 0–0 |
| E2, $n=8$–$100$: lengths with no FP | – | 10 of 93 | 7 of 93 | 93 of 93 | 93 of 93 |
| E2, $n=8$–$100$: lengths with constant wall Periodic | – | 4 of 93 | 0 of 93 | 0 of 93 | 0 of 93 |
| E2, $n=8$–$100$: mean TPR | – | 0.667 | 0.667 | 0.624 | 0.563 |
| E2: TPR 2/3 at every $n\ge$ | – | 8 | 8 | 15 | 25 |
| E2, $n=8$–$100$: lengths with FP, no pruning | – | – | – | 25 of 93 | 0 of 93 |
| E3: total flicker, 36 configurations | 3672 | 7132 | 5472 | 3792 | 3672 |
| E3: flicker at $p_{\mathrm{grad}}{=}0.9$, $p_{\mathrm{dem}}{=}0.3$, $\lambda{=}0.90$ | 54 | 138 | 102 | 58 | 54 |
| E3: flicker at $p_{\mathrm{grad}}{=}p_{\mathrm{dem}}{=}0.9$, $\lambda{=}0.90$ | 574 | 582 | 596 | 576 | 574 |

**Single-read-out calibration (E0).** Table 7 applies the single-read-out rule of §3.4 (level constant in $n$). The Chernoff bound is conservative at every candidate level. At $\delta{=}0.1$ the worst null rate over all grid points is 0.0050 and the worst pipeline rate 0.007, both within the 0.01 target, whereas $\delta{=}0.2$ exceeds it (0.0149 and 0.014). The selected single-read-out level is therefore $\delta{=}0.1$; the shipped rule replaced it (E5). Relaxing $\delta$ from 0.01 to 0.1 shortens the median read-out length at which a 4-on/4-off door becomes stably Periodic from 22 to 15 windows. The Gaussian-residual approximation of the periodogram, the textbook false-alarm formula [@zechmeister2009gls], fails the same check: at a level of 0.01 it reaches a null rate of 0.037 for $n$ near $T$, where binary sequences put real mass on exactly periodic patterns. No E1–E4 seed was used in E0.

**Table 7.** Calibration of the nominal level $\delta$ (E0) on seeds disjoint from E1–E4. *Null*: worst false-alarm rate over the grid of iid Bernoulli($m$) streams ($T/H\in\{8/3,24/2\}$, $m\in[0.05,0.95]$, $n\ge T$, 20,000 streams per point); *Pipeline*: worst per-read-out rate of 3000 clutter cells in the live `LayeredMap` (E2 parameters, pruning on, $n=8$–$100$); *Door*: median read-out length from which a 4-on/4-off door stays Periodic (40 phases). Rule, fixed before the run: the largest $\delta$ with both rates $\le 0.01$. For comparison, the Gaussian-residual approximation of the periodogram at level 0.01 reaches a null rate of 0.037.

| $\delta$ | Null rate | Pipeline rate | Door, median first $n$ | Selected |
|---|---|---|---|---|
| 0.01 | 0.0004 | 0.001 | 22 | no |
| 0.02 | 0.0007 | 0.002 | 20 | no |
| 0.05 | 0.0022 | 0.003 | 17 | no |
| **0.1** | **0.0050** | **0.007** | **15** | **yes** |
| 0.2 | 0.0149 | 0.014 | 14 | no (rate $>0.01$) |

**Periodicity detection (E2).** Both 50\%-duty doors are classified Periodic, with reference amplitudes 0.653 and 0.707 and Bonferroni-adjusted bounds below $10^{-3}$ (Table 5). The constant wall is Static with amplitude exactly 0 at every length. No aperiodic control is Periodic, so at the $n{=}64$ read-out the Periodic class reaches a true-positive rate of 2/3 and a false-positive rate of 0/5; the two earliest engines scored 1/5 (v0.1.0) and 2/5 (centred only) at this length. Figure 4 and Table 6 show that this is not a favourable read-out. Over all lengths $n=8$–$100$ the pipeline has no false positive at any length, as with the single-read-out level, against a mean false-positive rate of 0.363 (v0.1.0) and 0.331 (centred), with 0–4 of 5 per length in both. The gain is paid for in detection delay. The true-positive rate is 0 for $n=8$–19 and 1/3 at $n=20$–24, and it reaches 2/3 at $n=25$; the single-read-out level reached it at $n=15$ and the two earliest engines from $n{=}8$. The mean true-positive rate over the sweep falls from 0.667 to 0.624 (single read-out) and 0.563 (spent).

Two further observations qualify the zero. First, it is no longer owed to pruning. With the single-read-out level, a non-pruning reference model fed the same streams and read with the same predicate called `aperiodic_1` Periodic at 25 of 93 lengths ($n=29$–31, 45–50 and 52–67), a mean false-positive rate of 0.054: at $n{=}64$ this cell's amplitude is 0.329 and its bound $H\,\Theta=0.056<0.1$, a genuine tail event of this seed, roughly 3.7 null standard deviations. With the spent level the same reference model (dotted in Figure 4) has no false positive at any length, because $\delta_{64}=0.2\cdot8/(64\cdot65)\approx3.8\times10^{-4}$. Second, `aperiodic_0` ends Static. With $\lambda{=}1$ its log-odds is a random walk that reached $p_{\mathrm{grad}}$, and it was previously masked by a spurious Periodic label (centred engine) that demoted it. E2 does not score the Static class; E5 does, over 20 scenes, and finds this failure common (Table 11). The amplitude curves confirm the gate of (11): for the 4-on/4-off door the amplitude is exactly 0 at $n{=}6<T$, 0.653 at every sampled multiple of $T$ ($n=8,16,\dots,64$) and 0.581 at $n{=}12$, where the uncentred v0.1.0 amplitude had overshot to 0.871 (Figure 3). The miss, the 25\%-duty door, is not a failure of the test: the reference model detects it at every length from $n{=}57$ with the spent level (from $n{=}26$ at a single read-out), with amplitude 0.462 at $n{=}64$. The live pipeline never gets there, for a reason that follows from the update rule.

**Proposition 8 (Prune/maturity race).** Under the E2 parameters ($l_{\mathrm{hit}}{=}1$, $l_{\mathrm{miss}}{=}{-}1$, $\lambda{=}1$, $p_{\mathrm{prune}}{=}0.05$, $T{=}8$), a cell touched every window with the 2-on/6-off pattern is erased by (16) within at most 7 consecutive touched windows of its creation. Hence $n_i<T$ throughout every lifetime, $a_i=0$, and the cell is never classified Periodic.

*Proof.* With $\lambda{=}1$ and unit increments, $\ell_i$ is an integer walk from $0$ (the clamp at $\pm5$ never binds below). Since $\sigma(-2)\approx0.119\ge p_{\mathrm{prune}}>\sigma(-3)\approx0.047$, erasure happens exactly when $\ell_i$ first reaches $-3$, provided $a_i<a_{\min}$, which holds while $n_i<T$. If the cell is created with $j\ge3$ vacant windows left in its cycle, it reaches $-3$ after 3 windows. If $j\le2$ vacant windows remain, $\ell_i$ reaches $-j$, then $2-j$ after the two occupied windows, then $-3$ after $5-j$ further vacant windows: $7$ windows in total. If it is created in an occupied window, $\ell_i\le2$ after at most two hits and $-3$ after five more misses, again at most 7. In every case the lifetime is at most $7<T{=}8$ touched windows, so $a_i=0$ by (11) and $\pi_i$ is false. Erasure also needs $\neg g_i$, which holds because $\ell_i\le2$ throughout, so $p_i\le\sigma(2)\approx0.881<p_{\mathrm{grad}}{=}0.9$ and the cell never graduates. Erasure discards $\boldsymbol{\phi}_i$, so each re-created cell starts again from $n_i=0$. $\square$

The race couples the prune rule (16) to the touched-window gate; the reference model, which never prunes, is not subject to it.

**Per-dimension cost of the shared engine (E4).** At every tested extent `grid2d`'s `integrate()` is cheaper than `voxel3d`'s at the same nominal extent and hit rate. Over the 3 quiet-host recordings the ratio of median latencies is 7.7–8.0$\times$ at $100^2$, 8.1–8.5$\times$ at $250^2$ and 9.6–10.4$\times$ at $500^2$ (Table 8, Figure 5); the recordings agree to within a factor of 1.09. In all 16 timing files that exist, including 13 taken earlier on a loaded host or marked invalid (`results/e4_exploratory/`), `grid2d` is the cheaper backend at every extent, but the ratio in those files varies widely and is not used. The live-cell counts are exact: `voxel3d` ends each run holding 10.0$\times$, 6.5$\times$ and 4.4$\times$ as many cells, because its half-voxel ray march creates a hash entry for every traversed free voxel, whereas exact 2D Bresenham clears a bounded strip. We measured neither ray steps nor the per-cell update cost under each backend separately, so we do not attribute the latency difference to one cause. Memory is not measured: the harness multiplies the live-cell count by a fixed 56 B per cell (per-cell evidence and hash node with periodicity off; table and backend overhead excluded), which gives 37.0 MB for the largest 3D configuration (661,410 cells).

**Table 8.** Cost of the shared engine under each backend (E4): 150 random hits per window, 20 windows, resolution 1.0, periodicity off. Latency per `integrate()` call: median over the 3 quiet-host recordings of each recording's median of 15 repeats, with the smallest and largest recording median in brackets; it depends on the machine. Live-cell counts, their ratios and memory (at 56 B per cell) follow from the seeded geometry and reproduce exactly. Ratios are voxel3d over grid2d at equal extent, the latency ratio as its range over the recordings.

| Backend | Extent | $\mu$s / `integrate` [min, max] | Live cells | Memory |
|---|---|---|---|---|
| `grid2d` | $100^2$ | 66.9 [65, 67] | 8,553 | 479 KB |
| `voxel3d` | $100^2$ | 518.2 [516, 520] | 85,399 | 4.78 MB |
| *ratio* |  | *7.7–8.0$\times$* | *10.0$\times$* |  |
| `grid2d` | $250^2$ | 256.8 [257, 258] | 45,657 | 2.56 MB |
| `voxel3d` | $250^2$ | 2,163.3 [2,080, 2,193] | 295,300 | 16.5 MB |
| *ratio* |  | *8.1–8.5$\times$* | *6.5$\times$* |  |
| `grid2d` | $500^2$ | 673.7 [657, 692] | 150,096 | 8.41 MB |
| `voxel3d` | $500^2$ | 6,661.6 [6,622, 7,006] | 661,410 | 37.0 MB |
| *ratio* |  | *9.6–10.4$\times$* | *4.4$\times$* |  |

> **Figure 5.** **Per-call cost and live-cell count (E4)**, log scale. Latency bars are the median over the 3 quiet-host recordings and whiskers span the recordings; the live-cell counts are exact. Plotted from `results/e4_recordings/` and `e4_throughput.csv`. *(Drawn in the PDF with TikZ/pgfplots from the data in `paper/arxiv_ref/figures/data/`; not reproduced in this Markdown file.)*

## 5.3 Trajectory-Level False Alarms and Confusion (E5) {#sec:traj}

**Table 9.** Choice of the spent level $\delta$ (E5) on calibration seeds disjoint from every evaluation seed. *Worst ever-Periodic*: over 36 configurations (E2 and shipped parameters, touched every window or with probability 0.5, Bernoulli($m$) clutter, $m=0.1,\dots,0.9$, 2000 cells, 1024 windows), the largest count of cells labelled Periodic at some window, with the Wilson 95\% upper limit of that rate. *Door*: median first window from which a 4-on/4-off door (E2 parameters, $T{=}8$) or a 12-on/12-off door (shipped, $T{=}24$) stays Periodic. Rule, fixed before the run: the largest $\delta$ whose upper limit is $\le 0.01$ in every configuration.

| $\delta$ | Worst ever-Periodic (of 2000) | Wilson upper | Door, $T{=}8$ / $T{=}24$ | Selected |
|---|---|---|---|---|
| 0.01 | 0 | 0.0019 | 34 / 33 | no |
| 0.02 | 0 | 0.0019 | 32 / 30 | no |
| 0.05 | 0 | 0.0019 | 30 / 26 | no |
| 0.1 | 1 | 0.0028 | 27 / 24 | no |
| **0.2** | **2** | **0.0036** | **25 / 24** | **yes** |

**Table 10.** Trajectory-level false alarms of the live detector on held-out seeds (E5). Each entry is the worst Bernoulli($m$) clutter configuration over $m=0.1,\dots,0.9$: the number of 2000 cells (10 seeds of 200) labelled Periodic at some window up to $N$, with its Wilson 95\% interval for $N{=}1024$, and the worst per-window labelling rate. $q$: probability that a cell is touched in a window, independent of its occupancy. *Single*: $\delta{=}0.1$ at every read-out; *Spent*: $\delta{=}0.2$ spent over the touch count. *Markov*: temporally correlated clutter (two-state chain, mean 0.5, mean dwell 10 windows), outside the null of Proposition 3. Constant cells were never labelled Periodic.

| Parameters, touches | Rule | $N{=}64$ | $N{=}256$ | $N{=}1024$ | 95\% interval, $N{=}1024$ | Per window | Markov, $N{=}1024$ |
|---|---|---|---|---|---|---|---|
| E2, every window | Single | 84 | 280 | 953 ($m{=}0.4$) | [0.4547, 0.4984] | 0.0045 | 349 |
|  | Spent | 0 | 1 | 2 ($m{=}0.4$) | [0.0003, 0.0036] | $3.9{\times}10^{-6}$ | 2 |
| E2, $q{=}0.5$ | Single | 20 | 127 | 543 ($m{=}0.4$) | [0.2525, 0.2914] | 0.0047 | 79 |
|  | Spent | 0 | 0 | 2 ($m{=}0.4$) | [0.0003, 0.0036] | $3.9{\times}10^{-6}$ | 0 |
| Shipped, every window | Single | 64 | 66 | 202 ($m{=}0.3$) | [0.0885, 0.1150] | 0.0008 | 1998 |
|  | Spent | 2 | 2 | 2 ($m{=}0.4$) | [0.0003, 0.0036] | $1.0{\times}10^{-6}$ | 1908 |
| Shipped, $q{=}0.5$ | Single | 18 | 52 | 102 ($m{=}0.3$) | [0.0422, 0.0615] | 0.0009 | 1331 |
|  | Spent | 1 | 1 | 1 ($m{=}0.3$) | [0.0001, 0.0028] | $3.4{\times}10^{-6}$ | 409 |

**Table 11.** Four-class confusion on 20 held-out scenes, touched every window, read out after 64 windows with the shipped test (E5 for the E2 parameters, E6 for the others; same seeds). Per scene: 50 walls, 20 doors of each of three types at random phase, 100 aperiodic Bernoulli(0.5) cells, and 50 dynamic cells crossed by a mover with probability 0.1 per window. Doors: 4-on/4-off, 2-on/6-off and 2-on/2-off for the E2 parameter sets ($T{=}8$); the same duty cycles at three times the period for the shipped defaults ($T{=}24$). *Single P*: Periodic count under the single-read-out rule and, after the slash, cells labelled Periodic at some window up to 256 under the single-read-out and the spent rule (E2 parameters only). The recall of a class over this fixed mix of cell types is a property of the mix, so counts are reported per type.

| Parameters | Ground truth (cells) | Static | Periodic | Transient | Unknown | Single P / ever P (single, spent) |
|---|---|---|---|---|---|---|
| E2 ($\lambda{=}1$) | wall (1000) | 1000 | 0 | 0 | 0 | 0 / 0, 0 |
|  | door (1200) | 0 | 800 | 400 | 0 | 800 / 800, 800 |
|  | aperiodic (2000) | 1221 | 0 | 672 | 107 | 12 / 219, 1 |
|  | dynamic (1000) | 1 | 0 | 705 | 294 | 0 / 1, 0 |
|  aperiodic cells falsely Static: 0.611 [0.589, 0.632] (Wilson 95\%); after 256 windows 1223 of 2000 |  |  |  |  |  |  |
| E2 with $\lambda{=}0.97$ | wall (1000) | 1000 | 0 | 0 | 0 | – |
|  | door (1200) | 0 | 1200 | 0 | 0 | – |
|  | aperiodic (2000) | 1042 | 0 | 861 | 97 | – |
|  | dynamic (1000) | 0 | 0 | 686 | 314 | – |
|  aperiodic cells falsely Static: 0.521 [0.499, 0.543] (Wilson 95\%); after 256 windows 1019 of 2000 |  |  |  |  |  |  |
| Shipped defaults | wall (1000) | 1000 | 0 | 0 | 0 | – |
|  | door (1200) | 0 | 1091 | 18 | 91 | – |
|  | aperiodic (2000) | 1960 | 0 | 40 | 0 | – |
|  | dynamic (1000) | 11 | 0 | 917 | 72 | – |
|  aperiodic cells falsely Static: 0.980 [0.973, 0.985] (Wilson 95\%); after 256 windows 1983 of 2000 |  |  |  |  |  |  |

**What a deployed map does.** E0 and E2 read a cell once per run. The map labels every cell after every window, so the event that matters for a deployed map is that a non-periodic cell is labelled Periodic at *some* window of the run. E5 measures this event directly on held-out seeds, for the live detector with pruning, re-creation and re-testing, over horizons of 64, 256 and 1024 windows (Table 10; every configuration in Table 32).

**The single-read-out level does not control it.** With $\delta{=}0.1$ applied at every read-out, up to 84 of 2000 Bernoulli clutter cells are labelled Periodic within 64 windows and 953 of 2000 within 1024 (E2 parameters, $m{=}0.4$, 95\% interval [0.4547, 0.4984]). The per-window labelling rate stays small, at most 0.0047, but it accumulates over the run. The shipped parameters are affected less, because their slower log-odds walk prunes Bernoulli clutter less often, yet 202 of 2000 cells still flip at some window ($m{=}0.3$).

**The spent level does.** With $\delta{=}0.2$ spent over the touch count, chosen on the calibration seeds (Table 9), at most 2 of 2000 Bernoulli clutter cells are ever labelled Periodic within 1024 windows in every held-out configuration, a Wilson 95\% upper limit of 0.0036, and the per-window rate is at most $3.9{\times}10^{-6}$. Always-occupied and always-free cells are never labelled Periodic under either rule. The proved bound of Proposition 4 covers the never-pruned case at 0.2. For pruned clutter the proved bound $\delta\,\mathbb{E}[L_N]$ is vacuous (a Bernoulli(0.4) cell under the E2 parameters begins about 62 histories), and the measured rate sits far below it: the Chernoff bound is conservative, and successive read-outs of one history are strongly correlated, so the union bound over them is loose.

**Correlated clutter is flagged.** The Markov cells violate the independence the bound assumes: their occupancy has real power at low frequencies. Under the shipped $T{=}24$ the spent test labels 1908 of 2000 of them Periodic within 1024 windows (1998 at a single read-out); at $T{=}8$ it labels 2. The periodogram test cannot tell a slow random process from a period of 24 windows; this is a limitation of the null, not a failure of the bound.

**What it costs.** On held-out seeds the median window from which a noiseless 4-on/4-off door stays Periodic moves from 15 to 25, a 2-on/2-off door from 13 to 20, a 3-on/5-off door from 21 to 31, and a 4-on/4-off door with 10\% flipped windows from 26 to 48; at the shipped $T{=}24$ a 12-on/12-off door stays at 24 windows, and with 10\% flips it moves from 25 to 40. No door run fails to become Periodic.

**Four-class confusion.** Table 11 scores all four labels over 20 held-out scenes. Under the E2 parameters walls are Static and dynamic cells Transient or Unknown, with one exception in 1000. No cell other than a door is Periodic. All 800 50\%-duty doors are Periodic and none of the 400 25\%-duty doors, which Proposition 8 proves are never Periodic; the recall of the Periodic class is therefore a property of the scene mix, and we report counts per cell type rather than a recall. Within 256 windows the single-read-out level labels 219 of 2000 aperiodic cells Periodic at some window, the spent level 1. The dominant error is elsewhere: 1221 of 2000 aperiodic Bernoulli(0.5) cells end falsely Static, a share of 0.611 [0.589, 0.632].

**Table 12.** Bernoulli($m$) cells labelled Static (E6): of 2000 cells per entry (10 seeds of 200), touched every window, the number Static after 256 windows and, in parentheses, the number Static at some window up to 256. *Shipped, symmetric*: the shipped defaults with $l_{\mathrm{miss}}=-l_{\mathrm{hit}}$. $m^\dagger$ is the mean-field threshold of Remark 1. E3 scores Bernoulli(0.6) cells as walls, for which Static is the correct label; E5 scores Bernoulli(0.5) cells as clutter, for which it is an error.

| $m$ | E2 ($\lambda{=}1$) | E2, $\lambda{=}0.97$ | Shipped | Shipped, symmetric |
|---|---|---|---|---|
| $m^\dagger$ | 0.500 | 0.534 | 0.354 | 0.525 |
| 0.10 | 0 (65) | 0 (44) | 11 (485) | 7 (395) |
| 0.20 | 32 (788) | 21 (605) | 172 (1767) | 51 (1618) |
| 0.25 | 65 (1356) | 44 (1098) | 396 (1973) | 120 (1900) |
| 0.30 | 138 (1735) | 87 (1559) | 783 (1995) | 196 (1986) |
| 0.35 | 288 (1921) | 177 (1857) | 1279 (2000) | 320 (1998) |
| 0.40 | 481 (1996) | 367 (1975) | 1698 (2000) | 537 (2000) |
| 0.50 | 1206 (2000) | 1043 (2000) | 1974 (2000) | 1156 (2000) |
| 0.60 | 1797 (2000) | 1695 (2000) | 1998 (2000) | 1741 (2000) |
| 0.70 | 1965 (2000) | 1950 (2000) | 2000 (2000) | 1965 (2000) |
| 0.80 | 1998 (2000) | 1997 (2000) | 2000 (2000) | 1999 (2000) |

**False Static labels (E6).** The periodicity test does not decide this error; the graduation rule does, and decay does not remove it. With $\lambda{=}0.97$ added to the E2 parameters, 1042 of 2000 aperiodic cells still end Static. With the shipped defaults 1960 of 2000 do, a share of 0.980 [0.973, 0.985] (1983 after 256 windows), and so do 11 of 1000 dynamic cells; of the 1200 doors of that scene 1091 are Periodic after 64 windows and all 1200 after 256 (Table 11). With $\lambda{=}0.97$ all 1200 doors of the E2 scenes are Periodic, the 25\%-duty ones included; we did not analyse why the race of Proposition 8 is not lost there. Remark 1 gives the reason for the Static labels: the shipped $l_{\mathrm{hit}}{=}0.85$ and $l_{\mathrm{miss}}{=}{-}0.4$ put the mean-field threshold at $m^\dagger=0.354$, so a cell that is occupied in half of its windows drifts up to the clamp. Table 12 measures the transition. With the shipped defaults, 396 of 2000 Bernoulli(0.25) cells are Static after 256 windows, 783 at $m{=}0.30$, 1279 at $0.35$, 1698 at $0.40$ and 1974 at $0.50$; and 485 of 2000 Bernoulli(0.10) cells are Static at some window. Symmetric increments ($l_{\mathrm{miss}}=-l_{\mathrm{hit}}$) move $m^\dagger$ to 0.525 and the count at $m{=}0.50$ to 1156, but they cost noisy walls: of Bernoulli(0.60) cells, which E3 scores as walls, 1741 of 2000 are then Static, against 1998 with the shipped defaults. No parameter set of the sweep separates the two populations. On the 0.05 grid the share of Static cells rises from below 10\% to above 95\% over an interval of $m$ that is 0.30 wide with the shipped defaults and 0.35 wide with the other three sets. A user who knows the occupancy rate of the clutter to reject and of the walls to keep can place $m^\dagger$ between them through $l_{\mathrm{miss}}$ and $\lambda$, as far as the two rates lie further apart than that interval. We measured only these four sets, on iid cells touched in every window, and changed no default.

## 5.4 Ablation Studies {#sec:ablation}

**Table 13.** Hysteresis and decay ablation (E3), selected rows of the 36-configuration sweep on identical replayed noise (full sweep in Table 30). Band $=p_{\mathrm{grad}}-p_{\mathrm{dem}}$; flicker counts per-window `isStatic` toggles over 80 windows. Bold marks the lowest flicker in the sweep and the worst configuration.

| $p_{\mathrm{grad}}$ | $p_{\mathrm{dem}}$ | Band | $\lambda$ | Flicker $\downarrow$ | F1 $\uparrow$ |
|---|---|---|---|---|---|
| 0.9 | 0.9 (deg.) | 0.0 | 0.90 | **574** | **0.810** |
| 0.8 | 0.8 (deg.) | 0.0 | 0.90 | 343 | 0.947 |
| 0.7 | 0.7 (deg.) | 0.0 | 0.90 | 250 | 0.980 |
| 0.9 | 0.9 (deg.) | 0.0 | 0.97 | 150 | 1.000 |
| 0.9 | 0.9 (deg.) | 0.0 | 1.00 | 122 | 1.000 |
| 0.9 | 0.3 | 0.6 | 0.90 | 54 | 1.000 |
| 0.9 | 0.3 | 0.6 | 0.97 | **52** | **1.000** |
| 0.9 | 0.3 | 0.6 | 1.00 | **52** | **1.000** |

> **Figure 6.** **Flicker transitions across the E3 sweep** (colour: $\log_{10}$ flicker; annotation: raw count). The zero-band column ($p_{\mathrm{dem}}{=}p_{\mathrm{grad}}$) holds the worst cell of every panel; widening the band suppresses flicker far more than slowing decay does. Plotted from `e3_sensitivity.csv`. *(Drawn in the PDF with TikZ/pgfplots from the data in `paper/arxiv_ref/figures/data/`; not reproduced in this Markdown file.)*

**Hysteresis band versus decay (E3).** E3 ablates the two stabilisers of the static layer on identical replayed noise (Table 13, full sweep in Table 30). Removing the hysteresis band has the largest effect in the sweep: at $p_{\mathrm{grad}}{=}p_{\mathrm{dem}}{=}0.9$ and $\lambda{=}0.90$, the worst configuration, the wall layer flickers 574 times and final F1 drops to 0.810 (recall 0.68), whereas the same noise and decay with the band widened to 0.6 ($p_{\mathrm{dem}}{=}0.3$) flickers 54 times at F1 1.000, a more than tenfold reduction attributable to the band alone. Among zero-band configurations at $\lambda{=}0.90$, flicker rises with the coincident threshold (250 at 0.7, 343 at 0.8, 574 at 0.9), even though a higher threshold is usually the more conservative choice: a threshold pair with no gap is not a substitute for a band wherever it sits. Decay is second order. Holding the worst pair fixed, flicker falls as forgetting slows (574 at $\lambda{=}0.90$, 150 at 0.97, 122 at 1.0), because faster decay lets noisy log-odds cross the coincident threshold more often; with the widest band, decay barely matters (52–54 toggles across all three settings). Hysteresis, not decay, is what keeps the static layer stable (Figure 6).

**Periodicity on (E3).** Rerunning the sweep with periodicity on at the shipped periodicity defaults (legacy arm) leaves final F1 unchanged in every configuration (last column of Table 30), and flicker is now identical to that with periodicity off in every configuration. The total over the 36 configurations is 3672 in both cases, against 3792 with the single-read-out level, 5472 with the centred amplitude-only rule and 7132 with v0.1.0. The widest-band configuration at $\lambda{=}0.90$ has 54 toggles, against 58 (single read-out), 102 (centred) and 138 (v0.1.0). These walls are free in 40\% of their windows by construction, so their centred amplitude is sampling noise. Under the amplitude-only rule it crossed $a_{\min}$ often, and the unbanded $\vee\,\pi_i$ demotion (§3.3) toggled the wall out of the Static layer. With the single-read-out level, 1 or 2 such demotions remained per configuration, because each wall was re-tested in every window. With the spent level, the probability that $\pi_i$ demotes a wall during one uninterrupted history is at most $\delta$ (Proposition 4); none is demoted in any configuration.

## 5.5 The Sequential Test on the Synthetic Suite (sprt Arm and E7) {#sec:sprtsynth}

**Table 14.** The synthetic suite in both arms. *Legacy*: the v0.1 arithmetic with the explicit parameters of the first release; *sprt*: the sequential test at the shipped defaults, same scenes and seeds. Counts are cells of the scene mix stated with each experiment (Section 5.1). A shipped-default Bernoulli(0.5) cell lies between $m_0$ and $m_1$, where the test promises nothing.

| Measure | Legacy | Sprt |
|---|---|---|
| E1: first window with Static recall 1.0 (index $t$) | 2 | 3 |
| E1: final precision, `grid2d` / `voxel3d`, high clutter | 0.9253 / 0.9758 | 1.0000 / 1.0000 |
| E2 at $n{=}64$: Periodic true / false positives | 2/3, 0/5 | 3/3, 0/5 |
| E5: worst ever-Periodic of 2000 Bernoulli cells within 1024 windows (spent level) | 2 | 11 |
| E5: Markov clutter ever Periodic, shipped period, of 2000 | 1908 | 1969 |
| E6: Bernoulli(0.5) cells Static at window 64, shipped defaults, of 2000 | 1960 | 1120 |
| E6: dynamic cells Static at window 64, shipped defaults, of 1000 | 11 | 0 |
| E6: Bernoulli(0.5) cells read out Intermittent at window 256, of 2000 | – | 1 |
| E4: live cells, `grid2d` at $100^2$ | 8,553 | 9,077 |

The sprt arm repeats E0–E6 on the same scenes and seeds with `evidence_model = sprt` at the shipped pair (Table 14); the harness changed only by taking the arm as an argument, and the legacy arm still reproduces its committed files byte for byte (Appendix A). Nothing below is tuned: the periodic levels are chosen again by the pre-stated rules of E0 and E5 and come out as before, $\delta=0.1$ for a single read-out and $\delta=0.2$ spent over the touch count.

**E1.** With the sequential test, recall reaches 1.0 at the window index $t=3$, the derived 4th window, in all six runs, and the final precision is 1.0000 in every run. The false Static cells that the v0.1 rule created under 100 movers per window (precision 0.9253 for `grid2d` and 0.9758 for `voxel3d`) do not appear.

**E2, E5.** In E2 the Periodic class has 3/3 true positives and 0/5 false positives at $n=64$; the 25\%-duty door that the prune/maturity race cost the legacy arm is Periodic, because the periodicity gate keeps the cell alive until $T$ touched windows exist. In E5 the largest number of Bernoulli clutter cells of 2000 that are ever labelled Periodic within 1024 windows on held-out seeds is 11 (Wilson 95\% upper limit 0.0098), against 2 in the legacy arm: the 0.01 target is met only at the margin. Temporally correlated clutter at the shipped period is labelled Periodic in 1969 of 2000 cells (legacy: 1908), so the limitation of the null is unchanged.

**Table 15.** Sweep of the pair and the error rates (E3, sprt arm): 50 noisy walls (occupancy 0.6) among 10 movers per window, 80 windows, same replayed noise in every row, periodicity off. $\alpha{=}\beta$ in the second column. *Latency*: windows an always-hit cell needs; *Demote*: all-miss windows that demote a graduated cell. The sweep picks nothing; a row is a consequence of its pair. The ceiling margin is not swept because it does not enter the dynamics of a cell that is not at the ceiling.

| $m_0$ | $m_1$ | $\alpha{=}\beta$ | $A$ | Latency | Demote | Recall | F1 | Flicker |
|---|---|---|---|---|---|---|---|---|
| 0.2 | 0.7 | 0.05 | 2.944 | 3 | 4 | 0.78 | 0.876 | 169 |
| 0.2 | 0.7 | 0.01 | 4.595 | 4 | 5 | 0.90 | 0.947 | 101 |
| 0.2 | 0.7 | 0.001 | 6.907 | 6 | 8 | 0.96 | 0.980 | 60 |
| 0.2 | 0.6 | 0.05 | 2.944 | 3 | 5 | 0.94 | 0.969 | 113 |
| 0.2 | 0.6 | 0.01 | 4.595 | 5 | 7 | 0.98 | 0.990 | 65 |
| 0.2 | 0.6 | 0.001 | 6.907 | 7 | 10 | 1.00 | 1.000 | 52 |
| 0.3 | 0.7 | 0.05 | 2.944 | 4 | 4 | 0.58 | 0.734 | 143 |
| 0.3 | 0.7 | 0.01 | 4.595 | 6 | 6 | 0.84 | 0.913 | 68 |
| 0.3 | 0.7 | 0.001 | 6.907 | 9 | 9 | 0.80 | 0.889 | 48 |
| 0.1 | 0.5 | 0.05 | 2.944 | 2 | 6 | 1.00 | 1.000 | 72 |
| 0.1 | 0.5 | 0.01 | 4.595 | 3 | 8 | 1.00 | 1.000 | 54 |
| 0.1 | 0.5 | 0.001 | 6.907 | 5 | 12 | 1.00 | 1.000 | 52 |

**E3.** E3 has no $p_{\mathrm{grad}}$, $p_{\mathrm{dem}}$ or $\lambda$ in the sprt arm; it sweeps four pairs at three error rates instead (Table 15). At the shipped pair with $\alpha=\beta=0.01$ the wall layer has recall 0.90 and 101 toggles; over the 12 rows the flicker lies between 48 and 169. The latency and the number of all-miss windows that demote a wall are consequences of the pair and the error rates, not tuned constants.

**E6.** With the shipped pair, 1120 of 2000 Bernoulli(0.5) cells are Static at window 64, against 1960 in the legacy arm, and 0 of 1000 dynamic cells; 1 of the 2000 is read out Intermittent after 256 windows. A Bernoulli(0.5) cell lies between $m_0$ and $m_1$ and above the zero-drift rate $m^\ast=0.4391$, where the test promises nothing and drifts upward. After 256 windows, 2 of 2000 Bernoulli(0.20) cells are Static (107 at some window), 52 at $m=0.30$, 378 at $0.40$, 1101 at $0.50$, 1745 at $0.60$ and 1948 at $0.70$. Of the 1200 doors of the shipped-default scene, 1119 are Periodic after 64 windows and all 1200 after 256. The share of Static cells rises with $m$ along the operating characteristic of Table 3, not at a point.

**E4 and E7.** The cell counts of E4 grow slightly, for example 8,553 to 9,077 live cells for `grid2d` at $100^2$; no timing was recorded for the sequential test. E7 (Section 4.5, Tables 3 and 2) compares the engine with the exact lattice computation and Wald's closed form and measures the effect of autocorrelation; it checks the operating characteristic of the engine, and with it the first-test error rates of Proposition 7, under independent windows (Periodic veto off).

# 6 Calibrating the Pair on Real Data {#sec:calib}

A pair $(m_0,m_1)$ is a statement about the sensor, the window length and the resolution: the hit rate that structure reaches and the hit rate that clutter does not exceed. Section 4 makes the user state it. This section asks whether we can state it from data. We set the procedure down in a pre-registration before the first calibration measurement and applied it to calibration sequences that are separate from the test sequences of Section 7. The pre-registration was amended after that measurement and after an independent review of it; the amendments that set a definition, a threshold or a rule came before the measurement that the tables below report and before any test-sequence file was opened. The first amendment changed the time base of the LILocBench poses, the use of the semantic classes of KITTI 00, the survivorship of cells with few touched windows, the bound on the skew between the two lasers of the pose check and the wall-thickness gate. The second amendment set the quality gates, the rounding direction, the one-second window, the 3D miss branch, the move of Keble 02 to the calibration set, the freeze rule with its rule for $n_{\max}$, the decision on Option C and the floor rule of the engine. Four later amendments changed no definition, threshold or rule; they record review findings, the provenance rules and the provisional status of the pair. The third to the fifth were committed before the first logged opening of a test-sequence file, the sixth after it (access log of Section 7). The procedure below is the amended one. We report the outcome. *The outcome is that nothing froze.* The shipped pair is the pair that the procedure would give if the failed quality gate were waived, and it is labelled provisional wherever it appears.

## 6.1 Procedure {#sec:calibproc}

**Data and split.** The calibration set holds the LILocBench [@trekel2025lilocbench] sequences `mapping`, `static_0` and `dynamics_0` (2D, a wheeled robot in an office), the KTH benchmark [@zhang2023dynamic] sequence KITTI 00 (3D, 64 beams, a single drive) and, after a re-split described below, the Oxford Spires [@tao2024spires] sequence Keble 02 (3D, 64 beams, one site). The test set holds the sequences of Section 7. The split, the statistic, the thresholds and the rule below, in their amended form, were committed before any test-sequence file was opened; the files of the test set were recorded with size and modification time before and after each phase, and every later read of a test file is listed in an access log. The re-split moved Keble 02, the lowest-numbered Keble sequence that has a ground-truth trajectory (a rule that does not look at content), from the test set to the calibration set, in an amendment committed before any test file was opened. It was needed because KITTI 00 alone, a short drive, cannot show whether most structure voxels can be decided by a sequential test.

**Cells and labels.** For each cell touched in at least five windows, the hit rate is $r=H/N$ with $H$ the windows with a hit and $N$ the touched windows. A miss is a window in which a ray passed through the cell to a return beyond it; a beam without a return adds nothing. Structure cells are those that the reference holds as occupied (LILocBench: occupied in `static_0`; KITTI 00: static voxels above the ground; Keble 02: voxels of the terrestrial-laser-scanner map, restricted to the area around the trajectory). Clutter cells are those in which something moves (LILocBench: cells hit in `dynamics_0` that were free in the mapping reference; KITTI 00: voxels that hold a point flagged dynamic; Keble 02: hits that the terrestrial map does not contain). The window is one second, the length of the windows of the shipped parameter files of the node: 15 scans at 15 Hz in 2D, 10 scans at 10 Hz in 3D. A first calibration run used ten scans per window (0.67 s at 15 Hz); the one-second condition was added before the freeze step.

**Rule.** Let $q_s$ be the 10th percentile of the hit rate over structure cells and $q_c$ the 90th percentile over clutter cells. The pre-registered escalation rule has three steps. (i) If $q_s-q_c\ge0.15$, take $m_0=\lceil q_c\rceil_{0.05}$ and $m_1=\lfloor q_s\rfloor_{0.05}$, rounded toward each other so that the stated error rates cover the measured percentiles, with $\alpha=\beta=0.01$. (ii) Otherwise measure the gap again with a refined miss that does not count a miss next to the endpoint of a grazing beam (3D only). (iii) Otherwise, as amended, no pair is frozen: the 3D backend falls back on the 2D pair, flagged not calibrated for 3D, and for 2D the lead decides. The original step (iii) added a second statistic on run lengths (Option C); the second amendment replaced it after the first calibration measurement. In every branch the remaining overlap is reported as irreducible. The truncation $n_{\max}$ is the smallest multiple of 16 for which at most 3\% of cells at the zero-drift rate $m^\ast$ are still undecided under the exact rule.

**Quality gates.** A hit-rate percentile is only as good as the map that carries it. Two gates were fixed from sensor specifications in the second amendment, after the first calibration measurement (whose gate looked at the extent of a patch only) and before the measurement that the tables report. G1 measures the thickness $\sqrt{12\lambda_{\min}}$ of linear (2D) or planar and vertical (3D) patches of the accumulated points. It passes if the median and the 90th percentile of the thickness stay under limits derived from the error of the sensor and of the poses (Table 16); the limit of the 90th percentile is twice that of the median. G2 measures pose consistency: each return is compared with the returns of scans one to four seconds away, and the median and 90th percentile of the distance to the nearest returned point must stay within one and two cells of the finest grid of the sensor class. A sequence that fails a gate is excluded from the headline tables; the 2D pair needs all three LILocBench sequences to pass.

## 6.2 Result {#sec:calibresult}

**Table 16.** Quality gates of the calibration sequences (P1b, pre-registered thresholds from the sensor specifications). G1: wall thickness of planar patches (median and 90th percentile of the thickness, metres); G2: leave-one-out pose consistency (median and 90th percentile of the distance, metres). Limits are written under the column names. A sequence that fails a gate is excluded from the headline tables. *No sequence passes*, so the pair derived from them is not frozen.

| Sequence | Class | G1 patches | G1 median / p90 | G1 limits | G2 median / p90 | G2 limits | G1, G2 |
|---|---|---|---|---|---|---|---|
| `lil_mapping` | 2D | 590 | 0.1930 / 0.2506 | 0.1 / 0.2 | 0.0012 / 0.0048 | 0.05 / 0.1 | FAIL, PASS |
| `lil_static_0` | 2D | 391 | 0.1705 / 0.2545 | 0.1 / 0.2 | 0.0013 / 0.0060 | 0.05 / 0.1 | FAIL, PASS |
| `lil_dynamics_0` | 2D | 470 | 0.1832 / 0.2285 | 0.1 / 0.2 | 0.0021 / 0.0276 | 0.05 / 0.1 | FAIL, PASS |
| `kitti00` | 3D | 41 | 0.3720 / 0.4713 | 0.2 / 0.4 | 0.0387 / 0.1113 | 0.1 / 0.2 | FAIL, PASS |
| `keble02` | 3D | 2251 | 0.3212 / 0.4627 | 0.2 / 0.4 | 0.0174 / 0.0415 | 0.1 / 0.2 | FAIL, PASS |

**The gates.** G2 passes on all 5 calibration sequences. G1 fails on all 5: the median patch thickness on the three 2D sequences is between 0.1705 and 0.1930 m against a limit of 0.1 m (Table 16). We cannot say whether the cause is the poses, the time base of the scans, the walls themselves or the gate statistic itself, which a check recorded in the third amendment found not robust to points of a second surface inside a patch. A search for the time offset between scans and poses was run on the `mapping` sequence by a procedure written before use; a first calibration run without it measured pose-timing smear more than hit rates. The gate also fails on an exploratory run without the offset. Because no sequence passes, every table below is printed for the record and is not a headline table.

**Table 17.** Hit-rate gap of structure against clutter on the calibration sequences. $q_s$: 10th percentile of the per-cell hit rate over structure cells; $q_c$: 90th percentile over clutter cells; both for cells touched in at least five windows. The pre-registered rule needs a gap $q_s-q_c\ge0.15$. Block intervals over 10 m tiles in brackets. *LIL*: LILocBench, 2D, structure of `static_0` against hits in free space of `dynamics_0`. *KITTI 00*: KTH benchmark, 3D, structure voxels against voxels that hold a moving point (first calibration run, ten scans per window). *Keble 02*: Oxford Spires, 3D, structure against TLS-absent hits. $m_0,m_1$: the rounding rule of Section 6, defined only when the gap passes; all rows fail a quality gate (Table 16).

| Data | Cell, window | $q_s$ | $q_c$ | Gap [block interval] | $\ge0.15$ | $(m_0,m_1)$ | Cells (struct. / clutter) |
|---|---|---|---|---|---|---|---|
| LIL | 0.05 m, 15 scans | 0.7130 | 0.1725 | 0.5404 [0.4435, 0.5795] | yes | (0.20, 0.70) | 10,126 / 23,888 |
| LIL | 0.10 m, 15 scans | 0.7222 | 0.2381 | 0.4841 [0.3746, 0.5183] | yes | (0.25, 0.70) | 4,582 / 6,220 |
| KITTI 00 | 0.2 m, 10 scans | 0.1250 | 0.1667 | -0.0417 [-0.1071, -0.0238] | no | – | 121,651 / 5,791 |
| Keble 02 | 0.2 m, 10 scans, today's miss | 0.4667 | 1.0000 | -0.5333 [-0.6000, -0.4762] | no | – | 195,641 / 15,864 |
| Keble 02 | 0.2 m, 10 scans, refined miss | 0.5357 | 1.0000 | -0.4643 [-0.5333, -0.4000] | no | – | 192,255 / 13,990 |

**The hit-rate gap.** In 2D the gap is large: $q_s=0.7130$, $q_c=0.1725$, a gap of 0.5404 with a block-bootstrap interval of [0.4435, 0.5795] over 10,126 structure and 23,888 clutter cells (Table 17). The rounding rule gives $(m_0,m_1)=(0.20,0.70)$. The intervals of $q_s$ and $q_c$ cross rounding boundaries, so the pair at both ends of the interval is $(0.20,0.65)$ and $(0.30,0.75)$; both are evaluated in Section 7. At the coarser resolution of 0.1 m the rule gives $(0.25,0.70)$. With the pair $(0.20,0.70,0.01,0.01)$ the derived quantities are those of Table 1, among them $n_{\max}=80$ and an always-hit latency of 4 windows. In 3D the gap is negative under both miss definitions: $q_s=0.4667$ against $q_c=1.0000$ on Keble 02, a gap of -0.5333 with today's miss and -0.4643 with the refined miss, and $-0.0417$ on KITTI 00 ($q_s=0.1250$, $q_c=0.1667$). The Keble 02 result is not evidence that hit rates do not separate people from structure in 3D: 64.3 \% of the clutter cells lie more than half a metre above the device, so the clutter class is mostly not walking people. On KITTI 00 a structure voxel is touched in a median of 4 windows (14 windows close), too few for a sequential test to decide most of them; a lifelong mapper needs revisits.

**Table 18.** Evidence for a second statistic (Option C) on the calibration data: cells matched on hit rate (bands of width 0.2), lag-1 autocorrelation of the window outcomes. AUC: probability that a structure cell has a larger lag-1 autocorrelation than a clutter cell; above 0.5 means structure has the longer runs. Pre-registered reading: support for Option C if the AUC is at least 0.65 or at most 0.35 in a band with at least 100 cells in each group.

| Data | Hit-rate band | Struct. cells | Clutter cells | Lag-1, struct. | Lag-1, clutter | AUC | Reading |
|---|---|---|---|---|---|---|---|
| LIL, 0.05 m, 15 scans | $[0.2,0.4)$ | 99 | 1,382 | 0.329 | 0.360 | 0.493 | fewer than 100 cells |
|  | $[0.4,0.6)$ | 337 | 172 | 0.474 | 0.274 | 0.665 | support, structure longer |
|  | $[0.6,0.8)$ | 1,288 | 91 | 0.468 | 0.231 | 0.720 | fewer than 100 cells |
| Keble 02, 0.2 m, 10 scans | $[0.2,0.4)$ | 7,782 | 2,055 | 0.311 | 0.010 | 0.736 | support, structure longer |
|  | $[0.4,0.6)$ | 13,988 | 2,102 | 0.396 | 0.020 | 0.771 | support, structure longer |
|  | $[0.6,0.8)$ | 27,402 | 2,526 | 0.368 | -0.006 | 0.776 | support, structure longer |
| KITTI 00, 0.2 m, 10 scans | $[0.2,0.4)$ | 25,052 | 137 | 0.154 | 0.360 | 0.338 | support, structure shorter |
|  | $[0.4,0.6)$ | 22,018 | 134 | 0.260 | 0.339 | 0.468 | between |
|  | $[0.6,0.8)$ | 19,036 | 147 | 0.179 | 0.272 | 0.436 | between |

**A second statistic.** The pre-registered reading counts support for Option C if the lag-1 autocorrelation of the window outcomes separates structure from clutter at matched hit rate, with an AUC of at least 0.65 or at most 0.35 in a band with at least 100 cells in each group (Table 18). It does, with opposite signs: in LILocBench (AUC 0.665) and Keble 02 (0.736 to 0.776) structure has the longer runs; in KITTI 00 (0.338) it has the shorter ones. A statistic whose sign changes between datasets is not a fixed rule, and we did not implement Option C. The second amendment decided this after the first (ten-scan) measurement, where the trigger fired with opposite signs; Table 18 repeats the reading at one-second windows and on Keble 02.

**The pair on its own calibration data.** Replayed through the engine on the three one-second 2D sequences, the shipped pair with the Periodic veto on leaves 1.51 \% of the 22,273 clutter cells whose observed hit rate is at most $m_0$ ever Static, 30.5 \% of those above $m_0$, and 3.71 \% of all 24,096 clutter cells with a hit. The last two rows include cells touched fewer than five times, which the held-out rows of Section 7 exclude; the row at or below $m_0$ holds none, since a cell with a hit and a hit rate of at most $m_0$ has at least five touched windows. These per-cell rates exceed the per-history level $\alpha=0.01$ for the reasons of Section 4.4; they are the calibration rows that the held-out rates of Section 7 are read beside.

## 6.3 What froze and what did not {#sec:froze}

Nothing froze. The 2D pair $(0.20,0.70,0.01,0.01)$ is the result of the rule with the failed gate waived, not a freeze. The 3D rule gave no pair, and the 3D backend falls back on the 2D pair, which is *not calibrated for 3D*. The shipped defaults are therefore provisional: the node logs this at start and publishes it on its diagnostics topic, and a test of the library fails if the library defaults, the shipped parameter files and the pre-registration record disagree. The pair applies only to one-second windows, the resolutions measured and the sensors measured. The shipped parameter files set `window_mode` to `time` with one-second windows. The code defaults of the node are `window_mode` `scans` with `layer_interval` $10$, which are not one-second windows, so the pair holds for the shipped parameter files and not for a node started on the code defaults. Re-deriving the pair for another sensor needs a structure reference and per-cell clutter labels, one-second windows, the hit-rate probe (`hitrate_probe`) and the gates G1 and G2. The gate G1 failed here on benchmark data with reference poses, and the record does not show whether the poses or the gate statistic caused it. Without labels $(m_0,m_1)$ can only be chosen by judgement. We evaluate the point pair and both ends of its interval in Section 7 so that the reader sees how much the answers depend on the pair.

# 7 Evaluation on Held-Out Real Data {#sec:realeval}

This section reports what seven evidence rules do on real LiDAR data that none of them was fitted to. It tunes nothing and picks no winner: the pair of the sequential test was fixed before the first test-set run, and the tables show how the answers move with the rule. The wording of the outcomes is ours and follows the numbers of the tables; the pre-registration (`PREREG_P3.md`, with dated amendments) fixes the datasets, the arms, the metrics and the reporting rules before the first run. Before it was committed, harness validation had opened a few test-split files: the first scans of Keble 03 and its trajectory, the first frames and the pose files of one Boreas session, the first frames of both KTH sequences, and the first bag messages and the reference files of both LILocBench test sequences (`PREREG_P3.md`, provenance section, and the access log). Seven amendments were committed before the first evaluation step. The later nine were made during or after the evaluation (`eval_start.txt` and `deviations.md` give the order): they handle failed runs and resumption, the zip reader and the cache-frame read, the populations of two Spires rows, the ever-Static bound, the report-only rows, the re-pin of the engine tree, and corrections of the report. None of them changes an arm, a window, a resolution or a threshold of a run. The memory limit of the Spires runs at 0.1 m was raised once, from a lower value to the limit of $9$ GB. Amendments that define or correct a reported row after results existed say so, and the rows are marked.

## 7.1 Design {#sec:p3design}

**Arms.** All arms run through the same engine, with a window update per touched window, so that the rows differ only in the evidence rule.

- *shipped v0.1*: the rule of the first release, $l_{\mathrm{hit}}{=}0.85$, $l_{\mathrm{miss}}{=}-0.4$, leak 0.97, graduate at $0.8$ after three touched windows, demote at $0.45$ (`evidence_model = legacy`).
- *symmetric*: the same with $l_{\mathrm{miss}}=-l_{\mathrm{hit}}$.
- *OctoMap parameters on window evidence*: the probabilities $0.7$ and $0.4$ of the OctoMap sensor model [@hornung2013octomap] as increments, its clamps, no leak, occupied if $\ell>0$, never pruned. The increments are applied once per touched window, not once per ray and scan, so the row is not OctoMap and its numbers are not comparable to published OctoMap rows.
- *ELite factors on window evidence*: odds factors $9$ and $1/9$ per touched window, no leak, occupied if $\ell>0$. These are the factors that Eq. (5) of ELite [@gil2025elite] gives for an endpoint and for a sampled free point at zero distance from the ray (our arithmetic from the printed equations; the paper states no clamp, and we set $\pm50$). The row is not ELite.
- *SPRT $(0.20,0.70)$*: the sequential test of Section 4 at the shipped pair, $\alpha=\beta=0.01$; *SPRT low end* $(0.20,0.65)$ and *high end* $(0.30,0.75)$ are the pairs at the ends of the calibration interval (Section 6). These two rows show the dependence on the pair; they change nothing about the first.

The OctoMap and ELite arms have no Undecided state: “not Static” is “Rejected” for them. We did not build an arm for MTD-Map [@kim2026mtdmap]: we found no released code, and a reimplementation from the paper alone would fix choices that the paper does not fix.

**Datasets and what each can show.**

- *LILocBench* [@trekel2025lilocbench], `lt_changes_dynamics_0` (with people) and `lt_changes_0` (a control without people), 2D, 0.05 m cells on a 60 m by 60 m grid with its corner at $(-30,-30)$ m, larger than the grid of the shipped file (pre-registration), 15 scans per window (1.0 s). The truth is a reference map of the `mapping` session: a cell that was free there and is hit here is clutter or moved furniture, a reference-occupied cell is structure. No per-object truth exists. The test sequences come from the same building and robot as the calibration sequences, so they test the pair on new recordings and new people, not on a new environment.
- *Oxford Spires* [@tao2024spires], Keble sequences 03 to 05 against the terrestrial-laser map, 3D, 0.2 m and 0.1 m voxels, 10 and 20 scans per window. The sequences are from one day; there are no seasons and no doors. The clutter class is hits that the terrestrial map does not contain.
- *KTH dynamic-map benchmark* [@zhang2023dynamic], KITTI 05 (64 beams) and the semi-indoor sequence (16 beams), 3D, one pass, 0.2 m voxels and, for the semi-indoor sequence only, 0.1 m voxels (KITTI 05 at 0.1 m was not run, by pre-registration, for memory), windows of 10 and 2 frames. Score: static accuracy SA, dynamic accuracy DA, their geometric mean AA, with the benchmark's point-wise rule (the evaluator applies a $0.05$ m tolerance in double precision). A single pass cannot show lifelong behaviour.
- *Boreas* [@burnett2022boreas], three sessions of one route in late autumn, spring and summer, 3D, 0.2 m, ranges 3 to 25 m, Periodic veto off (memory; first amendment of the pre-registration). There is no truth for structure or clutter; the sessions are compared with each other.

**Settings and reporting rules.** Cells with fewer than five touched windows are not eligible for any rate. The Periodic veto is the same for all arms of a run: on is the primary setting (the node ships it), off the secondary setting, which isolates the evidence rule; the Boreas stage runs with the veto off only. Each run records host, load, commit, parameters and the checksums of its inputs, and every run is reported, the failed ones included: 622 runs ended with return code zero and 14 with a non-zero one. A further 82 earlier attempts are kept beside the final records (repeats after a crash, rebuilds and repeated checks). Among them, 3 Keble 04 runs failed on a truncated read of the frame cache and passed on a repeat with an identical command after the reader was changed to retry a failed read (eleventh amendment), one Keble 03 run at 0.1 m failed at a lower memory limit and was repeated at the limit of $9$ GB, and the first conversion of Keble 04 failed because the zip reader did not support zip64 archives.

**What is in the loop.** Every real-data number comes from the offline harness `p3_probe`, which runs `strata_core` without ROS on the reference poses of each dataset and counts windows in scans. For LILocBench the scans are shifted against the poses by $-0.13$ s, the value fixed before the evaluation. The ROS node was not in the loop, so TF latency, transport loss, localiser error and stamp offsets enter no real-data number. Intervals are Wilson intervals, which treat cells as independent, and, where tiles are known, the 95\% percentile interval of a bootstrap over 10 m tiles. The block interval is the one to read. Intervals are per row, are not adjusted for the number of rows, and no hypothesis test is run. Two kinds of row were defined after results existed: the near-wall rows of Table 20 and the ever-Static bound of the Spires clutter rows; they are marked report-only, and the bound is not used in the text. The populations and labels of the Spires latency and end-of-run false-Static rows were fixed by the twelfth amendment, written after the Keble 03 runs and part of the Keble 04 runs at 0.2 m had finished, before any table was written.

## 7.2 Nominal against realised false Static labels (LILocBench) {#sec:p3lil}

**Table 19.** False Static labels on clutter (LILocBench, 2D, 0.05 m cells, 1.0 s windows, Periodic veto on). Clutter: cells that were free in the mapping session and were hit at least once, touched in at least five windows. *Ever Static*: share of those cells Static at the close of any window; *at most $m_0$*: the same restricted to cells whose observed hit rate is at most $m_0{=}0.20$; *block*: 95\% percentile interval of a bootstrap over 10 m tiles (read this one); Wilson 95\% in brackets treats cells as independent. *End*: share Static at the end of the run; *Und.*, *Rej.*: shares of clutter cells that end Undecided or Rejected (arms without an Undecided state show 0). The nominal level $\alpha{=}0.01$ is per history under independent windows and is not the unit of this table. The upper block has people; the lower block (`lt_changes_0`) has none and is a control. Moved furniture puts structure into cells that were free in the mapping session, so the upper block is an upper bound for people alone. The OctoMap and ELite rows apply their increments once per touched window; they are not the published methods.

| Arm | Ever Static, all [Wilson] | block | Ever, $\le m_0$ | End | Und. | Rej. |
|---|---|---|---|---|---|---|
| shipped v0.1 | 21.16 \% [20.76, 21.57] | [17.49, 29.32] | 13.90 \% | 7.53 \% | 85.20 \% | 7.27 \% |
| symmetric | 21.40 \% [21.00, 21.82] | [17.76, 28.43] | 14.41 \% | 5.96 \% | 76.89 \% | 17.15 \% |
| OctoMap parameters on window evidence | 35.18 \% [34.70, 35.66] | [32.35, 46.78] | 28.07 \% | 8.19 \% | 0.00 \% | 91.81 \% |
| ELite factors on window evidence | 11.33 \% [11.02, 11.65] | [9.08, 15.13] | 4.99 \% | 4.22 \% | 0.00 \% | 95.78 \% |
| SPRT $(0.20,0.70)$ | 8.81 \% [8.53, 9.10] | [6.34, 12.54] | 2.93 \% | 4.15 \% | 92.62 \% | 3.23 \% |
| SPRT low end $(0.20,0.65)$ | 8.93 \% [8.65, 9.22] | [6.46, 12.65] | 2.99 \% | 4.40 \% | 92.43 \% | 3.16 \% |
| SPRT high end $(0.30,0.75)$ | 6.41 \% [6.17, 6.66] | [4.21, 8.92] | 1.61 \% | 3.26 \% | 93.32 \% | 3.42 \% |
| shipped v0.1 | 17.58 \% [17.12, 18.05] | [12.62, 25.74] | 7.19 \% | 9.39 \% | 84.68 \% | 5.93 \% |
| symmetric | 17.85 \% [17.39, 18.33] | [12.36, 25.55] | 7.81 \% | 6.91 \% | 77.84 \% | 15.25 \% |
| OctoMap parameters on window evidence | 26.15 \% [25.62, 26.70] | [20.28, 38.53] | 15.45 \% | 9.94 \% | 0.00 \% | 90.06 \% |
| ELite factors on window evidence | 11.81 \% [11.41, 12.21] | [7.78, 16.64] | 2.23 \% | 6.25 \% | 0.00 \% | 93.75 \% |
| SPRT $(0.20,0.70)$ | 9.01 \% [8.66, 9.37] | [5.07, 12.63] | 0.71 \% | 5.52 \% | 91.46 \% | 3.02 \% |
| SPRT low end $(0.20,0.65)$ | 9.13 \% [8.78, 9.49] | [5.14, 12.74] | 0.73 \% | 5.94 \% | 91.11 \% | 2.95 \% |
| SPRT high end $(0.30,0.75)$ | 6.83 \% [6.53, 7.15] | [3.35, 8.76] | 0.15 \% | 4.62 \% | 92.01 \% | 3.37 \% |

The level $\alpha=0.01$ is a bound for the first test of a history under independent windows (Proposition 7); we call it the nominal level per history. The quantity in Table 19 is per cell: the share of eligible clutter cells that were Static at the close of any window of the run. A rejected cell is erased and its next observation (a hit or a miss) starts a new history, so the unit differs from the bound, and the windows of a cell are autocorrelated (Table 2). For SPRT $(0.20,0.70)$ the share is 8.81 \% of the 38,538 eligible clutter cells (Wilson [8.53, 9.10]; tile-block interval [6.34, 12.54]). Restricted to the 34,580 clutter cells whose observed hit rate is at most $m_0$ it is 2.93 \% (Wilson [2.76, 3.11]; tile-block interval [1.87, 3.70]). *Both are above $\alpha$*, and we do not claim that the realised rate is within the nominal level. On the calibration sequences the same code and pair gave 1.51 \% at or below $m_0$ (Section 6.2); the held-out rate is 1.9 times this row, on the same eligibility rule. For all clutter the calibration row (3.71 \%) also counts cells touched fewer than five times, which the held-out rows exclude, so the ratio of 2.4 compares different populations. Table 2 shows the direction of the effect: autocorrelation of the windows raises the per-forward-test share above $\alpha$ (ratio 3.49 for the engine at lag-1 autocorrelation $0.23$, 12.30 for the bare walk). Restarts alone also exceed $\alpha$ with independent windows: 107 of 2000 Bernoulli($m_0$) cells of E6 are Static at some window within 256 windows, a per-cell share that the per-history level does not bound. The four arms other than the three SPRT arms graduate more clutter. Over all eligible clutter cells, shipped v0.1 gives 21.16 \%, symmetric 21.40 \%, OctoMap parameters on window evidence 35.18 \% and ELite factors on window evidence 11.33 \%; at or below $m_0$ they give 13.90 \%, 14.41 \%, 28.07 \% and 4.99 \%. The two ends of the pair give 8.93 \% and 6.41 \% for all clutter.

The control without people, `lt_changes_0`, gives 9.01 \% over all clutter and 0.71 \% at or below $m_0$ for SPRT $(0.20,0.70)$. The all-clutter rate is close to the rate with people. Moved furniture legitimately puts structure into cells that were free in the mapping session, so the rate of `lt_changes_dynamics_0` is an upper bound for people alone, and the control does not isolate the effect of people either. A person who lingers in a cell can produce a hit rate above $m_0$, and a hit-rate test cannot tell such a person from a wall.

**Undecided cells.** In the arms with an Undecided state, most clutter cells end the run Undecided: 92.62 \% for SPRT $(0.20,0.70)$ and 85.20 \% for shipped v0.1. The two binary arms end 91.81 \% (OctoMap parameters) and 95.78 \% (ELite factors) of the clutter cells Rejected; SPRT $(0.20,0.70)$ rejects 3.23 \%. Rejection waits for the touched windows of the periodicity gate, which is one reason for the Undecided share of the sequential arms; we did not measure how much of it the gate explains.

## 7.3 Walls, latency and near-wall cells {#sec:p3walls}

**Table 20.** Walls, latency and near-wall thickening (LILocBench `lt_changes_dynamics_0`, veto on). *Wall runs*: 366 reference-occupied cells on straight wall runs. *Rej.*: share Rejected at the end; *Not S*: share not Static at the end; *Windows*: median and 90th percentile of windows from first touch to the first Static window (one window is 1.0 s). *Near wall*: cells free in the mapping session within 0.10 m of an occupied cell, $n{=}6441$, share Static at the end and ever Static (definition added after the results were seen, so report-only).

| Arm | Rej. | Not S | Windows (p50 / p90) | Near wall, end | Near wall, ever |
|---|---|---|---|---|---|
| shipped v0.1 | 0.00 \% | 1.09 \% | 10 / 49 | 20.17 \% | 39.59 \% |
| symmetric | 0.55 \% | 9.02 \% | 10 / 50 | 13.91 \% | 38.50 \% |
| OctoMap parameters on window evidence | 1.37 \% | 1.37 \% | 1 / 33.6 | 22.23 \% | 54.32 \% |
| ELite factors on window evidence | 5.19 \% | 5.19 \% | 1 / 43 | 9.30 \% | 23.54 \% |
| SPRT $(0.20,0.70)$ | 0.00 \% | 14.21 \% | 33 / 156 | 8.09 \% | 16.63 \% |
| SPRT low end $(0.20,0.65)$ | 0.00 \% | 12.84 \% | 33 / 156 | 8.66 \% | 16.92 \% |
| SPRT high end $(0.30,0.75)$ | 0.00 \% | 21.04 \% | 41 / 190 | 5.64 \% | 10.25 \% |

On the 366 wall-run cells, SPRT $(0.20,0.70)$ and shipped v0.1 reject none (upper Wilson limit 1.04 \%); symmetric rejects 0.55 \%, OctoMap parameters 1.37 \% and ELite factors 5.19 \% (Table 20). The share of wall-run cells that are not Static at the end is 14.21 \% for SPRT $(0.20,0.70)$, 21.04 \% for its high-end pair and 1.09 \% for shipped v0.1. For SPRT $(0.20,0.70)$, 346 of the 366 wall-run cells were Static at the close of some window and 314 at the end. So 32 of the 52 cells that are not Static at the end graduated and then lost the label, by the reverse test or by the Periodic veto, which is one cause of the share; the other 20 never graduated. The windows from first touch to the first Static window have a median and 90th percentile of 33 and 156 for SPRT $(0.20,0.70)$, 10 and 49 for shipped v0.1, and 1 and 33.6 for OctoMap parameters; one window is 1.0 s. A longer wait is expected: the threshold $A=4.5951$ of the sequential test is higher than the $1.386$ of shipped v0.1. These numbers are not the latency of Section 4.5, which is that of an always-hit cell and is 4 windows; real wall cells are not always hit.

Cells that were free in the mapping session and lie within 0.10 m of an occupied cell, two cells wide at 0.05 m, are where a wall thickens (Table 20, 6441 cells, defined after the dumps existed). At the end of the run 8.09 \% of them are Static for SPRT $(0.20,0.70)$, 20.17 \% for shipped v0.1, 9.30 \% for ELite factors and 22.23 \% for OctoMap parameters; ever Static, 16.63 \%, 39.59 \%, 23.54 \% and 54.32 \%.

## 7.4 Oxford Spires: 3D, with a terrestrial reference {#sec:p3spires}

**Table 21.** Oxford Spires, Keble College sequences 03 to 05, 3D, 0.2 m voxels, $W{=}10$ scans (1.0 s), veto on. The 2D pair is used in 3D and is *not calibrated for 3D*. *Clutter*: TLS-absent hits, share Static at the end of the run (a class in which 64.3 \% of the cells on sequence 02 lie more than 0.5 m above the device, so these rates are not evidence about people; the unit is not that of $\alpha$). *Structure*: non-ground cells of the TLS reference map; Rej. is the share Rejected, Und. the share Undecided at the end; *Windows*: median / 90th percentile of windows from first touch to the first Static window over all labelled cells that were ever Static (a population that is not comparable with the LILocBench latency).

| Seq. | Arm | Clutter, Static | Struct. Rej. | Struct. Und. | Windows (p50 / p90) |
|---|---|---|---|---|---|
| Keble 03 | shipped v0.1 | 44.28 \% | 0.33 \% | 7.07 \% | 3 / 12 |
|  | symmetric | 38.67 \% | 1.87 \% | 10.94 \% | 3 / 12 |
|  | OctoMap parameters on window evidence | 51.39 \% | 7.45 \% | 0.00 \% | 1 / 5 |
|  | ELite factors on window evidence | 39.45 \% | 11.70 \% | 0.00 \% | 1 / 7 |
|  | SPRT $(0.20,0.70)$ | 28.28 \% | 0.70 \% | 16.76 \% | 5 / 16 |
|  | SPRT low end $(0.20,0.65)$ | 28.50 \% | 0.59 \% | 15.35 \% | 5 / 16 |
|  | SPRT high end $(0.30,0.75)$ | 15.57 \% | 1.10 \% | 22.39 \% | 7 / 19 |
| Keble 04 | shipped v0.1 | 52.12 \% | 0.66 \% | 10.56 \% | 4 / 18 |
|  | symmetric | 45.38 \% | 2.92 \% | 15.33 \% | 4 / 19 |
|  | OctoMap parameters on window evidence | 56.27 \% | 11.17 \% | 0.00 \% | 1 / 8 |
|  | ELite factors on window evidence | 44.78 \% | 16.81 \% | 0.00 \% | 1 / 11 |
|  | SPRT $(0.20,0.70)$ | 37.49 \% | 0.77 \% | 22.50 \% | 6 / 73 |
|  | SPRT low end $(0.20,0.65)$ | 38.39 \% | 0.67 \% | 20.65 \% | 6 / 68 |
|  | SPRT high end $(0.30,0.75)$ | 26.48 \% | 1.23 \% | 28.04 \% | 8 / 165 |
| Keble 05 | shipped v0.1 | 41.18 \% | 0.52 \% | 9.50 \% | 4 / 19 |
|  | symmetric | 35.69 \% | 2.50 \% | 13.91 \% | 4 / 20 |
|  | OctoMap parameters on window evidence | 45.36 \% | 9.80 \% | 0.00 \% | 1 / 8 |
|  | ELite factors on window evidence | 35.28 \% | 15.85 \% | 0.00 \% | 1 / 9 |
|  | SPRT $(0.20,0.70)$ | 28.18 \% | 0.71 \% | 21.00 \% | 6 / 83 |
|  | SPRT low end $(0.20,0.65)$ | 28.76 \% | 0.61 \% | 19.19 \% | 6 / 83 |
|  | SPRT high end $(0.30,0.75)$ | 18.01 \% | 1.17 \% | 27.18 \% | 8 / 173 |

In 3D the 2D pair is used and is not calibrated, and the clutter class is weak (Table 21); the rates are not evidence about people. At 0.2 m with 10 scans per window and the veto on, the share of the 18,791 clutter cells of Keble 03 that is Static at the end is 28.28 \% for SPRT $(0.20,0.70)$ against 44.28 \% for shipped v0.1; on Keble 04 and 05 it is 37.49 \% and 28.18 \% against 52.12 \% and 41.18 \%. The share of non-ground structure that ends Undecided is 16.76 \%, 22.50 \% and 21.00 \% for SPRT $(0.20,0.70)$ on Keble 03, 04 and 05, against 7.07 \%, 10.56 \% and 9.50 \% for shipped v0.1. The share Rejected is 0.70 \% to 0.77 \% for SPRT $(0.20,0.70)$, 0.33 \% to 0.66 \% for shipped v0.1, 7.45 \% to 11.17 \% for OctoMap parameters and 11.70 \% to 16.81 \% for ELite factors. The median windows to the first Static window of non-ground structure are 5 to 6 for SPRT $(0.20,0.70)$ and 3 to 4 for shipped v0.1; this population is not that of the LILocBench latency.

**Table 22.** Agreement between sessions. *Spires*: Jaccard index of the Static sets of the three same-day Keble sequences (pairs 03/04, 03/05, 04/05) in the 90 10 m tiles seen by all, 0.2 m, $W{=}10$, veto on. *Boreas*: Jaccard index of the Static sets of three independent maps of one route in autumn, spring and summer (pairs autumn/spring, autumn/summer, spring/summer), 617 tiles seen by all, 0.2 m, $W{=}10$, ranges 3 to 25 m, veto off. *Only cumulative*: share of the Static cells of the map built over the three sessions in turn that are Static in no independent session. Jaccard agreement is not accuracy: Boreas has no truth for structure or clutter.

| Arm | Spires, Jaccard 03/04 | 03/05 | 04/05 | Boreas, Jaccard aut./spr. | aut./sum. | spr./sum. | Boreas, only cumulative |
|---|---|---|---|---|---|---|---|
| shipped v0.1 | 0.643 | 0.706 | 0.789 | 0.568 | 0.447 | 0.535 | 13.28 \% |
| symmetric | 0.587 | 0.659 | 0.732 | 0.549 | 0.437 | 0.525 | 13.05 \% |
| OctoMap parameters on window evidence | 0.635 | 0.691 | 0.753 | 0.582 | 0.469 | 0.538 | 0.17 \% |
| ELite factors on window evidence | 0.636 | 0.684 | 0.759 | 0.544 | 0.440 | 0.514 | 0.71 \% |
| SPRT $(0.20,0.70)$ | 0.562 | 0.645 | 0.720 | 0.509 | 0.405 | 0.489 | 20.65 \% |
| SPRT low end $(0.20,0.65)$ | 0.582 | 0.664 | 0.742 | 0.511 | 0.405 | 0.491 | 20.57 \% |
| SPRT high end $(0.30,0.75)$ | 0.539 | 0.634 | 0.706 | 0.435 | 0.365 | 0.420 | 41.82 \% |

The Static sets of the three same-day sequences agree with a Jaccard index of 0.562, 0.645 and 0.720 for SPRT $(0.20,0.70)$ (pairs 03/04, 03/05, 04/05, in 90 shared tiles), against 0.643, 0.706 and 0.789 for shipped v0.1 (Table 22).

**Memory.** At 0.1 m with the veto on, every arm ran out of memory on Keble 03 (7 failed runs), and the later 0.1 m runs of those arms were not started and are listed. With the veto off all 42 runs at 0.1 m finished. The semi-indoor KTH runs at 0.1 m with the veto on failed for all arms in the same way (7 runs). All 14 failed runs ended with `std::bad_alloc` at the 9 GB limit of the harness.

## 7.5 KTH benchmark: one pass {#sec:p3kth}

**Table 23.** KTH dynamic-map benchmark, single pass, 0.2 m voxels, $W{=}10$ frames, veto on, static accuracy SA, dynamic accuracy DA and their geometric mean AA, in percent. KITTI 05 (64 beams, SuMa poses) and the semi-indoor sequence (16 beams, NDT-SLAM poses). Policy *static only* exports the points inside Static voxels; *not rejected* exports the points of every voxel that is not Rejected. The DA of an oracle map that holds exactly the ground-truth static points is 96.20 (KITTI 05) and 94.42 (semi-indoor); a larger DA is possible only for a map that drops static points, and pays for it in SA. Exporting every frame point (policy *all*) gives SA 100 and DA 0 in the 9 conditions that finished. The $(0.20,0.70)$ pair is not calibrated for 3D, the OctoMap and ELite rows apply their increments once per touched window and are not the published methods, and the numbers are not comparable with published removal scores. No row is a removal method.

| Arm | KITTI 05, static only SA | DA | AA | KITTI 05, not rejected SA | DA | Semi-indoor, static only, AA |
|---|---|---|---|---|---|---|
| shipped v0.1 | 90.20 | 83.19 | 86.62 | 99.99 | 4.99 | 74.45 |
| symmetric | 87.31 | 74.40 | 80.59 | 99.87 | 8.09 | 79.22 |
| OctoMap parameters on window evidence | 98.55 | 55.75 | 74.12 | 98.55 | 55.75 | 73.40 |
| ELite factors on window evidence | 87.72 | 97.55 | 92.50 | 87.72 | 97.55 | 73.34 |
| SPRT $(0.20,0.70)$ | 66.90 | 98.99 | 81.38 | 100.00 | 0.00 | 77.16 |
| SPRT low end $(0.20,0.65)$ | 67.44 | 98.97 | 81.70 | 100.00 | 0.00 | 77.77 |
| SPRT high end $(0.30,0.75)$ | 44.98 | 99.56 | 66.92 | 100.00 | 0.00 | 74.13 |

The KTH rows are a single pass with a pair that is not calibrated for 3D, and no row is a competitive removal score (Table 23). The dynamic accuracy of the oracle map that holds exactly the ground-truth static points is 96.20 on KITTI 05 and 94.42 on the semi-indoor sequence. A DA above the oracle is possible only for a map that drops static points: SPRT $(0.20,0.70)$ reaches DA 98.99 and ELite factors 97.55 on KITTI 05, and they pay in SA (66.90 and 87.72). At 0.2 m with 10 frames per window, the veto on and the policy *static only*, the rows (SA, DA, AA) are: shipped v0.1 90.20, 83.19, 86.62; symmetric 87.31, 74.40, 80.59; OctoMap parameters 98.55, 55.75, 74.12; ELite factors 87.72, 97.55, 92.50; SPRT $(0.20,0.70)$ 66.90, 98.99, 81.38. Under the policy *not rejected*, SPRT $(0.20,0.70)$ keeps every point (SA 100.00, DA 0.00) because no voxel is Rejected in this pass, and shipped v0.1 gives SA 99.99 and DA 4.99. On the semi-indoor sequence the AA with the policy *static only* is 74.45 for shipped v0.1, 79.22 for symmetric, 73.40 for OctoMap parameters, 73.34 for ELite factors and 77.16 for SPRT $(0.20,0.70)$. Exporting every frame point gives SA 100 and DA 0 in the 9 conditions that finished (5 with the veto off, 4 with it on); this is the check that the exporter and the evaluator are correct.

## 7.6 Boreas: across seasons {#sec:p3boreas}

The three Boreas sessions are mapped independently and, for comparison, once cumulatively. Table 22 gives the Jaccard index of the independent Static sets in the 617 10 m tiles seen by all three sessions, for the pairs autumn/spring, autumn/summer and spring/summer: 0.509, 0.405 and 0.489 for SPRT $(0.20,0.70)$; 0.568, 0.447 and 0.535 for shipped v0.1; 0.544, 0.440 and 0.514 for ELite factors; 0.582, 0.469 and 0.538 for OctoMap parameters. The share of the Static cells of the cumulative map that are Static in no independent session is 20.65 \% for SPRT $(0.20,0.70)$, 13.28 \% for shipped v0.1, 0.71 \% for ELite factors, 0.17 \% for OctoMap parameters and 41.82 \% for the high-end pair. Boreas has no truth for structure or clutter, so these are agreements and not accuracies, and a higher agreement is not a better map. The ground-height difference between the sessions is reported with the data and was not applied.

## 7.7 The Periodic veto {#sec:p3veto}

**Table 24.** Effect of the Periodic veto (primary setting: on). *KTH DA*: dynamic accuracy on KITTI 05, 0.2 m, $W{=}2$, policy not rejected. *Spires*: share of non-ground structure (label 3) Rejected on Keble 03, 04 and 05, 0.2 m, $W{=}10$, SPRT $(0.20,0.70)$. The veto also changes memory: the 0.1 m Spires runs and the semi-indoor 0.1 m runs finish with the veto off and fail with it on.

| Condition | Veto on | Veto off |
|---|---|---|
| KTH DA, SPRT $(0.20,0.70)$ | 23.14 | 1.16 |
| KTH DA, SPRT low end $(0.20,0.65)$ | 22.82 | 1.22 |
| KTH DA, SPRT high end $(0.30,0.75)$ | 24.45 | 1.15 |
| Spires Keble 03, structure Rejected | 0.70 \% | 0.24 \% |
| Spires Keble 04, structure Rejected | 0.77 \% | 0.32 \% |
| Spires Keble 05, structure Rejected | 0.71 \% | 0.29 \% |

The veto changes some rows a great deal (Table 24). The largest relative effect is the KTH dynamic accuracy under the policy *not rejected* on KITTI 05 at 0.2 m with two frames per window: for SPRT $(0.20,0.70)$ it is 23.14 with the veto on against 1.16 with it off, about 20 times; the low end gives 22.82 against 1.22 and the high end 24.45 against 1.15. The largest relative effect on a truth-labelled false reject is the Spires false reject of non-ground structure, between 2.5 and 3.0 times (ratios of the unrounded rates): 0.70 \% to 0.77 \% with the veto on against 0.24 \% to 0.32 \% with it off. The veto also changes memory use, as above. No run of this study contains a door with a known schedule, so these rows say what the veto does to the evidence rule, not whether it finds doors.

## 7.8 What these runs cannot show {#sec:p3cannot}

The runs cannot show per-object truth (LILocBench has none), seasons and doors (Spires covers one day), a removal-benchmark score (the KTH sequences are a single pass, the pair is not calibrated for 3D, and the OctoMap and ELite rows are not the published methods), or any false-Static or false-reject truth in Boreas. They cannot show that Periodic is right on real data, because no sequence has a door with a logged schedule. They show how seven rules differ on the same cells and how a stated nominal level relates to what happened on real clutter.

# 8 Discussion and Limitations {#sec:disc}

**What the evidence shows about the test.** The synthetic evidence supports three statements. The increments of the first release are a sequential probability ratio test whose hypotheses were implied, not chosen (Section 4.1); the operating characteristic of the test of version 0.2 on the real engine matches the exact computation of the rule and is approximated by Wald's closed form (E7 checks the operating characteristic of the engine, and with it the first-test error rates of Proposition 7, under independent windows, Periodic veto off); and the same bound is inflated by autocorrelated windows (Table 2). The real-data evidence supports a narrower statement: on LILocBench clutter the sequential test graduates fewer cells than the rules of the first release, at the price of a longer wait to graduate walls and of more Undecided structure on Spires, and the realised per-cell rate is above the nominal per-history level. It does not support a statement that the sequential test is the better map: the compared rows differ on different measures in different directions, and each dataset leaves one measure out (Section 7.8).

**Why the realised rate is above the nominal level.** Three things differ between the bound and the table. The unit differs: the bound is for the first test of a history, the table counts cells, a rejected cell starts a new history, and a reverse-test demotion restarts the forward test, which the bound does not cover. In E6 the unit alone already exceeds the level under independent windows: 107 of 2000 Bernoulli($m_0$) cells are Static at some window within 256 windows, which we attribute to no mechanism beyond restarts under independence. The windows are autocorrelated, and E7 shows the direction of that effect. The clutter class is not pure: moved furniture and lingering people have a hit rate above $m_0$, and no hit-rate test tells them from a wall. We did not separate the three contributions on the real data, so the factor between the realised and the nominal rate is not explained.

**What the evidence shows about the backends.** Across E1–E4 the two backends behave as one engine wearing two geometries. E1's near-identical F1 trajectories and E4's live-cell counts are the empirical face of (1): `LayeredMap` and `PeriodicityModel`, exercised directly in E2 and E3, never see which backend produced the id they update. The backends diverge in two places, the high-clutter precision gap and the per-call cost. Both runs execute the same classifier code on differently sampled input; neither difference was isolated to one mechanism.

**Cost analysis.** The window update is linear in the number of live cells (§3.5), so cost is governed by how many cells a backend creates. The flat voxel hash is the source of `voxel3d`'s 4.4–10.0$\times$ larger live-cell count: every sampled free voxel becomes its own entry, at an estimated 56 B with periodicity off, with no spatial compression. OctoMap [@hornung2013octomap] and UFOMap [@duberg2020ufomap] collapse coherent free or occupied regions into single octree nodes and would scale better to very large, sparsely occupied 3D extents. We accept the trade for $O(1)$ insert and lookup and a backend simple enough to unit-test without ROS. The largest synthetic configuration (E4) is estimated at 37.0 MB; this is the synthetic estimate at 56 B per live cell with periodicity off, not a measurement, and the real-data runs need far more (below). Warning: in `voxel3d` memory grows with the cells touched. Undecided cells are kept, and nothing is forgotten out of view (Remark 3), so over a lifelong run memory is unbounded. In `grid2d` memory is bounded by the fixed grid, which drops every endpoint and ray outside its array: at most `grid_width`$\times$`grid_height` cells, each with a Fourier record when the veto is on.

**Forgetting outside the field of view.** Remark 3 is an operational limitation, not a footnote. Decay runs only inside the touched branch, so a transient object that accumulates strong evidence and then leaves sensor coverage for good—a box dragged into a corridor the robot no longer visits—keeps its evidence indefinitely. Forgetting in STRATA is coupled to re-observation, not to elapsed time. In a bounded, frequently re-swept environment, the setting E1–E4 implicitly assume, this rarely matters; in a sparsely revisited one it is a real failure mode that the current code does not address.

**Free space is remembered.** The same coupling shows in `grid2d`'s free value. A cell that a ray has cleared reads free while its last observation was free and no Periodic, Static or occupied-leaning Transient class claims it, so free means last observed free, not currently free. An obstacle that appears where the sensor no longer looks reads free until it is observed; from its first hit it reads 50, out of view too, until a ray observes the cell free again. Free space is also carved only along beams that return a hit inside the array (§3.1): a beam without a return, or one that ends beyond the array, clears nothing, so open space bounded only by such beams stays Unknown, and occupied evidence along them is never contradicted. A map smaller than the sensor's range therefore keeps cells that a model clearing every ray would release. The shipped `grid2d` parameter file covers `grid_width` $\times$ `grid_height` cells of `grid_resolution` from its `origin` (caption of Table 25), 20 m by 20 m; endpoints outside the grid are dropped, and nothing on `/diagnostics` counts them. From a sensor outside the grid no ray clears a cell, while its endpoints inside the grid are still integrated as hits, so a cell that sees only hits, a standing person included, graduates after 4 windows and is not demoted while the sensor stays outside. Size the grid and its origin to the site before deployment; the memory bound of `grid2d` is then that grid. The last-observed-free bit costs one byte per grid cell; `voxel3d` publishes no free space.

**Scan-counted windows depend on the input rate.** With `window_mode` `scans` a window is `layer_interval` integrated messages, so its length in seconds, and the period that `period_windows` stands for, follow the rate of messages the node actually integrates; messages lost on a best-effort link, dropped for missing TF or skipped by an overloaded node stretch every window. In the synthetic ROS check, withholding 40 \% of the scans turns the tested door cells static (at least 95 \%, none periodic) with scan windows (`grid2d`), while time windows keep the door periodic in `grid2d` and out of the static map in `voxel3d`; the continuous integration (CI) asserts all three. Time windows trade this for rate-dependent evidence: a window with few messages is less likely to see a hit, so a sensor rate that varies with the phase of a cycle can itself look periodic, outside the null of the test. Once a second the node publishes on `/diagnostics` the input rate, the lost share (estimated per sensor frame from stamp gaps, valid while about a fifth of consecutive messages arrive back to back; above about 80 \% loss, set `expected_scan_rate_hz`), TF failures, the window duration and the period in seconds. It warns on lossy or jittery scan windows, empty or thin time windows, zero or stalled stamps and a stalled simulation clock, names a failing TF lookup, and turns ERROR when input or integration stops; Table 28 gives the exact levels and the check behind each. With time windows the period is `period_windows` $\times$ `window_period_s` seconds; a deployment should measure the rates on the robot, keep a window long enough to hold several messages, and choose `period_windows` from the period it wants to detect. The experiments integrate every message and are unaffected.

**Design trade-offs.** Three simplifications are deliberate. A single global `survival_decay` replaces the per-cell survival posterior of @rosen2016persistence and the learned transition rates of @tipaldi2013lifelong and @saarinen2012imac; E3 shows that, once paired with hysteresis, one uniform rate already yields F1 1.000 at both $\lambda{=}0.90$ and $1.0$, but a scene with sharply heterogeneous per-cell dynamics is untested. The flat hash trades memory scaling for simplicity, as above. And two backends behind an `if`/`else` factory is the right amount of mechanism for two, but a third backend would argue for a registry such as `pluginlib` [@ros2pluginlib]; this is a design judgement, not a measured result.

**When not to use STRATA.** It is not a substitute for SLAM: it estimates no pose and closes no loops, so a deployment without an external localizer publishing the `map`$\to$sensor transform has no path to a map. It is not multi-session mapping: comparing Monday's map with Friday's or replaying a past session needs the versioning of LT-mapper or @yang2025lifelong3d. It has no object-level reasoning, unlike the object-level change reasoning of Khronos [@schmid2024khronos]. And it does not discover periods: where the relevant cycles are unknown or are not harmonics of one base period, FreMEn's selection among candidate periods [@krajnik2017fremen] or warped hypertime [@krajnik2019warped] is the better tool. Within those bounds—one continuous session behind an external localizer, at the raw cell level, in 2D or 3D—it is the intended fit.

**Limitations of the real-data evidence.**

- *Provisional defaults.* The pair $(0.20,0.70,0.01,0.01)$ is the result of the calibration rule with a failed quality gate waived; the pre-registration is not frozen (Section 6). It applies at most to one-second windows and the 2D sensor and resolutions measured. The status text of the node and the comments of the shipped parameter files state the scope of $\alpha$ and $\beta$ of Proposition 7 (first test of a cell history, independent windows, Periodic veto off for $\beta$). The status text says, for the shipped pair, that the realised per-cell rates on real 2D clutter are above $\alpha$; the parameter-file comments say only that the realised per-cell rate on real clutter was above $\alpha$ (Section 7).
- *The 3D pair is not calibrated.* No 3D measurement passed the rule, the Keble 02 clutter class is mostly not walking people, and the 2D pair is used in 3D. Every 3D row of Section 7 carries this limit.
- *Realised rates above nominal.* The per-cell false-Static rates on real clutter are above $\alpha$, for reasons that we did not separate (above). No guarantee of this paper is stated for real windows.
- *Periodic is not validated on real data.* No public sequence has a door with a logged schedule. The veto is on in the primary runs and its effect is measured only as on against off; whether it finds doors, or labels slow clutter Periodic, is shown only on synthetic streams (E5).
- *Memory.* At 0.1 m with the veto on, 14 runs ended out of memory at the 9 GB limit of the harness (Keble 03, and the semi-indoor KTH sequence, a single pass); the veto-off runs finished. A Fourier record is allocated for every cell touched with the veto on (Section 3.5), the likely cause. The probe committed in the first amendment of the pre-registration measured the cost on the first ten frames of one Boreas test session, frames that had already been opened for reader validation (0.2 m voxels, ranges 3 to 50 m, sequential-test arm): on 2,582,017 cells the peak memory was 221 MB with the veto off against 1,212 MB with it on, more than five times. In `voxel3d` memory grows with the cells touched; Undecided cells are kept and nothing is forgotten out of view, so memory is unbounded over a lifelong run. `grid2d` is bounded by its fixed grid. The node crops no range: it drops only non-finite points. The Boreas runs were cropped to the range window of Section 7 and ran with the veto off because of memory (first amendment), so the shipped 3D parameter file was not run on an outdoor session, and the measured memory is not a bound for the node. Crop the cloud before the node. We did not extend the probe to a veto-on and veto-off table of the full runs.
- *Single pass and a pair that is not calibrated.* The KTH sequences are one pass, so they say little about lifelong behaviour and no row is a removal score. The OctoMap and ELite rows apply their increments once per touched window and are not the published methods; no arm for MTD-Map was built.
- *No truth in Boreas; one day in Spires; people and furniture mixed in LILocBench.* The Boreas comparison is agreement between sessions, not accuracy. The test sequences of LILocBench come from the same office and robot as the calibration sequences.
- *Consuming the map.* Only Static is drawn $100$ on the 2D ` /map`; Nav2's StaticLayer subscribes to `map` by default, so set its `map_topic` to this topic or remap it. A cell hit in the open window or last observed as a hit, and a Transient or Intermittent cell whose evidence leans occupied, read $50$; Periodic reads $75$; every other cell, Unknown included, reads $0$ if it was last observed free and $-1$ if no ray has cleared it (Section 3.5). An Undecided wall cell with $\ell\le0$ that was last seen free therefore reads free; `voxel3d` publishes Static voxel centres only. On LILocBench wall runs a Static cell appears after a median of 33 windows (upper decile 156; one window is one second), and 14.21 \% of the wall-run cells are not Static at the end. On a single pass, up to 22.50 \% of the non-ground structure of a Spires sequence ends Undecided. A Static cell stays $100$ until at least 5 all-miss touched windows have passed; hits in between raise $\ell$ back toward the cap and delay the demotion, and a cell that no ray re-observes stays $100$ indefinitely (Remark 3), so a removed obstacle can stay lethal for a long time. Do not use the STRATA map as the only source of obstacles: a planner that reads only the value $100$ sees a wall late and may not see part of it. Nav2 reads these values through `StaticLayer::interpretValue` (we checked the source of the Humble branch): with the default `trinary_costmap` true and `lethal_cost_threshold` $100$, only $100$ is lethal, and $50$ and $75$ read as free space, as does $-1$ when `track_unknown_space` is false, the costmap default; with `trinary_costmap` false they scale to partial costs, and a threshold at or below $50$ makes every cell drawn $50$ lethal. A map saved with ` /save_map` and served by `nav2_map_server` reads every cell drawn $50$ or $75$ as unknown (Table 28), so a planner that is allowed to plan through unknown space plans through them.
- *Measured 3D cost of the shipped-on veto.* The veto is on in the shipped 3D parameter file. The committed memory probe above shows more than five times the memory with it on, every run at 0.1 m with the veto on that was started (14 runs) ran out of memory, and the Spires false reject of non-ground structure is between 2.5 and 3.0 times that of the veto-off setting (ratios of the unrounded rates). Turning the veto off removes the Periodic label, which is not validated on real data anyway. We pick no winner and change no default.
- *Revisits.* The sequential test waits for evidence. On a single pass part of the structure stays Undecided (up to 22.50 \% of the non-ground structure of a Spires sequence), and a lifelong mapper needs revisits that these data do not contain.

**Limitations of the synthetic evidence.** The synthetic numbers come from a deterministic seeded harness driving hand-authored hit and miss patterns through the engine; the FreMEn grid of @krajnik2016persistent, ELite, LT-mapper and @berrio2021longterm report long-term real-world evidence of a kind that this paper does not. E1–E4 are single-seed and carry no dispersion estimate; only E5 to E7 are multi-seed. No FreMEn model is run on the same streams, so this paper does not show that the periodic test, whose false-alarm bound holds for iid clutter, labels periodic cells better than FreMEn's amplitude-ranked component selection. The synthetic evidence establishes a transparent state machine, not a reliable scene classifier: aperiodic cells of moderate occupancy end Static in both arms, low-duty doors are lost to pruning in the legacy arm under the E2 parameters, and at the shipped period correlated clutter is labelled Periodic (below). E4's timings are 3 recordings on one quiet host and are not portable. The ROS node has not been run on a robot or on a recorded bag, and it was not in the loop of the real-data runs of Section 7 either, which come from the offline harness with reference poses: TF latency, transport loss, localiser error and stamp offsets enter none of those numbers. The evidence for the node is the unit tests of its ROS-free helpers and the synthetic scenarios of Table 28, which continuous integration runs on ROS 2 Humble only, in one container image; the zero-stamp rule, the no-window-closing warning and the sensor-frame cap and expiry have no dedicated check. The saved map was loaded by `nav2_map_server` in that Humble job only; no other distribution is in the continuous integration, and the node does not verify a saved pair itself. The engine models single-session persistence only, forgets only what it re-observes (Remark 3), and loses low-duty-cycle periodic cells to the prune/maturity race of Proposition 8. The periodicity test has limits of its own. What is proved (Propositions 3 and 4) is a per-history bound under an iid null and occupancy-independent touches: at the shipped level, a cell that is never pruned is labelled Periodic at some window of the run with probability at most 0.2. The 0.01 target is a measurement: at most 2 of 2000 clutter cells within 1024 windows on held-out seeds (E5, legacy arm; 11 in the sprt arm), and for pruned clutter the proved bound grows with the number of re-created histories. The Wilson limits quoted with it are computed per configuration and are not adjusted for the 36 held-out clutter configurations examined. Temporally correlated clutter violates the null, and at the shipped $T{=}24$ the test labels most slowly switching cells Periodic (1908 of 2000 in E5, legacy arm; 1969 in the sprt arm). Spending costs detection delay: a noiseless 50\%-duty door with $T{=}8$ needs 25 windows instead of 15, and one with 10\% flipped windows 48 instead of 26. Finally, the dominant labelling error is not periodic at all. The graduation rule labels a cell Static when it is occupied in more than roughly $m^\dagger$ of its touched windows (Remark 1; 0.354 with the v0.1 defaults), so it cannot separate random clutter from a noisy wall of similar occupancy: E3 scores Bernoulli(0.6) cells as walls and E5 scores Bernoulli(0.5) cells as clutter, and with the v0.1 defaults 1998 and 1974 of 2000 such cells are Static after 256 windows (E6). Version 0.2 replaces those defaults by the sequential test; between $m_0$ and $m_1$ the test promises nothing, and E6 measures what it does there (Section 5.5). The natural next steps follow directly: a null that allows temporal correlation, for example by testing the harmonic against a local noise floor rather than against white noise, and an anytime-valid test that also covers re-created histories and correlated windows; a FreMEn baseline on the same synthetic streams; a second statistic whose sign is stable across datasets; a pair that passes the quality gate, with revisit-rich data in 3D; a recording of a door with a logged schedule, which would be the first real-data test of Periodic; field deployment on a 2D wheeled platform and a 3D-LiDAR platform; per-cell learned decay; exempting recently active cells from pruning, or feeding a running amplitude estimate into (16); and relaxing untouched cells toward the prior on a tick basis, without changing the touched-cell path that E1–E4 characterise.

# 9 Conclusion {#sec:concl}

STRATA decides between durable and plastic with a sequential probability ratio test whose two hit rates and two error rates the user states. We showed that the OctoMap increments of the first release are such a test between the hit rates 0.1975 and 0.4621, with hypotheses that were not chosen, and that a multiplicative leak removes the property on which its error bound rests. Engine version 0.2 derives increments and thresholds from $(m_0,m_1,\alpha,\beta)$ and ends every cell Static, Rejected or Undecided. Under independent windows clutter at a hit rate of at most $m_0$ graduates in the first test of one cell history (up to its first graduation, its first rejection or its retention as a periodic candidate) with probability at most 0.0101, and restarts after a demotion are not covered; the operating characteristic of the engine with the Periodic veto off matches the exact computation of the rule and is approximated by Wald's closed form; and at lag-1 autocorrelations of $0.23$ and $0.5$ the graduation rate per forward test (a history split at each demotion) is 3.49 and 10.87 times $\alpha$, while real calibration cells lie between 0.231 and 0.474, so we state no guarantee for real windows. The pre-registered calibration, amended after its first measurement, did not freeze: the wall-thickness gate failed on every calibration sequence, the shipped pair $(0.20,0.70,0.01,0.01)$ is provisional, and the 3D pair is not calibrated. On held-out LiDAR data from LILocBench, Oxford Spires, the KTH benchmark and Boreas, seven evidence rules differ in direction on different measures, and we pick none. On LILocBench clutter the realised per-cell rate of the sequential test is 8.81 \% (2.93 \% at an observed hit rate of at most $m_0$) against 21.16 \% for the rule of the first release; both are above the nominal per-history $\alpha=0.01$. The same per-cell state machine serves a 2D grid and a 3D voxel hash and builds without ROS under 114 behaviour-level tests. Its FreMEn-style periodic label has a false-alarm bound for iid clutter; on held-out synthetic seeds it labels at most 2 (legacy arm) and 11 (sprt arm) of 2000 Bernoulli clutter cells Periodic at some window within 1024 windows, and it is validated on synthetic streams only. STRATA is offered as a small module whose defaults are provisional: re-derive the pair for your sensor, your window length and your resolution, and read the realised rates beside the nominal ones.

# Appendix A Reproduction {#app:repro}

STRATA is released under the Apache-2.0 license at <https://github.com/kjungmo/strata> and targets ROS 2 Humble.

**Code pin.** The engine (`strata_core`), the node (`strata`) and the check scripts (`scripts/`) of the repository <https://github.com/kjungmo/strata> that this paper describes are those of commit `708d62f` (22 October 2026). The evidence model `sprt` was added in commit `0f5befc` (18 October 2026). `a7559d4` (19 October 2026) is the last commit of the v2 work in `strata_core/`, `strata/` and `scripts/` before the status-text commits; two later commits, the last of them the pin, changed only the status text of the node and the comments of the two shipped parameter files (they now state the scope of $\alpha$ and $\beta$ of Proposition 7), and `strata_core` and `scripts/` are identical to those of `a7559d4`. The experiment harness builds only `strata_core`. Every seeded synthetic result file (E0–E3, E5 and E6 in both arms, E7, and the E4 cell counts of the sprt arm) reproduces byte for byte from the `strata_core` tree `c780639` of `a7559d4`, which is identical at the pin (`paper/experiments/reproduction_log.txt`, its last two lines, which carry the sha256 of the result files). The real-data evaluation runs were built from the `strata_core` tree `bf96e78`, `d8db00e` or `c780639`, whose source and header trees are identical (paragraph Real data below). The calibration runs record a commit in their run manifests and step logs (`results_p1/`, `results_p1b/`); for P1b, `results_p1b/deviations.md` items 25 and 30 list the steps whose build tree is not recorded. No experiment builds or reads the node status text or the YAML comments, so the results hold for the pin. After the pin only documents and the checks of the paper changed (the manuscript, the READMEs, `SPEC.md`, the number check and its mutation report, the bibliography, and an editorial note in `PREREG_P3.md`); no file in `strata_core/`, `strata/` or `scripts/` and no result file changed. The number check compares the tree identifiers of the three directories with the pin's. The first release of this manuscript pinned an earlier commit with the v0.1 evidence rule (named in `paper/README.md`); the pin moved on purpose during the v2 work. The harness runs every seeded experiment in two arms: the *legacy* arm, the v0.1 arithmetic with the explicit parameters of the first release, reproduces the earlier result files byte for byte (`paper/experiments/check_legacy_equivalence.sh`), and the *sprt* arm writes new files `results/*_sprt.csv`.

**What reproduces.** `paper/experiments/check_reproduction.sh` rebuilds the harness from a clean checkout and requires every result file of E0–E3, E5 and E6 (both arms), of E7 and the E4 cell-count file of the sprt arm (`results/e4_cells_sprt.csv`) to come out byte for byte; `paper/experiments/reproduction_log.txt` lists the compilers with which it did (GCC 9.4.0 with libstdc++, on one x86-64 Linux host). The result is not independent of the C++ standard library. The streams come from `std::mt19937`, whose output the standard fixes, through three standard distributions, whose algorithms it does not fix. With clang 23.1.2 and libc++ (`check_stdlib.sh`, record in `results/stdlib_libcxx/`), 15 of the 19 seeded result files are byte-identical and four differ: the two E1 files, the E0 door file and the E6 confusion file. These are the experiments whose outcome depends on integers drawn with `std::uniform_int_distribution` (mover cells in E1's `grid2d` runs, door phases in E0 and E6). Under libc++ the final `grid2d` precision at high clutter in E1 is 0.9583 with 168 predicted cells instead of 0.9253 with 174, and every other final value of Table 4 is unchanged; five of the 20 E0 door medians move by one window (at $\delta{=}0.1$, 20 to 21 and 23 to 24) and the selected levels do not change; and 1976 instead of 1960 of the 2000 aperiodic cells of E6 are Static with the shipped defaults. E5's confusion draws door phases the same way, but its counts do not depend on the phases and its file is identical. 17 of the 20 result files of the sequential-test arm and E7 are byte-identical and three differ (the E0 door file, the E6 confusion file and the E4 cell counts of the sequential-test arm), again the files that depend on integers or reals drawn with a standard distribution; E7 draws from `std::mt19937_64` directly and is identical. No third standard library was tried.

**Real data.** The datasets are public and are not in the repository; their licences allow statistics and figures, not redistribution. Each is downloaded from its official source into a directory outside the repository. `paper/experiments/realdata/check_realdata.sh` verifies the recorded checksums when the data are present and skips cleanly when they are absent. The calibration, the freeze record and the evaluation are `reproduce_p1.sh`, `reproduce_p1b.sh` and `reproduce_p3.sh`. The evaluation steps before the fourteenth amendment were built from `strata_core` tree `bf96e78`, except one Boreas overlap step built from `d8db00e` (Amendment 3); the later steps, most of them, were built from `c780639`. The source and header trees of the three are identical, and only the unit-test directory differs (`results_p3/P3_REPORT.md`, section 5). The repository holds the summary tables, the JSON results, the step logs (host, load, commit, input checksums), the stat records of the test files before and after each phase, and an access log with a file-open trace of every evaluation step. Cell-level dumps stay outside the repository, so the near-wall rows of Table 20 are copied from the committed report `results_p3/P3_REPORT.md`, which is generated from those dumps, and the number check compares them there. Every other number of Sections 4 to 7 that comes from a run is computed from committed files by `tools/make_v2_artifacts.py` and enters the text as a macro, so the prose of these sections holds no hand-typed measured number.

From the repository root:

- Core engine and unit tests, no ROS required (continuous integration runs the same build with g++, Eigen3 and GoogleTest on `ubuntu-latest`):

```bash
cmake -S strata_core -B build -DSTRATA_CORE_BUILD_TESTS=ON
cmake –build build && (cd build && ctest)
```

- Unit tests, the calibration E0 (seeds from 20260928), experiments E1–E4 (seed 12345), E5 (calibration seeds from 30260928, evaluation seeds from 40260928) and E6 (sweep seeds from 50260928), each in both arms, and E7 (its own seeds, drawn directly from `std::mt19937_64`), writing the CSVs under `paper/experiments/results/`. E0–E3, E5 and E6 (both arms) and E7 rewrite their files byte for byte, and so does the E4 cell-count file of the sprt arm (`results/e4_cells_sprt.csv`). E4 reads the wall clock: this script writes its output to `paper/experiments/build/e4_throughput_local.csv` and fails if a cell count or byte estimate differs from the committed one (`check_e4_local.py`); timings for the paper are recorded separately with `paper/experiments/measure_e4.sh`. The comparison columns of Table 6 are the same harness built against the v0.1.0 `strata_core` (`results/pre_fix_2026-09-28/`), against the centred engine before the significance test (`results/pre_calibration_2026-09-28/`) and against the engine with the single-read-out level (`results/pre_spending_2026-09-28/`):

```bash
bash paper/experiments/run_all.sh
```

- Tables and plot data of this paper, and the number check (`paper/arxiv_ref/NUMBERS.md`). The check regenerates every table and plot-data file and compares it with the committed one. For the hand-written text it locates each numeral and requires it to be tied to a value recomputed from the result files or the code, or to be listed as structural with a reason (an experiment design constant restated from the harness, a symbol in a formula, a plot layout value). A numeral that is neither fails the check. Its limits are these. Structural numerals, among them the scenario sizes of §5.1 and the design constants written as symbol and value, are not parsed from the harness; they are kept in a committed list (`paper/arxiv_ref/structural_numerals.tsv`), so that changing one fails the check until the list is rewritten on purpose. A numeral that is tied to a recomputed value only by being equal to it, and not by its sentence, could be exchanged for another recomputed value without the check noticing; `NUMBERS.md` gives the count of each kind.

```bash
python3 paper/arxiv_ref/tools/make_artifacts.py
python3 paper/experiments/number_guard.py
```

Alternative backends can be replayed against the reference scenario of the unit test `Integration.WallStaticMoverTransientDoorPeriodic`.

# Appendix B Parameters {#app:params}

**Table 25.** Engine parameters and shipped defaults (the `LayeredMapParams` struct and both `params/*.yaml` files agree; the struct also holds the library-only `sprt_ceiling`, NaN for $A+2l_{\mathrm{hit}}$, which the node does not declare; `layer_interval` applies only to scan windows and is ignored with `window_mode` `time`, the setting of both files). The nine keys of the legacy group are the defaults of the first release; the sequential test ignores them. Geometry and node parameters are not part of the engine: `grid_width`/`grid_height` 400, `grid_resolution` 0.05 m and origin $(-10,-10)$ m for `grid2d`; `voxel_size` 0.2 m for `voxel3d`; `publish_period` 1.0 s; `window_mode` `time` in both files (`scans` when the parameter is not set) with `window_period_s` 1.0 s; `expected_scan_rate_hz` 0 (estimated from stamps), `rate_warn_drop_fraction` 0.05, `input_timeout_s` 5 s and `startup_timeout_s` 30 s. The node also declares `evidence_calibration`, a label that it sets to `user` when the pair differs from the shipped one.

| Parameter | Symbol | Default | Meaning |
|---|---|---|---|
| *Shared by both evidence models* |  |  |  |
| `layer_interval` | $L$ | 10 | integration ticks per scan window |
| `enable_periodicity` | — | true | run the Fourier model |
| `periodic_amplitude_min` | $a_{\min}$ | 0.3 | minimum harmonic amplitude (effect size) |
| `periodic_false_alarm` | $\delta$ | 0.2 | level of the significance test |
| `periodic_alpha_spending` | $\delta_n$ | true | spend $\delta$ over the touch count, Eq. (15) |
| `period_windows` | $T$ | 24 | base period and amplitude gate |
| `n_harmonics` | $H$ | 2 | harmonics per cell |
| *Legacy evidence model (read only when `evidence_model = legacy`; ignored by `sprt`)* |  |  |  |
| `l_hit` | $l_{\mathrm{hit}}$ | 0.85 | log-odds increment on an occupied window |
| `l_miss` | $l_{\mathrm{miss}}$ | $-0.4$ | log-odds increment on a free window |
| `l_min`, `l_max` | $l_{\min},l_{\max}$ | $-5.0$, $5.0$ | clamp bounds |
| `survival_decay` | $\lambda$ | 0.97 | per-window forgetting multiplier |
| `graduate_prob` | $p_{\mathrm{grad}}$ | 0.8 | probability to graduate to Static |
| `demote_prob` | $p_{\mathrm{dem}}$ | 0.45 | probability at or below which Static demotes |
| `min_observations` | $N_{\min}$ | 3 | touched windows before graduation |
| `prune_prob` | $p_{\mathrm{prune}}$ | 0.05 | erase non-static, non-periodic cells below this |
| *Sequential test (`evidence_model = sprt`, the default)* |  |  |  |
| `evidence_model` | — | `sprt` | evidence rule (`legacy`: first release) |
| `m0` | $m_0$ | 0.20 | clutter at or below this hit rate (provisional) |
| `m1` | $m_1$ | 0.70 | structure at or above this hit rate (provisional) |
| `alpha` | $\alpha$ | 0.01 | error rate for clutter at $m_0$ |
| `beta` | $\beta$ | 0.01 | error rate for structure at $m_1$ |
| `n_max_undecided` | $n_{\max}$ | 80 | windows before the Intermittent read-out |

**Table 26.** Per-experiment deviations from the defaults of Table 25, as compiled into each harness. They are the settings of the legacy arm, which also sets `evidence_model = legacy`; the sprt arm changes only the evidence model to the shipped test and leaves the other settings of this table as they are (E3 sweeps the pairs of Table 15 instead of the first-release knobs). “—”: default; “swept”: varied over the E3 grid; “n/a”: inert because periodicity is off; “false / true”: run once each way, periodicity on at the defaults.

| Parameter (default) | E1 | E2 | E3 | E4 |
|---|---|---|---|---|
| $L$ (10) | 1 | 1 | 1 | 1 |
| `enable_periodicity` (true) | false / true | true | false / true | false |
| $l_{\mathrm{hit}}$ (0.85) | — | 1.0 | — | — |
| $l_{\mathrm{miss}}$ ($-0.4$) | — | $-1.0$ | — | — |
| $\lambda$ (0.97) | — | 1.0 | swept | 1.0 |
| $p_{\mathrm{grad}}$ (0.8) | — | 0.9 | swept | — |
| $p_{\mathrm{dem}}$ (0.45) | — | 0.4 | swept | — |
| $N_{\min}$ (3) | — | 5 | — | — |
| $p_{\mathrm{prune}}$ (0.05) | — | — | 0.01 | — |
| $T$ (24) | — | 8 | — | n/a |
| $H$ (2) | — | 3 | — | n/a |
| $\delta$ (0.2, spent) | — | — | — | n/a |

# Appendix C Interfaces and State Transitions {#app:io}

**Table 27.** ROS 2 input/output contract of the `strata` node. Every scan or cloud is transformed with a full 6-DoF lookup of `global_frame` (default `map`) $\leftarrow$ sensor frame at the message stamp; a lookup that still fails after the wait of Table 28 drops the message with a throttled warning. The node neither subscribes to `/initialpose` nor broadcasts `map`$\to$`odom`; one mutex serialises integration, publishing, saving and the diagnostics status. Both timers run on the wall clock.

| Direction | Name (default) | Type, QoS | Condition |
|---|---|---|---|
| Subscribe | `scan_topic` (`/scan`) | `LaserScan`, sensor data | `grid2d` |
| Subscribe | `points_topic` (`/points`) | `PointCloud2`, sensor data | `voxel3d` |
| Publish | ` /map` | `OccupancyGrid`, depth 1, transient local | `grid2d` |
| Publish | ` /map_points` | `PointCloud2`, depth 1, volatile | `voxel3d` |
| Service | ` /save_map` | `Trigger` | both (`.pgm`+`.yaml` / `.pcd`) |
| Timer | `publish_period` (1.0 s) | — | both |
| Publish | `/diagnostics` | `DiagnosticArray`, depth 10 | both, one status a second |

**Node behaviour that a deployment depends on.** The node's rules for time, transforms, input health and saving live in ROS-free headers with their own unit tests, and continuous integration runs the launched node against synthetic input on ROS 2 Humble. Table 28 states each rule as implemented and names the check that exercises it; a rule without a check says so. None of these checks uses a robot or a recorded bag (§8).

**Table 28.** Behaviour of the `strata` node at the pinned commit and the committed check that exercises each rule. Unit tests are the 71 gtest cases of the node package (`strata/test/`); scripts are in `scripts/` and run in the Humble job of the continuous integration against the launched node with synthetic input. “—”: no dedicated check.

| Topic | Rule as implemented | Checked by |
|---|---|---|
| Time windows | Windows of `window_period_s` on header stamps, in integer nanoseconds from the first stamp. A later stamp closes the open window and every silent one. A stamp more than half a window behind the newest, or more than $10^6$ windows ahead, re-anchors the clock after closing the open window. | `test_window_clock` (6 cases); `synthetic_e2e.py` with time windows |
| Zero stamps | Dropped with time windows, integrated with scan windows; reported as a warning in both. | — |
| TF wait | The lookup at the message stamp is polled without blocking for 0.1 s of ROS time, at most 1 s of steady time. A ROS clock unchanged for 1 s counts as frozen and is not waited on. A clock that steps past the budget gets 20 ms more. A failed lookup drops the message. | `test_tf_wait` (7 cases); `check_coarse_clock.py`; `check_sim_clock_stall.py` |
| Input status | One status a second on `/diagnostics`. No message yet: OK, then ERROR after `startup_timeout_s`. Messages stop: ERROR after `input_timeout_s`. Lookups fail before anything was integrated: OK with the failing lookup named, then WARN after `startup_timeout_s`. Lookups fail later: named once nothing was integrated for 1 s, then ERROR after `input_timeout_s`. Every status carries message, integration and TF-failure totals, the running failure streak and the last tf2 error text. | `test_input_status` (18 cases); `check_no_localizer.py` on both backends |
| Clock and stamp warnings | WARN when `use_sim_time` is set and the ROS clock stands still for 3 s while messages arrive, and when messages are integrated but no time window closes for 10 window periods (and more than 2 s). | `check_sim_clock_stall.py` (clock); — (no window closing) |
| Rate statistics | Per sensor frame, from the stamps of integrated messages: rate, window duration, and loss estimated from gaps against the 20th-percentile interval or `expected_scan_rate_hz`. At most 16 frames are tracked; a frame silent for 10 window closes is forgotten. WARN on loss above `rate_warn_drop_fraction` or jittery scan windows, and on empty or thin time windows. | `test_rate_monitor` (4 cases); `synthetic_e2e.py` with 40 \% of the scans withheld; — (frame cap and expiry) |
| Saving | `grid2d`: PGM and `nav2_map_server` YAML are written to temporary files, flushed, renamed (image first) and the directory flushed. A save that fails before the renames leaves the previous pair untouched and answers `success: false` with the reason. A failure between the two renames can leave the new image beside the previous YAML, so the YAML records the size and SHA-256 of its image and `check_saved_map.py` verifies a pair; neither the node nor Nav2 runs that check. `voxel3d`: one PCD through the same steps. | `test_map_writer` (25 cases); saved-pair step of the CI job; `synthetic_e2e.py` with `--check-map-server` |
| Saved-map values | Free 254, Transient and Periodic 100, Static 0, Unknown 205 with thresholds 0.65 and 0.196, so `nav2_map_server` serves free, unknown, occupied and unknown. | `synthetic_e2e.py` with `--check-map-server`: all 160,000 cells of the synthetic scene, which holds no Transient cell |
| Parameters | Every key of both shipped YAML files is declared by the node and holds the file's value. | `check_param_binding.py` on both backends |

**Table 29.** Cell-class transitions of the legacy evidence model, all evaluated at window close. The transitions of the sequential model are those of Algorithm 2.

| From | To | Trigger |
|---|---|---|
| Unknown (absent) | Transient | first hit or miss creates the cell: $\ell{=}0$, $p{=}0.5\ge p_{\mathrm{prune}}$ |
| Transient | Static | Eq. (7): $p\ge p_{\mathrm{grad}}$, $N\ge N_{\min}$, $\neg\pi$ |
| Transient | Periodic | $\pi$, Eq. (13): $n\ge T$ and, for some $k$, $a_k\ge a_{\min}$ and $H\,\Theta(d_k)\le\delta_n$ |
| Transient | Unknown (erased) | Eq. (16): $p<p_{\mathrm{prune}}$, $a<a_{\min}$, $\neg g$ |
| Static | Transient / Periodic / Unknown | Eq. (8), then re-read by Eq. (17) |
| Periodic | Transient / Unknown | $\pi$ becomes false (pruned only once also $a<a_{\min}$) |
| Periodic | Static | $\neg\pi$ and Eq. (7) holds |
| Periodic | — | never graduates while $\pi$; never pruned while $a\ge a_{\min}$ |
| Static | — | never pruned while $g$ |

# Appendix D Extended Results {#app:results}

**Table 30.** Full E3 sweep (36 configurations, 50 noisy wall cells, occupancy probability 0.6, 10 movers/window, 80 windows, seed 12345). Deg. marks the zero-band configuration $p_{\mathrm{dem}}=p_{\mathrm{grad}}$. F1 to Flicker: periodicity off. *Flicker, P on*: the same sweep with periodicity on at the shipped $T{=}24$, $H{=}2$, $a_{\min}{=}0.3$, $\delta{=}0.2$ spent over the touch count (current engine; its F1 equals the periodicity-off F1 in 36 of 36 rows).

| $p_{\mathrm{grad}}$ | $p_{\mathrm{dem}}$ | $\lambda$ | Band | Deg. | F1 | Precision | Recall | Flicker | Flicker, P on |
|---|---|---|---|---|---|---|---|---|---|
| 0.6 | 0.3 | 0.90 | 0.3 | no | 1.000 | 1.000 | 1.000 | 58 | 58 |
| 0.6 | 0.5 | 0.90 | 0.1 | no | 1.000 | 1.000 | 1.000 | 108 | 108 |
| 0.6 | 0.6 | 0.90 | 0.0 | yes | 1.000 | 1.000 | 1.000 | 170 | 170 |
| 0.6 | 0.3 | 0.97 | 0.3 | no | 1.000 | 1.000 | 1.000 | 54 | 54 |
| 0.6 | 0.5 | 0.97 | 0.1 | no | 1.000 | 1.000 | 1.000 | 72 | 72 |
| 0.6 | 0.6 | 0.97 | 0.0 | yes | 1.000 | 1.000 | 1.000 | 92 | 92 |
| 0.6 | 0.3 | 1.00 | 0.3 | no | 1.000 | 1.000 | 1.000 | 52 | 52 |
| 0.6 | 0.5 | 1.00 | 0.1 | no | 1.000 | 1.000 | 1.000 | 68 | 68 |
| 0.6 | 0.6 | 1.00 | 0.0 | yes | 1.000 | 1.000 | 1.000 | 76 | 76 |
| 0.7 | 0.3 | 0.90 | 0.4 | no | 1.000 | 1.000 | 1.000 | 56 | 56 |
| 0.7 | 0.5 | 0.90 | 0.2 | no | 1.000 | 1.000 | 1.000 | 94 | 94 |
| 0.7 | 0.7 | 0.90 | 0.0 | yes | 0.980 | 1.000 | 0.960 | 250 | 250 |
| 0.7 | 0.3 | 0.97 | 0.4 | no | 1.000 | 1.000 | 1.000 | 54 | 54 |
| 0.7 | 0.5 | 0.97 | 0.2 | no | 1.000 | 1.000 | 1.000 | 66 | 66 |
| 0.7 | 0.7 | 0.97 | 0.0 | yes | 1.000 | 1.000 | 1.000 | 116 | 116 |
| 0.7 | 0.3 | 1.00 | 0.4 | no | 1.000 | 1.000 | 1.000 | 52 | 52 |
| 0.7 | 0.5 | 1.00 | 0.2 | no | 1.000 | 1.000 | 1.000 | 62 | 62 |
| 0.7 | 0.7 | 1.00 | 0.0 | yes | 1.000 | 1.000 | 1.000 | 86 | 86 |
| 0.8 | 0.3 | 0.90 | 0.5 | no | 1.000 | 1.000 | 1.000 | 56 | 56 |
| 0.8 | 0.5 | 0.90 | 0.3 | no | 1.000 | 1.000 | 1.000 | 82 | 82 |
| 0.8 | 0.8 | 0.90 | 0.0 | yes | 0.947 | 1.000 | 0.900 | 343 | 343 |
| 0.8 | 0.3 | 0.97 | 0.5 | no | 1.000 | 1.000 | 1.000 | 52 | 52 |
| 0.8 | 0.5 | 0.97 | 0.3 | no | 1.000 | 1.000 | 1.000 | 56 | 56 |
| 0.8 | 0.8 | 0.97 | 0.0 | yes | 1.000 | 1.000 | 1.000 | 124 | 124 |
| 0.8 | 0.3 | 1.00 | 0.5 | no | 1.000 | 1.000 | 1.000 | 52 | 52 |
| 0.8 | 0.5 | 1.00 | 0.3 | no | 1.000 | 1.000 | 1.000 | 56 | 56 |
| 0.8 | 0.8 | 1.00 | 0.0 | yes | 1.000 | 1.000 | 1.000 | 84 | 84 |
| 0.9 | 0.3 | 0.90 | 0.6 | no | 1.000 | 1.000 | 1.000 | 54 | 54 |
| 0.9 | 0.5 | 0.90 | 0.4 | no | 0.990 | 1.000 | 0.980 | 71 | 71 |
| 0.9 | 0.9 | 0.90 | 0.0 | yes | 0.810 | 1.000 | 0.680 | 574 | 574 |
| 0.9 | 0.3 | 0.97 | 0.6 | no | 1.000 | 1.000 | 1.000 | 52 | 52 |
| 0.9 | 0.5 | 0.97 | 0.4 | no | 1.000 | 1.000 | 1.000 | 54 | 54 |
| 0.9 | 0.9 | 0.97 | 0.0 | yes | 1.000 | 1.000 | 1.000 | 150 | 150 |
| 0.9 | 0.3 | 1.00 | 0.6 | no | 1.000 | 1.000 | 1.000 | 52 | 52 |
| 0.9 | 0.5 | 1.00 | 0.4 | no | 1.000 | 1.000 | 1.000 | 52 | 52 |
| 0.9 | 0.9 | 1.00 | 0.0 | yes | 1.000 | 1.000 | 1.000 | 122 | 122 |

# Appendix E Notation {#app:notation}

**Table 31.** Symbols used in §3. In the sequential model (Algorithm 2, Proposition 7) $n_i$ is the length of the current forward test and is reset at a reverse-test demotion (a veto demotion keeps it); the count of the Fourier model is not reset.

| Symbol | Meaning |
|---|---|
| $\mathcal{C}$, $i$ | set of integer cell ids; a cell |
| $\kappa_b$, $\rho_b$ | backend key map and free-space ray walk |
| $\Gamma_b$, $\Phi$, $\Psi$ | evidence extractor, engine update, class read-out (Eq. 1) |
| $h_{i,w}$, $m_{i,w}$ | hit and miss counts of cell $i$ in window $w$ |
| $\tau_i$, $o_i$, $f_i$ | touched, occupied and free indicators of a window |
| $\ell_i$, $p_i$ | log-odds occupancy and $\sigma(\ell_i)$ |
| $N_i$, $n_i$ | touched windows for legacy graduation and for the Fourier model (sequential: caption) |
| $g_i$, $\pi_i$ | graduated flag and periodic predicate |
| $a_i$, $a_{i,k}$, $u_k$, $v_k$ | dominant and harmonic amplitudes, cosine and sine coefficients of harmonic $k$ |
| $d_{i,k}$, $\mathbf{M}_{i,k}$, $\Theta$ | explained variance, centred phase design, Chernoff tail bound (Eq. 12) |
| $\delta$, $r^\star$, $a^\star(n)$ | nominal level, its critical $d$, amplitude threshold at uniform coverage (Eq. 14) |
| $\delta_n$, $L_N$ | level spent at touch count $n$ (Eq. 15); histories begun by window $N$ (Prop. 4) |
| $\lambda$, $l_{\mathrm{hit}}$, $l_{\mathrm{miss}}$ | survival decay and log-odds increments (derived ones: Eq. 20) |
| $m_0$, $m_1$, $\alpha$, $\beta$ | hit rates and error rates of the sequential test |
| $A$, $B$, $\ell_{\max}$, $n_{\max}$ | thresholds, cap and truncation of the test |
| $p_{\mathrm{grad}}$, $p_{\mathrm{dem}}$, $p_{\mathrm{prune}}$ | graduation, demotion and prune thresholds |
| $T$, $H$, $\omega$, $a_{\min}$ | base period, harmonics, base frequency, amplitude threshold |

# Appendix F Trajectory-Level Results in Full {#app:e5}

**Table 32.** All E5 held-out configurations: cells (of 2000) labelled Periodic at some window up to $N{=}1024$, and in parentheses the mean number of histories a cell began (pruning and re-creation). Columns: parameter set, touch probability $q$, rule (S: single read-out, $\delta{=}0.1$; Sp: spent, $\delta{=}0.2$).

| Cell | E2 parameters ($T{=}8$, $H{=}3$) $q{=}1$, S | $q{=}1$, Sp | $q{=}0.5$, S | $q{=}0.5$, Sp | Shipped ($T{=}24$, $H{=}2$) $q{=}1$, S | $q{=}1$, Sp | $q{=}0.5$, S | $q{=}0.5$, Sp |
|---|---|---|---|---|---|---|---|---|
| Bernoulli $m{=}0.1$ | 3 (266) | 0 (266) | 3 (133) | 0 (133) | 1 (76) | 0 (76) | 0 (38) | 0 (38) |
| Bernoulli $m{=}0.2$ | 141 (180) | 0 (180) | 77 (91) | 0 (91) | 77 (41) | 0 (41) | 40 (21) | 0 (21) |
| Bernoulli $m{=}0.3$ | 620 (112) | 0 (112) | 330 (57) | 0 (57) | 202 (12) | 0 (12) | 102 (7) | 1 (7) |
| Bernoulli $m{=}0.4$ | 953 (62) | 2 (62) | 543 (32) | 2 (32) | 88 (2) | 2 (2) | 58 (2) | 0 (2) |
| Bernoulli $m{=}0.5$ | 717 (23) | 2 (23) | 350 (12) | 1 (12) | 67 (1) | 0 (1) | 50 (1) | 1 (1) |
| Bernoulli $m{=}0.6$ | 199 (5) | 0 (5) | 96 (3) | 0 (3) | 42 (1) | 0 (1) | 52 (1) | 1 (1) |
| Bernoulli $m{=}0.7$ | 16 (1) | 0 (1) | 12 (1) | 0 (1) | 9 (1) | 0 (1) | 18 (1) | 0 (1) |
| Bernoulli $m{=}0.8$ | 4 (1) | 0 (1) | 6 (1) | 0 (1) | 2 (1) | 0 (1) | 4 (1) | 0 (1) |
| Bernoulli $m{=}0.9$ | 0 (1) | 0 (1) | 0 (1) | 0 (1) | 1 (1) | 0 (1) | 0 (1) | 0 (1) |
| always occupied | 0 (1) | 0 (1) | 0 (1) | 0 (1) | 0 (1) | 0 (1) | 0 (1) | 0 (1) |
| always free | 0 (342) | 0 (342) | 0 (171) | 0 (171) | 0 (114) | 0 (114) | 0 (57) | 0 (57) |
| Markov (correlated) | 349 (98) | 2 (98) | 79 (37) | 0 (37) | 1998 (15) | 1908 (15) | 1331 (5) | 409 (5) |
