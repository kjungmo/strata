#!/usr/bin/env bash
# Build and run the full strata_core evaluation harness (E0/E5 calibration, E1-E5).
# Reproduction: bash paper/experiments/run_all.sh  (from the repository root)
#
# Every seeded executable runs twice: with no argument it is the "legacy" arm (the v0.1
# arithmetic with explicit parameters; reproduces the committed results/*.csv byte for byte,
# see check_legacy_equivalence.sh) and with the argument "sprt" it is the sequential-test arm
# (evidence_model = Sprt at the shipped defaults; writes results/*_sprt.csv, new files).
# E0-E3, E5 and E6 are seeded and rewrite results/*.csv byte for byte. E4 reads the
# wall clock: here it only writes build/e4_throughput_local.csv, and check_e4_local.py
# compares its deterministic columns (cell counts, byte estimates) with the committed
# results/e4_throughput.csv. Timings for the paper are recorded separately, on a quiet
# host, with measure_e4.sh. STRATA_SKIP_E4=1 skips E4; STRATA_BUILD_JOBS sets the
# number of parallel build jobs (default: all cores).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"

cmake -S "${HERE}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DSTRATA_CORE_BUILD_TESTS=ON
cmake --build "${BUILD}" -j "${STRATA_BUILD_JOBS:-$(nproc)}"
( cd "${BUILD}/strata_core_build" && ctest --output-on-failure )

mkdir -p "${HERE}/results"
cd "${HERE}"   # executables write CSVs to ./results
"${BUILD}/e0_calibration"      # single-read-out level on held-out seeds
"${BUILD}/e5_trajectory"       # trajectory level: picks the shipped spent level, evaluates it
"${BUILD}/e1_static_quality"
"${BUILD}/e2_periodicity"
"${BUILD}/e3_sensitivity"
"${BUILD}/e6_default_confusion"   # false Static labels: shipped-default confusion, sweep over m
"${BUILD}/e7_sprt_oc"             # the sequential test on the engine: operating characteristic, autocorrelation

# sprt arm of E0-E3, E5 and E6 (new files results/*_sprt.csv; the legacy files above are not touched)
"${BUILD}/e0_calibration" sprt
"${BUILD}/e5_trajectory" sprt
"${BUILD}/e1_static_quality" sprt
"${BUILD}/e2_periodicity" sprt
"${BUILD}/e3_sensitivity" sprt
"${BUILD}/e6_default_confusion" sprt
"${BUILD}/e4_throughput" sprt     # cell counts only (results/e4_cells_sprt.csv); the timing is measure_e4.sh's

if [ "${STRATA_SKIP_E4:-0}" = "1" ]; then
  echo "E4 skipped (STRATA_SKIP_E4=1)"
else
  "${BUILD}/e4_throughput" "${BUILD}/e4_throughput_local.csv"
  python3 "${HERE}/check_e4_local.py" "${BUILD}/e4_throughput_local.csv"
fi

echo "---- CSV outputs ----"
ls -l "${HERE}/results"
