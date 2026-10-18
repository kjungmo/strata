#!/usr/bin/env bash
# Runs one command under a file-open trace and appends every data file it opened to the access log
# (PREREG_P1B Amendment 3 item 1: the trace, not the stat comparison, is the evidence of which test-split files were opened).
#
# usage: p3_traced.sh STEP cmd [args...]
#   STEP                a short name; the trace goes to <RESULTS>/access/STEP.strace
#   env P3_RESULTS_DIR  default <this dir>/results_p3
#       P3_DATA_PATTERN  regular expression (grep -E) for paths that count as data (default: datasets/strata)
#       P3_ALLOW_DIRTY   1 = run although the experiment directory has uncommitted files (must be disclosed in the report)
# The access log is <RESULTS>/access_log.tsv: time, step, commit, uncommitted files, path, number of open calls.
# A command runs only from committed scripts: the experiment directory (this one, minus the results_* directories, which hold outputs and not code) must be clean. Other agents edit
# strata_core in the same worktree; that cannot change a script, and binaries are built from a `git archive` copy of a commit by
# reproduce_p3.sh (stage build), whose step log records the commit. So the check here covers this directory only.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RES="${P3_RESULTS_DIR:-$HERE/results_p3}"
PATTERN="${P3_DATA_PATTERN:-datasets/strata}"
if [ $# -lt 2 ]; then echo "usage: p3_traced.sh STEP cmd [args...]" >&2; exit 2; fi
step="$1"; shift
mkdir -p "$RES/access"
dirty="$(git -C "$HERE" status --porcelain -- . ':(exclude)results_*' | wc -l)"
if [ "$dirty" != "0" ] && [ "${P3_ALLOW_DIRTY:-0}" != "1" ]; then
  echo "refusing to run '$step': $dirty uncommitted file(s) in the experiment directory outside results_p3 (commit first; P3_ALLOW_DIRTY=1 overrides and must be disclosed)" >&2
  exit 3
fi
commit="$(git -C "$HERE" rev-parse HEAD)"
trace="$RES/access/$step.strace"
start="$(date -Is)"
strace -f -e trace=openat,open -o "$trace" "$@"
rc=$?
log="$RES/access_log.tsv"
if [ ! -f "$log" ]; then printf 'time\tstep\tcommit\tuncommitted_files\tpath\topen_calls\n' > "$log"; fi
# successful opens only (a failed open ends in "= -1"); the path is the first quoted string of the call
grep -E "$PATTERN" "$trace" | grep -v -E '= -1 ' | grep -o -E '"[^"]+"' | tr -d '"' | grep -E "$PATTERN" | sort | uniq -c |
  while read -r n p; do printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$start" "$step" "$commit" "$dirty" "$p" "$n" >> "$log"; done
echo "step $step rc=$rc trace=$trace"
exit $rc
