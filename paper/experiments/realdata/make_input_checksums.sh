#!/usr/bin/env bash
# Writes data_inputs.sha256: sha256 of every data file phase 1 reads (CALIBRATION split only).
# Paths in the file are relative to the data root, so the file can be checked with
# check_realdata.sh on any machine that holds the same data.
# usage: make_input_checksums.sh [DATA_ROOT]   (default ~/datasets/strata)
set -euo pipefail
ROOT="${1:-$HOME/datasets/strata}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/data_inputs.sha256"
cd "$ROOT"
{
  nice -n 19 sha256sum kth/00/gt_cloud.pcd
  for f in kth/00/pcd/*.pcd; do nice -n 19 sha256sum "$f"; done
  for s in mapping static_0 dynamics_0; do
    nice -n 19 sha256sum "lilocbench/bags/$s.bag" "lilocbench/gt_x/$s/gt_poses.txt"
  done
} > "$OUT.tmp"
mv "$OUT.tmp" "$OUT"
wc -l "$OUT"
