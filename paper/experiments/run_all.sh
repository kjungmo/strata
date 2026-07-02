#!/usr/bin/env bash
# Build and run the full strata_core evaluation harness (E1-E4).
# Reproduction: bash paper/experiments/run_all.sh  (from the worktree root)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"

cmake -S "${HERE}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD}" -j

mkdir -p "${HERE}/results"
cd "${HERE}"   # executables write CSVs to ./results
"${BUILD}/e1_static_quality"
"${BUILD}/e2_periodicity"
"${BUILD}/e3_sensitivity"
"${BUILD}/e4_throughput"

echo "---- CSV outputs ----"
ls -l "${HERE}/results"
