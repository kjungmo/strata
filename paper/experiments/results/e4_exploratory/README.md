# E4 timings taken on a loaded host (exploratory)

Every E4 timing file produced before the quiet-host protocol of
`../../measure_e4.sh` existed is kept here, so that nothing is hidden. None of these
files supports a number in the paper except the statement that `grid2d` was cheaper
per `integrate()` call than `voxel3d` at every extent in every one of them
(`number_guard.py` checks that statement against all files listed in `index.csv`).

All but the first were taken on the same host (AMD Ryzen 5 5500GT, 12 logical CPUs,
Linux on WSL2) while other jobs kept it busy; `index.csv` gives the 1-minute load
average where it was recorded. The ratio of median latencies at the smallest extent
ranges from 9.9x to 27.6x over these files.

Four further runs were printed during the audit and their files were not kept: one
run of the first-release harness in a fresh clone and three immediate repeats of it,
and one trial of the 15-repeat harness. They are not used for anything.

Valid recordings are in `../e4_recordings/`.
