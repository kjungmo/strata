#!/usr/bin/env bash
# Reproduces every phase 1 number from the cached real data. One single-threaded process at a
# time, niced. Definitions: PREREG_P1.md. Needs the data under DATA_ROOT (see check_realdata.sh).
#
# usage: reproduce_p1.sh STAGE...      STAGE: build check labels gates convert kth lil analyze report all
#   env: DATA_ROOT (default ~/datasets/strata)   BUILD_DIR (default /tmp/strata_p1_build)
#        RESULTS_DIR (default <this dir>/results_p1)    P1_ONLY (optional: "kth" or "lil" for the kth/lil stages)
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA="${DATA_ROOT:-$HOME/datasets/strata}"
BUILD="${BUILD_DIR:-/tmp/strata_p1_build}"
RES="${RESULTS_DIR:-$HERE/results_p1}"
OUT="$DATA/p1"                               # bulky outputs: label files, frame caches, cell tables
PROBE="$BUILD/realdata/hitrate_probe"
mkdir -p "$RES/logs" "$RES/meta" "$OUT/kth00" "$OUT/lil"

KTH_RES=(0.2 0.1)
LIL_RES=(0.05 0.10)
WINDOWS=(10 2)
LIL_SEQS=(mapping static_0 dynamics_0)
EGO_RADIUS=3.01
GROUND_H=0.30

stamp() { date -Is; }
step() {   # step NAME cmd...   : logs start, end, load, return code; stops the script on failure
  local name="$1"; shift
  local log="$RES/logs/$name.log"
  { echo "STEP $name"; echo "START $(stamp) host=$(hostname) load=$(cut -d' ' -f1-3 /proc/loadavg) cores=$(nproc)";
    echo "CMD $*"; } > "$log"
  nice -n 19 "$@" >> "$log" 2>&1
  local rc=$?
  { echo "END $(stamp) rc=$rc load=$(cut -d' ' -f1-3 /proc/loadavg)"; } >> "$log"
  if [ $rc -ne 0 ]; then echo "step $name FAILED rc=$rc (see $log)"; exit $rc; fi
  echo "step $name ok"
}
keepmeta() { cp "$@" "$RES/meta/"; }

stage_build() {
  step build_configure cmake -S "$HERE/.." -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
  step build_make cmake --build "$BUILD" -j1 --target hitrate_probe
}

stage_check() {
  step check_inputs bash "$HERE/check_realdata.sh" "$DATA"
  {
    echo "commit=$(git -C "$HERE" rev-parse HEAD)"
    echo "tree_dirty_files=$(git -C "$HERE" status --porcelain | wc -l)"
    echo "host=$(hostname)"; echo "kernel=$(uname -r)"; echo "cores=$(nproc)"
    echo "cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
    echo "loadavg=$(cat /proc/loadavg)"
    echo "compiler=$(c++ --version | head -1)"; echo "cmake=$(cmake --version | head -1)"
    echo "python=$(python3 -I --version 2>&1)"; echo "numpy=$(python3 -I -c 'import numpy; print(numpy.__version__)')"
    echo "data_root=$DATA"
    echo "data_inputs_sha256_of_list=$(sha256sum "$HERE/data_inputs.sha256" | cut -d' ' -f1)"
    echo "ego_radius_m=$EGO_RADIUS"; echo "ground_height_m=$GROUND_H"
    echo "kth_resolutions=${KTH_RES[*]}"; echo "lil_resolutions=${LIL_RES[*]}"; echo "windows=${WINDOWS[*]}"
  } > "$RES/run_manifest.txt"
}

stage_labels() {
  for r in "${KTH_RES[@]}"; do
    step "kth_labels_r$r" "$PROBE" kth-labels --gt "$DATA/kth/00/gt_cloud.pcd" --dir "$DATA/kth/00" --res "$r" \
      --ground-h "$GROUND_H" --ego-radius "$EGO_RADIUS" --out "$OUT/kth00/labels_r$r.bin" --meta "$OUT/kth00/labels_r$r.meta"
    keepmeta "$OUT/kth00/labels_r$r.meta"
  done
}

stage_convert() {
  for s in "${LIL_SEQS[@]}"; do
    step "lil_convert_$s" "$PROBE" lil-convert --bag "$DATA/lilocbench/bags/$s.bag" --gt "$DATA/lilocbench/gt_x/$s/gt_poses.txt" \
      --deskew 1 --out "$OUT/lil/cache_$s.bin" --meta "$OUT/lil/cache_$s.meta"
    cp "$OUT/lil/cache_$s.meta" "$RES/meta/lil_convert_$s.meta"
  done
}

stage_gates() {
  step kth_gate "$PROBE" kth-gate --dir "$DATA/kth/00" --gt "$DATA/kth/00/gt_cloud.pcd" --res 0.2 --meta "$OUT/kth00/gate.meta"
  cp "$OUT/kth00/gate.meta" "$RES/meta/kth_gate.meta"
  for s in "${LIL_SEQS[@]}"; do
    step "lil_gate_$s" "$PROBE" lil-gate --cache "$OUT/lil/cache_$s.bin" --res 0.05 --stride 5 --meta "$OUT/lil/gate_$s.meta"
    cp "$OUT/lil/gate_$s.meta" "$RES/meta/lil_gate_$s.meta"
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
    step "lil_reference_r$r" "$PROBE" lil-reference --cache "$OUT/lil/cache_mapping.bin" --res "$r" --min-touches 10 \
      --occ 0.5 --free 0.05 --margin "$margin" --out "$OUT/lil/ref_r$r.bin" --meta "$OUT/lil/ref_r$r.meta"
    cp "$OUT/lil/ref_r$r.meta" "$RES/meta/lil_ref_r$r.meta"
    for s in "${LIL_SEQS[@]}"; do for w in "${WINDOWS[@]}"; do
      step "lil_measure_${s}_r${r}_W$w" "$PROBE" lil-measure --cache "$OUT/lil/cache_$s.bin" --labels "$OUT/lil/ref_r$r.bin" \
        --res "$r" --window "$w" --out "$OUT/lil/cells_${s}_r${r}_W$w.csv" --meta "$OUT/lil/cells_${s}_r${r}_W$w.meta"
      cp "$OUT/lil/cells_${s}_r${r}_W$w.meta" "$RES/meta/lil_cells_${s}_r${r}_W$w.meta"
    done; done
  done
}

stage_analyze() {
  step analyze python3 -I "$HERE/analyze_p1.py" --data-dir "$OUT" --out-dir "$RES"
}

stage_report() {
  step report python3 -I "$HERE/make_report.py" --results-dir "$RES" --out "$RES/P1_REPORT.md"
}

if [ $# -eq 0 ]; then echo "usage: $0 STAGE...  (build check labels gates convert kth lil analyze report all)"; exit 2; fi
for st in "$@"; do
  case "$st" in
    build) stage_build;; check) stage_check;; labels) stage_labels;; convert) stage_convert;; gates) stage_gates;;
    kth) stage_kth;; lil) stage_lil;; analyze) stage_analyze;; report) stage_report;;
    all) stage_build; stage_check; stage_labels; stage_convert; stage_gates; stage_kth; stage_lil; stage_analyze; stage_report;;
    *) echo "unknown stage $st"; exit 2;;
  esac
done
