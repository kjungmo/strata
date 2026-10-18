#!/usr/bin/env bash
# Static test: the primary (P3_PERIODICITY=1) and secondary (=0) passes of the evaluation stages must not share a step name, an output
# prefix or a finish_eval name. Every step of the stages eval_lil, eval_spires and eval_kth that carries an arm must have p$PER in its
# name; the spires_agree files and the finish_eval names must carry it too. Usage: test_p3_step_names.sh [reproduce_p3.sh]
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
F="${1:-$HERE/reproduce_p3.sh}"
fail=0
body="$(awk '/^lil_sequence\(\)/,/^stage_eval_boreas\(\)/' "$F")"
while IFS= read -r line; do
  case "$line" in
    *'must step "'*'$arm'*|*'must step "'*'${arm}'*)
      name="$(printf '%s' "$line" | sed -n 's/.*must step "\([^"]*\)".*/\1/p')"
      case "$name" in *'p$PER'*|*'p${PER}'*) ;; *) echo "FAIL: step name without p\$PER: $name"; fail=1;; esac;;
  esac
done <<< "$body"
if printf '%s\n' "$body" | grep -E 'spires_agree_' | grep -vE 'spires_agree_(\$\{arm\}|\*)_p(\$\{PER\}|\$PER)' >/dev/null; then echo "FAIL: spires_agree path without p\$PER"; fail=1; fi
if printf '%s\n' "$body" | grep -E 'finish_eval ' | grep -vE 'finish_eval "eval_[a-z]+_p\$PER"' >/dev/null; then echo "FAIL: finish_eval name without p\$PER"; fail=1; fi
if [ "$fail" = 0 ]; then echo 'OK: step names, spires_agree prefixes and finish_eval names carry p$PER'; fi
exit $fail
