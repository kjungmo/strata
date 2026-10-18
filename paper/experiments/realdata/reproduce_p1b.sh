#!/usr/bin/env bash
# Reproduces every phase 1b number from the cached real data (PREREG_P1B.md). One single-threaded process at a
# time, niced. Needs the data under DATA_ROOT and the phase 1 outputs under DATA_ROOT/p1 (run B).
#
# usage: reproduce_p1b.sh STAGE...
#   STAGE: build  check  gates2d  gatekth  lil15  equiv  tls  offset  convert  gatespires  labels  measure  statafter  analyze  freeze  report  replay
#   env: DATA_ROOT (default ~/datasets/strata)  BUILD_DIR (default /tmp/strata_p1b_build)
#        RESULTS_DIR (default <this dir>/results_p1b)  OUT_DIR (default /mnt/d/wsl/strata/p1b: bulky outputs, never in the repository)
#        MEM_LIMIT_KB (address space per step, default 6000000)
# Logs show paths relative to the placeholders <DATA_ROOT>, <BUILD_DIR>, <REALDATA_DIR>, <RESULTS_DIR>, <OUT_DIR>.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA="${DATA_ROOT:-$HOME/datasets/strata}"
BUILD="${BUILD_DIR:-/tmp/strata_p1b_build}"
RES="${RESULTS_DIR:-$HERE/results_p1b}"
OUT="${OUT_DIR:-/mnt/d/wsl/strata/p1b}"
P1="$DATA/p1"                              # phase 1 outputs (run B): frame caches, label files, cell tables
PROBE="$BUILD/realdata/p1b_probe"
HPROBE="$BUILD/realdata/hitrate_probe"
KEBLE="$DATA/spires/keble"
SEQ="$KEBLE/sequences/2024-03-12-keble-college-02"
mkdir -p "$RES/logs" "$RES/meta" "$OUT"

stamp() { date -Is; }
scrub() {
  local s="$1"
  s="${s//$OUT/<OUT_DIR>}"; s="${s//$P1/<DATA_ROOT>/p1}"; s="${s//$DATA/<DATA_ROOT>}"; s="${s//$BUILD/<BUILD_DIR>}"
  s="${s//$RES/<RESULTS_DIR>}"; s="${s//$HERE/<REALDATA_DIR>}"; s="${s//$HOME/\~}"
  printf '%s' "$s"
}
SOFT=0
step() {   # step NAME cmd...: logs start, end, load, return code; stops the script on failure unless SOFT=1
  local name="$1"; shift
  local log="$RES/logs/$name.log"
  { echo "STEP $name"; echo "START $(stamp) host=$(hostname) load=$(cut -d' ' -f1-3 /proc/loadavg) cores=$(nproc)";
    echo "COMMIT $(git -C "$HERE" rev-parse HEAD)"
    echo "TREE_STATUS_PORCELAIN_LINES $(git -C "$HERE" status --porcelain | wc -l) (outside results: $(git -C "$HERE" status --porcelain -- . ':(exclude)results_p1b' ':(exclude)results_p1' | wc -l))"
    echo "CMD $(scrub "$*")"; } > "$log"
  ( ulimit -v "${MEM_LIMIT_KB:-6000000}"; exec nice -n 19 "$@" ) 2>&1 | sed "s|$HOME|~|g; s|$OUT|<OUT_DIR>|g" >> "$log"
  local rc=${PIPESTATUS[0]}
  { echo "END $(stamp) rc=$rc load=$(cut -d' ' -f1-3 /proc/loadavg)"; } >> "$log"
  if [ $rc -ne 0 ] && [ "$SOFT" != "1" ]; then echo "step $name FAILED rc=$rc (see $log)"; exit $rc; fi
  echo "step $name rc=$rc"
}
keepmeta() { cp "$@" "$RES/meta/"; }

stage_build() {   # builds only from a clean committed tree, so the commit in every step log is the commit of the binary
  local dirty; dirty="$(git -C "$HERE" status --porcelain -- . ':(exclude)results_p1b' ':(exclude)results_p1' | wc -l)"
  if [ "$dirty" != "0" ] && [ "${ALLOW_DIRTY_BUILD:-0}" != "1" ]; then
    echo "refusing to build: $dirty uncommitted file(s) outside the results directories (commit first; ALLOW_DIRTY_BUILD=1 overrides and must be disclosed)"; exit 3
  fi
  step build_configure cmake -S "$HERE/.." -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
  step build_make cmake --build "$BUILD" -j4 --target hitrate_probe p1b_probe
}

