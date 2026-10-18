#!/usr/bin/env bash
# Phase 3 harness runner (PREREG_P3.md). One single-threaded process at a time, niced. Binaries are built from a `git archive` copy of
# HEAD, so the commit in every step log is the commit of the binary (other agents edit strata_core in the same worktree).
#
# usage: reproduce_p3.sh STAGE...
#   Stages that never open a test-split file:
#     build  check  unit  smoke_kth  smoke_lil
#   Stages that open test-split files for the first N records only (reader validation, logged under file-open traces):
#     validate_readers
#   Stages that stat test-split files:
#     statafter
#   Test evaluations (refuse to run unless P3_RUN_EVALUATIONS=1 and PREREG_P3.md is committed and unmodified; see guard_eval):
#     eval_lil  eval_spires  eval_kth  eval_boreas
#   env: DATA_ROOT (default ~/datasets/strata)  BUILD_DIR (default ~/.cache/strata_p3_build, on ext4)
#        RESULTS_DIR (default <this dir>/results_p3)  OUT_DIR (default /mnt/d/wsl/strata/p3; bulky outputs, never in the repository)
#        MEM_LIMIT_KB (address space per step, default 6000000)  P3_PERIODICITY (1 = primary, 0 = secondary; default 1)
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA="${DATA_ROOT:-$HOME/datasets/strata}"
BUILD="${BUILD_DIR:-$HOME/.cache/strata_p3_build}"
RES="${RESULTS_DIR:-$HERE/results_p3}"
OUT="${OUT_DIR:-/mnt/d/wsl/strata/p3}"
P1="$DATA/p1"
PER="${P3_PERIODICITY:-1}"
P3="$BUILD/build/realdata/p3_probe"
HP="$BUILD/build/realdata/hitrate_probe"
PB="$BUILD/build/realdata/p1b_probe"
KEBLE="$DATA/spires/keble"
ARMS="shipped_v0_1 symmetric octomap elite_like sprt"
mkdir -p "$RES/logs" "$RES/runs" "$RES/access" "$OUT"

stamp() { date -Is; }
scrub() {
  local s="$1"
  s="${s//$OUT/<OUT_DIR>}"; s="${s//$DATA/<DATA_ROOT>}"; s="${s//$BUILD/<BUILD_DIR>}"; s="${s//$RES/<RESULTS_DIR>}"; s="${s//$HERE/<REALDATA_DIR>}"; s="${s//$HOME/\~}"
  printf '%s' "$s"
}
tree_dirty() { git -C "$HERE" status --porcelain -- ':(glob)p3_*' ':(glob)test_p3*' ':(glob)mutation_p3.py' ':(glob)reproduce_p3.sh' ':(glob)data_inputs_p3*' ':(glob)PREREG_P3.md' ':(glob)CMakeLists.txt' ':(glob)realdata_io.hpp' ':(glob)p1b_*' ':(glob)probe_core.hpp' ':(glob)hitrate_probe.cpp' ':(glob)stat_test_split.sh' ':(glob)compare_stat.py' | wc -l; }

