#!/usr/bin/env bash
# Replay of the advisory table through the real engine (phase 2, rail R1).
#
#   bash paper/experiments/realdata/run_replay_p2.sh
#
# 1. checks the sha256 of the cell tables it reads (replay_inputs.sha256: phase 1 run B; replay_inputs_p1b.sha256:
#    the W = 15 tables of phase 1b; calibration split only),
# 2. builds strata_core with its tests (build tree outside the repository, default /tmp/strata_p2_core_build),
# 3. runs the ReplayP1 gtests, which write results_p2/replay_advisory_table.txt, replay_kitti.txt and replay_shipped_pair_W15.txt,
# 4. writes results_p2/replay_run.log (commit, host, load, input check, test summary).
# Skips cleanly (exit 0, nothing written) when the cell tables are absent: they are derived from
# licensed data and never enter the repository.
# Environment: STRATA_P1_DIR (default ~/datasets/strata/p1), STRATA_P1B_DIR (default /mnt/d/wsl/strata/p1b;
# without it the W = 15 test is skipped and said so), BUILD_DIR, JOBS (default 4).
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${HERE}/../../.." && pwd)"
P1="${STRATA_P1_DIR:-$HOME/datasets/strata/p1}"
P1B="${STRATA_P1B_DIR:-/mnt/d/wsl/strata/p1b}"
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
PART_C=1
if [ -d "${P1B}" ]; then
  if ! ( cd "${P1B}" && sha256sum --quiet -c "${HERE}/results_p2/replay_inputs_p1b.sha256" ) >> "${LOG}" 2>&1; then
    echo "FAIL: an input differs from replay_inputs_p1b.sha256 (see ${LOG})"; exit 1
  fi
  echo "INPUTS_OK $(wc -l < "${HERE}/results_p2/replay_inputs_p1b.sha256") files match replay_inputs_p1b.sha256" >> "${LOG}"
else
  PART_C=0; echo "NOTE no ${P1B}: the W = 15 test is skipped" >> "${LOG}"
fi
cmake -S "${REPO}/strata_core" -B "${BUILD}" -DSTRATA_CORE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release > "${BUILD}.cmake.log" 2>&1 \
  || { echo "FAIL: cmake (see ${BUILD}.cmake.log)"; exit 1; }
cmake --build "${BUILD}" -j"${JOBS:-4}" > "${BUILD}.build.log" 2>&1 || { echo "FAIL: build (see ${BUILD}.build.log)"; exit 1; }
STRATA_P1_DIR="${P1}" STRATA_P1B_DIR="${P1B}" STRATA_REPLAY_OUT="${OUT}/replay_advisory_table.txt" \
  STRATA_REPLAY_W15_OUT="${OUT}/replay_shipped_pair_W15.txt" STRATA_REPLAY_KITTI_OUT="${OUT}/replay_kitti.txt" \
  "${BUILD}/strata_core_tests" --gtest_filter='ReplayP1.*' > "${OUT}/replay_gtest.txt" 2>&1
rc=$?
{ tail -4 "${OUT}/replay_gtest.txt"; echo "gtest rc=${rc}"; echo "END $(date -Is)"; } >> "${LOG}"
if [ "${PART_C}" = 1 ] && grep -q SKIPPED "${OUT}/replay_gtest.txt"; then echo "FAIL: a replay test was skipped"; exit 1; fi
if [ "${PART_C}" = 0 ] && [ "$(grep -c '^\[  SKIPPED \] ReplayP1' "${OUT}/replay_gtest.txt")" -gt 1 ]; then echo "FAIL: more than the W = 15 test was skipped"; exit 1; fi
[ "${rc}" -eq 0 ] && echo "OK: advisory table reproduced; see ${OUT}/replay_advisory_table.txt" || echo "FAIL: replay tests (see ${OUT}/replay_gtest.txt)"
exit "${rc}"
