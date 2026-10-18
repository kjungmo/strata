#!/usr/bin/env bash
# Tests the clean-tree rule of reproduce_p1b.sh (Amendment 4 item 2, review finding 2): the check covers the whole repository,
# because p1b_probe and hitrate_probe link strata_core (../../strata_core), not only paper/experiments/realdata.
# A temporary git repository with the same layout is used; the real repository is untouched.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
R="$T/repo"; RD="$R/paper/experiments/realdata"
mkdir -p "$RD/results_p1b" "$RD/results_p1" "$R/strata_core"
cp "$HERE/reproduce_p1b.sh" "$RD/"
echo "int core = 1;" > "$R/strata_core/core.cpp"
echo "# experiments" > "$R/paper/experiments/CMakeLists.txt"
echo "# realdata" > "$RD/CMakeLists.txt"
echo "x" > "$RD/results_p1b/keep.txt"; echo "x" > "$RD/results_p1/keep.txt"
git -C "$R" init -q
git -C "$R" add -A
git -C "$R" -c user.name=t -c user.email=t@example.com commit -q -m init

run_check() { RESULTS_DIR="$RD/results_p1b" BUILD_DIR="$T/build" OUT_DIR="$T/out" DATA_ROOT="$T/data" bash "$RD/reproduce_p1b.sh" check >/dev/null
              grep '^tree_dirty_files_outside_results=' "$RD/results_p1b/run_manifest.txt" | cut -d= -f2; }
expect() { [ "$1" = "$2" ] || { echo "FAIL: $3 (expected $2, got $1)"; exit 1; }; }

expect "$(run_check)" 0 "clean tree"

# files in the two result directories do not count, tracked or not
echo "y" >> "$RD/results_p1b/keep.txt"; echo "new" > "$RD/results_p1/new.txt"; echo "new" > "$RD/results_p1b/new.txt"
expect "$(run_check)" 0 "changes in the result directories only"
git -C "$R" checkout -q -- . ; rm -f "$RD/results_p1/new.txt" "$RD/results_p1b/new.txt"

# a dirty file in strata_core (outside paper/experiments/realdata) counts, and build refuses
echo "int core = 2;" > "$R/strata_core/core.cpp"
expect "$(run_check)" 1 "dirty strata_core file"
set +e
out="$(RESULTS_DIR="$RD/results_p1b" BUILD_DIR="$T/build" OUT_DIR="$T/out" DATA_ROOT="$T/data" bash "$RD/reproduce_p1b.sh" build 2>&1)"; rc=$?
set -e
expect "$rc" 3 "build must refuse with a dirty strata_core file"
case "$out" in *refusing*) ;; *) echo "FAIL: no refusal message: $out"; exit 1;; esac
git -C "$R" checkout -q -- .

# a dirty paper/experiments/CMakeLists.txt counts too
echo "# edited" >> "$R/paper/experiments/CMakeLists.txt"
expect "$(run_check)" 1 "dirty paper/experiments/CMakeLists.txt"
git -C "$R" checkout -q -- .

# an untracked new source file counts
echo "int extra;" > "$R/strata_core/extra.cpp"
expect "$(run_check)" 1 "untracked file in strata_core"
rm "$R/strata_core/extra.cpp"

# the override is explicit
echo "int core = 3;" > "$R/strata_core/core.cpp"
set +e
out="$(ALLOW_DIRTY_BUILD=1 RESULTS_DIR="$RD/results_p1b" BUILD_DIR="$T/build" OUT_DIR="$T/out" DATA_ROOT="$T/data" bash "$RD/reproduce_p1b.sh" build 2>&1)"; rc=$?
set -e
case "$out" in *refusing*) echo "FAIL: ALLOW_DIRTY_BUILD=1 must pass the check"; exit 1;; esac

# the step log carries the whole-repository count
git -C "$R" checkout -q -- .
RESULTS_DIR="$RD/results_p1b" BUILD_DIR="$T/build" OUT_DIR="$T/out" DATA_ROOT="$T/data" bash "$RD/reproduce_p1b.sh" check >/dev/null
echo "int core = 4;" > "$R/strata_core/core.cpp"
SCRIPT_STEP_LOG="$RD/results_p1b/logs/build_configure.log"
RESULTS_DIR="$RD/results_p1b" BUILD_DIR="$T/build" OUT_DIR="$T/out" DATA_ROOT="$T/data" ALLOW_DIRTY_BUILD=1 bash "$RD/reproduce_p1b.sh" build >/dev/null 2>&1 || true
grep -q 'whole repository outside the result directories: 1' "$SCRIPT_STEP_LOG" || { echo "FAIL: step log lacks the whole-repository count"; cat "$SCRIPT_STEP_LOG"; exit 1; }
echo "reproduce_p1b tree check tests ok"
