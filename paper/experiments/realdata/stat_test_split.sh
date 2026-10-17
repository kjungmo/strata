# usage: bash stat_test.sh OUTFILE.tsv   (writes OUTFILE.tsv and OUTFILE_boreas.tsv)
# Records size, mtime, atime, ctime of every TEST-split file, plus Keble 02 (moved to calibration by Amendment 2).
out="$1"
outb="${out%.tsv}_boreas.tsv"
cd "${DATA_ROOT:-$HOME/datasets/strata}" || exit 1
hdr1="# stat record of test-split files; root = datasets/strata; columns: path<TAB>size<TAB>mtime<TAB>atime<TAB>ctime"
hdr2="# host=$(hostname) recorded=$(date --iso-8601=ns) mount: D: is 9p drvfs with noatime, so atime is not informative"
echo "$hdr1" > "$out"; echo "$hdr2" >> "$out"
echo "$hdr1" > "$outb"; echo "$hdr2" >> "$outb"
{
  find kth/05 kth/semindoor kth/05.zip kth/semindoor.zip -type f | sort
  find lilocbench/bags/lt_changes_0.bag lilocbench/bags/lt_changes_dynamics_0.bag lilocbench/gt_x/lt_changes_0 lilocbench/gt_x/lt_changes_dynamics_0 -type f | sort
  find spires/keble -type f | sort
} | while IFS= read -r p; do
  stat --printf '%n\t%s\t%y\t%x\t%z\n' -- "$p" >> "$out"
done
find boreas -type f | sort | while IFS= read -r p; do
  stat --printf '%n\t%s\t%y\t%x\t%z\n' -- "$p" >> "$outb"
done
wc -lc "$out" "$outb"