stage_check() {
  {
    echo "commit=$(git -C "$HERE" rev-parse HEAD)"
    echo "tree_dirty_files_outside_results=$(git -C "$HERE" status --porcelain -- . ':(exclude)results_p1b' ':(exclude)results_p1' | wc -l)"
    echo "host=$(hostname)"; echo "kernel=$(uname -r)"; echo "cores=$(nproc)"
    echo "cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
    echo "loadavg=$(cat /proc/loadavg)"; echo "mem_available_kb=$(grep MemAvailable /proc/meminfo | tr -s ' ' | cut -d' ' -f2)"
    echo "compiler=$(c++ --version | head -1)"; echo "cmake=$(cmake --version | head -1)"
    echo "python=$(python3 -I --version 2>&1)"; echo "numpy=$(python3 -I -c 'import numpy; print(numpy.__version__)')"
    echo "data_root=<DATA_ROOT>"; echo "out_dir=<OUT_DIR>"
  } > "$RES/run_manifest.txt"
}

stage_gates2d() {
  for s in mapping static_0 dynamics_0; do
    step "gate2d_$s" "$PROBE" gate2d --cache "$P1/lil/cache_$s.bin" --meta "$OUT/gate2d_$s.meta"
    keepmeta "$OUT/gate2d_$s.meta"
  done
}

stage_gatekth() {
  step gate_kth "$PROBE" gate-kth --dir "$DATA/kth/00" --gt "$DATA/kth/00/gt_cloud.pcd" --meta "$OUT/gate_kth.meta"
  keepmeta "$OUT/gate_kth.meta"
}

stage_lil15() {   # W = 15 on the LILocBench calibration sequences (run B time base and reference labels)
  for r in 0.05 0.10; do for s in mapping static_0 dynamics_0; do
    step "lil_measure_${s}_r${r}_W15" "$HPROBE" lil-measure --cache "$P1/lil/cache_$s.bin" --labels "$P1/lil/ref_r$r.bin" \
      --res "$r" --window 15 --out "$OUT/lil_cells_${s}_r${r}_W15.csv" --meta "$OUT/lil_cells_${s}_r${r}_W15.meta"
    keepmeta "$OUT/lil_cells_${s}_r${r}_W15.meta"
  done; done
}

stage_equiv() {   # lean probe vs the real engine on KITTI 00, today's miss: the cell tables must be byte-identical
  for r in 0.2 0.1; do
    step "equiv_kth_r$r" "$PROBE" kth-measure3 --dir "$DATA/kth/00" --labels "$P1/kth00/labels_r$r.bin" --res "$r" --windows 10,2 \
      --out-prefix "$OUT/kth_r$r"
    for w in 10 2; do
      keepmeta "$OUT/kth_r${r}_W${w}_today.meta" "$OUT/kth_r${r}_W${w}_refined.meta"
    done
  done
  {
    echo "# lean probe (today's miss) against the real engine cell tables of run B, KITTI 00"
    for r in 0.2 0.1; do for w in 10 2; do
      a="$OUT/kth_r${r}_W${w}_today.csv"; b="$P1/kth00/cells_r${r}_W$w.csv"
      if cmp -s "$a" "$b"; then v=IDENTICAL; else v=DIFFERENT; fi
      echo "r=$r W=$w $v sha256_lean=$(sha256sum "$a" | cut -c1-16) sha256_engine=$(sha256sum "$b" | cut -c1-16)"
    done; done
  } > "$RES/equivalence_kth.txt"
  cat "$RES/equivalence_kth.txt"
  grep -q DIFFERENT "$RES/equivalence_kth.txt" && { echo "EQUIVALENCE FAILED"; exit 4; }
  return 0
}

stage_tls() {
  step spires_tls "$PROBE" spires-tls --pcd "$KEBLE/ground_truth_map/keble-college/merged-cloud-1cm.pcd" \
    --gt "$SEQ/processed/trajectory/gt-tum.txt" --radius 30 --out-dir "$OUT" --meta "$OUT/spires_tls.meta"
  keepmeta "$OUT/spires_tls.meta"
}