# step NAME [--traced] [--input P]... [--output P]... -- cmd...
#   logs host, load, commit and the command (p3_runlog.py run), and with --traced runs the command under the file-open trace (p3_traced.sh)
step() {
  local name="$1"; shift
  local traced=0; local -a rl=()
  while [ $# -gt 0 ] && [ "$1" != "--" ]; do
    case "$1" in --traced) traced=1; shift;; --input|--output) rl+=("$1" "$2"); shift 2;; *) echo "step: bad option $1"; exit 2;; esac
  done
  shift
  local log="$RES/logs/$name.log"
  # a repeated step keeps the earlier attempt (failures are reported, PREREG_P3.md section 4)
  local ts; ts="$(date +%Y%m%dT%H%M%S)"
  [ -f "$log" ] && mv "$log" "$RES/logs/$name.attempt-$ts.log"
  [ -f "$RES/runs/$name.json" ] && mv "$RES/runs/$name.json" "$RES/runs/$name.attempt-$ts.json"
  { echo "STEP $name"; echo "START $(stamp) host=$(hostname) load=$(cut -d' ' -f1-3 /proc/loadavg) cores=$(nproc)"
    echo "COMMIT $(git -C "$HERE" rev-parse HEAD)"; echo "EXPERIMENT_DIR_UNCOMMITTED $(tree_dirty)"
    echo "BINARY_COMMIT $(cat "$BUILD/commit" 2>/dev/null || echo unknown)"
    echo "CMD $(scrub "$*")"; } > "$log"
  local -a inner=("$@")
  if [ "$traced" = "1" ]; then inner=(bash "$HERE/p3_traced.sh" "$name" "$@"); fi
  ( ulimit -v "${MEM_LIMIT_KB:-6000000}"; exec nice -n 19 python3 -I "$HERE/p3_runlog.py" run --step "$name" --results "$RES" "${rl[@]}" -- "${inner[@]}" ) 2>&1 | sed "s|$HOME|~|g; s|$OUT|<OUT_DIR>|g" >> "$log"
  local rc=${PIPESTATUS[0]}
  echo "END $(stamp) rc=$rc load=$(cut -d' ' -f1-3 /proc/loadavg)" >> "$log"
  echo "step $name rc=$rc"
  return $rc
}
must() { "$@" || { echo "stopping: step failed"; exit 1; }; }
engine_has_sprt() { "$P3" arms | grep -q '^engine_has_sprt=1'; }

stage_build() {   # from a git archive copy of HEAD: the binary is the committed code
  local commit; commit="$(git -C "$HERE" rev-parse HEAD)"
  rm -rf "$BUILD/src"; mkdir -p "$BUILD/src"
  git -C "$(git -C "$HERE" rev-parse --show-toplevel)" archive HEAD | tar -x -C "$BUILD/src"
  echo "$commit" > "$BUILD/commit"
  must step build_configure -- cmake -S "$BUILD/src/paper/experiments" -B "$BUILD/build" -DCMAKE_BUILD_TYPE=Release
  must step build_make -- cmake --build "$BUILD/build" -j4 --target p3_probe hitrate_probe p1b_probe test_p3
}

stage_check() {
  {
    echo "commit=$(git -C "$HERE" rev-parse HEAD)"; echo "binary_commit=$(cat "$BUILD/commit" 2>/dev/null || echo none)"
    echo "experiment_dir_uncommitted=$(tree_dirty)"
    echo "host=$(hostname)"; echo "kernel=$(uname -r)"; echo "cores=$(nproc)"
    echo "cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
    echo "loadavg=$(cat /proc/loadavg)"; echo "mem_available_kb=$(grep MemAvailable /proc/meminfo | tr -s ' ' | cut -d' ' -f2)"
    echo "compiler=$(c++ --version | head -1)"; echo "cmake=$(cmake --version | head -1)"; echo "python=$(python3 -I --version 2>&1)"
    echo "numpy=$(python3 -I -c 'import numpy; print(numpy.__version__)')"
    echo "p3_probe_sha256=$(sha256sum "$P3" 2>/dev/null | cut -d' ' -f1)"
    echo "engine_has_sprt=$("$P3" arms 2>/dev/null | grep engine_has_sprt | cut -d= -f2)"
    echo "data_root=<DATA_ROOT>"; echo "out_dir=<OUT_DIR>"
    python3 -I "$HERE/p3_runlog.py" check-inputs --data-root "$DATA" --list "$HERE/data_inputs_p3.sha256"
  } > "$RES/run_manifest.txt" 2>&1
  cat "$RES/run_manifest.txt"
}

stage_unit() {
  must step unit_cpp -- "$BUILD/build/realdata/test_p3"
  must step unit_py -- bash -c "cd '$HERE' && python3 -I -m unittest discover -s . -p 'test_p3*.py'"
}

