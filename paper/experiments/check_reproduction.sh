#!/usr/bin/env bash
# Rebuild and rerun the whole harness, then check that every committed seeded result
# file under results/ is unchanged (E4's timings go to build/, see run_all.sh).
# Prints one REPRODUCED line naming the compiler, or fails and lists what changed.
#
#   bash paper/experiments/check_reproduction.sh            # from a clean checkout
#   bash paper/experiments/check_reproduction.sh >> paper/experiments/reproduction_log.txt
#
# The line is only meaningful from a clean checkout: the script refuses to run when
# results/, the harness or one of the pinned code directories differs from the commit.
# STRATA_SKIP_E4=1 and STRATA_BUILD_JOBS are passed on to run_all.sh.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${HERE}"

dirty="$(git status --porcelain -- :/strata_core :/strata :/scripts :/paper/experiments/src :/paper/experiments/results \
          :/paper/experiments/CMakeLists.txt :/paper/experiments/run_all.sh)"
if [ -n "${dirty}" ]; then
  echo "the working tree differs from the commit; start from a clean checkout:" >&2
  echo "${dirty}" >&2
  exit 2
fi
rm -rf build
LOG="$(mktemp)"
bash run_all.sh > "${LOG}" 2>&1 || { tail -30 "${LOG}" >&2; rm -f "${LOG}"; exit 1; }
mv "${LOG}" build/run_all.log

CXX_BIN="$(grep -m1 '^CMAKE_CXX_COMPILER:' build/CMakeCache.txt | sed 's/.*=//')"
changed="$(git status --porcelain -- results)"
if [ -n "${changed}" ]; then
  echo "NOT REPRODUCED with $("${CXX_BIN}" --version | head -1):" >&2
  echo "${changed}" >&2
  exit 1
fi
# Hash of the seeded result files (every results/*.csv except E4's), in name order.
HASH="$(ls results/*.csv | grep -v '/e4_throughput.csv$' | LC_ALL=C sort | xargs cat | sha256sum | cut -d' ' -f1)"
E4NOTE="E4 cell counts equal"
[ "${STRATA_SKIP_E4:-0}" = "1" ] && E4NOTE="E4 not run"
echo "REPRODUCED $(date -u +%Y-%m-%d) | $("${CXX_BIN}" --version | head -1) | $(uname -sm) | strata_core tree $(git rev-parse HEAD:strata_core) | seeded results sha256 ${HASH}: E0-E3, E5 and E6 files byte-identical, ${E4NOTE}"
