#!/usr/bin/env bash
# Reproduces every phase 1 number from the cached real data. One single-threaded process at a
# time, niced. Definitions: PREREG_P1.md (sections 0 to 10 and Amendment 1 in section 11).
# Needs the data under DATA_ROOT (see check_realdata.sh).
#
# usage: reproduce_p1.sh STAGE...      STAGE: build check labels offset convert gates kth lil analyze report all
#   env: DATA_ROOT (default ~/datasets/strata)   BUILD_DIR (default /tmp/strata_p1_build)
#        RESULTS_DIR (default <this dir>/results_p1)
#        KTH_RES_LIST / LIL_RES_LIST (default all resolutions, e.g. KTH_RES_LIST="0.2")
#        MEM_LIMIT_KB (address-space limit per step, default 5000000: a step that needs more fails instead of starving other jobs)
#        LIL_TIMEBASE  selected (default): the scan-to-GT offset chosen by the `offset` stage on `mapping` (Amendment 1, 11.1)
#                      run_a: anchor start, offset 0 (the original definition); needs no `offset` stage
#        LIL_SUBDIR    directory under DATA_ROOT/p1 for the LILocBench caches and cell tables (default lil;
#                      run A was kept as lil_run_a_dt0)
#        SKIP_KTH / NO_FIGURES  (analyze stage) set to any value: LILocBench tables only / no figures
# Logs show paths relative to the placeholders <DATA_ROOT>, <BUILD_DIR>, <REALDATA_DIR>, <RESULTS_DIR>.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA="${DATA_ROOT:-$HOME/datasets/strata}"
BUILD="${BUILD_DIR:-/tmp/strata_p1_build}"
RES="${RESULTS_DIR:-$HERE/results_p1}"
OUT="$DATA/p1"                               # bulky outputs: label files, frame caches, cell tables
LILD="$OUT/${LIL_SUBDIR:-lil}"
PROBE="$BUILD/realdata/hitrate_probe"
mkdir -p "$RES/logs" "$RES/meta" "$OUT/kth00" "$LILD"

KTH_RES=(${KTH_RES_LIST:-0.2 0.1})
LIL_RES=(${LIL_RES_LIST:-0.05 0.10})
WINDOWS=(10 2)
LIL_SEQS=(mapping static_0 dynamics_0)
EGO_RADIUS=3.01
GROUND_H=0.30
OFFSET_STRIDE=10
TIMEBASE="${LIL_TIMEBASE:-selected}"

stamp() { date -Is; }
# Replaces the machine specific prefixes in a command line so that committed logs carry no personal paths.
scrub() {
  local s="$1"
  s="${s//$OUT/<DATA_ROOT>/p1}"; s="${s//$DATA/<DATA_ROOT>}"; s="${s//$BUILD/<BUILD_DIR>}"
  s="${s//$RES/<RESULTS_DIR>}"; s="${s//$HERE/<REALDATA_DIR>}"; s="${s//$HOME/\~}"
  printf '%s' "$s"
}
SOFT=0
step() {   # step NAME cmd...   : logs start, end, load, return code; stops the script on failure unless SOFT=1
  local name="$1"; shift
  local log="$RES/logs/$name.log"
  { echo "STEP $name"; echo "START $(stamp) host=$(hostname) load=$(cut -d' ' -f1-3 /proc/loadavg) cores=$(nproc)";
    echo "CMD $(scrub "$*")"; } > "$log"
  ( ulimit -v "${MEM_LIMIT_KB:-5000000}"; exec nice -n 19 "$@" ) 2>&1 | sed "s|$HOME|~|g" >> "$log"
  local rc=${PIPESTATUS[0]}
  { echo "END $(stamp) rc=$rc load=$(cut -d' ' -f1-3 /proc/loadavg)"; } >> "$log"
  if [ $rc -ne 0 ] && [ "$SOFT" != "1" ]; then echo "step $name FAILED rc=$rc (see $log)"; exit $rc; fi
  echo "step $name rc=$rc"
}
keepmeta() { cp "$@" "$RES/meta/"; }
metaval() { grep "^$2=" "$1" | head -1 | cut -d= -f2; }

stage_build() {
  step build_configure cmake -S "$HERE/.." -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
  step build_make cmake --build "$BUILD" -j1 --target hitrate_probe
}