# ----------------------------------------------------------------------------- reader validation ------
# First N records only (N = 10). Each command runs under the file-open trace; the access log lists every data file opened.
stage_validate_readers() {
  local n=10 dt
  dt="$(grep '^selected_dt_s=' "$HERE/results_p1b/meta/spires_offset.meta" | cut -d= -f2)"
  # Keble 03: the per-scan zip reader, the GT poses and the extrinsic of calibration/README.md as used for Keble 02
  must step validate_spires_keble03 --traced -- "$P3" spires-validate --zip "$KEBLE/sequences/2024-03-12-keble-college-03/raw/lidar-clouds.zip" \
    --gt "$KEBLE/sequences/2024-03-12-keble-college-03/processed/trajectory/gt-tum.txt" --dt "$dt" --n $n
  # Boreas: layout by bytes, ranges, pose convention, ENU from latitude and longitude, conversion of the first frames
  must step validate_boreas_2020 --traced -- "$P3" boreas-validate --seq "$DATA/boreas/boreas-2020-11-26-13-58" --n $n
  # KTH test sequences
  must step validate_kth_05 --traced -- "$P3" kth-validate --dir "$DATA/kth/05" --n $n
  must step validate_kth_semindoor --traced -- "$P3" kth-validate --dir "$DATA/kth/semindoor" --n $n
  # LILocBench lt_changes bags and GT
  for s in lt_changes_0 lt_changes_dynamics_0; do
    must step "validate_lil_$s" --traced -- "$P3" lil-validate --bag "$DATA/lilocbench/bags/$s.bag" --gt "$DATA/lilocbench/gt_x/$s/gt_poses.txt" --n $n
  done
}

# Pose-only gate before any Boreas run (PREREG_P3.md section 5.3): the common frame and the 10 m tile overlap of the three trajectories.
stage_boreas_gate() {
  must step boreas_overlap --traced -- "$P3" boreas-overlap --seqs "$DATA/boreas/boreas-2020-11-26-13-58,$DATA/boreas/boreas-2021-04-08-12-44,$DATA/boreas/boreas-2021-06-17-17-52"
}

# ------------------------------------------------------------------------- calibration smoke tests ------
# The same code on CALIBRATION data (KITTI 00, LILocBench dynamics_0): harness checks, in-sample, not results.
stage_smoke_kth() {
  local seq="$DATA/kth/00" d="$OUT/smoke_kth00"; mkdir -p "$d"
  MEM_LIMIT_KB=9000000 must step smoke_kth00_selfcheck -- python3 -I "$HERE/p3_eval.py" kth-selfcheck --gt "$seq/gt_cloud.pcd"
  for arm in shipped_v0_1 octomap; do
    must step "smoke_kth00_export_$arm" -- "$P3" kth-export --dir "$seq" --arm "$arm" --res 0.2 --window 10 --periodicity "$PER" \
      --policies static_only,not_rejected,all --out-prefix "$d/$arm" --meta "$d/$arm.meta"
    for pol in static_only not_rejected all; do
      MEM_LIMIT_KB=9000000 must step "smoke_kth00_eval_${arm}_$pol" -- bash -c "python3 -I '$HERE/p3_eval.py' kth --gt '$seq/gt_cloud.pcd' --map '$d/${arm}_$pol.pcd' > '$RES/smoke_kth00_${arm}_$pol.json'"
    done
  done
}

stage_smoke_lil() {
  local d="$OUT/smoke_lil"; mkdir -p "$d"
  must step smoke_lil_refexport -- "$P3" ref-export --labels "$P1/lil/ref_r0.05.bin" --res 0.05 --out "$d/ref_r0.05.csv"
  for arm in $ARMS; do
    if [ "$arm" = "sprt" ] && ! engine_has_sprt; then echo "arm sprt NOT RUN: this build has no evidence_model = sprt" | tee -a "$RES/logs/arms_not_run.txt"; continue; fi
    must step "smoke_lil_dyn0_$arm" -- "$P3" arm-eval --cache "$P1/lil/cache_dynamics_0.bin" --labels "$P1/lil/ref_r0.05.bin" --arm "$arm" --res 0.05 --window 15 \
      --periodicity "$PER" --out "$d/dump_dynamics_0_$arm.csv" --meta "$d/dump_dynamics_0_$arm.meta"
    must step "smoke_lil_dyn0_eval_$arm" -- bash -c "python3 -I '$HERE/p3_eval.py' lil --dump '$d/dump_dynamics_0_$arm.csv' --res 0.05 > '$RES/smoke_lil_dynamics_0_$arm.json'"
  done
}

