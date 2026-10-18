#!/usr/bin/env bash
# Replay of the advisory table through the real engine (phase 2, rail R1).
#
#   bash paper/experiments/realdata/run_replay_p2.sh
#
# 1. checks the sha256 of the phase 1 cell tables it reads (replay_inputs.sha256; calibration split only),
# 2. builds strata_core with its tests (build tree outside the repository, default /tmp/strata_p2_core_build),
# 3. runs the ReplayP1 gtests, which write results_p2/replay_advisory_table.txt,
# 4. writes results_p2/replay_run.log (commit, host, load, input check, test summary).
# Skips cleanly (exit 0, nothing written) when the cell tables are absent: they are derived from
# licensed data and never enter the repository.
# Environment: STRATA_P1_DIR (default ~/datasets/strata/p1), BUILD_DIR, JOBS (default 4).
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${HERE}/../../.." && pwd)"
P1="${STRATA_P1_DIR:-$HOME/datasets/strata/p1}"
BUILD="${BUILD_DIR:-/tmp/strata_p2_core_build}"
OUT="${HERE}/results_p2"
if [ ! -d "${P1}/lil" ] || [ ! -d "${P1}/kth00" ]; then echo "SKIP: no phase 1 cell tables under ${P1}"; exit 0; fi
mkdir -p "${OUT}"
LOG="${OUT}/replay_run.log"
{
  echo "START $(date -Is) host=$(hostname) load=$(cut -d' ' -f1-3 /proc/loadavg) commit=$(cd "${REPO}" && git rev-parse HEAD)"
  echo "TREE_STATUS $(cd "${REPO}" && git status --porcelain -- strata_core paper/experiments/CMakeLists.txt | wc -l) uncommitted file(s) under strata_core"
} > "${LOG}"
if ! ( cd "${P1}" && sha256sum --quiet -c "${HERE}/results_p2/replay_inputs.sha256" ) >> "${LOG}" 2>&1; then
  echo "FAIL: an input differs from replay_inputs.sha256 (see ${LOG})"; exit 1
fi
echo "INPUTS_OK $(wc -l < "${HERE}/results_p2/replay_inputs.sha256") files match replay_inputs.sha256" >> "${LOG}"
cmake -S "${REPO}/strata_core" -B "${BUILD}" -DSTRATA_CORE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release > "${BUILD}.cmake.log" 2>&1 \
  || { echo "FAIL: cmake (see ${BUILD}.cmake.log)"; exit 1; }
cmake --build "${BUILD}" -j"${JOBS:-4}" > "${BUILD}.build.log" 2>&1 || { echo "FAIL: build (see ${BUILD}.build.log)"; exit 1; }
STRATA_P1_DIR="${P1}" STRATA_REPLAY_OUT="${OUT}/replay_advisory_table.txt" \
  "${BUILD}/strata_core_tests" --gtest_filter='ReplayP1.*' > "${OUT}/replay_gtest.txt" 2>&1
rc=$?
{ tail -4 "${OUT}/replay_gtest.txt"; echo "gtest rc=${rc}"; echo "END $(date -Is)"; } >> "${LOG}"
if grep -q SKIPPED "${OUT}/replay_gtest.txt"; then echo "FAIL: a replay test was skipped"; exit 1; fi
[ "${rc}" -eq 0 ] && echo "OK: advisory table reproduced; see ${OUT}/replay_advisory_table.txt" || echo "FAIL: replay tests (see ${OUT}/replay_gtest.txt)"
exit "${rc}"
