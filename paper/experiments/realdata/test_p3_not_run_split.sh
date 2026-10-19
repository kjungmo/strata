#!/usr/bin/env bash
# Test (Amendment 12): a skipped Spires 0.1 m step that is itself in failed_steps.tsv goes to skipped_repeat.tsv, not to not_run_steps.tsv;
# a step that never ran goes to not_run_steps.tsv. The block under test is cut out of reproduce_p3.sh.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
RES="$T"; PER=1; r=0.1; s=03; stamp() { echo T0; }
printf 'x\tspires_03_sprt_r0.1_W10_p1\trc=1\n' > "$RES/failed_steps.tsv"
blk="$(awk '/^            # Amendment 12: a step that is itself/,/^            continue$/' "$HERE/reproduce_p3.sh")"
[ -n "$blk" ] || { echo "FAIL: block not found"; exit 1; }
check() { arm=$1; w=$2; eval "f() { for _ in 1; do $blk; done; }"; f; }
check sprt 10; check sprt 20
n1="$(wc -l < "$RES/skipped_repeat.tsv")"; n2="$(wc -l < "$RES/not_run_steps.tsv")"
if grep -q 'sprt_r0.1_W10_p1' "$RES/skipped_repeat.tsv" && grep -q 'sprt_r0.1_W20_p1' "$RES/not_run_steps.tsv" && [ "$n1" = 1 ] && [ "$n2" = 1 ] && ! grep -q W10 "$RES/not_run_steps.tsv"; then
  echo "OK: failed step -> skipped_repeat.tsv, never-run step -> not_run_steps.tsv"
else
  echo "FAIL ($n1,$n2)"; exit 1
fi
