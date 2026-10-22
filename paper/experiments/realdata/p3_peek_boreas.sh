#!/usr/bin/env bash
# Reader validation for Boreas, step 1 (text only): header and first three rows of the pose files, the first 96 bytes of the
# first lidar frame. Opens only the files named below; nothing is decoded into a statistic. Run it through p3_traced.sh.
#   bash p3_traced.sh boreas_peek bash p3_peek_boreas.sh
set -u
DATA="${DATA_ROOT:-$HOME/datasets/strata}"
S="$DATA/boreas/boreas-2020-11-26-13-58"
for f in applanix/gps_post_process.csv applanix/lidar_poses.csv applanix/lidar_times.txt calib/T_applanix_lidar.txt; do
  echo "== $f"
  head -n 3 "$S/$f"
done
first="$(ls "$S/lidar" | head -n 1)"
echo "== lidar/$first"
stat --printf 'size=%s\n' "$S/lidar/$first"
od -A d -t f4 -N 96 "$S/lidar/$first"
