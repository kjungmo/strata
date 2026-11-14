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
bad 'periodic_min_periods must be finite and >= 1' periodic_min_periods:=0.5
bad 'periodic_min_periods must be finite and >= 1' periodic_min_periods:=0.0
bad 'undecided_min_evidence must be in (0, 1]' undecided_min_evidence:=0.0
bad 'undecided_min_evidence must be in (0, 1]' undecided_min_evidence:=1.5
bad 'export_undecided is implemented for backend voxel3d only' export_undecided:=true
bad 'graduate_prob must be in (0, 1)' backend:=voxel3d export_undecided:=true evidence_model:=legacy graduate_prob:=1.0
# legacy starts, and the sprt keys are not validated then
out=$(timeout 8 ros2 run strata strata_node_main --ros-args -p evidence_model:=legacy -p m0:=5.0 2>&1); code=$?
if [ "$code" -ne 124 ]; then echo "FAIL: legacy start exited $code"; echo "$out" | tail -3; fail=1
elif grep -qF "strata up" <<< "$out" && grep -qF "evidence: legacy" <<< "$out"; then echo "ok: evidence_model:=legacy starts"
else echo "FAIL: legacy start did not log the evidence line"; echo "$out" | tail -3; fail=1; fi
# the shipped default starts with the sprt model
out=$(timeout 8 ros2 run strata strata_node_main 2>&1); code=$?
if [ "$code" -eq 124 ] && grep -qF "sprt: clutter m0=0.2" <<< "$out" && grep -qF "provisional (PREREG not frozen: G1 failed)" <<< "$out"; then echo "ok: the default starts with the sprt model, labelled provisional"
else echo "FAIL: default start (exit $code)"; echo "$out" | tail -3; fail=1; fi
# voxel3d without the shipped YAML: the 2D pair is labelled as the fallback and the node warns
out=$(timeout 8 ros2 run strata strata_node_main --ros-args -p backend:=voxel3d 2>&1); code=$?
if [ "$code" -eq 124 ] && grep -qF "not calibrated for backend voxel3d" <<< "$out" && grep -qF "NOT calibrated for voxel3d" <<< "$out"; then
  echo "ok: backend:=voxel3d without a YAML warns that the pair is not calibrated"
else echo "FAIL: voxel3d default start (exit $code)"; echo "$out" | tail -3; fail=1; fi
# voxel3d with the export on starts and says what the exported voxels are; the default start does not say it
out=$(timeout 8 ros2 run strata strata_node_main --ros-args -p backend:=voxel3d -p export_undecided:=true -p periodic_min_periods:=2.0 2>&1); code=$?
if [ "$code" -eq 124 ] && grep -qF "export_undecided is on" <<< "$out" && grep -qF "alpha bounds clutter becoming Static only" <<< "$out"; then
  echo "ok: backend:=voxel3d export_undecided:=true starts and warns that the exported voxels are not static"
else echo "FAIL: voxel3d export start (exit $code)"; echo "$out" | tail -3; fail=1; fi
out=$(timeout 8 ros2 run strata strata_node_main --ros-args -p backend:=voxel3d 2>&1)
if grep -qF "export_undecided is on" <<< "$out"; then echo "FAIL: the default voxel3d start says the export is on"; fail=1; else echo "ok: the default start does not export undecided voxels"; fi
# grid2d with the key left out stays measured_2d and does not warn
out=$(timeout 8 ros2 run strata strata_node_main 2>&1)
if grep -qF "not calibrated for backend" <<< "$out"; then echo "FAIL: grid2d default start warns"; fail=1; else echo "ok: grid2d default start does not warn"; fi
# a tuned v0.1 key on the sprt default warns; the same key on legacy does not; sprt keys on legacy warn
out=$(timeout 8 ros2 run strata strata_node_main --ros-args -p l_hit:=1.2 2>&1)
if grep -qF "these v0.1 keys are ignored: l_hit" <<< "$out"; then echo "ok: a v0.1 key on the sprt default warns"; else echo "FAIL: no v0.1 key warning"; fail=1; fi
out=$(timeout 8 ros2 run strata strata_node_main --ros-args -p evidence_model:=legacy -p l_hit:=1.2 2>&1)
if grep -qF "keys are ignored" <<< "$out"; then echo "FAIL: legacy warns about v0.1 keys"; fail=1; else echo "ok: legacy with a v0.1 key does not warn"; fi
out=$(timeout 8 ros2 run strata strata_node_main --ros-args -p evidence_model:=legacy -p m0:=0.3 2>&1)
if grep -qF "these sprt keys are ignored: m0" <<< "$out"; then echo "ok: an sprt key on legacy warns"; else echo "FAIL: no sprt key warning on legacy"; fail=1; fi
# a user-tuned pair under the measured_2d label is relabelled "user" and warns; the untouched default does not
out=$(timeout 8 ros2 run strata strata_node_main --ros-args -p m0:=0.30 -p m1:=0.60 2>&1)
if grep -qF 'differ from the measured values' <<< "$out" && grep -qF 'changed to "user"' <<< "$out" && ! grep -qF "PREREG not frozen" <<< "$out"; then
  echo "ok: a tuned pair is relabelled user and warns"
else echo "FAIL: tuned pair (m0, m1) keeps the measured label"; echo "$out" | tail -4; fail=1; fi
out=$(timeout 8 ros2 run strata strata_node_main --ros-args -p m0:=0.30 -p m1:=0.60 -p evidence_calibration:=user 2>&1)
if grep -qF 'differ from the measured values' <<< "$out"; then echo "FAIL: user label still warns about the pair"; fail=1; else echo "ok: evidence_calibration:=user with a tuned pair does not warn"; fi
out=$(timeout 8 ros2 run strata strata_node_main 2>&1)
if grep -qF "keys are ignored" <<< "$out"; then echo "FAIL: default start warns about ignored keys"; fail=1; else echo "ok: default start has no ignored-key warning"; fi
exit "$fail"