# --------------------------------------------------------------------------------- test evaluations ------
guard_eval() {
  if [ "${P3_RUN_EVALUATIONS:-0}" != "1" ]; then echo "refusing: test evaluations need P3_RUN_EVALUATIONS=1 (set after the review of PREREG_P3.md)"; exit 4; fi
  if [ -z "$(git -C "$HERE" log -1 --format=%H -- PREREG_P3.md 2>/dev/null)" ]; then echo "refusing: PREREG_P3.md is not committed"; exit 4; fi
  if [ -n "$(git -C "$HERE" status --porcelain -- PREREG_P3.md data_inputs_p3.sha256 p3_arms.hpp)" ]; then echo "refusing: PREREG_P3.md, the arm table or the input list has uncommitted changes"; exit 4; fi
  if [ "$(tree_dirty)" != "0" ]; then echo "refusing: uncommitted files in the experiment directory"; exit 4; fi
  if [ "$(cat "$BUILD/commit" 2>/dev/null)" != "$(git -C "$HERE" rev-parse HEAD)" ]; then echo "refusing: the binary was not built from HEAD (run the build stage)"; exit 4; fi
  { echo "evaluations started $(stamp)"; echo "head $(git -C "$HERE" rev-parse HEAD)"; echo "prereg_p3_last_commit $(git -C "$HERE" log -1 --format='%H %cI' -- PREREG_P3.md)"; } >> "$RES/eval_start.txt"
}

arms_to_run() {
  for arm in $ARMS; do
    if [ "$arm" = "sprt" ] && ! engine_has_sprt; then echo "arm sprt NOT RUN: this build has no evidence_model = sprt" | tee -a "$RES/logs/arms_not_run.txt" >&2; continue; fi
    echo "$arm"
  done
  if engine_has_sprt; then echo sprt_low_end; echo sprt_high_end; fi
}

lil_sequence() {   # lil_sequence SEQ: convert (run B time base), then every arm against the mapping reference
  local seq="$1" d="$OUT/lil"; mkdir -p "$d"
  [ -f "$d/cache_$seq.bin" ] || must step "lil_convert_$seq" --traced -- "$HP" lil-convert --bag "$DATA/lilocbench/bags/$seq.bag" --gt "$DATA/lilocbench/gt_x/$seq/gt_poses.txt" \
    --deskew 1 --anchor start --time-offset -0.13 --out "$d/cache_$seq.bin" --meta "$d/cache_$seq.meta"
  cp "$d/cache_$seq.meta" "$RES/meta_lil_convert_$seq.meta" 2>/dev/null
  # time-base diagnostic of the lt_changes bag (selects nothing; PREREG_P3.md section 6.1): the scan-to-GT offset search of phase 1
  must step "lil_offset_$seq" --traced -- "$HP" lil-offset --bag "$DATA/lilocbench/bags/$seq.bag" --gt "$DATA/lilocbench/gt_x/$seq/gt_poses.txt" \
    --out "$d/offset_$seq.csv" --meta "$RES/meta_lil_offset_$seq.meta"
  for arm in $(arms_to_run); do
    must step "lil_${seq}_$arm" -- "$P3" arm-eval --cache "$d/cache_$seq.bin" --labels "$P1/lil/ref_r0.05.bin" --arm "$arm" --res 0.05 --window 15 --periodicity "$PER" \
      --out "$d/dump_${seq}_${arm}_p$PER.csv" --meta "$d/dump_${seq}_${arm}_p$PER.meta"
    must step "lil_eval_${seq}_$arm" -- bash -c "python3 -I '$HERE/p3_eval.py' lil --dump '$d/dump_${seq}_${arm}_p$PER.csv' --res 0.05 > '$RES/lil_${seq}_${arm}_p$PER.json'"
  done
}
stage_eval_lil() { guard_eval; lil_sequence lt_changes_dynamics_0; lil_sequence lt_changes_0; }

