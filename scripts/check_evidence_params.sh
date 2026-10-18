#!/usr/bin/env bash
# The node refuses an unusable evidence configuration at startup with a message that names it,
# and starts with the legacy model on request.
#
#   bash scripts/check_evidence_params.sh        # needs a sourced ROS 2 workspace with `strata` built
#
# Each bad case must exit non-zero within 15 s and print the expected text; the legacy case must
# print "strata up" and "evidence: legacy" and keep running until stopped (the timeout, exit 124).
set -u
fail=0
bad() {  # expected text, then the --ros-args parameter overrides
  local want="$1"; shift
  local args=()
  for kv in "$@"; do args+=(-p "$kv"); done
  local out code=0
  out=$(timeout 15 ros2 run strata strata_node_main --ros-args "${args[@]}" 2>&1) || code=$?
  if [ "$code" -eq 0 ] || [ "$code" -eq 124 ]; then echo "FAIL: ($*) exit $code, expected a refusal"; fail=1; return; fi
  if ! grep -qF -- "$want" <<< "$out"; then echo "FAIL: ($*) output lacks: $want"; echo "$out" | tail -3; fail=1; return; fi
  echo "ok: ($*) refused: $want"
}
bad 'evidence_model must be "sprt" or "legacy"' evidence_model:=foo
bad 'need 0 < m0 < m1 < 1' m0:=0.8
bad 'need 0 < m0 < m1 < 1' m1:=1.0
bad 'need alpha > 0, beta > 0 and alpha + beta < 1' alpha:=0.6 beta:=0.5
bad 'need alpha > 0, beta > 0 and alpha + beta < 1' alpha:=0.0
bad 'n_max_undecided must be >= 0' n_max_undecided:=-1
bad 'evidence_calibration must be' evidence_calibration:=bogus
# legacy starts, and the sprt keys are not validated then
out=$(timeout 8 ros2 run strata strata_node_main --ros-args -p evidence_model:=legacy -p m0:=5.0 2>&1); code=$?
if [ "$code" -ne 124 ]; then echo "FAIL: legacy start exited $code"; echo "$out" | tail -3; fail=1
elif grep -qF "strata up" <<< "$out" && grep -qF "evidence: legacy" <<< "$out"; then echo "ok: evidence_model:=legacy starts"
else echo "FAIL: legacy start did not log the evidence line"; echo "$out" | tail -3; fail=1; fi
# the shipped default starts with the sprt model
out=$(timeout 8 ros2 run strata strata_node_main 2>&1); code=$?
if [ "$code" -eq 124 ] && grep -qF "evidence: sprt: clutter m0=0.2" <<< "$out"; then echo "ok: the default starts with the sprt model"
else echo "FAIL: default start (exit $code)"; echo "$out" | tail -3; fail=1; fi
# voxel3d without the shipped YAML: the 2D pair is labelled as the fallback and the node warns
out=$(timeout 8 ros2 run strata strata_node_main --ros-args -p backend:=voxel3d 2>&1); code=$?
if [ "$code" -eq 124 ] && grep -qF "not calibrated for backend voxel3d" <<< "$out" && grep -qF "NOT calibrated for voxel3d" <<< "$out"; then
  echo "ok: backend:=voxel3d without a YAML warns that the pair is not calibrated"
else echo "FAIL: voxel3d default start (exit $code)"; echo "$out" | tail -3; fail=1; fi
# grid2d with the key left out stays measured_2d and does not warn
out=$(timeout 8 ros2 run strata strata_node_main 2>&1)
if grep -qF "not calibrated for backend" <<< "$out"; then echo "FAIL: grid2d default start warns"; fail=1; else echo "ok: grid2d default start does not warn"; fi
exit "$fail"
