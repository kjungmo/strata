# strata map-engine mathematics (extracted from source)

Source of truth: `strata_core/{layered_map,periodicity,types}.{hpp,cpp}`, `map_backend.hpp`,
`SPEC.md`, and `strata/params/{grid2d,voxel3d}.yaml`. Everything below is *as implemented*.
Discrepancies vs SPEC prose are flagged inline with **[SPEC-DIFF]**.

Notation: $\ell$ = cell log-odds, $p=\sigma(\ell)$ occupancy prob, $t$ = window index
(`window_count_`), $H$ = `n_harmonics`, $T$ = `period_windows`, $\lambda$ = `survival_decay`.

---

## 1. Update equations (exactly as implemented)

### 1.1 Per-frame accumulation and windowing
Each `observeHit`/`observeMiss(id)` only increments the cell's window counters (no log-odds
math): `window_hits++` / `window_misses++`. Cells are created lazily on first touch
(`cells_[id]`) with $\ell=0$, `observations=0`, `graduated=false`.

`tick()` advances the frame counter and closes a window on the interval boundary:
$$\text{closeWindow} \iff \big(\texttt{layer\_interval}\le 1\big)\ \lor\ \big(\texttt{integration\_count} \bmod \texttt{layer\_interval} = 0\big).$$
`endWindow()` runs the layered update once per closed window over **all** live cells.

### 1.2 Window occupancy decision (in `endWindow`)
With window counts $h_w=$`window_hits`, $m_w=$`window_misses`:
$$\text{touched}=[\,h_w>0 \lor m_w>0\,],\quad \text{occ}=[\,h_w>0\,],\quad \text{free}=[\,h_w=0 \land m_w>0\,].$$
(A single hit outweighs any number of misses in the same window.)

### 1.3 Log-odds hit/miss update + decay + clamp — **only for touched cells**
```
if touched:
    l += (occ ? l_hit : (free ? l_miss : 0))
    l *= survival_decay
    l  = min(l_max, max(l_min, l))
    observations += 1
```
$$\ell \leftarrow \operatorname{clamp}\!\Big(\lambda\big(\ell + \underbrace{[\text{occ}]\,l_{\text{hit}} + [\text{free}]\,l_{\text{miss}}}_{\text{one increment}}\big),\; l_{\min},\, l_{\max}\Big),\qquad
\operatorname{clamp}(x,a,b)=\min(b,\max(a,x)).$$
Occupancy probability: $\;p=\sigma(\ell)=\dfrac{1}{1+e^{-\ell}}$.

Ordering note (load-bearing): decay multiplies the **already-incremented** value, so the fresh
hit/miss is itself attenuated by $\lambda$ the same window. This is not the classic
"decay-then-add" Persistence-Filter order.

