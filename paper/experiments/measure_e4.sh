#!/usr/bin/env bash
# One E4 timing recording on a quiet host (the only wall-clock experiment of the paper).
#
#   bash paper/experiments/measure_e4.sh            # one recording, about one minute
#
# Run it at least three times, as separate invocations; the paper reports the range
# over the valid recordings (paper/arxiv_ref/tools/make_artifacts.py reads them).
#
# Rules, fixed before any recording was taken:
#  * Admission: the 1-minute load average must be below MAX_LOAD_BEFORE (0.50) when
#    the script starts. The benchmark is one thread; 0.50 means that on average less
#    than half of one core is busy with anything else, so the pinned core, the shared
#    cache and the memory bus are essentially free. Recordings taken at load 7 to 15
#    gave backend ratios between 10x and 28x at one extent, which is why a threshold
#    is needed at all.
#  * The script refuses to start above the threshold. STRATA_E4_FORCE=1 overrides the
#    refusal; such a recording is written with "valid: no" and is reported, not used.
#  * After the run the 1-minute load average must be below MAX_LOAD_AFTER (1.50: the
#    benchmark itself contributes about 1.0). Above it the recording is "valid: no".
#  * The benchmark is pinned to one core with taskset (STRATA_E4_CORE, default the
#    highest-numbered core). One warm-up repeat per configuration is discarded by the
#    harness; the 15 measured repeats are all written to timings.csv.
#  * /proc/loadavg, nproc and the five largest CPU consumers are recorded before and
#    after. Nothing is deleted: invalid recordings stay in results/e4_recordings/.
#
# Output: results/e4_recordings/<UTC time>/{summary.csv,timings.csv,env.txt}
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"
MAX_LOAD_BEFORE=0.50
MAX_LOAD_AFTER=1.50
CORE="${STRATA_E4_CORE:-$(( $(nproc) - 1 ))}"

load1() { cut -d' ' -f1 /proc/loadavg; }
below() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a < b) }'; }
top5() { ps -eo pcpu,comm --sort=-pcpu | sed -n '2,6p' | awk '{printf "%s%s %s", (NR>1 ? "; " : ""), $1, $2} END {print ""}'; }

if [ -n "$(git -C "${HERE}" status --porcelain -- :/strata_core :/paper/experiments/src)" ]; then
  echo "strata_core/ or the harness source differs from the commit; commit or restore first" >&2
  exit 2
fi

# Build before the admission check, single-threaded, so the build is not measured.
cmake -S "${HERE}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "${BUILD}" --target e4_throughput -j 1 > /dev/null

BEFORE="$(cat /proc/loadavg)"
VALID=yes
REASON=""
if ! below "$(load1)" "${MAX_LOAD_BEFORE}"; then
  if [ "${STRATA_E4_FORCE:-0}" != "1" ]; then
    echo "1-minute load average $(load1) is not below ${MAX_LOAD_BEFORE}; not started." >&2
    echo "largest CPU consumers: $(top5)" >&2
    exit 3
  fi
  VALID=no
  REASON="load before ${BEFORE%% *} not below ${MAX_LOAD_BEFORE} (forced)"
fi
TOP_BEFORE="$(top5)"

OUT="${HERE}/results/e4_recordings/$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "${OUT}"
START=$(date +%s)
taskset -c "${CORE}" "${BUILD}/e4_throughput" "${OUT}/summary.csv" "${OUT}/timings.csv" > /dev/null
END=$(date +%s)
AFTER="$(cat /proc/loadavg)"
TOP_AFTER="$(top5)"
if ! below "$(load1)" "${MAX_LOAD_AFTER}"; then
  VALID=no
  REASON="${REASON:+${REASON}; }load after ${AFTER%% *} not below ${MAX_LOAD_AFTER}"
fi

CXX_BIN="$(grep -m1 '^CMAKE_CXX_COMPILER:' "${BUILD}/CMakeCache.txt" | sed 's/.*=//')"
{
  echo "valid: ${VALID}"
  echo "invalid_reason: ${REASON:-none}"
  echo "recorded_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "seconds: $(( END - START ))"
  echo "max_load_before: ${MAX_LOAD_BEFORE}"
  echo "max_load_after: ${MAX_LOAD_AFTER}"
  echo "loadavg_before: ${BEFORE}"
  echo "loadavg_after: ${AFTER}"
  echo "top_cpu_before: ${TOP_BEFORE}"
  echo "top_cpu_after: ${TOP_AFTER}"
  echo "logical_cpus: $(nproc)"
  echo "pinned_core: ${CORE}"
  echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | sed 's/.*: //')"
  echo "kernel: $(uname -srm)"
  echo "compiler: $("${CXX_BIN}" --version | head -1)"
  echo "build_type: $(grep -m1 '^CMAKE_BUILD_TYPE:' "${BUILD}/CMakeCache.txt" | sed 's/.*=//')"
  echo "cxx_flags_release: $(grep -m1 '^CMAKE_CXX_FLAGS_RELEASE:' "${BUILD}/CMakeCache.txt" | sed 's/.*=//')"
  echo "strata_core_tree: $(git -C "${HERE}" rev-parse HEAD:strata_core)"
  echo "harness_e4_blob: $(git -C "${HERE}" rev-parse HEAD:paper/experiments/src/e4_throughput.cpp)"
} > "${OUT}/env.txt"
cat "${OUT}/env.txt"
python3 "${HERE}/check_e4_local.py" "${OUT}/summary.csv"
echo "recording written to ${OUT} (valid: ${VALID})"
