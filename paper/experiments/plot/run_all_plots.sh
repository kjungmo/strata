#!/usr/bin/env bash
# Regenerate every figure in paper/figures/ from the measured CSVs in
# paper/experiments/results/. Run from anywhere; paths are resolved
# relative to this script's own location via style.py.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for script in plot_e1_f1_vs_time.py plot_e2_periodicity.py plot_e3_sensitivity.py \
              plot_e4_throughput.py plot_architecture.py; do
    echo "=== running $script ==="
    python3 "$HERE/$script"
done