stage_eval_spires() {
  guard_eval
  local dt; dt="$(grep '^selected_dt_s=' "$HERE/results_p1b/meta/spires_offset.meta" | cut -d= -f2)"
  for s in 03 04 05; do
    local seq="$KEBLE/sequences/2024-03-12-keble-college-$s" d="$OUT/spires_$s"; mkdir -p "$d"
    must step "spires_${s}_tls" --traced -- "$PB" spires-tls --pcd "$KEBLE/ground_truth_map/keble-college/merged-cloud-1cm.pcd" --gt "$seq/processed/trajectory/gt-tum.txt" \
      --radius 30 --out-dir "$d" --meta "$d/spires_tls.meta"
    must step "spires_${s}_convert" --traced -- "$PB" spires-convert --zip "$seq/raw/lidar-clouds.zip" --gt "$seq/processed/trajectory/gt-tum.txt" --dt "$dt" \
      --out "$d/cache.bin" --meta "$d/spires_convert.meta"
    for r in 0.2 0.1; do
      must step "spires_${s}_labels_r$r" -- "$PB" spires-labels --tls-dir "$d" --cache "$d/cache.bin" --res "$r" --out "$d/labels_r$r.bin" --meta "$d/labels_r$r.meta"
      for arm in $(arms_to_run); do
        for w in 10 20; do
          must step "spires_${s}_${arm}_r${r}_W$w" -- "$P3" arm-eval --cache "$d/cache.bin" --labels "$d/labels_r$r.bin" --arm "$arm" --res "$r" --window "$w" --periodicity "$PER" \
            --format counts --out "$d/counts_${arm}_r${r}_W${w}_p$PER.csv" --meta "$d/counts_${arm}_r${r}_W${w}_p$PER.meta"
          must step "spires_eval_${s}_${arm}_r${r}_W$w" -- bash -c "python3 -I '$HERE/p3_eval.py' spires --counts '$d/counts_${arm}_r${r}_W${w}_p$PER.csv' --latency '$d/counts_${arm}_r${r}_W${w}_p$PER.csv.latency' > '$RES/spires_${s}_${arm}_r${r}_W${w}_p$PER.json'"
        done
      done
    done
  done
  # agreement of the Static sets of the three same-day sequences (one TLS frame), per arm
  for arm in $(arms_to_run); do
    must step "spires_static_${arm}" -- "$P3" static-export --caches "$OUT/spires_03/cache.bin,$OUT/spires_04/cache.bin,$OUT/spires_05/cache.bin" --arm "$arm" --res 0.2 --window 10 \
      --periodicity "$PER" --mode independent --out-prefix "$OUT/spires_agree_${arm}" --meta "$OUT/spires_agree_${arm}.meta"
    must step "spires_jaccard_${arm}" -- bash -c "python3 -I '$HERE/p3_eval.py' jaccard --ids '$OUT/spires_agree_${arm}_s0_static.ids' '$OUT/spires_agree_${arm}_s1_static.ids' '$OUT/spires_agree_${arm}_s2_static.ids' --res 0.2 > '$RES/spires_agree_${arm}.json'"
  done
}

stage_eval_kth() {
  guard_eval
  # KITTI 05 at 0.2 m (W = 10: 32 windows; W = 2: 160 windows) and semi-indoor at 0.2 m and 0.1 m (W = 10: 96 windows; W = 2)
  for cfg in "05 0.2 10" "05 0.2 2" "semindoor 0.2 10" "semindoor 0.2 2" "semindoor 0.1 10"; do
    set -- $cfg; local seq="$DATA/kth/$1" r="$2" w="$3" d="$OUT/kth_$1"; mkdir -p "$d"
    if [ ! -f "$RES/kth_selfcheck_$1.json" ]; then
      MEM_LIMIT_KB=9000000 must step "kth_selfcheck_$1" --traced -- bash -c "python3 -I '$HERE/p3_eval.py' kth-selfcheck --gt '$seq/gt_cloud.pcd' > '$RES/kth_selfcheck_$1.json'"
    fi
    for arm in $(arms_to_run); do
      must step "kth_${1}_${arm}_r${r}_W$w" --traced -- "$P3" kth-export --dir "$seq" --arm "$arm" --res "$r" --window "$w" --periodicity "$PER" \
        --policies static_only,not_rejected --out-prefix "$d/${arm}_r${r}_W${w}_p$PER" --meta "$d/${arm}_r${r}_W${w}_p$PER.meta"
      for pol in static_only not_rejected; do
        MEM_LIMIT_KB=9000000 must step "kth_eval_${1}_${arm}_r${r}_W${w}_$pol" --traced -- bash -c "python3 -I '$HERE/p3_eval.py' kth --gt '$seq/gt_cloud.pcd' --map '$d/${arm}_r${r}_W${w}_p${PER}_$pol.pcd' > '$RES/kth_${1}_${arm}_r${r}_W${w}_p${PER}_$pol.json'"
        rm -f "$d/${arm}_r${r}_W${w}_p${PER}_$pol.pcd"          # bulky; the meta file and the JSON keep the numbers
      done
    done
  done
}

