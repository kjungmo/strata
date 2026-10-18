#!/usr/bin/env bash
# Tests the guard of the test-evaluation stages of reproduce_p3.sh: they must refuse unless P3_RUN_EVALUATIONS=1, PREREG_P3.md is
# committed and unmodified, the experiment directory is clean and the binary was built from HEAD. A temporary git repository is used.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
R="$T/repo"; RD="$R/paper/experiments/realdata"
mkdir -p "$RD" "$T/build/build/realdata" "$T/data" "$T/res"
cp "$HERE/reproduce_p3.sh" "$HERE/p3_runlog.py" "$RD/"
echo "# placeholder" > "$RD/p3_arms.hpp"; echo "# list" > "$RD/data_inputs_p3.sha256"
# a fake p3_probe: no sprt engine; every other subcommand fails, which is fine: the test only looks at the guard
cat > "$T/build/build/realdata/p3_probe" <<'EOF'
#!/usr/bin/env bash
[ "$1" = "arms" ] && { echo "engine_has_sprt=0"; exit 0; }
exit 1
EOF
chmod +x "$T/build/build/realdata/p3_probe"
git -C "$R" init -q
git -C "$R" add -A
git -C "$R" -c user.name=t -c user.email=t@example.com commit -q -m init
head_commit() { git -C "$R" rev-parse HEAD; }
echo "$(head_commit)" > "$T/build/commit"

run() { set +e; out="$(DATA_ROOT="$T/data" BUILD_DIR="$T/build" RESULTS_DIR="$T/res" OUT_DIR="$T/out" "$@" bash "$RD/reproduce_p3.sh" eval_lil 2>&1)"; rc=$?; set -e; }
expect_refusal() {
  local why="$1"; shift
  run "$@"
  [ "$rc" = "4" ] || { echo "FAIL ($why): expected exit 4, got $rc"; echo "$out"; exit 1; }
  case "$out" in *"$MSG"*) ;; *) echo "FAIL ($why): message lacks '$MSG'"; echo "$out"; exit 1;; esac
}

MSG="P3_RUN_EVALUATIONS=1"; expect_refusal "no opt-in" env
MSG="P3_RUN_EVALUATIONS=1"; expect_refusal "opt-in 0" env P3_RUN_EVALUATIONS=0
MSG="PREREG_P3.md is not committed"; expect_refusal "no PREREG_P3.md" env P3_RUN_EVALUATIONS=1

echo "# prereg" > "$RD/PREREG_P3.md"
git -C "$R" add -A; git -C "$R" -c user.name=t -c user.email=t@example.com commit -q -m prereg
echo "$(head_commit)" > "$T/build/commit"

echo "# changed after the commit" >> "$RD/PREREG_P3.md"
MSG="uncommitted changes"; expect_refusal "PREREG_P3.md modified" env P3_RUN_EVALUATIONS=1
git -C "$R" checkout -q -- .

echo "# data" > "$RD/p3_arms.hpp"
MSG="uncommitted changes"; expect_refusal "arm table modified" env P3_RUN_EVALUATIONS=1
git -C "$R" checkout -q -- .

echo "x" > "$RD/stray.txt"
MSG="uncommitted files in the experiment directory"; expect_refusal "untracked file" env P3_RUN_EVALUATIONS=1
rm "$RD/stray.txt"

echo "0000" > "$T/build/commit"
MSG="binary was not built from HEAD"; expect_refusal "stale binary" env P3_RUN_EVALUATIONS=1
echo "$(head_commit)" > "$T/build/commit"

# everything in order: the guard passes (the stage then fails later because the fake probe does nothing) and records the start
run env P3_RUN_EVALUATIONS=1
[ "$rc" != "4" ] || { echo "FAIL: the guard refused although all conditions hold"; echo "$out"; exit 1; }
grep -q "evaluations started" "$T/res/eval_start.txt" || { echo "FAIL: no start record"; exit 1; }
grep -q "prereg_p3_last_commit" "$T/res/eval_start.txt" || { echo "FAIL: start record lacks the PREREG_P3.md commit"; exit 1; }

# the other evaluation stages share the guard
for st in eval_spires eval_kth eval_boreas; do
  set +e; out="$(DATA_ROOT="$T/data" BUILD_DIR="$T/build" RESULTS_DIR="$T/res2" OUT_DIR="$T/out" bash "$RD/reproduce_p3.sh" "$st" 2>&1)"; rc=$?; set -e
  [ "$rc" = "4" ] || { echo "FAIL: $st ran without the opt-in (rc $rc)"; exit 1; }
done
echo "reproduce_p3 guard tests ok"
