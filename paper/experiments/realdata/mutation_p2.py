#!/usr/bin/env python3
"""Mutation evidence for the phase 2 engine tests (implementer checklist item 8).

Applies one textual mutant at a time to a SCRATCH COPY of strata_core (the repository is never
modified), rebuilds, runs the gtest binary and the defaults check, and records killed / survived /
build error. Writes results_p2/mutation_record.md.

    python3 -I mutation_p2.py --scratch /tmp/strata_p2_mut --out results_p2/mutation_record.md [--only engine|defaults|tests]

Three sweeps:
  engine    one mutant per comparison operator, constant and branch of the sequential test, its
            derivation, its validation, the classifier and the Grid2D render (MUTANTS below);
  defaults  mutants of the files that state the shipped defaults (check_defaults.py must fail);
  tests     every numeric literal inside an EXPECT_/ASSERT_ line of test_sprt.cpp is changed once;
            a survivor is listed with the reason it is allowed (a tolerance or a count that no
            assertion pins) or is a gap.
Each `old` text must occur exactly once in its file.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
LM = "src/layered_map.cpp"
LH = "include/strata_core/layered_map.hpp"

# (file in strata_core, old, new, note, equivalent?)  equivalent: documented reason, or None.
MUTANTS = [
    # ---- derivation
    (LM, "d.l_hit = std::log(p.m1 / p.m0);", "d.l_hit = std::log(p.m0 / p.m1);", "l_hit inverted", None),
    (LM, "d.l_miss = std::log((1.0 - p.m1) / (1.0 - p.m0));", "d.l_miss = std::log((1.0 - p.m0) / (1.0 - p.m1));", "l_miss inverted", None),
    (LM, "d.A = std::log((1.0 - p.beta) / p.alpha);", "d.A = std::log(1.0 / p.alpha);", "A without (1 - beta)", None),
    (LM, "d.B = std::log(p.beta / (1.0 - p.alpha));", "d.B = std::log(p.alpha / (1.0 - p.beta));", "B with alpha and beta swapped", None),
    (LM, "d.B = std::log(p.beta / (1.0 - p.alpha));", "d.B = std::log(p.beta);", "B without (1 - alpha)", None),
    (LM, "d.ceiling = d.A + 2.0 * d.l_hit;", "d.ceiling = d.A + 1.0 * d.l_hit;", "ceiling one hit lower", None),
    (LM, "d.ceiling = d.A + 2.0 * d.l_hit;", "d.ceiling = d.A + 3.0 * d.l_hit;", "ceiling three hits", None),
    (LM, "d.demote_level = d.ceiling - d.A;", "d.demote_level = d.ceiling - d.A - 1.0;", "demotion level lower", None),
    (LM, "d.demote_level = d.ceiling - d.A;", "d.demote_level = d.ceiling - 0.5 * d.A;", "demotion level higher", None),
    (LM, "d.m_star = -d.l_miss / (d.l_hit - d.l_miss);", "d.m_star = d.l_miss / (d.l_hit - d.l_miss);", "m* sign", None),
    (LM, "while (l < d.A) { l += d.l_hit;", "while (l <= d.A) { l += d.l_hit;", "latency loop <=", None),
    (LM, "while (l > d.demote_level) { l += d.l_miss; ++d.demote_misses; }", "while (l >= d.demote_level) { l += d.l_miss; ++d.demote_misses; }", "demote_misses loop >=",
     "equivalent: a step lands exactly on the level only for a rational ratio of the increments"),
    # ---- validation
    (LM, "p.m0 > 0.0 && p.m0 < p.m1 && p.m1 < 1.0", "p.m0 >= 0.0 && p.m0 < p.m1 && p.m1 < 1.0", "m0 > 0", None),
    (LM, "p.m0 > 0.0 && p.m0 < p.m1 && p.m1 < 1.0", "p.m0 > 0.0 && p.m0 <= p.m1 && p.m1 < 1.0", "m0 < m1", None),
    (LM, "p.m0 > 0.0 && p.m0 < p.m1 && p.m1 < 1.0", "p.m0 > 0.0 && p.m0 < p.m1 && p.m1 <= 1.0", "m1 < 1", None),
    (LM, "p.alpha > 0.0 && p.beta > 0.0 && p.alpha + p.beta < 1.0", "p.alpha >= 0.0 && p.beta > 0.0 && p.alpha + p.beta < 1.0", "alpha > 0", None),
    (LM, "p.alpha > 0.0 && p.beta > 0.0 && p.alpha + p.beta < 1.0", "p.alpha > 0.0 && p.beta >= 0.0 && p.alpha + p.beta < 1.0", "beta > 0", None),
    (LM, "p.alpha > 0.0 && p.beta > 0.0 && p.alpha + p.beta < 1.0", "p.alpha > 0.0 && p.beta > 0.0 && p.alpha + p.beta <= 1.0", "alpha + beta < 1", None),
    (LM, "if (p.n_max_undecided < 0)", "if (p.n_max_undecided <= 0)", "n_max >= 0", None),
    (LM, "if (!(fin(p.sprt_ceiling) && p.sprt_ceiling >= d.A))", "if (!(p.sprt_ceiling >= d.A))", "ceiling finite", None),
    (LM, "if (!(fin(p.sprt_ceiling) && p.sprt_ceiling >= d.A))", "if (!(fin(p.sprt_ceiling) && p.sprt_ceiling > d.A))", "ceiling >= A", None),
    (LM, "if (std::isnan(p.sprt_ceiling)) {", "if (false) {", "ceiling NaN derives", None),
    (LM, "if (params_.evidence_model == EvidenceModel::Sprt) {\n    sprt_ = deriveSprt(params_);", "if (params_.evidence_model != EvidenceModel::Sprt) {\n    sprt_ = deriveSprt(params_);", "model switch", None),
    (LM, "if (use_sprt_) { endWindowSprt(); return; }", "if (false) { endWindowSprt(); return; }", "sprt path never taken", None),
    # ---- update, graduation, demotion, rejection
    (LM, "e.log_odds += occ ? d.l_hit : d.l_miss;", "e.log_odds += occ ? d.l_miss : d.l_hit;", "increments swapped", None),
    (LM, "if (e.log_odds > d.ceiling) e.log_odds = d.ceiling;\n      e.observations++;", "e.observations++;", "no ceiling", None),
    (LM, "if (e.log_odds > d.ceiling) e.log_odds = d.ceiling;\n      e.observations++;", "if (e.log_odds >= d.ceiling) e.log_odds = d.ceiling;\n      e.observations++;", "ceiling clamp >=",
     "equivalent: assigns the value it already has"),
    (LM, "if (e.observations >= gate && e.log_odds < d.B) e.log_odds = d.B;", "if (e.log_odds < d.B) e.log_odds = d.B;", "floor before the gate (the rejected design)", None),
    (LM, "if (e.observations >= gate && e.log_odds < d.B) e.log_odds = d.B;", "if (e.observations > gate && e.log_odds < d.B) e.log_odds = d.B;", "floor from n > T", None),
    (LM, "if (e.observations >= gate && e.log_odds < d.B) e.log_odds = d.B;", "if (e.observations >= gate && e.log_odds > d.B) e.log_odds = d.B;", "floor direction", None),
    (LM, "if (e.observations >= gate && e.log_odds < d.B) e.log_odds = d.B;\n", "\n", "no floor at all", None),
    (LM, "if (!e.graduated && !periodic && e.log_odds >= d.A) {\n      e.graduated = true;", "if (!e.graduated && e.log_odds >= d.A) {\n      e.graduated = true;", "graduation ignores the Periodic veto", None),
    (LM, "if (!e.graduated && !periodic && e.log_odds >= d.A) {\n      e.graduated = true;", "if (!e.graduated && !periodic && e.log_odds > d.A) {\n      e.graduated = true;", "graduate at l > A",
     "equivalent: l = A exactly does not occur for irrational increments"),
    (LM, "if (!e.graduated && !periodic && e.log_odds >= d.A) {\n      e.graduated = true;", "if (!e.graduated && !periodic && e.log_odds >= d.A + 0.5) {\n      e.graduated = true;", "graduate at A + 0.5", None),
    (LM, "e.graduated = true;\n      e.log_odds = d.ceiling;", "e.graduated = true;", "no restart at the ceiling", None),
    (LM, "e.graduated = true;\n      e.log_odds = d.ceiling;", "e.graduated = true;\n      e.log_odds = d.A;", "restart at A, not the ceiling", None),
    (LM, "if (periodic) e.graduated = false;\n      else if", "if (false) e.graduated = false;\n      else if", "periodic cell not demoted", None),
    (LM, "else if (e.log_odds <= d.demote_level) { e.graduated = false; e.log_odds = 0.0; }", "else if (e.log_odds < d.demote_level) { e.graduated = false; e.log_odds = 0.0; }", "demote at l < level",
     "equivalent: l = level exactly does not occur for irrational increments"),
    (LM, "else if (e.log_odds <= d.demote_level) { e.graduated = false; e.log_odds = 0.0; }", "else if (e.log_odds <= d.demote_level) { e.graduated = false; }", "no restart after the reverse test", None),
    (LM, "else if (e.log_odds <= d.demote_level) { e.graduated = false; e.log_odds = 0.0; }", "else if (e.log_odds <= d.demote_level + 1.0) { e.graduated = false; e.log_odds = 0.0; }", "demote one step early", None),
    (LM, "else if (e.log_odds <= d.demote_level) { e.graduated = false; e.log_odds = 0.0; }", "else if (e.log_odds >= d.demote_level) { e.graduated = false; e.log_odds = 0.0; }", "demote direction", None),
    (LM, "const bool rejected = !e.graduated && e.observations >= gate && e.log_odds <= d.B;", "const bool rejected = !e.graduated && e.observations > gate && e.log_odds <= d.B;", "reject from n > T", None),
    (LM, "const bool rejected = !e.graduated && e.observations >= gate && e.log_odds <= d.B;", "const bool rejected = !e.graduated && e.log_odds <= d.B;", "reject before the gate", None),
    (LM, "const bool rejected = !e.graduated && e.observations >= gate && e.log_odds <= d.B;", "const bool rejected = !e.graduated && e.observations >= gate && e.log_odds < d.B;", "reject at l < B", None),
    (LM, "const bool rejected = !e.graduated && e.observations >= gate && e.log_odds <= d.B;", "const bool rejected = e.observations >= gate && e.log_odds <= d.B;", "graduated cells can be rejected", None),
    (LM, "if (rejected && amp < params_.periodic_amplitude_min) {", "if (rejected && amp > params_.periodic_amplitude_min) {", "keep non-candidates", None),
    (LM, "if (rejected && amp < params_.periodic_amplitude_min) {", "if (rejected) {", "no periodic-candidate guard", None),
    (LM, "if (rejected && amp < params_.periodic_amplitude_min) {\n      periodicity_.erase(it->first);", "if (rejected && amp < params_.periodic_amplitude_min) {", "periodicity history kept after rejection", None),
    (LM, "const int gate = std::max(1, params_.periodicity.period_windows);\n  for", "const int gate = std::max(1, params_.periodicity.period_windows) + 1;\n  for", "gate T + 1", None),
    (LM, "const int gate = std::max(1, params_.periodicity.period_windows);\n  for", "const int gate = 1;\n  for", "gate 1", None),
    # ---- read-out
    (LM, "if (n >= std::max(1, params_.periodicity.period_windows) && it->second.log_odds <= sprt_.B)", "if (n > std::max(1, params_.periodicity.period_windows) && it->second.log_odds <= sprt_.B)", "Unknown from n > T", None),
    (LM, "if (n >= std::max(1, params_.periodicity.period_windows) && it->second.log_odds <= sprt_.B)", "if (n >= std::max(1, params_.periodicity.period_windows) && it->second.log_odds < sprt_.B)", "Unknown at l < B", None),
    (LM, "if (n >= std::max(1, params_.periodicity.period_windows) && it->second.log_odds <= sprt_.B)", "if (n >= std::max(1, params_.periodicity.period_windows) || it->second.log_odds <= sprt_.B)", "Unknown with ||", None),
    (LM, "if (params_.n_max_undecided > 0 && n >= params_.n_max_undecided) return CellClass::Intermittent;", "if (params_.n_max_undecided > 0 && n > params_.n_max_undecided) return CellClass::Intermittent;", "Intermittent from n > n_max", None),
    (LM, "if (params_.n_max_undecided > 0 && n >= params_.n_max_undecided) return CellClass::Intermittent;", "if (params_.n_max_undecided >= 0 && n >= params_.n_max_undecided) return CellClass::Intermittent;", "n_max 0 means never", None),
    (LM, "if (params_.n_max_undecided > 0 && n >= params_.n_max_undecided) return CellClass::Intermittent;", "if (params_.n_max_undecided > 0 || n >= params_.n_max_undecided) return CellClass::Intermittent;", "Intermittent with ||", None),
    (LM, "if (classify(kv.first) == CellClass::Intermittent) o.push_back(kv.first);", "if (classify(kv.first) == CellClass::Transient) o.push_back(kv.first);", "intermittentCells wrong class", None),
    (LM, "int LayeredMap::observations(CellId id) const {\n  auto it = cells_.find(id);\n  return it == cells_.end() ? 0 : it->second.observations;", "int LayeredMap::observations(CellId id) const {\n  auto it = cells_.find(id);\n  return it == cells_.end() ? 0 : it->second.observations + 1;", "observations() off by one", None),
    # ---- defaults in the header
    (LH, "EvidenceModel evidence_model{EvidenceModel::Sprt};", "EvidenceModel evidence_model{EvidenceModel::Legacy};", "default model legacy", None),
    (LH, "double m0{0.20};", "double m0{0.25};", "default m0", None),
    (LH, "double m1{0.70};", "double m1{0.75};", "default m1", None),
    (LH, "double alpha{0.01};", "double alpha{0.05};", "default alpha", None),
    (LH, "double beta{0.01};", "double beta{0.05};", "default beta", None),
    (LH, "int n_max_undecided{80};", "int n_max_undecided{64};", "default n_max_undecided", None),
    # ---- backends
    ("src/grid2d_backend.cpp", "for(CellId id: layered_.intermittentCells()) if(layered_.occupancyProb(id)>0.5) put(id,50);", "for(CellId id: layered_.intermittentCells()) if(layered_.occupancyProb(id)<0.5) put(id,50);", "Intermittent render direction", None),
    ("src/grid2d_backend.cpp", "  for(CellId id: layered_.intermittentCells()) if(layered_.occupancyProb(id)>0.5) put(id,50);\n", "", "Intermittent not rendered", None),
    ("include/strata_core/grid2d_backend.hpp", "std::size_t intermittentCellCount() const override { return layered_.intermittentCells().size(); }", "std::size_t intermittentCellCount() const override { return 0; }", "Grid2D count", None),
    ("include/strata_core/voxel3d_backend.hpp", "std::size_t intermittentCellCount() const override { return layered_.intermittentCells().size(); }", "std::size_t intermittentCellCount() const override { return 0; }", "Voxel3D count", None),
    # ---- legacy path (existing tests; the bit-equivalence script is the second witness)
    (LM, "      e.log_odds *= params_.survival_decay;\n", "", "legacy: no survival decay", None),
    (LM, "      if (occ) e.log_odds += params_.l_hit;\n      else if (freed) e.log_odds += params_.l_miss;", "      if (occ) e.log_odds += params_.l_hit;\n      else if (freed) e.log_odds += params_.l_hit;", "legacy: miss adds l_hit", None),
]

# Mutants of the files that state the shipped defaults. Run with check_defaults.py; it must exit 1.
DEFAULTS = [
    ("strata_params_grid2d", "strata/params/grid2d.yaml", "    m0: 0.20", "    m0: 0.25", "grid2d m0"),
    ("strata_params_grid2d", "strata/params/grid2d.yaml", "    m1: 0.70", "    m1: 0.65", "grid2d m1"),
    ("strata_params_grid2d", "strata/params/grid2d.yaml", "    alpha: 0.01 ", "    alpha: 0.02 ", "grid2d alpha"),
    ("strata_params_grid2d", "strata/params/grid2d.yaml", "    beta: 0.01 ", "    beta: 0.02 ", "grid2d beta"),
    ("strata_params_grid2d", "strata/params/grid2d.yaml", "    n_max_undecided: 80", "    n_max_undecided: 64", "grid2d n_max_undecided"),
    ("strata_params_grid2d", "strata/params/grid2d.yaml", 'evidence_model: "sprt"', 'evidence_model: "legacy"', "grid2d evidence_model"),
    ("strata_params_grid2d", "strata/params/grid2d.yaml", 'evidence_calibration: "measured_2d"', 'evidence_calibration: "fallback_2d_pair"', "grid2d calibration label"),
    ("strata_params_voxel3d", "strata/params/voxel3d.yaml", "    m0: 0.20", "    m0: 0.30", "voxel3d m0"),
    ("strata_params_voxel3d", "strata/params/voxel3d.yaml", 'evidence_calibration: "fallback_2d_pair"', 'evidence_calibration: "measured_2d"', "voxel3d calibration label"),
    ("strata_params_grid2d", "strata/params/grid2d.yaml", "    l_hit: 0.85", "    l_hit: 0.9", "grid2d legacy l_hit against the library"),
    ("strata_params_voxel3d", "strata/params/voxel3d.yaml", "    period_windows: 24", "    period_windows: 12", "voxel3d period_windows against the library"),
    ("prereg", "paper/experiments/realdata/PREREG.json", '"m0": 0.2,', '"m0": 0.25,', "PREREG m0"),
    ("prereg", "paper/experiments/realdata/PREREG.json", '"n_max_undecided": 80,', '"n_max_undecided": 96,', "PREREG n_max_undecided"),
    ("node", "strata/src/mapping_node.cpp", 'declare_parameter<double>("m0", def.m0)', 'declare_parameter<double>("m0", 0.2)', "node m0 from a literal"),
    ("node", "strata/src/mapping_node.cpp", 'declare_parameter<std::string>("evidence_model", "sprt")', 'declare_parameter<std::string>("evidence_model", "legacy")', "node evidence_model default"),
    ("header", "strata_core/include/strata_core/layered_map.hpp", "double m1{0.70};", "double m1{0.65};", "library m1 against YAML and PREREG"),
]


def sh(cmd, cwd=None, timeout=1800):
    r = subprocess.run(cmd, cwd=cwd, shell=isinstance(cmd, str), capture_output=True, text=True, timeout=timeout)
    return r.returncode, r.stdout + r.stderr


def build_and_test(scr, jobs):
    b = os.path.join(scr, "build")
    rc, out = sh(["cmake", "--build", b, "-j%d" % jobs])
    if rc != 0:
        return "build error", [l for l in out.splitlines() if "error" in l][:2]
    rc, out = sh([os.path.join(b, "strata_core_tests")], cwd=scr)
    failed = sorted(set(re.findall(r"\[  FAILED  \] (\S+)", out)))
    failed = [f for f in failed if "." in f and not f.endswith(")")]
    rc2, out2 = sh(["ctest", "-R", "defaults_match_yaml_and_prereg"], cwd=b)
    if rc2 != 0:
        failed.append("ctest:defaults_match_yaml_and_prereg")
    return ("killed" if (rc != 0 or failed) else "survived"), failed


def setup(scr, jobs):
    if os.path.exists(scr):
        shutil.rmtree(scr)
    os.makedirs(scr)
    for rel in ("strata_core", "strata/params", "strata/src/mapping_node.cpp", "paper/experiments/realdata/PREREG.json"):
        src = os.path.join(REPO, rel)
        dst = os.path.join(scr, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.isdir(src):
            shutil.copytree(src, dst, ignore=shutil.ignore_patterns("build", "__pycache__"))
        else:
            shutil.copy(src, dst)
    # strata_core's CMake finds the repository root one level up from strata_core: use the scratch root
    b = os.path.join(scr, "build")
    os.makedirs(b)
    rc, out = sh(["cmake", "-S", os.path.join(scr, "strata_core"), "-B", b, "-DSTRATA_CORE_BUILD_TESTS=ON", "-DCMAKE_BUILD_TYPE=Release"])
    if rc != 0:
        raise SystemExit("cmake failed:\n" + out)
    st, failed = build_and_test(scr, jobs)
    if st != "survived":
        raise SystemExit("the unmutated scratch copy does not pass: %s %s" % (st, failed))


def apply(path, old, new):
    s = open(path).read()
    if s.count(old) != 1:
        raise SystemExit("mutant text occurs %d times in %s: %r" % (s.count(old), path, old[:70]))
    open(path, "w").write(s.replace(old, new))
    return s


def engine(scr, jobs, rows):
    for rel, old, new, note, equiv in MUTANTS:
        path = os.path.join(scr, "strata_core", rel)
        orig = apply(path, old, new)
        try:
            st, failed = build_and_test(scr, jobs)
        finally:
            open(path, "w").write(orig)
        rows.append(("engine", note, rel, st, failed, equiv))
        print("%-9s %-52s %s %s" % (st, note[:52], rel, ",".join(failed[:3])), flush=True)


def defaults(scr, rows):
    for _, rel, old, new, note in DEFAULTS:
        path = os.path.join(scr, rel)
        orig = apply(path, old, new)
        try:
            rc, out = sh([sys.executable, "-I", os.path.join(scr, "strata_core/test/check_defaults.py"), scr])
        finally:
            open(path, "w").write(orig)
        st = "killed" if rc != 0 else "survived"
        rows.append(("defaults", note, rel, st, [out.strip().splitlines()[1].strip()] if rc != 0 and len(out.strip().splitlines()) > 1 else [], None))
        print("%-9s %s" % (st, note), flush=True)
    rc, out = sh([sys.executable, "-I", os.path.join(scr, "strata_core/test/check_defaults.py"), scr])
    if rc != 0:
        raise SystemExit("unmutated defaults check fails: " + out)


NUM = re.compile(r"(?<![\w.])(\d+\.\d+|\d+)(?![\w.])")


def tests(scr, jobs, rows):
    path = os.path.join(scr, "strata_core", "test", "test_sprt.cpp")
    orig = open(path).read()
    lines = orig.split("\n")
    work = []
    for i, line in enumerate(lines):
        st = line.lstrip()
        if not (st.startswith("EXPECT_") or st.startswith("ASSERT_")):
            continue
        code = line.split("//")[0]
        # skip string literals
        masked = re.sub(r'"[^"]*"', lambda m: " " * len(m.group(0)), code)
        for m in NUM.finditer(masked):
            work.append((i, m.start(), m.group(0)))
    print("test literals to mutate:", len(work), flush=True)
    for i, pos, tok in work:
        new = str(int(tok) + 1) if tok.isdigit() else repr(float(tok) * 1.5 + 0.001)
        mutated = lines[:]
        mutated[i] = lines[i][:pos] + new + lines[i][pos + len(tok):]
        open(path, "w").write("\n".join(mutated))
        try:
            b = os.path.join(scr, "build")
            rc, out = sh(["cmake", "--build", b, "-j%d" % jobs, "--target", "strata_core_tests"])
            if rc != 0:
                st, failed = "build error", []
            else:
                rc, out = sh([os.path.join(b, "strata_core_tests"), "--gtest_filter=Sprt.*"], cwd=scr)
                failed = sorted(set(re.findall(r"\[  FAILED  \] (\S+)", out)))
                st = "killed" if (rc != 0 or failed) else "survived"
        finally:
            open(path, "w").write(orig)
        rows.append(("tests", "line %d: %s -> %s" % (i + 1, tok, new), lines[i].strip()[:90], st, failed, None))
        print("%-9s line %d %s -> %s" % (st, i + 1, tok, new), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scratch", default="/tmp/strata_p2_mut")
    ap.add_argument("--out", default=os.path.join(HERE, "results_p2", "mutation_record.md"))
    ap.add_argument("--only", choices=["engine", "defaults", "tests"])
    ap.add_argument("--jobs", type=int, default=4)
    a = ap.parse_args()
    t0 = time.time()
    setup(a.scratch, a.jobs)
    rows = []
    if a.only in (None, "engine"):
        engine(a.scratch, a.jobs, rows)
    if a.only in (None, "defaults"):
        defaults(a.scratch, rows)
    if a.only in (None, "tests"):
        tests(a.scratch, a.jobs, rows)
    with open(a.out, "w") as f:
        f.write("# Mutation record, phase 2 (strata_core 0.2.0)\n\n")
        f.write("Made by `mutation_p2.py` on a scratch copy; the repository was not modified. %d mutants, %.0f s.\n\n" % (len(rows), time.time() - t0))
        for kind in ("engine", "defaults", "tests"):
            sel = [r for r in rows if r[0] == kind]
            if not sel:
                continue
            killed = sum(1 for r in sel if r[3] == "killed")
            surv = [r for r in sel if r[3] == "survived"]
            err = [r for r in sel if r[3] == "build error"]
            f.write("## %s: %d mutants, %d killed, %d survived, %d build errors\n\n" % (kind, len(sel), killed, len(surv), len(err)))
            f.write("| mutant | where | result | killed by (first) |\n|---|---|---|---|\n")
            for r in sel:
                extra = ("; EQUIVALENT: " + r[5]) if (r[3] == "survived" and r[5]) else ""
                f.write("| %s | %s | %s%s | %s |\n" % (r[1].replace("|", "/"), r[2].replace("|", "/"), r[3], extra, ", ".join(r[4][:3])))
            f.write("\n")
    print("wrote", a.out)


if __name__ == "__main__":
    main()
