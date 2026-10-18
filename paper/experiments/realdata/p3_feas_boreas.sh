#!/usr/bin/env bash
# Engine cost on the first 10 Boreas frames (the frames already opened for reader validation): cells, memory and time, for several
# range windows and voxel sizes. Used to choose a Boreas configuration that fits the host (PREREG_P3.md section 8.5). Selects nothing from labels.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
B="${BUILD_DIR:-$HOME/.cache/strata_p3_build}/build/realdata"
D="${DATA_ROOT:-$HOME/datasets/strata}/boreas/boreas-2020-11-26-13-58"
O=/mnt/d/wsl/strata/p3/feas
mkdir -p $O
cd $HERE
echo "r_max res periodicity arm cells rss_kb seconds"
for rmax in 50 25 15; do
  bash p3_traced.sh feas_boreas_convert_r$rmax $B/p3_probe boreas-convert --seq $D --ref $D --max-frames 10 --r-max $rmax --out $O/cache10_r$rmax.bin --meta $O/cache10_r$rmax.meta > /dev/null
  for res in 0.2 0.4; do
    for per in 0 1; do
      arm=sprt
      /usr/bin/time -v nice -n 19 $B/p3_probe static-export --caches $O/cache10_r$rmax.bin --arm $arm --res $res --window 10 --periodicity $per --mode independent \
        --out-prefix $O/f --meta $O/f.meta 2> $O/f.time
      rss=$(grep "Maximum resident" $O/f.time | awk '{print $NF}')
      cells=$(grep "session0_map_cells_end" $O/f.meta | cut -d= -f2)
      secs=$(grep "^seconds" $O/f.meta | cut -d= -f2)
      echo "$rmax $res $per $arm $cells $rss $secs"
    done
  done
done
