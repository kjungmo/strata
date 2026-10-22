# Mutation record, Amendment 4 (Boreas rotation convention), 2026-10-18

Same method as `mutation_record.md`: `mutation_p3.py` applies one textual mutant at a time to a copy of the sources and runs `test_p3`. Three runs:

* **First pass** (tests of commit 420db38): 30 `a4` mutants, 17 killed, 13 survived: gyro limit, row count, quadrant rule, quadrant row minimum, gyro scale, rows without gyro, voxel neighbours in z, share denominator, limit at 0.3 m, contrast limit, motion between the frames, frame pose of each source, GPS source gated.
  Tests were added for 12 of them (commit f87e038). `a4 convention: gyro scale` is equivalent: the Pearson correlation of the yaw rate with the gyro does not change with the scale of either series.
* **Second pass** (tests of commit f87e038), the 30 `a4` mutants: 29 killed, 1 equivalent survivor (below, tables A and B).
* **Third pass**, the 94 Boreas mutants of the earlier lists (`p3_boreas.hpp` and the Boreas commands of `p3_probe.cpp`), rerun because the synthetic Boreas sessions changed: 91 killed, 3 equivalent survivors (table C; the same three as in `mutation_record.md`, with the same reasons).

The mutants of `lidar pose:` and `rotation:` follow the new code (`pa.q * calib_file.q`, `.inverse()` on the rotation).

## A: convention, CSV and rotation mutants (second pass)
Mutants: 15, killed: 14, survived: 1, other: 0.

| file | mutant | result |
|---|---|---|
| p3_boreas.hpp | a4 rotation: not transposed | killed |
| p3_boreas.hpp | a4 convention: speed limit | killed |
| p3_boreas.hpp | a4 convention: velocity azimuth | killed |
| p3_boreas.hpp | a4 convention: forward axis | killed |
| p3_boreas.hpp | a4 convention: percentiles | killed |
| p3_boreas.hpp | a4 convention: forward axis limit | killed |
| p3_boreas.hpp | a4 convention: gyro limit | killed |
| p3_boreas.hpp | a4 convention: row count | killed |
| p3_boreas.hpp | a4 convention: both percentiles | killed |
| p3_boreas.hpp | a4 convention: quadrant rule | killed |
| p3_boreas.hpp | a4 convention: quadrant row minimum | killed |
| p3_boreas.hpp | a4 convention: gyro sign | killed |
| p3_boreas.hpp | a4 convention: gyro scale | SURVIVED (the Pearson correlation of the yaw rate with the gyro does not change with the scale of either series (degrees or radians per second)) |
| p3_boreas.hpp | a4 convention: rows without gyro | killed |
| p3_boreas.hpp | a4 csv: velocity columns | killed |

## B: frame consistency and command mutants (second pass)
Mutants: 15, killed: 15, survived: 0, other: 0.

| file | mutant | result |
|---|---|---|
| p3_boreas.hpp | a4 frame: neighbour voxels in z | killed |
| p3_boreas.hpp | a4 frame: exact share | killed |
| p3_boreas.hpp | a4 frame: share denominator | killed |
| p3_boreas.hpp | a4 frame: limit at 0.1 m | killed |
| p3_boreas.hpp | a4 frame: limit at 0.3 m | killed |
| p3_boreas.hpp | a4 frame: contrast limit | killed |
| p3_probe.cpp | a4 cli: overlap needs the convention | killed |
| p3_probe.cpp | a4 cli: overlap verdict includes the convention | killed |
| p3_probe.cpp | a4 cli: convention rows | killed |
| p3_probe.cpp | a4 cli: motion between the frames | killed |
| p3_probe.cpp | a4 cli: frame pose of each source | killed |
| p3_probe.cpp | a4 cli: gps source is gated | killed |
| p3_probe.cpp | a4 cli: contrast source | killed |
| p3_probe.cpp | a4 cli: contrast rotation | killed |
| p3_probe.cpp | a4 cli: points are rotated | killed |

## C: Boreas mutants of the earlier passes (third pass)
Mutants: 94, killed: 91, survived: 3, other: 0.

