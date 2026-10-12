#!/usr/bin/env bash
# Rebuild and rerun the whole harness, then check that every committed result file
# under results/ is unchanged (E4's timings go to build/, see run_all.sh).
# Prints one REPRODUCED line naming the compiler, or fails and lists what changed.
#
#   bash paper/experiments/check_reproduction.sh            # from a clean checkout
#   bash paper/experiments/check_reproduction.sh >> paper/experiments/reproduction_log.txt
#
# The line is only meaningful from a clean checkout: the script refuses to run when
# results/ already differs from the commit.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${HERE}"

if [ -n "$(git status --porcelain -- results)" ]; then
  echo "results/ already differs from the commit; start from a clean checkout" >&2
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
echo "REPRODUCED $(date -u +%Y-%m-%d) | $("${CXX_BIN}" --version | head -1) | $(uname -sm) | strata_core tree $(git rev-parse HEAD:strata_core) | results tree $(git rev-parse HEAD:paper/experiments/results): E0-E3 and E5 files byte-identical, E4 cell counts equal"
