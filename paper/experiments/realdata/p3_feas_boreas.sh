#!/usr/bin/env bash
# Engine cost on the first 10 Boreas frames (the frames already opened for reader validation): cells, memory and time, for several
# range windows and voxel sizes. Used to choose a Boreas configuration that fits the host (PREREG_P3.md section 14, Amendment 1).
# Selects nothing from labels. The engine is the `sprt` arm; it runs on the first 10 frames of boreas-2020-11-26-13-58 only.
#
# The first run of this probe (2026-10-18 12:11 to 12:15, commits 214891e and c671879) printed its table to the terminal and was not saved;
# its per-configuration files were overwritten except the last ones (results_p3/feas/original/). This script keeps everything it prints:
# the table (results_p3/feas/feas_table.txt), the convert meta of every range window and the meta and time files of every configuration.
# It was run again for the record after review finding 5, with the same 10 frames (the new reads are in access_log.tsv under feas_rerun_*).
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
B="${BUILD_DIR:-$HOME/.cache/strata_p3_build}/build/realdata"
D="${DATA_ROOT:-$HOME/datasets/strata}/boreas/boreas-2020-11-26-13-58"
O="${FEAS_OUT:-/mnt/d/wsl/strata/p3/feas_rerun}"        # bulky caches stay outside the repository
R="${FEAS_RECORD:-$HERE/results_p3/feas}"                  # small records, committed
mkdir -p "$O" "$R"
cd "$HERE"
{
  echo "# engine commit $(cat "${BUILD_DIR:-$HOME/.cache/strata_p3_build}/commit" 2>/dev/null || echo unknown); started $(date -Is); host $(hostname); load $(cut -d' ' -f1-3 /proc/loadavg)"
  echo "r_max res periodicity arm cells rss_kb seconds"
  for rmax in 50 25 15; do
    bash p3_traced.sh feas_rerun_boreas_convert_r$rmax "$B/p3_probe" boreas-convert --seq "$D" --ref "$D" --max-frames 10 --r-max "$rmax" --out "$O/cache10_r$rmax.bin" --meta "$O/cache10_r$rmax.meta" > /dev/null
    cp "$O/cache10_r$rmax.meta" "$R/convert_r$rmax.meta"
    for res in 0.2 0.4; do
      for per in 0 1; do
        arm=sprt
        /usr/bin/time -v nice -n 19 "$B/p3_probe" static-export --caches "$O/cache10_r$rmax.bin" --arm $arm --res $res --window 10 --periodicity $per --mode independent \
          --out-prefix "$O/f" --meta "$O/f.meta" 2> "$O/f.time"
        rss=$(grep "Maximum resident" "$O/f.time" | awk '{print $NF}')
        cells=$(grep "session0_map_cells_end" "$O/f.meta" | cut -d= -f2)
        secs=$(grep "^seconds" "$O/f.meta" | cut -d= -f2)
        cp "$O/f.meta" "$R/f_r${rmax}_res${res}_per${per}.meta"; cp "$O/f.time" "$R/f_r${rmax}_res${res}_per${per}.time"
        echo "$rmax $res $per $arm $cells $rss $secs"
      done
    done
  done
  echo "# finished $(date -Is)"
} | tee "$R/feas_table.txt"
