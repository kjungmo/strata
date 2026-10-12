#!/usr/bin/env bash
# Build and run the full strata_core evaluation harness (E0/E5 calibration, E1-E5).
# Reproduction: bash paper/experiments/run_all.sh  (from the repository root)
#
# E0-E3 and E5 are seeded and rewrite results/*.csv byte for byte. E4 reads the wall
# clock, so its timing columns differ on every run and machine: by default it writes
# build/e4_throughput_local.csv, and check_e4_local.py compares the deterministic
# columns (cell counts, byte estimates) with the committed results/e4_throughput.csv
# and prints both sets of timings. To replace the committed reference timings, run
#   STRATA_E4_RECORD=1 bash paper/experiments/run_all.sh
# on an otherwise idle machine; it also writes results/e4_environment.txt.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"

cmake -S "${HERE}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DSTRATA_CORE_BUILD_TESTS=ON
cmake --build "${BUILD}" -j
( cd "${BUILD}/strata_core_build" && ctest --output-on-failure )

mkdir -p "${HERE}/results"
cd "${HERE}"   # executables write CSVs to ./results
"${BUILD}/e0_calibration"      # single-read-out level on held-out seeds
"${BUILD}/e5_trajectory"       # trajectory level: picks the shipped spent level, evaluates it
"${BUILD}/e1_static_quality"
"${BUILD}/e2_periodicity"
"${BUILD}/e3_sensitivity"

if [ "${STRATA_E4_RECORD:-0}" = "1" ]; then
  "${BUILD}/e4_throughput" results/e4_throughput.csv
  CXX_BIN="$(grep -m1 '^CMAKE_CXX_COMPILER:' "${BUILD}/CMakeCache.txt" | sed 's/.*=//')"
  {
    echo "recorded_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "commit: $(git -C "${HERE}" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "strata_core_tree: $(git -C "${HERE}" rev-parse HEAD:strata_core 2>/dev/null || echo unknown)"
    echo "harness_e4_blob: $(git -C "${HERE}" hash-object src/e4_throughput.cpp 2>/dev/null || echo unknown)"
    echo "kernel: $(uname -srm)"
    echo "cpu: $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | sed 's/.*: //' || echo unknown)"
    echo "logical_cpus: $(nproc 2>/dev/null || echo unknown)"
    echo "load_average_at_end: $(cut -d' ' -f1-3 /proc/loadavg 2>/dev/null || echo unknown)"
    echo "compiler: ${CXX_BIN} ($("${CXX_BIN}" --version | head -1))"
    echo "build_type: $(grep -m1 '^CMAKE_BUILD_TYPE:' "${BUILD}/CMakeCache.txt" | sed 's/.*=//')"
    echo "cxx_flags_release: $(grep -m1 '^CMAKE_CXX_FLAGS_RELEASE:' "${BUILD}/CMakeCache.txt" | sed 's/.*=//')"
  } > results/e4_environment.txt
  cat results/e4_environment.txt
else
  "${BUILD}/e4_throughput" "${BUILD}/e4_throughput_local.csv"
  python3 "${HERE}/check_e4_local.py" "${BUILD}/e4_throughput_local.csv"
fi

echo "---- CSV outputs ----"
ls -l "${HERE}/results"
