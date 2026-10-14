#!/usr/bin/env bash
# Fails if a file larger than 1 MB, or a data file (.pcd .bag .bin .zip .npy), is tracked, staged
# or unignored-untracked under paper/experiments/realdata/. Real data never enters the repository.
# usage: test_no_data_staged.sh            check this directory
#        test_no_data_staged.sh --selftest prove that the check can fail (temporary git repo)
set -euo pipefail
LIMIT=1048576

scan() {   # $1 = directory inside a git work tree
  local dir="$1" bad=0 f size
  while IFS= read -r f; do
    [ -f "$dir/$f" ] || continue
    size=$(stat -c %s "$dir/$f")
    if [ "$size" -gt "$LIMIT" ]; then echo "TOO LARGE ($size bytes): $f"; bad=1; fi
    case "$f" in *.pcd|*.bag|*.bin|*.zip|*.npy) echo "DATA FILE: $f"; bad=1;; esac
  done < <(git -C "$dir" ls-files --cached --others --exclude-standard -- .)
  return $bad
}

if [ "${1:-}" = "--selftest" ]; then
  T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
  git -C "$T" init -q
  head -c 2000000 /dev/zero > "$T/big.txt"
  if scan "$T" >/dev/null; then echo "selftest FAILED: a 2 MB file was not detected"; exit 1; fi
  rm "$T/big.txt"; echo x > "$T/frame.pcd"
  if scan "$T" >/dev/null; then echo "selftest FAILED: a .pcd file was not detected"; exit 1; fi
  rm "$T/frame.pcd"; echo x > "$T/small.txt"
  scan "$T" || { echo "selftest FAILED: a small text file was flagged"; exit 1; }
  printf 'big.dat\n' > "$T/.gitignore"; head -c 2000000 /dev/zero > "$T/big.dat"
  scan "$T" || { echo "selftest FAILED: an ignored file was flagged"; exit 1; }
  echo "selftest ok"
  exit 0
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if ! git -C "$HERE" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "not inside a git work tree: skipped"; exit 0
fi
scan "$HERE" && echo "no data files under $HERE"
