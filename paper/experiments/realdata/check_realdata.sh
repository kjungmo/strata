#!/usr/bin/env bash
# Verifies the recorded sha256 of every data file phase 1 reads. Skips cleanly (exit 0) when the
# data are not present, so the repository's reproduction harness passes without data.
# usage: check_realdata.sh [DATA_ROOT]   (default ~/datasets/strata)
# exit: 0 ok or skipped, 1 a checksum differs or a listed file is missing while the root exists
set -uo pipefail
ROOT="${1:-$HOME/datasets/strata}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIST="$HERE/data_inputs.sha256"
if [ ! -f "$LIST" ]; then echo "SKIP: $LIST not found"; exit 0; fi
if [ ! -d "$ROOT/kth/00" ] && [ ! -d "$ROOT/lilocbench" ]; then
  echo "SKIP: no real data under $ROOT"; exit 0
fi
cd "$ROOT" || { echo "SKIP: cannot enter $ROOT"; exit 0; }
n=$(wc -l < "$LIST")
if nice -n 19 sha256sum --quiet -c "$LIST"; then
  echo "OK: $n files match the recorded sha256"; exit 0
fi
echo "FAIL: a data file is missing or differs from data_inputs.sha256"; exit 1
