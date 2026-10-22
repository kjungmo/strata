#!/usr/bin/env bash
# Re-runs the three advisory replay scripts on the committed run B cell tables (calibration split only) and compares
# their output with the output stored by the advisor. Output: results_p1b/advice_replay/. One process at a time.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTD="${RESULTS_DIR:-$HERE/../results_p1b}/advice_replay"
ADVICE_DIR="${ADVICE_DIR:-}"      # directory with the advisor's replay_out.txt, replay2_out.txt, replay3_out.txt (optional)
mkdir -p "$OUTD"
{ echo "START $(date -Is) host=$(hostname) load=$(cut -d' ' -f1-3 /proc/loadavg) commit=$(git -C "$HERE" rev-parse HEAD)"; } > "$OUTD/run.log"
for pair in "sprt_replay.py:replay_out.txt" "sprt_replay2.py:replay2_out.txt" "sprt_replay3.py:replay3_out.txt"; do
  script="${pair%%:*}"; out="${pair##*:}"
  nice -n 19 python3 -I "$HERE/$script" > "$OUTD/$out" 2> "$OUTD/${out%.txt}.err"
  echo "$script rc=$? $(date -Is)" >> "$OUTD/run.log"
  if [ -n "$ADVICE_DIR" ] && [ -f "$ADVICE_DIR/$out" ]; then
    if diff -q "$ADVICE_DIR/$out" "$OUTD/$out" > /dev/null; then echo "$out IDENTICAL to the advisor's output" >> "$OUTD/run.log"
    else diff "$ADVICE_DIR/$out" "$OUTD/$out" > "$OUTD/${out%.txt}.diff"; echo "$out DIFFERS (see ${out%.txt}.diff)" >> "$OUTD/run.log"; fi
  fi
done
echo "END $(date -Is)" >> "$OUTD/run.log"