stage_eval_boreas() {
  guard_eval
  local ids=(boreas-2020-11-26-13-58 boreas-2021-04-08-12-44 boreas-2021-06-17-17-52) d="$OUT/boreas"; mkdir -p "$d"
  stage_boreas_gate
  local caches=""
  # pass 1: common frame without a vertical offset; the ground height per 10 m tile gives the offset of each session to the first one
  for id in "${ids[@]}"; do
    must step "boreas_convert0_$id" --traced -- "$P3" boreas-convert --seq "$DATA/boreas/$id" --ref "$DATA/boreas/${ids[0]}" --out "$d/cache0_$id.bin" --meta "$d/cache0_$id.meta"
    must step "boreas_ground_$id" -- "$P3" tile-ground --cache "$d/cache0_$id.bin" --out "$d/ground_$id.csv"
  done
  # pass 2: the same conversion with the offset (0 for the reference session)
  for id in "${ids[@]}"; do
    local zoff=0
    if [ "$id" != "${ids[0]}" ]; then zoff="$(python3 -I "$HERE/p3_eval.py" zoffset --a "$d/ground_${ids[0]}.csv" --b "$d/ground_$id.csv" --field median)"; fi
    python3 -I "$HERE/p3_eval.py" zoffset --a "$d/ground_${ids[0]}.csv" --b "$d/ground_$id.csv" > "$RES/boreas_zoffset_$id.json"
    echo "$id $zoff" >> "$RES/boreas_zoffsets_used.txt"
    must step "boreas_convert_$id" --traced -- "$P3" boreas-convert --seq "$DATA/boreas/$id" --ref "$DATA/boreas/${ids[0]}" --out "$d/cache_$id.bin" --meta "$d/cache_$id.meta" --z-offset "$zoff"
    caches="${caches:+$caches,}$d/cache_$id.bin"
  done
  for arm in $(arms_to_run); do
    for mode in independent cumulative; do
      must step "boreas_static_${arm}_$mode" -- "$P3" static-export --caches "$caches" --arm "$arm" --res 0.2 --window 10 --periodicity "$PER" --mode "$mode" \
        --out-prefix "$d/${arm}_$mode" --meta "$d/${arm}_$mode.meta"
    done
    must step "boreas_jaccard_$arm" -- bash -c "python3 -I '$HERE/p3_eval.py' jaccard --ids '$d/${arm}_independent_s0_static.ids' '$d/${arm}_independent_s1_static.ids' '$d/${arm}_independent_s2_static.ids' --res 0.2 > '$RES/boreas_${arm}_independent.json'"
    must step "boreas_newstatic_$arm" -- bash -c "python3 -I '$HERE/p3_eval.py' newstatic --cum '$d/${arm}_cumulative_s2_static.ids' --ind '$d/${arm}_independent_s0_static.ids' '$d/${arm}_independent_s1_static.ids' '$d/${arm}_independent_s2_static.ids' > '$RES/boreas_${arm}_newstatic.json'"
  done
}

stage_statafter() {
  bash "$HERE/stat_test_split.sh" "$RES/test_split_stat_after.tsv"
  python3 -I "$HERE/compare_stat.py" --before "$RES/test_split_stat_before.tsv" "$RES/test_split_stat_before_boreas.tsv" \
    --after "$RES/test_split_stat_after.tsv" "$RES/test_split_stat_after_boreas.tsv" --out "$RES/test_split_stat_compare.md"
}

if [ $# -eq 0 ]; then echo "usage: $0 STAGE..."; exit 2; fi
for st in "$@"; do
  case "$st" in
    build) stage_build;; check) stage_check;; unit) stage_unit;; validate_readers) stage_validate_readers;;
    boreas_gate) stage_boreas_gate;; smoke_kth) stage_smoke_kth;; smoke_lil) stage_smoke_lil;; statafter) stage_statafter;;
    eval_lil) stage_eval_lil;; eval_spires) stage_eval_spires;; eval_kth) stage_eval_kth;; eval_boreas) stage_eval_boreas;;
    *) echo "unknown stage $st"; exit 2;;
  esac
done
