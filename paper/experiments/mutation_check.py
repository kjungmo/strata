#!/usr/bin/env python3
"""Does number_guard.py notice a changed numeral? Mutation check of the arXiv package.

    python3 paper/experiments/mutation_check.py [--tables] > mutation_report.txt

Works on a scratch copy of the repository. For every numeral in the hand-written
sources (sections/*.tex and the hand-written figures/*.tex; with --tables also one
numeral in three per generated table), it changes the last digit, runs the guard and
records whether a new failure appears. Prints each survivor and a summary line.
Takes roughly half an hour (one guard run per numeral).
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SKIP = re.compile(r"(?<!\\)%.*|\\(?:ref|label|cite[tp]?|eqref|input|url|nolinkurl|includegraphics)\*?(?:\[[^\]]*\])*\{[^}]*\}")
NUM = re.compile(r"(?<![\\A-Za-z_.\d])\d+(?:\{,\}\d{3})*(?:\.\d+)?(?!\d)")


def bump(tok):
    d = tok[-1]
    return tok[:-1] + ("8" if d == "9" else str(int(d) + 1))


def failures(root):
    r = subprocess.run([sys.executable, str(root / "paper/experiments/number_guard.py")], capture_output=True, text=True)
    # the cross-check of the companions would flag every changed numeral by itself and hide
    # the checks of the manuscript that this run is meant to test
    return {l for l in r.stdout.splitlines() if l.startswith("FAIL") and not l.startswith("FAIL [companions]")} | ({"CRASH"} if "Traceback" in r.stderr else set())


def main():
    tables = "--tables" in sys.argv
    with tempfile.TemporaryDirectory() as td:
        root = Path(td) / "repo"
        shutil.copytree(REPO, root, ignore=shutil.ignore_patterns(".git", "build", "build_libcxx", "install", "log"))
        base = failures(root)
        print(f"baseline failures in the copy (not a git checkout): {len(base)}")
        A = root / "paper/arxiv_ref"
        files = sorted((A / "sections").glob("*.tex")) + [A / "figures" / n for n in
                ("e1_f1.tex", "e2_amplitude.tex", "e2_fpr.tex", "e3_flicker.tex", "overview.tex")]
        if tables:
            files += sorted((A / "tables").glob("*.tex")) + [A / "figures" / "e4_cost.tex"]
        total = caught = 0
        for p in files:
            rel = str(p.relative_to(A))
            orig = p.read_text(encoding="utf-8")
            masked = SKIP.sub(lambda m: " " * len(m.group(0)), orig)
            toks = [(m.start(), m.group(0)) for m in NUM.finditer(masked)]
            if rel.startswith("tables/") or rel.endswith("e4_cost.tex"):
                toks = toks[::3]
            n = c = 0
            for pos, tok in toks:
                p.write_text(orig[:pos] + bump(tok) + orig[pos + len(tok):], encoding="utf-8")
                new = failures(root) - base
                p.write_text(orig, encoding="utf-8")
                n += 1
                if new:
                    c += 1
                else:
                    ctx = orig[max(0, pos - 40):pos + len(tok) + 20].replace("\n", " ")
                    print(f"SURVIVOR {rel}: {tok!r} ...{ctx}...")
            print(f"{rel}: {n} numerals mutated, {c} caught, {n - c} survived", flush=True)
            total += n
            caught += c
        print(f"TOTAL: {total} numerals mutated, {caught} caught, {total - caught} survived")


if __name__ == "__main__":
    main()
