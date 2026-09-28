#!/usr/bin/env bash
# Build and run the full strata_core evaluation harness (E0 calibration, E1-E4).
# Reproduction: bash paper/experiments/run_all.sh  (from the worktree root)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"

cmake -S "${HERE}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DSTRATA_CORE_BUILD_TESTS=ON
cmake --build "${BUILD}" -j
( cd "${BUILD}/strata_core_build" && ctest --output-on-failure )

mkdir -p "${HERE}/results"
cd "${HERE}"   # executables write CSVs to ./results
"${BUILD}/e0_calibration"      # picks periodic_false_alarm on held-out seeds
"${BUILD}/e1_static_quality"
"${BUILD}/e2_periodicity"
"${BUILD}/e3_sensitivity"
"${BUILD}/e4_throughput"

echo "---- CSV outputs ----"
ls -l "${HERE}/results"
