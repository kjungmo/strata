#!/usr/bin/env bash
# Tests check_realdata.sh on a tiny fake data root: skip when absent, pass when matching,
# fail when a file differs or is missing. The list is a temporary copy; the real one is untouched.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/tool" "$T/root/kth/00"
cp "$HERE/check_realdata.sh" "$T/tool/"
echo hello > "$T/root/kth/00/a.txt"
( cd "$T/root" && sha256sum kth/00/a.txt ) > "$T/tool/data_inputs.sha256"

out=$(bash "$T/tool/check_realdata.sh" "$T/nonexistent") || { echo "absent data must skip with exit 0"; exit 1; }
case "$out" in SKIP*) ;; *) echo "absent data: expected SKIP, got: $out"; exit 1;; esac

out=$(bash "$T/tool/check_realdata.sh" "$T/root") || { echo "matching data must pass: $out"; exit 1; }
case "$out" in OK*) ;; *) echo "matching: expected OK, got: $out"; exit 1;; esac

echo changed > "$T/root/kth/00/a.txt"
if bash "$T/tool/check_realdata.sh" "$T/root" >/dev/null 2>&1; then echo "a changed file must fail"; exit 1; fi

echo hello > "$T/root/kth/00/a.txt"; mv "$T/root/kth/00/a.txt" "$T/root/kth/00/b.txt"
if bash "$T/tool/check_realdata.sh" "$T/root" >/dev/null 2>&1; then echo "a missing file must fail"; exit 1; fi

rm "$T/tool/data_inputs.sha256"
bash "$T/tool/check_realdata.sh" "$T/root" | grep -q '^SKIP' || { echo "missing list must skip"; exit 1; }
echo "check_realdata tests ok"
