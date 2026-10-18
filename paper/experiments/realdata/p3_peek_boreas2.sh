#!/usr/bin/env bash
# Reader validation for Boreas, step 2 (pose convention): the pose rows of the first 10 lidar frames of the slice and the
# post-processed GPS rows of the first 0.2 s of the slice. Used only to read the sign convention of heading against the
# velocity direction and to see which rows of the pose file belong to the slice. Run it through p3_traced.sh.
#   bash p3_traced.sh boreas_peek2 bash p3_peek_boreas2.sh
set -u
DATA="${DATA_ROOT:-$HOME/datasets/strata}"
S="$DATA/boreas/boreas-2020-11-26-13-58"
t0=1606417543533737
echo "== lidar_poses.csv rows with GPSTime in [t0, t0 + 1.0 s] (first 10 frames of the slice)"
awk -F, -v t0="$t0" 'NR==1 || ($1 >= t0 && $1 <= t0 + 1000000)' "$S/applanix/lidar_poses.csv"
echo "== gps_post_process.csv rows with GPSTime in [t0, t0 + 0.2 s] (seconds)"
awk -F, -v t0="$t0" 'NR==1 || ($1 >= t0 / 1e6 && $1 <= t0 / 1e6 + 0.2)' "$S/applanix/gps_post_process.csv" | cut -d, -f1-10,17-18
echo "== lidar_times.txt line count and the first slice frame"
wc -l < "$S/applanix/lidar_times.txt"
grep -n "$t0" "$S/applanix/lidar_times.txt"
