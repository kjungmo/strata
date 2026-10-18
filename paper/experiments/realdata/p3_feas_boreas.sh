#!/usr/bin/env bash
# Engine cost on the first 10 Boreas frames (the frames already opened for reader validation): cells, memory and time per frame.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
B="${BUILD_DIR:-$HOME/.cache/strata_p3_build}/build/realdata"
D="${DATA_ROOT:-$HOME/datasets/strata}/boreas/boreas-2020-11-26-13-58"
O=/mnt/d/wsl/strata/p3/feas
mkdir -p $O
cd $HERE
bash p3_traced.sh feas_boreas_convert $B/p3_probe boreas-convert --seq $D --ref $D --max-frames 10 --out $O/cache10.bin --meta $O/cache10.meta
grep -E "frames_written|points_used|points_out_of_range" $O/cache10.meta
for arm in shipped_v0_1 sprt; do
  /usr/bin/time -v nice -n 19 $B/p3_probe static-export --caches $O/cache10.bin --arm $arm --res 0.2 --window 10 --periodicity 1 --mode independent --out-prefix $O/f_$arm --meta $O/f_$arm.meta 2> $O/f_$arm.time
  grep -E "Maximum resident|Elapsed" $O/f_$arm.time
  grep -E "map_cells_end|static_cells|observed_cells|seconds" $O/f_$arm.meta
done
