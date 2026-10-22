#!/usr/bin/env bash
# EXPLORATORY and post hoc: why do the LILocBench sequences fail G1? Runs the same G1/G2 code on a subset of steps or one laser.
# These runs set nothing: the pre-registered gate is `reproduce_p1b.sh gates2d`. Output: results_p1b/exploratory/.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA="${DATA_ROOT:-$HOME/datasets/strata}"
BUILD="${BUILD_DIR:-/tmp/strata_p1b_build}"
RES="${RESULTS_DIR:-$HERE/results_p1b}/exploratory"
mkdir -p "$RES"
PROBE="$BUILD/realdata/p1b_probe"
for s in static_0 mapping; do
  for variant in "front:0" "rear:0" "both:900" "both:300"; do
    laser="${variant%%:*}"; n="${variant##*:}"
    name="gate2d_${s}_${laser}_steps${n}"
    { echo "START $(date -Is) load=$(cut -d' ' -f1-3 /proc/loadavg)"; } > "$RES/$name.log"
    nice -n 19 "$PROBE" gate2d --cache "$DATA/p1/lil/cache_$s.bin" --laser "$laser" --max-steps "$n" --meta "$RES/$name.meta" >> "$RES/$name.log" 2>&1
    echo "END $(date -Is) rc=$?" >> "$RES/$name.log"
  done
done
