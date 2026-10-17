## Deviations and notes

1. `analyze_p1.py` (code committed before the first measurement in 32bf5b6) was changed after the first
   measurement. Defect: it looked for the LILocBench alternative-resolution cell tables under the name
   `..._r0.1_...`, but `reproduce_p1.sh` writes `..._r0.10_...`, so the first analysis listed all six
   0.10 m LILocBench tables as "missing" (visible in `p1_inventory.csv`). Fix: the file-name token is now
   `0.10`; the label in the tables stays `0.1`. `make_report.py` had the same naming defect for the reference
   meta files and was fixed the same way. No statistic, definition or threshold changed.
2. Gate outcome. All four wall-thickness gates fail (section 5 of PREREG_P1). The gate was not changed after
   seeing the result. The pre-registered consequence is that no sequence enters a headline table. The
   numbers are given in full because the lead asked for the calibration statistics; they must be read as
   statistics of maps that did not meet the pre-registered thickness bound. Possible reasons, not verified:
   (a) KITTI 00 GT cloud is built from scan poses with some registration error and includes sloped
   surfaces and vegetation in the "vertical patch" set; (b) LILocBench GT poses at 0.05 m cell size give
   wall thickness of 7 to 8 cells (0.35 to 0.42 m) in all three sequences, including the mapping sequence that
   defines the reference.
3. The first measurement run (2026-10-14, 09:36 to 10:02) was cut off during `lil_measure_dynamics_0_r0.05_W2`
   and before the 0.10 m LILocBench steps. The whole pipeline was re-run on 2026-10-18 (03:53 to 04:36 KST)
   from `reproduce_p1.sh all`. The KITTI 00 and LILocBench 0.05 m cell tables of the second run are
   byte-identical (sha256) to the first run: the probe is deterministic.
4. The "alternative miss definition" was not measured: today's definition is already pass-through only
   (PREREG_P1 section 1).
