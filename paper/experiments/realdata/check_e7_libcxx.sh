#!/usr/bin/env bash
# Is E7 (paper/experiments/src/e7_sprt_oc.cpp) independent of the C++ standard library?
#
#   CXX=clang++ bash paper/experiments/realdata/check_e7_libcxx.sh        # needs clang++ with libc++
#
# E7 draws from std::mt19937_64 raw output only (no <random> distribution), so its two CSVs must be
# byte-identical when built with clang++ and libc++ instead of GCC and libstdc++. This builds E7 and
# strata_core by hand (no ZLIB needed), runs it in a scratch directory and compares with the committed
# results/e7_oc.csv and results/e7_autocorr.csv. Exit 1 if either differs.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WT="$(cd "${HERE}/../../.." && pwd)"
CXX="${CXX:-clang++}"
B="${BUILD_DIR:-/tmp/strata_e7_libcxx}"
EIGEN="${EIGEN_INCLUDE:-/usr/include/eigen3}"
rm -rf "${B}"; mkdir -p "${B}/run/results"
echo "compiler: $("${CXX}" --version | head -1)"
SRC="${WT}/strata_core/src/types.cpp ${WT}/strata_core/src/periodicity.cpp ${WT}/strata_core/src/layered_map.cpp ${WT}/strata_core/src/grid2d_backend.cpp ${WT}/strata_core/src/voxel3d_backend.cpp"
# shellcheck disable=SC2086
"${CXX}" -std=c++17 -O2 -stdlib=libc++ -I"${WT}/strata_core/include" -I"${WT}/paper/experiments/src" -I"${EIGEN}" \
  "${WT}/paper/experiments/src/e7_sprt_oc.cpp" ${SRC} -o "${B}/e7_sprt_oc" > "${B}/build.log" 2>&1 \
  || { grep -m5 error "${B}/build.log" >&2; exit 1; }
echo "standard_library: $(ldd "${B}/e7_sprt_oc" | grep -o 'libc++\.so[^ ]*' | head -1)"
( cd "${B}/run" && "${B}/e7_sprt_oc" > /dev/null )
rc=0
for f in e7_oc.csv e7_autocorr.csv; do
  if cmp -s "${B}/run/results/${f}" "${WT}/paper/experiments/results/${f}"; then echo "identical: ${f}"; else echo "DIFFERENT: ${f}"; rc=1; fi
done
exit "${rc}"
