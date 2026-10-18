#!/usr/bin/env bash
# Legacy bit-equivalence (strata_core 0.2.0, P2 acceptance test).
#
# evidence_model = legacy must reproduce the v0.1 arithmetic bit for bit. The seeded
# experiments E0-E3, E5 and E6 state their parameters explicitly with
# `evidence_model = EvidenceModel::Legacy`; this script runs their executables from a
# scratch directory and compares every CSV they write with the committed file under
# results/, byte for byte. Any difference, a missing output or an extra output fails.
#
#   bash paper/experiments/check_legacy_equivalence.sh [BUILD_DIR]
#
# BUILD_DIR holds the built executables (default: paper/experiments/build, as made by
# run_all.sh). E4 reads the wall clock and E7 is new in 0.2.0; neither is part of the comparison.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${1:-${HERE}/build}"
EXPS="e0_calibration e5_trajectory e1_static_quality e2_periodicity e3_sensitivity e6_default_confusion"

for e in ${EXPS}; do
  [ -x "${BUILD}/${e}" ] || { echo "FAIL: ${BUILD}/${e} is not built" >&2; exit 2; }
done
SCR="$(mktemp -d)"
trap 'rm -rf "${SCR}"' EXIT
mkdir "${SCR}/results"
cd "${SCR}"                               # the executables write ./results/*.csv
for e in ${EXPS}; do "${BUILD}/${e}" > "${SCR}/${e}.log" 2>&1 || { echo "FAIL: ${e} exited non-zero" >&2; tail -5 "${SCR}/${e}.log" >&2; exit 1; }; done

n=0; bad=0
for f in "${HERE}"/results/*.csv; do
  b="$(basename "${f}")"
  case "${b}" in e4_throughput.csv|e7_*.csv) continue ;; esac   # E4: wall clock; E7: new in 0.2.0, no legacy counterpart
  if [ ! -f "${SCR}/results/${b}" ]; then echo "FAIL: ${b} was not written" >&2; bad=$((bad + 1)); continue; fi
  if cmp -s "${f}" "${SCR}/results/${b}"; then n=$((n + 1)); else echo "FAIL: ${b} differs from the committed file" >&2; bad=$((bad + 1)); fi
done
for g in "${SCR}"/results/*.csv; do
  b="$(basename "${g}")"
  [ -f "${HERE}/results/${b}" ] || { echo "FAIL: ${b} written but not committed" >&2; bad=$((bad + 1)); }
done
[ "${bad}" -eq 0 ] || { echo "legacy equivalence FAILED: ${bad} problem(s), ${n} identical" >&2; exit 1; }
echo "legacy equivalence OK: ${n} CSV files byte-identical to the committed results (E0-E3, E5, E6)"
