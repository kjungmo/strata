cd "$(dirname "$0")/results_p1b"
f=test_split_stat_before.tsv
head -2 $f > before_boreas.tmp
grep -P '^boreas/' $f >> before_boreas.tmp
grep -v -P '^boreas/' $f > before_main.tmp
mv before_main.tmp test_split_stat_before.tsv
mv before_boreas.tmp test_split_stat_before_boreas.tsv
wc -lc test_split_stat_before*.tsv
