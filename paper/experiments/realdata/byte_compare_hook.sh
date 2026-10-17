#!/usr/bin/env bash
# usage: byte_compare.sh unset|noop
# Builds the experiment harness (E0-E3, E5, E6) with -j1, runs it niced in a scratch
# directory and compares every seeded CSV with the committed results byte for byte.
set -uo pipefail
variant="$1"
W=/home/cona/kangj/strata/.claude/worktrees/v2-sprt
T=/home/cona/.claude/jobs/c7546c85/tmp/v2p1
B=$T/b_exp_$variant
R=$T/run_$variant
flags=()
if [ "$variant" = noop ]; then flags=(-DCMAKE_CXX_FLAGS=-DSTRATA_TEST_NOOP_OBSERVER); fi
echo "variant=$variant start $(date -Is) load $(cat /proc/loadavg)"
nice -n 19 cmake -S "$W/paper/experiments" -B "$B" -DCMAKE_BUILD_TYPE=Release "${flags[@]}" > "$B.cmake.log" 2>&1 || { cat "$B.cmake.log"; exit 1; }
nice -n 19 cmake --build "$B" -j1 --target e0_calibration e1_static_quality e2_periodicity e3_sensitivity e5_trajectory e6_default_confusion > "$B.build.log" 2>&1 || { tail -30 "$B.build.log"; exit 1; }
echo "built $(date -Is)"
rm -rf "$R"; mkdir -p "$R/results"; cd "$R"
for e in e0_calibration e5_trajectory e1_static_quality e2_periodicity e3_sensitivity e6_default_confusion; do
  s=$(date +%s)
  nice -n 19 "$B/$e" > "$R/$e.stdout" 2> "$R/$e.stderr"; rc=$?
  echo "$e rc=$rc $(( $(date +%s) - s )) s load $(cat /proc/loadavg)"
  if [ -s "$R/$e.stderr" ]; then tail -2 "$R/$e.stderr"; fi
done
bad=0; n=0
for f in "$R"/results/*.csv; do
  n=$((n+1))
  if ! cmp -s "$f" "$W/paper/experiments/results/$(basename "$f")"; then echo "DIFFERS $(basename "$f")"; bad=$((bad+1)); fi
done
echo "variant=$variant compared $n csv files, $bad differ"
ls "$R"/results | wc -l
HASH="$(ls "$R"/results/*.csv | LC_ALL=C sort | xargs cat | sha256sum | cut -d' ' -f1)"
echo "sha256 of all seeded csv (name order): $HASH"
echo "end $(date -Is)"