stage_check() {
  step check_inputs bash "$HERE/check_realdata.sh" "$DATA"
  {
    echo "commit=$(git -C "$HERE" rev-parse HEAD)"
    # files changed in the tree outside the result directory: the analysis code must be committed
    echo "tree_dirty_files_outside_results=$(git -C "$HERE" status --porcelain -- . ':(exclude)results_p1' | wc -l)"
    echo "host=$(hostname)"; echo "kernel=$(uname -r)"; echo "cores=$(nproc)"
    echo "cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
    echo "loadavg=$(cat /proc/loadavg)"
    echo "compiler=$(c++ --version | head -1)"; echo "cmake=$(cmake --version | head -1)"
    echo "python=$(python3 -I --version 2>&1)"; echo "numpy=$(python3 -I -c 'import numpy; print(numpy.__version__)')"
    echo "data_root=<DATA_ROOT>"
    echo "data_inputs_sha256_of_list=$(sha256sum "$HERE/data_inputs.sha256" | cut -d' ' -f1)"
    echo "ego_radius_m=$EGO_RADIUS"; echo "ground_height_m=$GROUND_H"
    echo "kth_resolutions=${KTH_RES[*]}"; echo "lil_resolutions=${LIL_RES[*]}"; echo "windows=${WINDOWS[*]}"
    echo "lil_timebase=$TIMEBASE"; echo "lil_subdir=${LIL_SUBDIR:-lil}"
    if [ "$TIMEBASE" = "selected" ] && [ -f "$LILD/offset_mapping.meta" ]; then
      echo "lil_anchor=$(metaval "$LILD/offset_mapping.meta" selected_anchor)"; echo "lil_time_offset_s=$(metaval "$LILD/offset_mapping.meta" selected_dt_s)"
    else echo "lil_anchor=start"; echo "lil_time_offset_s=0"; fi
  } > "$RES/run_manifest.txt"
}

stage_labels() {
  for r in "${KTH_RES[@]}"; do
    step "kth_labels_r$r" "$PROBE" kth-labels --gt "$DATA/kth/00/gt_cloud.pcd" --dir "$DATA/kth/00" --res "$r" \
      --ground-h "$GROUND_H" --ego-radius "$EGO_RADIUS" --classes 1 --out "$OUT/kth00/labels_r$r.bin" --meta "$OUT/kth00/labels_r$r.meta"
    keepmeta "$OUT/kth00/labels_r$r.meta"
  done
}

# Scan-to-GT offset search (Amendment 1, 11.1): the selection comes from `mapping`; the other two
# sequences are diagnostics and may report an invalid search without stopping the run.
stage_offset() {
  step lil_offset_mapping "$PROBE" lil-offset --bag "$DATA/lilocbench/bags/mapping.bag" --gt "$DATA/lilocbench/gt_x/mapping/gt_poses.txt" \
    --stride "$OFFSET_STRIDE" --out "$LILD/offset_mapping.csv" --meta "$LILD/offset_mapping.meta"
  cp "$LILD/offset_mapping.csv" "$RES/meta/lil_offset_mapping.csv"; cp "$LILD/offset_mapping.meta" "$RES/meta/lil_offset_mapping.meta"
  SOFT=1
  for s in static_0 dynamics_0; do
    step "lil_offset_$s" "$PROBE" lil-offset --bag "$DATA/lilocbench/bags/$s.bag" --gt "$DATA/lilocbench/gt_x/$s/gt_poses.txt" \
      --stride "$OFFSET_STRIDE" --out "$LILD/offset_$s.csv" --meta "$LILD/offset_$s.meta"
    cp "$LILD/offset_$s.csv" "$RES/meta/lil_offset_$s.csv"; cp "$LILD/offset_$s.meta" "$RES/meta/lil_offset_$s.meta"
  done
  SOFT=0
}

stage_convert() {
  local anchor=start dt=0
  if [ "$TIMEBASE" = "selected" ]; then
    local m="$LILD/offset_mapping.meta"
    [ -f "$m" ] || { echo "no offset search result $m: run the offset stage first"; exit 2; }
    [ "$(metaval "$m" valid)" = "1" ] || { echo "the offset search on mapping is not valid: $(metaval "$m" reason)"; exit 3; }
    anchor="$(metaval "$m" selected_anchor)"; dt="$(metaval "$m" selected_dt_s)"
  elif [ "$TIMEBASE" != "run_a" ]; then echo "unknown LIL_TIMEBASE $TIMEBASE"; exit 2; fi
  for s in "${LIL_SEQS[@]}"; do
    step "lil_convert_$s" "$PROBE" lil-convert --bag "$DATA/lilocbench/bags/$s.bag" --gt "$DATA/lilocbench/gt_x/$s/gt_poses.txt" \
      --deskew 1 --anchor "$anchor" --time-offset "$dt" --out "$LILD/cache_$s.bin" --meta "$LILD/cache_$s.meta"
    cp "$LILD/cache_$s.meta" "$RES/meta/lil_convert_$s.meta"
  done
}

