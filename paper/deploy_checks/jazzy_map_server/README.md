# Saved map loaded by Jazzy's `nav2_map_server` (one recorded run, not in CI)

CI loads the saved PGM + YAML pair in `nav2_map_server` on ROS 2 Humble only. This
directory holds one manual check on Jazzy and what is needed to repeat it:

- `saved_map/` — a pair written by `~/save_map` of the node at commit `312b09c` on
  Humble, for the synthetic scene of `scripts/synthetic_e2e.py` (shipped `grid2d.yaml`).
- `jazzy_map_check.py` — starts the sourced distribution's map_server on that YAML and
  compares the served map with the PGM, cell by cell.
- `recorded_run.txt` — the output of the run of 2026-10-12: `nav2_map_server` 1.3.11
  from RoboStack (`ros-jazzy`), configure and activate succeed, 160000 cells, 0
  mismatches, the two `strata_image_*` keys are ignored without a message.

To repeat it in a sourced Jazzy environment that has `nav2_map_server`:

```bash
cd paper/deploy_checks/jazzy_map_server
python3 ../../../strata/scripts/check_saved_map.py saved_map/strata_2d.yaml
python3 jazzy_map_check.py saved_map/strata_2d.yaml
```

Limits: one package build, one synthetic map, one host. The node itself was not built
or run on Jazzy.