**[SPEC-DIFF #1 — the big one]** SPEC §3.2/§3.3 state the decay happens "each window / every
window" and that "a cell that stops being observed decays out." In code the entire block above
(decay **and** clamp included) is inside `if (touched)`. An **untouched** cell (out of FOV, no
hit and no miss that window) does **not** decay — its $\ell$ freezes. Forgetting is coupled to
being re-observed as free (a miss = touched), so clutter fades only while it stays in the sensor
field of view; anything that leaves the FOV persists indefinitely. SPEC's blanket "every window"
/ "stops being observed decays out" is inaccurate.

### 1.4 FreMEn feed (only for touched cells, when `enable_periodicity`)
`periodicity_.gather(id, occ, window_count_)` with $v=[\text{occ}]\in\{0,1\}$, phase index $t$:
$$n \mathrel{+}= 1,\quad S_0 \mathrel{+}= v,\quad
C_k \mathrel{+}= v\cos\big((k{+}1)\omega t\big),\quad S_k \mathrel{+}= v\sin\big((k{+}1)\omega t\big),\quad k=0..H{-}1,$$
$$\omega = \frac{2\pi}{\max(1,\,T)}.$$
$n$ counts **touched windows fed to gather**, not elapsed windows.

### 1.5 FreMEn phase prediction (`predict(id,t)`)
Returns $0.5$ if cell absent or $n\le 0$; else
$$\hat p(t)=\operatorname{clip}_{[0,1]}\!\left(\frac{S_0}{n}+\sum_{k=0}^{H-1}\Big[\underbrace{\tfrac{2C_k}{n}}_{a_k}\cos\big((k{+}1)\omega t\big)+\underbrace{\tfrac{2S_k}{n}}_{b_k}\sin\big((k{+}1)\omega t\big)\Big]\right).$$
DC term is the empirical mean $S_0/n$; harmonic coefficients $a_k=2C_k/n,\;b_k=2S_k/n$.
`periodicProb(id)` = `predict(id, window_count_)` (prediction at the current phase).

### 1.6 FreMEn dominant-harmonic amplitude + periodicity test
$$a=\begin{cases}0, & n < T\\[4pt]\displaystyle\max_{0\le k<H}\sqrt{a_k^2+b_k^2}=\max_k\sqrt{\big(\tfrac{2C_k}{n}\big)^2+\big(\tfrac{2S_k}{n}\big)^2}, & n \ge T\end{cases}$$
Periodicity test: $\text{periodic}=\texttt{enable\_periodicity}\ \land\ a \ge a_{\min}$ (`periodic_amplitude_min`).

**[SPEC-DIFF #3]** SPEC §3.5 says amplitude is zero "before `period_windows` windows have
**elapsed**." Code gates on $n<T$ where $n$ is the count of **touched** windows (calls to
`gather`), not elapsed wall/window time. A sparsely-observed cell needs $T$ touches, which can
span far more than $T$ windows.

### 1.7 Schmitt-trigger graduate / demote (in `endWindow`, evaluated every window for every cell)
$p$ and $a$ are recomputed for **all** cells (touched or not). Then:
$$\textbf{graduate:}\quad \lnot g \ \land\ \lnot\text{periodic}\ \land\ p \ge p_{\text{grad}}\ \land\ \text{observations}\ge N_{\min}\ \Rightarrow\ g\leftarrow\text{true}$$
$$\textbf{demote:}\quad g \ \land\ \big(p \le p_{\text{dem}}\ \lor\ \text{periodic}\big)\ \Rightarrow\ g\leftarrow\text{false}$$
($p_{\text{grad}}=$`graduate_prob`, $p_{\text{dem}}=$`demote_prob`, $N_{\min}=$`min_observations`.)
The $[p_{\text{dem}},\,p_{\text{grad}}]$ gap is the hysteresis band. Extra terms beyond SPEC's
formal block: `!periodic` blocks a strongly-periodic cell from ever graduating; `|| periodic`
force-demotes a graduated cell that later reveals periodicity. Graduate uses the **post-update**
$p$. The two rules can't fight in one window (graduate needs $\lnot$periodic and $p\ge p_{\text{grad}}>p_{\text{dem}}$).

**[SPEC-DIFF #2]** SPEC §3.4's formal pseudocode block omits the periodic guard entirely:
```
graduate: !graduated && p >= graduate_prob && observations >= min_observations
demote  :  graduated && p <= demote_prob
```
The `!periodic` (graduate) and `|| periodic` (demote) terms are only mentioned in §1/§3.5 prose
and code comments. A Method section transcribing §3.4 verbatim would be wrong.

### 1.8 Pruning (end of `endWindow`, after clearing window counters)
$$\text{erase cell} \iff \lnot g\ \land\ p < p_{\text{prune}}\ \land\ a < a_{\min}\quad(\text{also erases its FreMEn coeffs}).$$
Static cells ($g$) and periodic cells ($a\ge a_{\min}$) are never pruned.

### 1.9 Cell-class state machine (`classify`, snapshot; drivers = §1.7/§1.8)
States: **Unknown, Transient, Periodic, Static**. `classify(id)` priority ladder:
$$\text{absent}\to\text{Unknown};\quad g\to\text{Static};\quad (\text{enable}\land a\ge a_{\min})\to\text{Periodic};\quad p\ge p_{\text{prune}}\to\text{Transient};\quad \text{else}\to\text{Unknown}.$$
Note a *present* cell classifies **Unknown** when $p<p_{\text{prune}}$ and not periodic/graduated
(it is generally pruned the same window). All transitions (evidence-driven, at window close):

| From | To | Trigger |
|---|---|---|
| (implicit) Unknown | Transient | first `observeHit/Miss`: cell created, $\ell{=}0\Rightarrow p{=}0.5\ge p_{\text{prune}}$ |
| Transient | Static | $p\ge p_{\text{grad}}\land\text{obs}\ge N_{\min}\land\lnot\text{periodic}$ (graduate) |
| Transient | Periodic | $a\ge a_{\min}$ (needs $n\ge T$ touched windows) |
| Transient | Unknown (erased) | $p<p_{\text{prune}}\land a<a_{\min}\land\lnot g$ (prune) |
| Static | Transient / Periodic / Unknown | demote ($p\le p_{\text{dem}}\lor\text{periodic}$), then re-classified by ladder |
| Periodic | Transient/Unknown | $a$ falls below $a_{\min}$ (then subject to prune) |
| Periodic | — | never graduates (`!periodic` guard), never pruned while $a\ge a_{\min}$ |
| Static | — | never pruned while $g$ |

---

## 2. Tunable parameters (defaults from struct + params/*.yaml — they agree)

| Name | Default | Units | Meaning |
|---|---|---|---|
| `layer_interval` | 10 | frames/window | integration ticks per layered-update window |
| `l_hit` | 0.85 | log-odds | increment on an **occupied** window |
| `l_miss` | -0.4 | log-odds | increment on a **free** window |
| `l_min` | -5.0 | log-odds | clamp lower bound |
| `l_max` | 5.0 | log-odds | clamp upper bound |
| `survival_decay` ($\lambda$) | 0.97 | dimensionless (×/window) | Persistence-Filter forgetting multiplier |
| `graduate_prob` | 0.8 | probability | $p$ to promote → Static |
| `demote_prob` | 0.45 | probability | $p$ at/below which Static demotes (hysteresis floor) |
| `min_observations` | 3 | touch count | min touched windows before a cell may graduate |
| `prune_prob` | 0.05 | probability | erase non-static, non-periodic cell below this $p$ |
| `enable_periodicity` | true | bool | run the FreMEn model |
| `periodic_amplitude_min` ($a_{\min}$) | 0.3 | amplitude (occ units) | dominant-harmonic magnitude to classify Periodic |
| `period_windows` ($T$) | 24 | windows | FreMEn base period; also amplitude-validity gate ($n\ge T$) |
| `n_harmonics` ($H$) | 2 | count | Fourier harmonics tracked per cell |

Geometry/node params (not part of the engine math): `grid_width` 400, `grid_height` 400,
`grid_resolution` 0.05 m, `grid_origin_{x,y}` -10.0 m (grid2d); `voxel_size` 0.2 m (voxel3d);
`backend`, `global_frame`=`map`, `scan_topic`/`points_topic`, `publish_period` 1.0 s, `save_path`.

Derived constants worth citing: graduate threshold $p_{\text{grad}}{=}0.8\Rightarrow \ell\ge\ln 4\approx1.386$;
with $l_{\text{hit}}{=}0.85,\lambda{=}0.97$ the decayed fixed point of repeated hits is
$\ell^\star=\lambda l_{\text{hit}}/(1-\lambda)\approx 27.5$, clamped to $l_{\max}=5\Rightarrow p_{\max}=\sigma(5)\approx0.9933$;
$p_{\text{prune}}{=}0.05\Rightarrow\ell<\ln(1/19)\approx-2.944$.

---

## 3. Data-structure facts

- **Cell keying is a raw `std::int64_t` (`CellId`)** (`types.hpp`). The engine stores
  `std::unordered_map<CellId, CellEvidence>`; FreMEn stores `std::unordered_map<CellId, Coeff>`.
- **Geometry-free core.** `LayeredMap` and `PeriodicityModel` hold **no coordinates, no
  resolution, no origin** — only int64 ids. All geometry lives in `types.hpp`/backends:
  `gridCellId(m,gx,gy) = (int64)gy * m.width + gx` (2D), voxel hashing in the 3D backend. The
  identical engine therefore drives 2D and 3D (SPEC §1); `map_backend.hpp` is the only surface
  that sees `Eigen::Vector3d` endpoints.
- **Per-cell state size** (measured, x86-64, libstdc++):
  - `CellEvidence` = **24 bytes** (double $\ell$ 8; int observations/window_hits/window_misses
    4+4+4; bool graduated 1; padded to 24).
  - `Coeff` header = **64 bytes** (double n 8, double s0 8, two `std::vector<double>` headers
    24+24) **+ heap** $2{\cdot}H{\cdot}8$ bytes for the `c`,`s` arrays (= 32 B at $H{=}2$) ⇒
    ~96 B payload per periodic-tracked cell.
  - Plus per-entry `unordered_map` node overhead (~key + next-ptr + bucket, ~32 B) on each map.
  - FreMEn `Coeff` exists only for cells touched at least once with periodicity enabled.

---

## 4. Asymptotic cost ($N$ = live cells, $H$ = `n_harmonics`)

| Operation | Cost |
|---|---|
| `observeHit` / `observeMiss` | $O(1)$ amortized (one hash increment) |
| `tick()` non-closing frame | $O(1)$ |
| `tick()`/`endWindow()` window close | $O(N\cdot H)$ — per cell: $O(1)$ log-odds + `gather` $O(H)$ + `amplitude` $O(H)$ + graduate/demote/prune $O(1)$; iterates **all** cells incl. untouched. $H$ const ⇒ $O(N)$. |
| `classify`/`occupancyProb`/`periodicProb`/`isStatic`(single) | $O(1)$ lookup + $O(H)$ for amplitude/predict |
| `staticCells` | $O(N)$ |
| `transientCells`/`periodicCells` | $O(N\cdot H)$ (call `classify` per cell) |
| Memory | $O(N)$ evidence (~24 B + node) + $O(N_{\text{touched}})$ FreMEn (~96 B + node at $H{=}2$) |

Cost is per **window**, i.e. amortized $O(N/\texttt{layer\_interval})$ per frame; between windows a
frame is $O(1)$ plus $O(1)$ per endpoint/ray-cell for the backend (not part of core).