stage_gates() {
  step kth_gate "$PROBE" kth-gate --dir "$DATA/kth/00" --gt "$DATA/kth/00/gt_cloud.pcd" --res 0.2 --meta "$OUT/kth00/gate.meta"
  cp "$OUT/kth00/gate.meta" "$RES/meta/kth_gate.meta"
  for s in "${LIL_SEQS[@]}"; do
    step "lil_gate_$s" "$PROBE" lil-gate --cache "$LILD/cache_$s.bin" --res 0.05 --stride 5 --meta "$LILD/gate_$s.meta"
    cp "$LILD/gate_$s.meta" "$RES/meta/lil_gate_$s.meta"
  done
}

stage_kth() {
  for r in "${KTH_RES[@]}"; do for w in "${WINDOWS[@]}"; do
    step "kth_measure_r${r}_W$w" "$PROBE" kth-measure --dir "$DATA/kth/00" --labels "$OUT/kth00/labels_r$r.bin" \
      --res "$r" --window "$w" --out "$OUT/kth00/cells_r${r}_W$w.csv" --meta "$OUT/kth00/cells_r${r}_W$w.meta"
    keepmeta "$OUT/kth00/cells_r${r}_W$w.meta"
  done; done
}

stage_lil() {
  for r in "${LIL_RES[@]}"; do
    local margin; margin=$(python3 -I -c "print(max(1, round(0.10 / $r)))")
    step "lil_reference_r$r" "$PROBE" lil-reference --cache "$LILD/cache_mapping.bin" --res "$r" --min-touches 10 \
      --occ 0.5 --free 0.05 --margin "$margin" --out "$LILD/ref_r$r.bin" --meta "$LILD/ref_r$r.meta"
    cp "$LILD/ref_r$r.meta" "$RES/meta/lil_ref_r$r.meta"
    for s in "${LIL_SEQS[@]}"; do for w in "${WINDOWS[@]}"; do
      step "lil_measure_${s}_r${r}_W$w" "$PROBE" lil-measure --cache "$LILD/cache_$s.bin" --labels "$LILD/ref_r$r.bin" \
        --res "$r" --window "$w" --out "$LILD/cells_${s}_r${r}_W$w.csv" --meta "$LILD/cells_${s}_r${r}_W$w.meta"
      cp "$LILD/cells_${s}_r${r}_W$w.meta" "$RES/meta/lil_cells_${s}_r${r}_W$w.meta"
    done; done
  done
}

stage_analyze() {
  # analyze_p1.py reads <data-dir>/kth00 and <data-dir>/lil; a different LIL_SUBDIR is linked in as "lil"
  local dd="$OUT"
  if [ "${LIL_SUBDIR:-lil}" != "lil" ]; then
    dd="$OUT/analysis_${LIL_SUBDIR}"; mkdir -p "$dd"
    ln -sfn "$OUT/kth00" "$dd/kth00"; ln -sfn "$LILD" "$dd/lil"
  fi
  local extra=()
  [ -n "${SKIP_KTH:-}" ] && extra+=(--skip-kth)          # run A is re-analysed for LILocBench only
  [ -n "${NO_FIGURES:-}" ] && extra+=(--no-figures)
  step analyze python3 -I "$HERE/analyze_p1.py" --data-dir "$dd" --out-dir "$RES" "${extra[@]+"${extra[@]}"}"
}

stage_report() {
  step report python3 -I "$HERE/make_report.py" --results-dir "$RES" --out "$RES/P1_REPORT.md"
}

if [ $# -eq 0 ]; then echo "usage: $0 STAGE...  (build check labels offset convert gates kth lil analyze report all)"; exit 2; fi
for st in "$@"; do
  case "$st" in
    build) stage_build;; check) stage_check;; labels) stage_labels;; offset) stage_offset;; convert) stage_convert;; gates) stage_gates;;
    kth) stage_kth;; lil) stage_lil;; analyze) stage_analyze;; report) stage_report;;
    all) stage_build; stage_check; stage_labels
         if [ "$TIMEBASE" = "selected" ]; then stage_offset; fi
         stage_convert; stage_gates; stage_kth; stage_lil; stage_analyze; stage_report;;
    *) echo "unknown stage $st"; exit 2;;
  esac
done