stage_offset() {
  SOFT=1
  step spires_offset "$PROBE" spires-offset --zip "$SEQ/raw/lidar-clouds.zip" --gt "$SEQ/processed/trajectory/gt-tum.txt" \
    --tls-dir "$OUT" --csv "$OUT/spires_offset.csv" --meta "$OUT/spires_offset.meta"
  SOFT=0
  keepmeta "$OUT/spires_offset.meta" "$OUT/spires_offset.csv"
}

metaval() { grep "^$2=" "$1" | head -1 | cut -d= -f2; }

stage_convert() {
  local m="$OUT/spires_offset.meta"
  [ -f "$m" ] || { echo "no offset search result: run the offset stage first"; exit 2; }
  local dt; dt="$(metaval "$m" selected_dt_s)"
  step spires_convert "$PROBE" spires-convert --zip "$SEQ/raw/lidar-clouds.zip" --gt "$SEQ/processed/trajectory/gt-tum.txt" \
    --dt "$dt" --out "$OUT/cache_keble02.bin" --meta "$OUT/spires_convert.meta"
  keepmeta "$OUT/spires_convert.meta"
}

stage_gatespires() {
  step gate_spires "$PROBE" gate-spires --cache "$OUT/cache_keble02.bin" --meta "$OUT/gate_spires.meta"
  keepmeta "$OUT/gate_spires.meta"
}

stage_labels() {
  for r in 0.2 0.1; do
    step "spires_labels_r$r" "$PROBE" spires-labels --tls-dir "$OUT" --cache "$OUT/cache_keble02.bin" --res "$r" \
      --out "$OUT/labels_keble02_r$r.bin" --meta "$OUT/labels_keble02_r$r.meta"
    keepmeta "$OUT/labels_keble02_r$r.meta"
  done
}

stage_measure() {
  for r in 0.2 0.1; do
    step "spires_measure_r$r" "$PROBE" spires-measure --cache "$OUT/cache_keble02.bin" --labels "$OUT/labels_keble02_r$r.bin" --res "$r" \
      --windows 10,20 --out-prefix "$OUT/keble02_r$r"
    for w in 10 20; do for k in today refined; do keepmeta "$OUT/keble02_r${r}_W${w}_$k.meta"; done; done
  done
}

stage_statafter() {   # stat record of the test-split files after the work, and its comparison with the record before
  bash "$HERE/stat_test_split.sh" "$RES/test_split_stat_after.tsv"
  python3 -I "$HERE/compare_stat.py" --before "$RES/test_split_stat_before.tsv" "$RES/test_split_stat_before_boreas.tsv" \
    --after "$RES/test_split_stat_after.tsv" "$RES/test_split_stat_after_boreas.tsv" --out "$RES/test_split_stat_compare.md"
}

stage_analyze() {
  step analyze python3 -I "$HERE/analyze_p1b.py" --data-dir "$OUT" --old-meta-dir "$HERE/results_p1/meta" --meta-dir "$RES/meta" --out-dir "$RES"
}

stage_freeze() {
  step freeze python3 -I "$HERE/freeze_prereg.py" --results-dir "$RES" --out-dir "$HERE" --commit "$(git -C "$HERE" rev-parse --short HEAD)"
}

stage_report() {
  step report python3 -I "$HERE/make_report_p1b.py" --results-dir "$RES" --prereg-dir "$HERE" --out "$RES/P1B_REPORT.md"
}

stage_replay() {
  ADVICE_DIR="${ADVICE_DIR:-}" RESULTS_DIR="$RES" bash "$HERE/advice_replay/run_advice_replay.sh"
}

if [ $# -eq 0 ]; then echo "usage: $0 STAGE..."; exit 2; fi
for st in "$@"; do
  case "$st" in
    build) stage_build;; check) stage_check;; gates2d) stage_gates2d;; gatekth) stage_gatekth;; lil15) stage_lil15;;
    equiv|kthref) stage_equiv;; tls) stage_tls;; offset) stage_offset;; convert) stage_convert;; gatespires) stage_gatespires;;
    labels) stage_labels;; measure) stage_measure;; statafter) stage_statafter;; analyze) stage_analyze;; freeze) stage_freeze;;
    report) stage_report;; replay) stage_replay;;
    *) echo "unknown stage $st"; exit 2;;
  esac
done
