#!/usr/bin/env bash
# Sensitivity of the seeded experiments to the C++ standard library.
#
#   CXX=clang++ bash paper/experiments/check_stdlib.sh     # needs clang++ with libc++
#
# Builds the harness with $CXX and -stdlib=libc++ in build_libcxx/, reruns E0-E3, E5
# and E6 in a scratch directory (the committed results/ is not touched), and records
# under results/stdlib_libcxx/ which result files come out byte-identical to the
# committed ones (written with GCC and libstdc++) and a copy of every file that does
# not. E4 is not run (wall clock). The harness draws from std::mt19937 through
# std::bernoulli_distribution, std::uniform_real_distribution and
# std::uniform_int_distribution; the C++ standard fixes the engine's output but not
# the distributions' algorithms.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CXX="${CXX:-clang++}"
B="${HERE}/build_libcxx"
OUT="${HERE}/results/stdlib_libcxx"
JOBS="${STRATA_BUILD_JOBS:-1}"

cmake -S "${HERE}" -B "${B}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="${CXX}" \
  -DCMAKE_CXX_FLAGS=-stdlib=libc++ > "${B}.cmake.log" 2>&1 || { tail -5 "${B}.cmake.log" >&2; exit 1; }
cmake --build "${B}" -j "${JOBS}" > "${B}.build.log" 2>&1 || { grep -m5 error "${B}.build.log" >&2; exit 1; }

W="$(mktemp -d)"
mkdir -p "${W}/results"
( cd "${W}" && for x in e0_calibration e5_trajectory e1_static_quality e2_periodicity e3_sensitivity e6_default_confusion; do
    "${B}/${x}" > /dev/null
  done )

rm -rf "${OUT}"
mkdir -p "${OUT}"
{
  echo "compiler: $("${CXX}" --version | head -1)"
  echo "standard_library: $(ldd "${B}/e2_periodicity" | grep -o 'libc++\.so[^ ]*' | head -1)"
  echo "host: $(uname -sm)"
  echo "strata_core_tree: $(git -C "${HERE}" rev-parse HEAD:strata_core)"
  for f in $(cd "${W}/results" && ls *.csv | sort); do
    if cmp -s "${W}/results/${f}" "${HERE}/results/${f}"; then
      echo "identical: ${f}"
    else
      echo "different: ${f}"
      cp "${W}/results/${f}" "${OUT}/${f}"
    fi
  done
} > "${OUT}/summary.txt"
rm -rf "${W}"
cat "${OUT}/summary.txt"
