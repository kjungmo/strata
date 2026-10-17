#!/usr/bin/env python3
"""Mutation evidence for the phase 1b tests (implementer checklist item 8). Applies one textual mutant at a time to a
source file, rebuilds and runs the matching tests, and records killed / survived / build error. The source file is
restored after every mutant (and at exit). Writes results_p1b/mutation_record.md.

    python3 -I mutation_p1b.py --build-dir /tmp/strata_p1b_build --out results_p1b/mutation_record.md [--only cpp|py]
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))

# (file, old text, new text, note). Each `old` must occur exactly once in the file.
CPP = [
    ("p1b_core.hpp", "if (l3 / l1 > prm.ratio_max) continue;", "if (l3 / l1 < prm.ratio_max) continue;", "G1 3D flatness test inverted"),
    ("p1b_core.hpp", "if ((l2 - l3) / l1 < prm.mid_min) continue;", "if ((l2 - l3) / l1 > prm.mid_min) continue;", "G1 3D planarity test inverted"),
    ("p1b_core.hpp", "if (std::fabs(es.eigenvectors().col(0).z()) > prm.nz_max) continue;", "if (std::fabs(es.eigenvectors().col(0).z()) < prm.nz_max) continue;", "G1 vertical test inverted"),
    ("p1b_core.hpp", "if (std::sqrt(12.0 * l2) < prm.min_extent_m) continue;\n      res.thickness_m.push_back(std::sqrt(12.0 * l3));", "if (std::sqrt(12.0 * l2) > prm.min_extent_m) continue;\n      res.thickness_m.push_back(std::sqrt(12.0 * l3));", "G1 3D extent test inverted"),
    ("p1b_core.hpp", "if (l2 / l1 > prm.ratio_max) continue;                    // not straight", "if (l2 / l1 < prm.ratio_max) continue;                    // not straight", "G1 2D straightness inverted"),
    ("p1b_core.hpp", "if (std::sqrt(12.0 * l1) < prm.min_extent_m) continue;", "if (std::sqrt(12.0 * l1) > prm.min_extent_m) continue;", "G1 2D extent inverted"),
    ("p1b_core.hpp", "v.pass = r.accepted >= min_patches && v.p50 <= t50 && v.p90 <= t90;", "v.pass = r.accepted > min_patches && v.p50 <= t50 && v.p90 <= t90;", "G1 patch count boundary"),
    ("p1b_core.hpp", "v.pass = r.accepted >= min_patches && v.p50 <= t50 && v.p90 <= t90;", "v.pass = r.accepted >= min_patches && v.p50 < t50 && v.p90 <= t90;", "G1 median boundary"),
    ("p1b_core.hpp", "v.pass = r.accepted >= min_patches && v.p50 <= t50 && v.p90 <= t90;", "v.pass = r.accepted >= min_patches && v.p50 <= t50 && v.p90 < t90;", "G1 p90 boundary"),
    ("p1b_core.hpp", "v.pass = r.queries > 0 && v.matched_share >= min_share && v.p50 <= d50 && v.p90 <= d90;", "v.pass = r.queries > 0 && v.matched_share > min_share && v.p50 <= d50 && v.p90 <= d90;", "G2 matched share boundary"),
    ("p1b_core.hpp", "v.pass = r.queries > 0 && v.matched_share >= min_share && v.p50 <= d50 && v.p90 <= d90;", "v.pass = r.queries > 0 && v.matched_share >= min_share && v.p50 < d50 && v.p90 <= d90;", "G2 median boundary"),
    ("p1b_core.hpp", "v.pass = r.queries > 0 && v.matched_share >= min_share && v.p50 <= d50 && v.p90 <= d90;", "v.pass = r.queries > 0 && v.matched_share >= min_share && v.p50 <= d50 && v.p90 < d90;", "G2 p90 boundary"),
    ("p1b_core.hpp", "v.pass = r.queries > 0 && v.matched_share", "v.pass = r.queries >= 0 && v.matched_share", "G2 needs a query"),
    ("p1b_core.hpp", "if (lag < prm.lag_lo || lag > prm.lag_hi) continue;", "if (lag <= prm.lag_lo || lag > prm.lag_hi) continue;", "G2 lag lower bound"),
    ("p1b_core.hpp", "if (lag < prm.lag_lo || lag > prm.lag_hi) continue;", "if (lag < prm.lag_lo || lag >= prm.lag_hi) continue;", "G2 lag upper bound"),
    ("p1b_core.hpp", "if (best <= prm.d_cap_m * prm.d_cap_m) {", "if (best < prm.d_cap_m * prm.d_cap_m) {", "G2 cap boundary"),
    ("p1b_core.hpp", "if (s.count < prm.min_tls_points) continue;", "if (s.count <= prm.min_tls_points) continue;", "TLS occupancy threshold"),
    ("p1b_core.hpp", "if (s.minz - cmin <= static_cast<float>(prm.ground_height_m))", "if (s.minz - cmin < static_cast<float>(prm.ground_height_m))", "ground height boundary"),
    ("p1b_core.hpp", "if (tls.countOf(s.key) > 0) { ++c.hit_voxels_tls_present; continue; }", "if (tls.countOf(s.key) > 1) { ++c.hit_voxels_tls_present; continue; }", "clutter: TLS-present test"),
    ("p1b_core.hpp", "if ((dx || dy || dz) && tls.countOf(vi.pack(x + dx, y + dy, z + dz)) > 0) near = true;", "if (tls.countOf(vi.pack(x + dx, y + dy, z + dz)) > 0 && dx) near = true;", "clutter: buffer neighbourhood"),
    ("p1b_core.hpp", "if (!columns.find(columnKey(ctr.x(), ctr.y()))) { ++c.hit_voxels_unsupported; continue; }", "if (columns.find(columnKey(ctr.x(), ctr.y()))) { ++c.hit_voxels_unsupported; continue; }", "clutter: support rule inverted"),
    ("p1b_core.hpp", "auto keepFrac = [&](std::size_t n) { return n > prm.quota ? q / static_cast<double>(n) : 1.0; };", "auto keepFrac = [&](std::size_t n) { return n >= prm.quota ? q / static_cast<double>(n) : 1.0; };", "thinning quota boundary"),
    ("p1b_core.hpp", "if (c.spec.refined) { if (cheb > 1) touch(c, idx, 2); }", "if (c.spec.refined) { if (cheb >= 1) touch(c, idx, 2); }", "refined miss: distance rule"),
    ("p1b_core.hpp", "if (structure && cheb <= 1) c.graze[idx] = 1;", "if (structure && cheb < 1) c.graze[idx] = 1;", "grazing window flag"),
    ("p1b_core.hpp", "if (structure) { ++cnt_.structure_miss_events; if (cheb <= 1) ++cnt_.structure_miss_events_graze; }", "if (structure) { ++cnt_.structure_miss_events; if (cheb < 1) ++cnt_.structure_miss_events_graze; }", "grazing event counter"),
    ("p1b_core.hpp", "const bool hit = (f & 1) != 0, miss = !hit && (f & 2) != 0;", "const bool hit = (f & 1) != 0, miss = (f & 2) != 0;", "hit must win over miss"),
    ("p1b_core.hpp", "if (++c.steps_in_window < c.spec.W) continue;", "if (++c.steps_in_window <= c.spec.W) continue;", "window length"),
    ("p1b_core.hpp", "if (all[i].score < tie * best) continue;", "if (all[i].score <= tie * best) continue;", "offset tie band"),
    ("p1b_core.hpp", "if (pick < 0 || std::fabs(all[i].dt) < std::fabs(all[pick].dt) - 1e-12) pick = static_cast<int>(i);", "if (pick < 0 || std::fabs(all[i].dt) > std::fabs(all[pick].dt) - 1e-12) pick = static_cast<int>(i);", "offset prefers the smaller |dt|"),
    ("p1b_core.hpp", "bool hit = tls005.countOf(vi.pack(x, y, z)) >= min_points;", "bool hit = tls005.countOf(vi.pack(x, y, z)) > min_points;", "offset score: point threshold"),
    ("p1b_io.hpp", "if (r < opt.r_min || r > opt.r_max) { ++rep.points_out_of_range; continue; }", "if (r <= opt.r_min || r > opt.r_max) { ++rep.points_out_of_range; continue; }", "range window lower bound"),
    ("p1b_io.hpp", "if (r < opt.r_min || r > opt.r_max) { ++rep.points_out_of_range; continue; }", "if (r < opt.r_min || r >= opt.r_max) { ++rep.points_out_of_range; continue; }", "range window upper bound"),
    ("p1b_io.hpp", "const Eigen::Vector3d w = p.t + p.q * (ex.q * pl + ex.t);", "const Eigen::Vector3d w = p.t + p.q * (ex.q * pl);", "extrinsic translation"),
    ("p1b_io.hpp", "return (per_point ? s.ts[i] : scan_time) + opt.dt;", "return (per_point ? s.ts[i] : scan_time) - opt.dt;", "sign of the time offset"),
    ("p1b_io.hpp", "if (s_.size() < 2 || !(t >= s_.front().t) || t > s_.back().t) return false;", "if (s_.size() < 2 || !(t >= s_.front().t) || t >= s_.back().t) return false;", "track end"),
    ("p1b_io.hpp", "if (hi->t - lo->t > max_gap) return false;", "if (hi->t - lo->t >= max_gap) return false;", "gap boundary"),
    ("p1b_io.hpp", "out.q = lo->pose.q.slerp(a, hi->pose.q);", "out.q = lo->pose.q;", "orientation interpolation"),
    ("p1b_io.hpp", "return nearest(x, y) <= r_;", "return nearest(x, y) < r_;", "area boundary is inside"),
    ("p1b_io.hpp", "const double half_diag = 0.7072 * kCell;", "const double half_diag = 0.3 * kCell;", "area memo half diagonal"),
    ("p1b_io.hpp", "if (!area.contains(v[0], v[1])) { ++t.points_discarded; continue; }", "if (area.contains(v[0], v[1])) { ++t.points_discarded; continue; }", "area filter inverted"),
    ("p1b_io.hpp", "if (s.count == 0 || v[2] < s.minz) s.minz = v[2];\n        if (s.count < 0xFFFFFFFFu) ++s.count;\n      }\n      auto& c", "if (s.count == 0 || v[2] > s.minz) s.minz = v[2];\n        if (s.count < 0xFFFFFFFFu) ++s.count;\n      }\n      auto& c", "TLS lowest z"),
    ("p1b_io.hpp", "if (e.method == 0) {\n      if (e.csize != e.usize)", "if (e.method == 1) {\n      if (e.csize != e.usize)", "zip stored method"),
    ("p1b_io.hpp", "if (buf.size() < need) fail(name, \"truncated: \"", "if (buf.size() <= need) fail(name, \"truncated: \"", "PCD size: exact size accepted"),
    ("p1b_io.hpp", "if (buf.size() - need > 65536) fail(", "if (buf.size() - need > 6553600) fail(", "PCD zero tail: length limit"),
    ("p1b_io.hpp", "if (buf[i] != 0) fail(name, \"non-zero byte after the last record", "if (false) fail(name, \"non-zero byte after the last record", "PCD zero tail: content check"),
]

PY = [
    ("analyze_p1b.py", "m0 = max(0.05, ceil_005(q_c))", "m0 = max(0.05, floor_005(q_c))", "rounding direction of m0"),
    ("analyze_p1b.py", "m1 = min(0.95, floor_005(q_s))", "m1 = min(0.95, ceil_005(q_s))", "rounding direction of m1"),
    ("analyze_p1b.py", "m0 = max(0.05, ceil_005(q_c))", "m0 = max(0.0, ceil_005(q_c))", "lower bound of m0"),
    ("analyze_p1b.py", "m1 = min(0.95, floor_005(q_s))", "m1 = min(1.0, floor_005(q_s))", "upper bound of m1"),
    ("analyze_p1b.py", "return round(math.ceil(x / 0.05 - 1e-9) * 0.05, 2)", "return round(math.ceil(x / 0.05) * 0.05, 2)", "ceil tolerance"),
    ("analyze_p1b.py", "return round(math.floor(x / 0.05 + 1e-9) * 0.05, 2)", "return round(math.floor(x / 0.05) * 0.05, 2)", "floor tolerance"),
    ("analyze_p1b.py", "qs_b[b] = a1.pct_weighted(us, ws, 10.0)", "qs_b[b] = a1.pct_weighted(us, ws, 20.0)", "bootstrap percentile of q_s"),
    ("analyze_p1b.py", "qc_b[b] = a1.pct_weighted(uc, wc, 90.0)", "qc_b[b] = a1.pct_weighted(uc, wc, 80.0)", "bootstrap percentile of q_c"),
    ("analyze_p1b.py", '"yes" if gap >= GAP_RULE else "no"', '"yes" if gap > GAP_RULE else "no"', "gap rule boundary in the table"),
    ("freeze_prereg.py", "elif num(r[\"gap\"]) < GAP_RULE:", "elif num(r[\"gap\"]) <= GAP_RULE:", "2D gap rule boundary"),
    ("freeze_prereg.py", "if valid and gt >= GAP_RULE:", "if valid and gt > GAP_RULE:", "3D branch T boundary"),
    ("freeze_prereg.py", "elif valid and gr >= GAP_RULE:", "elif valid and gr > GAP_RULE:", "3D branch R boundary"),
    ("freeze_prereg.py", "valid = all(p for _, p, _ in checks)", "valid = any(p for _, p, _ in checks)", "all checks must pass"),
    ("freeze_prereg.py", "equiv_ok = len(lines) == 4 and all(", "equiv_ok = len(lines) >= 0 and all(", "equivalence needs four tables"),
    ("freeze_prereg.py", "m0 = max(0.05, round(math.ceil(q_c / 0.05 - 1e-9) * 0.05, 2))", "m0 = max(0.05, round(math.floor(q_c / 0.05 - 1e-9) * 0.05, 2))", "freeze rounding of m0"),
    ("freeze_prereg.py", "m1 = min(0.95, round(math.floor(q_s / 0.05 + 1e-9) * 0.05, 2))", "m1 = min(0.95, round(math.ceil(q_s / 0.05 + 1e-9) * 0.05, 2))", "freeze rounding of m1"),
    ("freeze_prereg.py", "for seq in (\"lil_mapping\", \"lil_static_0\", \"lil_dynamics_0\"):", "for seq in (\"lil_static_0\", \"lil_dynamics_0\"):", "2D needs the mapping gate"),
    ("derive_defaults.py", "if l >= A - EPS:\n                grad += p", "if l > A - EPS:\n                grad += p", "graduation boundary"),
    ("derive_defaults.py", "elif n >= T and l <= B + EPS:", "elif n > T and l <= B + EPS:", "reject only once n >= T"),
    ("derive_defaults.py", "elif n >= T and l <= B + EPS:", "elif l <= B + EPS:", "gate removed"),
    ("derive_defaults.py", "\"ceiling\": A + 2 * l_hit", "\"ceiling\": A + 1 * l_hit", "ceiling"),
    ("derive_defaults.py", "if u[n] <= limit:", "if u[n] < limit:", "n_max boundary"),
    ("derive_defaults.py", "mstar = -l_miss / (l_hit - l_miss)", "mstar = l_miss / (l_hit - l_miss)", "zero-drift rate"),
]


# Mutants that change behaviour only at exact floating point equality, which the tolerance EPS = 1e-9 of the dynamic program
# (graduation) or the discrete grid of n (n_max) makes unreachable. They are reported as survivors, with this reason.
EQUIVALENT = {
    "graduation boundary": "differs only if l equals A to within 1e-9 by floating point coincidence; the tolerance EPS is there to absorb that",
    "n_max boundary": "differs only if the undecided probability equals 0.03 exactly",
}


def read(p):
    with open(p) as f:
        return f.read()


def write(p, s):
    with open(p, "w") as f:
        f.write(s)


def run(cmd, cwd=None, timeout=1800):
    t0 = time.time()
    p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    return p.returncode, p.stdout.decode(errors="replace"), time.time() - t0


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--jobs", default="2")
    a = ap.parse_args(argv)
    rows = []
    todo = [("cpp", m) for m in CPP] + [("py", m) for m in PY]
    if a.only:
        todo = [t for t in todo if t[0] == a.only]
    backups = {}
    try:
        for kind, (fn, old, new, note) in todo:
            path = os.path.join(HERE, fn)
            src = read(path)
            if src.count(old) != 1:
                rows.append((kind, fn, note, "NOT APPLIED (pattern found %d times)" % src.count(old)))
                continue
            backups[path] = src
            write(path, src.replace(old, new))
            try:
                if kind == "cpp":
                    rc, out, _ = run(["cmake", "--build", a.build_dir, "-j", a.jobs, "--target", "test_p1b"])
                    if rc != 0:
                        rows.append((kind, fn, note, "BUILD ERROR"))
                        continue
                    rc, out, _ = run([os.path.join(a.build_dir, "realdata", "test_p1b")])
                else:
                    rc, out, _ = run([sys.executable, "-I", "-m", "unittest", "discover", "-s", HERE, "-p", "test_p1b_analysis.py"], cwd=HERE)
                res = "killed" if rc != 0 else ("SURVIVED (equivalent in practice: %s)" % EQUIVALENT[note] if note in EQUIVALENT else "SURVIVED")
                rows.append((kind, fn, note, res))
            finally:
                write(path, backups.pop(path))
    finally:
        for p, s in backups.items():
            write(p, s)
    if any(k == "cpp" for k, *_ in todo):
        run(["cmake", "--build", a.build_dir, "-j", a.jobs, "--target", "test_p1b"])      # rebuild the unmutated tests
    killed = sum(1 for r in rows if r[3] == "killed")
    surv = [r for r in rows if r[3].startswith("SURVIVED")]
    other = [r for r in rows if r[3] != "killed" and not r[3].startswith("SURVIVED")]
    L = ["# Mutation record, phase 1b tests", "",
         "Generated by `mutation_p1b.py` on %s. One textual mutant at a time; the C++ tests are `test_p1b`, the Python tests `test_p1b_analysis.py`." % time.strftime("%Y-%m-%dT%H:%M:%S%z"),
         "", "Mutants: %d, killed: %d, survived: %d, other: %d." % (len(rows), killed, len(surv), len(other)), "",
         "| file | mutant | result |", "|---|---|---|"]
    for kind, fn, note, res in rows:
        L.append("| %s | %s | %s |" % (fn, note, res))
    L.append("")
    write(a.out, "\n".join(L))
    print("mutants %d killed %d survived %d other %d" % (len(rows), killed, len(surv), len(other)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