| file | mutant | result |
|---|---|---|
| p3_boreas.hpp | boreas: record size | killed |
| p3_boreas.hpp | boreas: size test | killed |
| p3_boreas.hpp | boreas: finite test | killed |
| p3_boreas.hpp | boreas: column order | killed |
| p3_boreas.hpp | layout: point count | killed |
| p3_boreas.hpp | layout: non-finite share | killed |
| p3_boreas.hpp | layout: max range | killed |
| p3_boreas.hpp | layout: z range | killed |
| p3_boreas.hpp | layout: median range low | killed |
| p3_boreas.hpp | layout: median range high | killed |
| p3_boreas.hpp | layout: intensity low | killed |
| p3_boreas.hpp | layout: intensity high | killed |
| p3_boreas.hpp | layout: ring integer share | killed |
| p3_boreas.hpp | layout: ring low | killed |
| p3_boreas.hpp | layout: ring high | killed |
| p3_boreas.hpp | layout: time low | killed |
| p3_boreas.hpp | layout: time high | killed |
| p3_boreas.hpp | layout: time span | killed |
| p3_boreas.hpp | csv: time must increase | killed |
| p3_boreas.hpp | csv: window end inclusive | killed |
| p3_boreas.hpp | csv: window start inclusive | killed |
| p3_boreas.hpp | csv: origin columns | killed |
| p3_boreas.hpp | csv: time scale | killed |
| p3_boreas.hpp | csv: column count | killed |
| p3_boreas.hpp | rotation: axes of heading and pitch | killed |
| p3_boreas.hpp | rotation: sign of roll | killed |
| p3_boreas.hpp | calib: number count | killed |
| p3_boreas.hpp | calib: last row | killed |
| p3_boreas.hpp | calib: rotation test | killed |
| p3_boreas.hpp | compose: rotation order | killed |
| p3_boreas.hpp | compose: translation | killed |
| p3_boreas.hpp | ecef: polar radius | killed |
| p3_boreas.hpp | ecef: curvature radius | killed |
| p3_boreas.hpp | enu: north axis | killed |
| p3_boreas.hpp | convert: dt of the origin | killed |
| p3_boreas.hpp | convert: r_min boundary | killed |
| p3_boreas.hpp | convert: r_max boundary | killed |
| p3_boreas.hpp | convert: per-point time | killed |
| p3_boreas.hpp | convert: dt of the points | killed |
| p3_boreas.hpp | convert: rotation of the points | killed |
| p3_boreas.hpp | frame time: microseconds | killed |
| p3_boreas.hpp | frame time: digits only | killed |
| p3_boreas.hpp | tiles: floor of x | killed |
| p3_boreas.hpp | tiles: near neighbourhood | killed |
| p3_boreas.hpp | tiles: share denominator | killed |
| p3_boreas.hpp | convert: calibration of the origin | killed |
| p3_boreas.hpp | convert: calibration of the points | killed |
| p3_probe.cpp | cli: validate compares the calibrated pose | killed |
| p3_probe.cpp | boreas-convert: frame step | killed |
| p3_probe.cpp | boreas-convert: frame step group | killed |
| p3_probe.cpp | boreas-convert: frame step bound | killed |
| p3_probe.cpp | boreas-convert: frame step meta | killed |
| p3_probe.cpp | cli: boreas-convert reference | killed |
| p3_boreas.hpp | lidar pose: order of the rotations | killed |
| p3_boreas.hpp | lidar pose: inverse of the file rotation | killed |
| p3_boreas.hpp | lidar pose: translation sign | killed |
| p3_boreas.hpp | lidar pose: translation frame | killed |
| p3_boreas.hpp | lidar pose: translation of the applanix | killed |
| p3_boreas.hpp | common frame: z offset sign | killed |
| p3_boreas.hpp | common frame: z offset axis | killed |
| p3_boreas.hpp | common frame: rotation | killed |
| p3_boreas.hpp | enu agreement: reference origin needed | killed |
| p3_boreas.hpp | enu agreement: rows without lat/lon skipped | killed |
| p3_boreas.hpp | enu agreement: north difference | killed |
| p3_boreas.hpp | enu agreement: east difference | killed |
| p3_boreas.hpp | enu agreement: worst is the maximum | killed |
| p3_boreas.hpp | enu agreement: horizontal distance | killed |
| p3_boreas.hpp | enu agreement: row count | killed |
| p3_boreas.hpp | enu agreement: means | killed |
| p3_boreas.hpp | enu agreement: limit value | killed |
| p3_boreas.hpp | csv: lat/lon per row | killed |
| p3_boreas.hpp | csv: first-row east/north | killed |
| p3_boreas.hpp | altitude: sign | killed |
| p3_boreas.hpp | altitude: radius | killed |
| p3_boreas.hpp | altitude: boundary at the radius | killed |
| p3_boreas.hpp | altitude: nearest row wins | killed |
| p3_boreas.hpp | altitude: percentiles | killed |
| p3_boreas.hpp | altitude: neighbour cells | killed |
| p3_probe.cpp | cli: overlap needs the frame check | killed |
| p3_probe.cpp | cli: overlap needs both checks | killed |
| p3_probe.cpp | cli: overlap frame check of every session | killed |
| p3_probe.cpp | cli: overlap frame check against the reference | killed |
| p3_probe.cpp | cli: overlap frame check needs rows | killed |
| p3_probe.cpp | cli: overlap verdict | killed |
| p3_probe.cpp | cli: overlap trajectory source | killed |
| p3_probe.cpp | cli: validate frame check | killed |
| p3_probe.cpp | cli: validate frame check rows | SURVIVED (unreachable: a valid origin needs the latitude and longitude columns, so every GPS row of the window carries them and `ea.rows > 0` equals `gps.size() > 10 n`, which is checked just before) |
| p3_probe.cpp | cli: validate reference origin | killed |
| p3_probe.cpp | cli: convert refuses another frame | killed |
| p3_probe.cpp | cli: convert needs rows to check | SURVIVED (unreachable: `trackOf(gps)` already fails for fewer than two GPS rows in the window, and every row carries latitude and longitude when the origin is valid) |
| p3_probe.cpp | cli: convert z offset | killed |
| p3_probe.cpp | cli: convert meta of the frame check | killed |
| p3_probe.cpp | cli: validate converts in the file frame | SURVIVED (the validator prints only counts of the converted points; a vertical shift of 1 m changes none of them) |
| p3_probe.cpp | cli: pose agreement bound | killed |
