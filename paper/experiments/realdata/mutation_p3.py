#!/usr/bin/env python3
"""Mutation evidence for the phase 3 tests (implementer checklist item 8). Applies one textual mutant at a time to a COPY of the
sources (the worktree is never touched), rebuilds the matching tests and records killed / survived / build error.

    python3 -I mutation_p3.py --scratch DIR --out results_p3/mutation_record.md [--only cpp|py] [--check] [--jobs 2]

DIR is a scratch directory; it gets a `git archive` copy of HEAD (plus the current realdata sources) and its own build tree.
--check only verifies that every pattern occurs exactly once.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
RD = "paper/experiments/realdata"

# (file, old text, new text, note). Each `old` must occur exactly once in the file.
CPP = [
    # ---- arms
    ("p3_arms.hpp", "a.l_miss = -a.l_hit;", "a.l_miss = a.l_hit;", "symmetric: sign of l_miss"),
    ("p3_arms.hpp", "a.l_hit = logit(0.7);", "a.l_hit = logit(0.6);", "octomap: p_hit"),
    ("p3_arms.hpp", "a.l_miss = logit(0.4);", "a.l_miss = logit(0.45);", "octomap: p_miss"),
    ("p3_arms.hpp", "a.l_min = logit(0.12);", "a.l_min = logit(0.15);", "octomap: lower clamp"),
    ("p3_arms.hpp", "a.l_max = logit(0.97);", "a.l_max = logit(0.9);", "octomap: upper clamp"),
    ("p3_arms.hpp", "a.l_hit = std::log(9.0); a.l_miss = -std::log(9.0);", "a.l_hit = std::log(8.0); a.l_miss = -std::log(9.0);", "elite_like: hit factor"),
    ("p3_arms.hpp", "a.l_hit = std::log(9.0); a.l_miss = -std::log(9.0);", "a.l_hit = std::log(9.0); a.l_miss = -std::log(8.0);", "elite_like: miss factor"),
    ("p3_arms.hpp", "a.l_min = -50.0; a.l_max = 50.0; a.decay = 1.0;", "a.l_min = -5.0; a.l_max = 5.0; a.decay = 1.0;", "elite_like: clamps"),
    ("p3_arms.hpp", "a.prune_prob = 0.0; a.binary = true;\n    a.source = \"OctoMap", "a.prune_prob = 0.0; a.binary = false;\n    a.source = \"OctoMap", "octomap: binary flag"),
    ("p3_arms.hpp", "a.prune_prob = 0.0; a.binary = true;\n    a.source = \"ELite", "a.prune_prob = 0.0; a.binary = false;\n    a.source = \"ELite", "elite_like: binary flag"),
    ("p3_arms.hpp", "a.graduate_prob = 0.5 + 1e-12; a.demote_prob = 0.5; a.min_observations = 1; a.prune_prob = 0.0; a.binary = true;\n    a.source = \"OctoMap",
     "a.graduate_prob = 0.5 - 1e-12; a.demote_prob = 0.5; a.min_observations = 1; a.prune_prob = 0.0; a.binary = true;\n    a.source = \"OctoMap", "octomap: strict occupancy threshold"),
    ("p3_arms.hpp", "if (name == \"sprt\") { a.m0 = 0.20; a.m1 = 0.70; }", "if (name == \"sprt\") { a.m0 = 0.25; a.m1 = 0.70; }", "sprt: m0"),
    ("p3_arms.hpp", "if (name == \"sprt\") { a.m0 = 0.20; a.m1 = 0.70; }", "if (name == \"sprt\") { a.m0 = 0.20; a.m1 = 0.75; }", "sprt: m1"),
    ("p3_arms.hpp", "if (name == \"sprt_low_end\") { a.m0 = 0.20; a.m1 = 0.65; }", "if (name == \"sprt_low_end\") { a.m0 = 0.20; a.m1 = 0.60; }", "sprt_low_end: m1"),
    ("p3_arms.hpp", "if (name == \"sprt_high_end\") { a.m0 = 0.30; a.m1 = 0.75; }", "if (name == \"sprt_high_end\") { a.m0 = 0.35; a.m1 = 0.75; }", "sprt_high_end: m0"),
    ("p3_arms.hpp", "a.sprt = true; a.alpha = 0.01; a.beta = 0.01; a.n_max_undecided = 80;", "a.sprt = true; a.alpha = 0.02; a.beta = 0.01; a.n_max_undecided = 80;", "sprt: alpha"),
    ("p3_arms.hpp", "a.sprt = true; a.alpha = 0.01; a.beta = 0.01; a.n_max_undecided = 80;", "a.sprt = true; a.alpha = 0.01; a.beta = 0.02; a.n_max_undecided = 80;", "sprt: beta"),
    ("p3_arms.hpp", "a.sprt = true; a.alpha = 0.01; a.beta = 0.01; a.n_max_undecided = 80;", "a.sprt = true; a.alpha = 0.01; a.beta = 0.01; a.n_max_undecided = 64;", "sprt: n_max_undecided"),
    ("p3_arms.hpp", "if (window_steps < 1) throw", "if (window_steps < 0) throw", "window length lower bound"),
    ("p3_arms.hpp", "p.enable_periodicity = periodicity;", "p.enable_periodicity = !periodicity;", "periodicity switch"),
    ("p3_arms.hpp", "p.layer_interval = window_steps;", "p.layer_interval = window_steps + 1;", "window length passed on"),
    ("p3_arms.hpp", "p.survival_decay = a.decay;", "p.survival_decay = 1.0;", "decay passed on"),
    ("p3_arms.hpp", "p.min_observations = a.min_observations;", "p.min_observations = 3;", "min_observations passed on"),
    ("p3_arms.hpp", "p.prune_prob = a.prune_prob;", "p.prune_prob = 0.05;", "prune_prob passed on"),
    ("p3_arms.hpp", "p.demote_prob = a.demote_prob;", "p.demote_prob = 0.45;", "demote_prob passed on"),
    ("p3_arms.hpp", "p.graduate_prob = a.graduate_prob;", "p.graduate_prob = 0.8;", "graduate_prob passed on"),
    ("p3_arms.hpp", "p.l_hit = a.l_hit; p.l_miss = a.l_miss;", "p.l_hit = a.l_hit; p.l_miss = a.l_hit;", "l_miss passed on"),
    ("p3_arms.hpp", "p.l_min = a.l_min; p.l_max = a.l_max;", "p.l_min = a.l_max; p.l_max = a.l_max;", "l_min passed on"),
    ("p3_arms.hpp", "p.l_min = a.l_min; p.l_max = a.l_max;", "p.l_min = a.l_min; p.l_max = a.l_min;", "l_max passed on"),
    ("p3_arms.hpp", "p.m0 = a.m0; p.m1 = a.m1; p.alpha = a.alpha; p.beta = a.beta; p.n_max_undecided = a.n_max_undecided;",
     "p.m0 = a.m1; p.m1 = a.m1; p.alpha = a.alpha; p.beta = a.beta; p.n_max_undecided = a.n_max_undecided;", "sprt m0 passed on"),
    ("p3_arms.hpp", "p.m0 = a.m0; p.m1 = a.m1; p.alpha = a.alpha; p.beta = a.beta; p.n_max_undecided = a.n_max_undecided;",
     "p.m0 = a.m0; p.m1 = a.m1; p.alpha = a.beta; p.beta = a.beta; p.n_max_undecided = 0;", "sprt n_max passed on"),
    # ---- verdicts, tally, driver
    ("p3_run.hpp", "if (m.isStatic(id)) return Verdict::Static;", "if (!m.isStatic(id)) return Verdict::Static;", "verdict: static test"),
    ("p3_run.hpp", "if (binary) return Verdict::Rejected;", "if (!binary) return Verdict::Rejected;", "verdict: binary arms"),
    ("p3_run.hpp", "m.classify(id) == strata_core::CellClass::Unknown ? Verdict::Rejected : Verdict::Undecided", "m.classify(id) != strata_core::CellClass::Unknown ? Verdict::Rejected : Verdict::Undecided", "verdict: pruned means rejected"),
    ("p3_run.hpp", "case Policy::StaticOnly: return v == Verdict::Static;", "case Policy::StaticOnly: return v != Verdict::Static;", "policy static_only"),
    ("p3_run.hpp", "case Policy::NotRejected: return v != Verdict::Rejected;", "case Policy::NotRejected: return v == Verdict::Static;", "policy not_rejected"),
    ("p3_run.hpp", "default: return true;", "default: return false;", "policy all"),
    ("p3_run.hpp", "return v == Verdict::Static ? \"static\" : v == Verdict::Undecided ? \"undecided\" : \"rejected\";", "return v == Verdict::Static ? \"rejected\" : v == Verdict::Undecided ? \"undecided\" : \"static\";", "verdict names"),
    ("p3_run.hpp", "if (hit) ++c.h;", "if (!hit) ++c.h;", "tally: hits"),
    ("p3_run.hpp", "if (watch_ && watch_->find(id) == watch_->end()) return;", "if (watch_ && watch_->find(id) != watch_->end()) return;", "tally: watch filter"),
    ("p3_run.hpp", "if (track_) touched_.push_back(id);", "if (!track_) touched_.push_back(id);", "tally: latency tracking switch"),
    ("p3_run.hpp", "if (c.first_static < 0 && m.isStatic(id)) c.first_static = m.windowCount();", "if (c.first_static <= 0 && m.isStatic(id)) c.first_static = m.windowCount();", "tally: first_static sentinel"),
    ("p3_run.hpp", "if (c.first_static < 0 && m.isStatic(id)) c.first_static = m.windowCount();", "if (c.first_static < 0 && m.isStatic(id)) c.first_static = m.windowCount() - 1;", "tally: window index"),
    ("p3_run.hpp", "if (have_step && fr.group != cur_group) endStep();", "if (have_step && fr.group == cur_group) endStep();", "driver: step boundary"),
    ("p3_run.hpp", "if (b.tick()) { ++st.windows; if (tally) tally->poll(b.layered()); }", "if (b.tick()) { ++st.windows; }", "driver: poll after a window"),
    ("p3_run.hpp", "if (b.tick()) { ++st.windows; if (tally) tally->poll(b.layered()); }", "if (b.tick()) { if (tally) tally->poll(b.layered()); }", "driver: window count"),
    ("p3_run.hpp", "if (grid) { int gx, gy; if (!strata_core::worldToGrid(", "if (grid) { int gx, gy; if (strata_core::worldToGrid(", "driver: off-grid count"),
    ("p3_run.hpp", "<< verdictName(verdictOf(m, id, binary)) <<", "<< verdictName(verdictOf(m, id, !binary)) <<", "dump: binary flag"),
    ("p3_run.hpp", "if (it != tally.tally().end()) c = it->second;", "if (it == tally.tally().end()) c = CellTally{};", "dump: tally lookup"),
    ("p3_run.hpp", "o_.seekp(0);", "o_.seekp(1);", "pcd writer: header rewrite"),
    ("p3_run.hpp", "o_.write(reinterpret_cast<const char*>(xyz), 12); ++n_;", "o_.write(reinterpret_cast<const char*>(xyz), 8); ++n_;", "pcd writer: record size"),
    ("p3_run.hpp", "o_.write(reinterpret_cast<const char*>(xyz), 12); ++n_;", "o_.write(reinterpret_cast<const char*>(xyz), 12);", "pcd writer: point count"),
    ("p3_run.hpp", "const std::string pad(40 - 2 * num.size(), ' ');", "const std::string pad(40 - num.size(), ' ');", "pcd writer: header padding"),
    ("p3_run.hpp", "const std::string num = std::to_string(n);", "char buf[32]; std::snprintf(buf, sizeof buf, \"%03zu\", n); const std::string num = buf;", "pcd writer: plain decimal count"),
    # ---- KTH exporter
    ("p3_kth.hpp", "if (o.max_frames > 0 && files.size() > o.max_frames) files.resize(o.max_frames);", "if (o.max_frames >= 0 && files.size() > o.max_frames) files.resize(o.max_frames);", "kth: max_frames 0 means all"),
    ("p3_kth.hpp", "if (o.max_frames > 0 && files.size() > o.max_frames) files.resize(o.max_frames);", "if (o.max_frames > 0 && files.size() > o.max_frames) files.resize(o.max_frames - 1);", "kth: max_frames value"),
    ("p3_kth.hpp", "if (!c.hdr.has_viewpoint) fail(", "if (c.hdr.has_viewpoint) fail(", "kth: viewpoint required"),
    ("p3_kth.hpp", "b.integrate(obs, c.hdr.t);", "b.integrate(obs, Eigen::Vector3d::Zero());", "kth: integration origin"),
    ("p3_kth.hpp", "if (b.tick()) ++rep.windows;", "if (!b.tick()) ++rep.windows;", "kth: window count"),
    ("p3_kth.hpp", "if (o.max_range > 0.0 && !((p - c.hdr.t).norm() < o.max_range))", "if (o.max_range > 0.0 && !(p.norm() < o.max_range))", "kth: range from the viewpoint"),
    ("p3_kth.hpp", "++rep.verdict_points[static_cast<int>(v)];", "++rep.verdict_points[0];", "kth: verdict histogram"),
    ("p3_kth.hpp", "if (keeps(o.policies[k], v)) w[k]->add(&c.xyz[i]);", "if (!keeps(o.policies[k], v)) w[k]->add(&c.xyz[i]);", "kth: policy applied"),
    ("p3_kth.hpp", "const Verdict v = verdictOf(b.layered(), b.voxelId(p), arm.binary);", "const Verdict v = verdictOf(b.layered(), b.voxelId(p), !arm.binary);", "kth: binary flag"),
    ("p3_kth.hpp", "if (n.size() > 4 && n.substr(n.size() - 4) == \".pcd\") out.push_back(n);", "if (n.size() > 4 && n.substr(n.size() - 4) != \".pcd\") out.push_back(n);", "kth: file filter"),
    # ---- Boreas
    ("p3_boreas.hpp", "constexpr std::size_t kBoreasRecordBytes = 24;", "constexpr std::size_t kBoreasRecordBytes = 28;", "boreas: record size"),
    ("p3_boreas.hpp", "buf.size() % kBoreasRecordBytes != 0", "buf.size() % kBoreasRecordBytes == 0", "boreas: size test"),
    ("p3_boreas.hpp", "finite = finite && std::isfinite(x);", "finite = finite || std::isfinite(x);", "boreas: finite test"),
    ("p3_boreas.hpp", "s.intensity.push_back(v[3]); s.ring.push_back(v[4]); s.t.push_back(v[5]);", "s.intensity.push_back(v[3]); s.ring.push_back(v[5]); s.t.push_back(v[4]);", "boreas: column order"),
    ("p3_boreas.hpp", "if (s.points < 1000) return", "if (s.points <= 1000) return", "layout: point count"),
    ("p3_boreas.hpp", "if (s.dropped_nonfinite * 100 > s.points) return", "if (s.dropped_nonfinite * 100 >= s.points) return", "layout: non-finite share"),
    ("p3_boreas.hpp", "if (s.range_max > 500.0) return", "if (s.range_max >= 500.0) return", "layout: max range"),
    ("p3_boreas.hpp", "if (std::fabs(s.z_min) > 200.0 || std::fabs(s.z_max) > 200.0) return", "if (std::fabs(s.z_min) > 200.0 && std::fabs(s.z_max) > 200.0) return", "layout: z range"),
    ("p3_boreas.hpp", "if (s.range_median < 1.0 || s.range_median > 100.0) return", "if (s.range_median <= 1.0 || s.range_median > 100.0) return", "layout: median range low"),
    ("p3_boreas.hpp", "if (s.range_median < 1.0 || s.range_median > 100.0) return", "if (s.range_median < 1.0 || s.range_median >= 100.0) return", "layout: median range high"),
    ("p3_boreas.hpp", "if (s.intensity_min < 0.0 || s.intensity_max > 65535.0) return", "if (s.intensity_min <= 0.0 || s.intensity_max > 65535.0) return", "layout: intensity low"),
    ("p3_boreas.hpp", "if (s.intensity_min < 0.0 || s.intensity_max > 65535.0) return", "if (s.intensity_min < 0.0 || s.intensity_max >= 65535.0) return", "layout: intensity high"),
    ("p3_boreas.hpp", "if (s.ring_integer_share < 0.999) return", "if (s.ring_integer_share <= 0.999) return", "layout: ring integer share"),
    ("p3_boreas.hpp", "if (s.ring_min < 0.0 || s.ring_max > 127.0) return", "if (s.ring_min <= 0.0 || s.ring_max > 127.0) return", "layout: ring low"),
    ("p3_boreas.hpp", "if (s.ring_min < 0.0 || s.ring_max > 127.0) return", "if (s.ring_min < 0.0 || s.ring_max >= 127.0) return", "layout: ring high"),
    ("p3_boreas.hpp", "if (s.t_min < -0.2 || s.t_max > 0.2) return", "if (s.t_min <= -0.2 || s.t_max > 0.2) return", "layout: time low"),
    ("p3_boreas.hpp", "if (s.t_min < -0.2 || s.t_max > 0.2) return", "if (s.t_min < -0.2 || s.t_max >= 0.2) return", "layout: time high"),
    ("p3_boreas.hpp", "if (s.t_max - s.t_min < 0.05) return", "if (s.t_max - s.t_min <= 0.05) return", "layout: time span"),
    ("p3_boreas.hpp", "if (!(t > prev)) fail(path,", "if (!(t >= prev)) fail(path,", "csv: time must increase"),
    ("p3_boreas.hpp", "if (t > t_hi) break;", "if (t >= t_hi) break;", "csv: window end inclusive"),
    ("p3_boreas.hpp", "if (t < t_lo) continue;", "if (t <= t_lo) continue;", "csv: window start inclusive"),
    ("p3_boreas.hpp", "if (origin && clat >= 0 && clon >= 0)", "if (origin && clat >= 0 || clon >= 0)", "csv: origin columns"),
    ("p3_boreas.hpp", "const double t = std::strtod(c[ct].c_str(), &end) * time_scale;", "const double t = std::strtod(c[ct].c_str(), &end);", "csv: time scale"),
    ("p3_boreas.hpp", "if (c.size() < need) fail(path,", "if (c.size() <= need) fail(path,", "csv: column count"),
    ("p3_boreas.hpp", "Eigen::AngleAxisd(r.heading, Eigen::Vector3d::UnitZ()) *\n                            Eigen::AngleAxisd(r.pitch, Eigen::Vector3d::UnitY()) *",
     "Eigen::AngleAxisd(r.pitch, Eigen::Vector3d::UnitZ()) *\n                            Eigen::AngleAxisd(r.heading, Eigen::Vector3d::UnitY()) *", "rotation: axes of heading and pitch"),
    ("p3_boreas.hpp", "Eigen::AngleAxisd(r.roll, Eigen::Vector3d::UnitX()));", "Eigen::AngleAxisd(-r.roll, Eigen::Vector3d::UnitX()));", "rotation: sign of roll"),
    ("p3_boreas.hpp", "if (v.size() != 12 && v.size() != 16)", "if (v.size() != 12 || v.size() != 16)", "calib: number count"),
    ("p3_boreas.hpp", "if (v.size() == 16 && (v[12] != 0 || v[13] != 0 || v[14] != 0 || v[15] != 1))", "if (v.size() == 16 && (v[12] != 0 && v[13] != 0 && v[14] != 0 && v[15] != 1))", "calib: last row"),
    ("p3_boreas.hpp", "if ((R * R.transpose() - Eigen::Matrix3d::Identity()).norm() > 1e-6 || std::fabs(R.determinant() - 1.0) > 1e-6)", "if ((R * R.transpose() - Eigen::Matrix3d::Identity()).norm() > 1e-6 && std::fabs(R.determinant() - 1.0) > 1e-6)", "calib: rotation test"),
    ("p3_boreas.hpp", "o.q = (a.q * b.q).normalized(); o.t = a.t + a.q * b.t; return o;", "o.q = (b.q * a.q).normalized(); o.t = a.t + a.q * b.t; return o;", "compose: rotation order"),
    ("p3_boreas.hpp", "o.q = (a.q * b.q).normalized(); o.t = a.t + a.q * b.t; return o;", "o.q = (a.q * b.q).normalized(); o.t = a.t + b.t; return o;", "compose: translation"),
    ("p3_boreas.hpp", "return Eigen::Vector3d((N + h) * c * std::cos(lon), (N + h) * c * std::sin(lon), (N * (1.0 - e2) + h) * s);", "return Eigen::Vector3d((N + h) * c * std::cos(lon), (N + h) * c * std::sin(lon), (N + h) * s);", "ecef: polar radius"),
    ("p3_boreas.hpp", "const double N = kWgs84A / std::sqrt(1.0 - e2 * s * s);", "const double N = kWgs84A / std::sqrt(1.0 + e2 * s * s);", "ecef: curvature radius"),
    ("p3_boreas.hpp", "-std::sin(lat) * std::cos(lon), -std::sin(lat) * std::sin(lon), std::cos(lat),", "-std::sin(lat) * std::cos(lon), std::sin(lat) * std::sin(lon), std::cos(lat),", "enu: north axis"),
    ("p3_boreas.hpp", "if (!applanix.at(frame_time + opt.dt, pa0, opt.max_gap_s))", "if (!applanix.at(frame_time - opt.dt, pa0, opt.max_gap_s))", "convert: dt of the origin"),
    ("p3_boreas.hpp", "if (r < opt.r_min || r > opt.r_max) { ++rep.points_out_of_range; continue; }", "if (r <= opt.r_min || r > opt.r_max) { ++rep.points_out_of_range; continue; }", "convert: r_min boundary"),
    ("p3_boreas.hpp", "if (r < opt.r_min || r > opt.r_max) { ++rep.points_out_of_range; continue; }", "if (r < opt.r_min || r >= opt.r_max) { ++rep.points_out_of_range; continue; }", "convert: r_max boundary"),
    ("p3_boreas.hpp", "if (!applanix.at(frame_time + static_cast<double>(s.t[i]) + opt.dt, pa, opt.max_gap_s))", "if (!applanix.at(frame_time + opt.dt, pa, opt.max_gap_s))", "convert: per-point time"),
    ("p3_boreas.hpp", "if (!applanix.at(frame_time + static_cast<double>(s.t[i]) + opt.dt, pa, opt.max_gap_s))", "if (!applanix.at(frame_time + static_cast<double>(s.t[i]) - opt.dt, pa, opt.max_gap_s))", "convert: dt of the points"),
    ("p3_boreas.hpp", "const Eigen::Vector3d w = lp.t + lp.q * Eigen::Vector3d(x, y, z);", "const Eigen::Vector3d w = lp.t + Eigen::Vector3d(x, y, z);", "convert: rotation of the points"),
    ("p3_boreas.hpp", "return std::strtod(s.c_str(), nullptr) * 1e-6;", "return std::strtod(s.c_str(), nullptr) * 1e-3;", "frame time: microseconds"),
    ("p3_boreas.hpp", "if (s.empty() || s.find_first_not_of(\"0123456789\") != std::string::npos) return std::nan(\"\");", "if (s.empty()) return std::nan(\"\");", "frame time: digits only"),
    ("p3_boreas.hpp", "std::floor(p.x() / tile_m)", "std::trunc(p.x() / tile_m)", "tiles: floor of x"),
    ("p3_boreas.hpp", "for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) if (m.count({k.x + dx, k.y + dy})) return true;", "for (int dx = 0; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) if (m.count({k.x + dx, k.y + dy})) return true;", "tiles: near neighbourhood"),
    ("p3_boreas.hpp", "if (!a.empty()) { o.share_a_in_b = double(a_in_b) / a.size(); o.share_a_near_b = double(a_near) / a.size(); }", "if (!a.empty()) { o.share_a_in_b = double(a_in_b) / b.size(); o.share_a_near_b = double(a_near) / a.size(); }", "tiles: share denominator"),
    # ---- added after the first reader validations
    ("p3_boreas.hpp", "Pose3 p; p.q = (calib_file.q.inverse() * pa.q).normalized(); p.t = pa.t + calib_file.t; return p;", "Pose3 p; p.q = (pa.q * calib_file.q.inverse()).normalized(); p.t = pa.t + calib_file.t; return p;", "lidar pose: yaw shift on the world side"),
    ("p3_boreas.hpp", "Pose3 p; p.q = (calib_file.q.inverse() * pa.q).normalized(); p.t = pa.t + calib_file.t; return p;", "Pose3 p; p.q = (calib_file.q * pa.q).normalized(); p.t = pa.t + calib_file.t; return p;", "lidar pose: inverse of the file rotation"),
    ("p3_boreas.hpp", "Pose3 p; p.q = (calib_file.q.inverse() * pa.q).normalized(); p.t = pa.t + calib_file.t; return p;", "Pose3 p; p.q = (calib_file.q.inverse() * pa.q).normalized(); p.t = pa.t - calib_file.t; return p;", "lidar pose: translation sign"),
    ("p3_boreas.hpp", "Pose3 p; p.q = (calib_file.q.inverse() * pa.q).normalized(); p.t = pa.t + calib_file.t; return p;", "Pose3 p; p.q = (calib_file.q.inverse() * pa.q).normalized(); p.t = pa.t + pa.q * calib_file.t; return p;", "lidar pose: translation frame"),
    ("p3_boreas.hpp", "const Pose3 l0 = compose(to_common, lidarPoseFromApplanix(pa0, calib_file));", "const Pose3 l0 = compose(to_common, pa0);", "convert: calibration of the origin"),
    ("p3_boreas.hpp", "const Pose3 lp = compose(to_common, lidarPoseFromApplanix(pa, calib_file));", "const Pose3 lp = compose(to_common, pa);", "convert: calibration of the points"),
    ("p3_probe.cpp", "const Pose3 lidar = lidarPoseFromApplanix(pa, T), ref = poseOf(*row);", "const Pose3 lidar = pa, ref = poseOf(*row);", "cli: validate compares the calibrated pose"),
    ("p3_probe.cpp", "const bool before_gt = !ok && tmid + dt < gt.t0();", "const bool before_gt = !ok;", "cli: spires scans before the track"),
    ("p3_probe.cpp", "v.check(ok || before_gt, entries[k].name + \": no ground-truth pose at the scan time\");", "v.check(true, entries[k].name + \": no ground-truth pose at the scan time\");", "cli: spires pose check"),
    ("p3_probe.cpp", "const std::size_t expected = (f.stamp + dt >= gt.t0() ? 1u : 0u) + (r.stamp + dt >= gt.t0() ? 1u : 0u);", "const std::size_t expected = 2;", "cli: lil expected scans"),
    ("p3_probe.cpp", "const std::size_t expected = (f.stamp + dt >= gt.t0() ? 1u : 0u) + (r.stamp + dt >= gt.t0() ? 1u : 0u);", "const std::size_t expected = (f.stamp + dt > gt.t0() ? 1u : 0u) + (r.stamp + dt > gt.t0() ? 1u : 0u);", "cli: lil track start"),
    ("p3_probe.cpp", "if (std::hypot(x - fr.origin.x(), y - fr.origin.y()) > radius) continue;", "if (std::hypot(x - fr.origin.x(), y - fr.origin.y()) < radius) continue;", "tile-ground: radius"),
    ("p3_probe.cpp", "if (n < min_points) continue;", "if (n <= min_points) continue;", "tile-ground: minimum points"),
    ("p3_probe.cpp", "if (static_cast<double>(acc) >= pctl / 100.0 * n) break;", "if (static_cast<double>(acc) >= 0.5 * n) break;", "tile-ground: percentile"),
    ("p3_probe.cpp", "o << kv.first.x << \",\" << kv.first.y << \",\" << n << \",\" << kZ0 + (bin + 0.5) * kBin << \"\\n\";", "o << kv.first.x << \",\" << kv.first.y << \",\" << n << \",\" << kZ0 + bin * kBin << \"\\n\";", "tile-ground: bin centre"),
    ("p3_kth.hpp", "if (o.max_range > 0.0 && !((p - c.hdr.t).norm() < o.max_range))", "if (o.max_range >= 0.0 && !((p - c.hdr.t).norm() < o.max_range))", "kth: no range cut by default"),
    ("p3_kth.hpp", "if (o.max_range > 0.0 && !((p - c.hdr.t).norm() < o.max_range))", "if (o.max_range > 0.0 && !((p - c.hdr.t).norm() <= o.max_range))", "kth: range rule is strict"),
    ("p3_kth.hpp", "double max_range{0.0};", "double max_range{50.0};", "kth: default range"),
    ("p3_run.hpp", "if (c.first_touch < 0) c.first_touch = window_index;", "c.first_touch = window_index;", "tally: first touch"),
    ("p3_run.hpp", "<< c.first_static << \",\" << c.first_touch << \"\\n\";", "<< c.first_static << \",\" << c.n << \"\\n\";", "dump: first_touch column"),
    ("p3_probe.cpp", "++lat[{int(kv.second), c.first_static - c.first_touch + 1}];", "++lat[{int(kv.second), c.first_static}];", "cli: latency from the first touch"),
    ("p3_probe.cpp", "++lat[{int(kv.second), c.first_static - c.first_touch + 1}];", "++lat[{int(kv.second), c.first_static - c.first_touch}];", "cli: latency counts the first window"),
    ("p3_probe.cpp", "for (std::size_t k = 0; k < nf; k += step) {", "for (std::size_t k = 0; k < nf; k += 1) {", "boreas-convert: frame step"),
    ("p3_probe.cpp", "static_cast<std::uint32_t>(k / step), fr, rep)) w.add(fr);", "static_cast<std::uint32_t>(k), fr, rep)) w.add(fr);", "boreas-convert: frame step group"),
    ("p3_probe.cpp", "if (step < 1) fail(\"arguments\", \"--frame-step must be at least 1\");", "if (step < 2) fail(\"arguments\", \"--frame-step must be at least 1\");", "boreas-convert: frame step bound"),
    ("p3_probe.cpp", "m.put(\"frame_step\", step);", "m.put(\"frame_step\", 1);", "boreas-convert: frame step meta"),
    # ---- command line
    ("p3_probe.cpp", "if (it->second == \"1\" || it->second == \"on\" || it->second == \"true\") return true;", "if (it->second == \"0\" || it->second == \"on\" || it->second == \"true\") return true;", "cli: flag true"),
    ("p3_probe.cpp", "const BoreasSession ref = loadSession(need(a, \"ref\"));\n  const std::size_t maxf", "const BoreasSession ref = s;\n  const std::size_t maxf", "cli: boreas-convert reference"),
    ("p3_probe.cpp", "c.n >= static_cast<std::uint32_t>(nmin) ? 1 : 0", "c.n > static_cast<std::uint32_t>(nmin) ? 1 : 0", "cli: eligibility boundary"),
    ("p3_probe.cpp", "if (c.first_static >= 0) ++lat[{int(kv.second), c.first_static - c.first_touch + 1}];", "if (c.first_static > 0) ++lat[{int(kv.second), c.first_static - c.first_touch + 1}];", "cli: latency sentinel"),
    ("p3_probe.cpp", "if (cache.dim() != ldim) fail(", "if (cache.dim() == ldim) fail(", "cli: dimension check"),
    ("p3_probe.cpp", "if (cache.dim() != 3) fail(caches[i], \"not a 3D cache\");", "if (cache.dim() == 3) fail(caches[i], \"not a 3D cache\");", "cli: static-export dimension"),
    ("p3_probe.cpp", "if (!b || mode == \"independent\") b.reset(", "if (!b || mode == \"cumulative\") b.reset(", "cli: static-export modes"),
    # ---- Boreas shared frame (review finding 1, amendment 3)
    ("p3_boreas.hpp", "p.t = Eigen::Vector3d(0.0, 0.0, z_offset); return p;", "p.t = Eigen::Vector3d(0.0, 0.0, -z_offset); return p;", "common frame: z offset sign"),
    ("p3_boreas.hpp", "p.t = Eigen::Vector3d(0.0, 0.0, z_offset); return p;", "p.t = Eigen::Vector3d(z_offset, 0.0, 0.0); return p;", "common frame: z offset axis"),
    ("p3_boreas.hpp", "p.q = Eigen::Quaterniond::Identity(); p.t = Eigen::Vector3d(0.0, 0.0, z_offset)", "p.q = Eigen::Quaterniond(Eigen::AngleAxisd(0.01, Eigen::Vector3d::UnitZ())); p.t = Eigen::Vector3d(0.0, 0.0, z_offset)", "common frame: rotation"),
    ("p3_boreas.hpp", "if (!ref.valid) fail(\"common frame\", \"reference origin latitude and longitude are missing\");", "", "enu agreement: reference origin needed"),
    ("p3_boreas.hpp", "if (!std::isfinite(r.lat) || !std::isfinite(r.lon)) continue;", "", "enu agreement: rows without lat/lon skipped"),
    ("p3_boreas.hpp", "const double dx = r.east - e.x(), dy = r.north - e.y();", "const double dx = r.east - e.x(), dy = 0.0;", "enu agreement: north difference"),
    ("p3_boreas.hpp", "const double dx = r.east - e.x(), dy = r.north - e.y();", "const double dx = 0.0, dy = r.north - e.y();", "enu agreement: east difference"),
    ("p3_boreas.hpp", "a.worst_m = std::max(a.worst_m, std::hypot(dx, dy));", "a.worst_m = std::min(a.worst_m, std::hypot(dx, dy));", "enu agreement: worst is the maximum"),
    ("p3_boreas.hpp", "a.worst_m = std::max(a.worst_m, std::hypot(dx, dy));", "a.worst_m = std::max(a.worst_m, std::fabs(dx));", "enu agreement: horizontal distance"),
    ("p3_boreas.hpp", "sx += dx; sy += dy; ++a.rows;", "sx += dx; sy += dy;", "enu agreement: row count"),
    ("p3_boreas.hpp", "a.mean_dx_m = sx / a.rows; a.mean_dy_m = sy / a.rows;", "a.mean_dx_m = sx; a.mean_dy_m = sy;", "enu agreement: means"),
    ("p3_boreas.hpp", "constexpr double kEnuAgreementTolM = 0.10;", "constexpr double kEnuAgreementTolM = 5.0;", "enu agreement: limit value"),
    ("p3_boreas.hpp", "if (clat >= 0 && clon >= 0) { r.lat = std::strtod(c[clat].c_str(), nullptr); r.lon = std::strtod(c[clon].c_str(), nullptr); }", "if (clat >= 0 && clon >= 0) { r.lon = std::strtod(c[clat].c_str(), nullptr); r.lat = std::strtod(c[clon].c_str(), nullptr); }", "csv: lat/lon per row"),
    ("p3_boreas.hpp", "origin->east = std::strtod(c[ce].c_str(), nullptr); origin->north = std::strtod(c[cn].c_str(), nullptr);", "origin->east = std::strtod(c[cn].c_str(), nullptr); origin->north = std::strtod(c[ce].c_str(), nullptr);", "csv: first-row east/north"),
    ("p3_boreas.hpp", "if (bi >= 0) dz.push_back(b[static_cast<std::size_t>(bi)].up - r.up);", "if (bi >= 0) dz.push_back(r.up - b[static_cast<std::size_t>(bi)].up);", "altitude: sign"),
    ("p3_boreas.hpp", "double best = radius; long bi = -1;", "double best = radius * 2; long bi = -1;", "altitude: radius"),
    ("p3_boreas.hpp", "if (d <= best) { best = d; bi = static_cast<long>(j); }", "if (d < best) { best = d; bi = static_cast<long>(j); }", "altitude: boundary at the radius"),
    ("p3_boreas.hpp", "if (d <= best) { best = d; bi = static_cast<long>(j); }", "if (d <= radius) { best = d; bi = static_cast<long>(j); }", "altitude: nearest row wins"),
    ("p3_boreas.hpp", "o.median = q(0.5); o.p10 = q(0.1); o.p90 = q(0.9);", "o.median = q(0.5); o.p10 = q(0.9); o.p90 = q(0.1);", "altitude: percentiles"),
    ("p3_boreas.hpp", "for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) {\n      auto it = grid.find", "for (int dx = 0; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) {\n      auto it = grid.find", "altitude: neighbour cells"),
    ("p3_probe.cpp", "const bool pass = overlap_ok && enu_ok;", "const bool pass = overlap_ok;", "cli: overlap needs the frame check"),
    ("p3_probe.cpp", "const bool pass = overlap_ok && enu_ok;", "const bool pass = overlap_ok || enu_ok;", "cli: overlap needs both checks"),
    ("p3_probe.cpp", "if (!ok) enu_ok = false;", "", "cli: overlap frame check of every session"),
    ("p3_probe.cpp", "const EnuAgreement ea = enuAgreement(gps, org[0]);", "const EnuAgreement ea = enuAgreement(gps, org[i]);", "cli: overlap frame check against the reference"),
    ("p3_probe.cpp", "const bool ok = ea.rows > 0 && ea.worst_m < kEnuAgreementTolM;", "const bool ok = ea.worst_m < kEnuAgreementTolM;", "cli: overlap frame check needs rows"),
    ("p3_probe.cpp", "if (o.share_a_near_b < need_share || o.share_b_near_a < need_share) overlap_ok = false;", "if (o.share_a_near_b < need_share && o.share_b_near_a < need_share) overlap_ok = false;", "cli: overlap verdict"),
    ("p3_probe.cpp", "for (const auto& r : poses[i]) xy[i].emplace_back(r.east, r.north);", "for (const auto& r : gps) xy[i].emplace_back(r.east, r.north);", "cli: overlap trajectory source"),
    ("p3_probe.cpp", "v.check(ea.worst_m < kEnuAgreementTolM, \"east and north of the files differ from the geodetic ENU at the reference origin by more than 0.10 m\");", "", "cli: validate frame check"),
    ("p3_probe.cpp", "v.check(ea.rows > 0, \"no GPS row compared with its latitude and longitude\");", "", "cli: validate frame check rows"),
    ("p3_probe.cpp", "readBoreasPoseCsv(need(a, \"ref\") + \"/applanix/gps_post_process.csv\", 1.0, 1e300, -1e300, &refo);", "refo = org;", "cli: validate reference origin"),
    ("p3_probe.cpp", "if (!(ea.worst_m < kEnuAgreementTolM))\n    fail(s.dir,", "if (false)\n    fail(s.dir,", "cli: convert refuses another frame"),
    ("p3_probe.cpp", "if (ea.rows == 0) fail(s.dir, \"no GPS row with latitude and longitude in the window\");", "", "cli: convert needs rows to check"),
    ("p3_probe.cpp", "const Pose3 to_common = commonFrame(zoff);", "const Pose3 to_common = commonFrame(0.0);", "cli: convert z offset"),
    ("p3_probe.cpp", "m.put(\"enu_rows_checked\", ea.rows); m.put(\"enu_worst_horizontal_diff_m\", ea.worst_m);", "m.put(\"enu_rows_checked\", 0); m.put(\"enu_worst_horizontal_diff_m\", 0.0);", "cli: convert meta of the frame check"),
    ("p3_probe.cpp", "track, T, commonFrame(), co,", "track, T, commonFrame(1.0), co,", "cli: validate converts in the file frame"),
    ("p3_eval.py", "SPIRES_STRUCTURE_LABELS = (3, 2)", "SPIRES_STRUCTURE_LABELS = (3,)", "spires: ground block by default"),
    ("p3_eval.py", "p.add_argument(\"--structure-labels\", default=\"3,2\")", "p.add_argument(\"--structure-labels\", default=\"3\")", "spires: ground block on the command line"),
    ("p3_eval.py", "if int(r[\"label\"]) in lat:", "if True:", "spires: latency of unlisted labels"),
    ("p3_eval.py", "w = lat[lab]\n", "w = lat[structure_labels[0]]\n", "spires: latency per label"),
    ("p3_eval.py", "        shared = np.intersect1d(shared, t, assume_unique=True)", "        shared = np.union1d(shared, t)", "jaccard: tiles observed by all sessions"),
    ("p3_eval.py", "tiles = [np.unique(tiles_of_ids(o_, res, tile_m)) for o_ in observed_sets]", "tiles = [np.unique(tiles_of_ids(s_, res, tile_m)) for s_ in static_sets]", "jaccard: tiles come from the observed sets"),
    ("p3_eval.py", "if np.setdiff1d(s_, o_, assume_unique=True).size:", "if False:", "jaccard: Static inside observed"),
    ("p3_eval.py", "if len(static_sets) != len(observed_sets) or len(static_sets) < 2:", "if len(static_sets) < 2:", "jaccard: set count"),
    ("p3_eval.py", "if len(args.observed) != len(args.ids):", "if False:", "jaccard: command line count"),
    ("p3_eval.py", "r[\"pairwise_tiles\"] = jaccard_in_shared_tiles(static_sets[i], static_sets[j], res, observed_sets[i], observed_sets[j], tile_m)", "r[\"pairwise_tiles\"] = jaccard_in_shared_tiles(static_sets[i], static_sets[j], res, None, None, tile_m)", "jaccard: pairwise secondary uses observed sets"),
    ("p3_probe.cpp", "v.check(worst_pos < 0.10,", "v.check(worst_pos < 100.0,", "cli: pose agreement bound"),
    ("p3_probe.cpp", "v.check(med > 0.5 && med < 60.0, files[k] +", "v.check(med > 0.5 || med < 60.0, files[k] +", "cli: kth range sanity"),
    ("p3_probe.cpp", "v.check(std::fabs(c.hdr.q_norm - 1.0) <= 1e-3,", "v.check(std::fabs(c.hdr.q_norm - 1.0) <= 1e3,", "cli: kth quaternion"),
]

PY = [
    ("p3_eval.py", "ok = (d * d).sum(axis=1) <= tol2", "ok = (d * d).sum(axis=1) < tol2", "matched: boundary inclusive"),
    ("p3_eval.py", "offsets = [(0, 0, 0)] + [(a, b, c) for a in (-1, 0, 1) for b in (-1, 0, 1) for c in (-1, 0, 1) if (a, b, c) != (0, 0, 0)]", "offsets = [(0, 0, 0)] + [(a, b, c) for a in (0, 1) for b in (-1, 0, 1) for c in (-1, 0, 1) if (a, b, c) != (0, 0, 0)]", "matched: neighbourhood"),
    ("p3_eval.py", "if tol <= 0:", "if tol < 0:", "matched: tol must be positive"),
    ("p3_eval.py", "if np.abs(q).max() >= lim or np.abs(t).max() >= lim:", "if np.abs(q).max() >= lim and np.abs(t).max() >= lim:", "matched: range guard"),
    ("p3_eval.py", "sa = 100.0 * int((kept & ~dyn).sum()) / n_sta", "sa = 100.0 * int((kept & dyn).sum()) / n_sta", "scores: SA numerator"),
    ("p3_eval.py", "da = 100.0 * int((~kept & dyn).sum()) / n_dyn", "da = 100.0 * int((kept & dyn).sum()) / n_dyn", "scores: DA numerator"),
    ("p3_eval.py", "aa = math.sqrt(sa * da)", "aa = 0.5 * (sa + da)", "scores: AA"),
    ("p3_eval.py", "ha = 0.0 if sa + da == 0 else 2.0 * sa * da / (sa + da)", "ha = 0.0 if sa + da == 0 else sa * da / (sa + da)", "scores: HA"),
    ("p3_eval.py", "if n_dyn == 0 or n_sta == 0:", "if n_dyn == 0 and n_sta == 0:", "scores: both classes needed"),
    ("p3_eval.py", "if not set(vals.tolist()) <= {0.0, 1.0}:", "if not set(vals.tolist()) <= {0.0, 1.0, 5.0}:", "cli: GT flag values"),
    ("p3_eval.py", "d = 1 + z * z / n", "d = 1 + z / n", "wilson: denominator"),
    ("p3_eval.py", "h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))", "h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n))", "wilson: half width"),
    ("p3_eval.py", "return (float(np.percentile(r, 2.5)), float(np.percentile(r, 97.5)))", "return (float(np.percentile(r, 5.0)), float(np.percentile(r, 97.5)))", "bootstrap: lower percentile"),
    ("p3_eval.py", "return (float(np.percentile(r, 2.5)), float(np.percentile(r, 97.5)))", "return (float(np.percentile(r, 2.5)), float(np.percentile(r, 95.0)))", "bootstrap: upper percentile"),
    ("p3_eval.py", "BOOT_SEED = 20261014", "BOOT_SEED = 20261015", "bootstrap: seed"),
    ("p3_eval.py", "s = rng.randint(0, u.size, u.size)", "s = rng.randint(0, u.size, u.size - 1)", "bootstrap: resample size"),
    ("p3_eval.py", "if not den.any():", "if den.all():", "bootstrap: empty denominator"),
    ("p3_eval.py", "num = np.bincount(inv, weights=f.astype(float), minlength=u.size)", "num = np.bincount(inv, weights=(~f).astype(float), minlength=u.size)", "bootstrap: numerator"),
    ("p3_eval.py", "ok = (cnt >= min_cells) & (l1 > 0) & (l2 / l1 <= ratio_max) & (np.sqrt(12 * np.maximum(l1, 0)) >= min_extent)", "ok = (cnt > min_cells) & (l1 > 0) & (l2 / l1 <= ratio_max) & (np.sqrt(12 * np.maximum(l1, 0)) >= min_extent)", "wall run: cell count"),
    ("p3_eval.py", "ok = (cnt >= min_cells) & (l1 > 0) & (l2 / l1 <= ratio_max) & (np.sqrt(12 * np.maximum(l1, 0)) >= min_extent)", "ok = (cnt >= min_cells) & (l1 > 0) & (l2 / l1 < ratio_max) & (np.sqrt(12 * np.maximum(l1, 0)) >= min_extent)", "wall run: ratio"),
    ("p3_eval.py", "ok = (cnt >= min_cells) & (l1 > 0) & (l2 / l1 <= ratio_max) & (np.sqrt(12 * np.maximum(l1, 0)) >= min_extent)", "ok = (cnt >= min_cells) & (l1 > 0) & (l2 / l1 <= ratio_max) & (np.sqrt(12 * np.maximum(l1, 0)) > 2 * min_extent)", "wall run: extent"),
    ("p3_eval.py", "if (dx * dx + dy * dy) * res * res > rho * rho + 1e-12:", "if (dx * dx + dy * dy) * res * res > rho * rho * 4:", "wall run: radius"),
    ("p3_eval.py", "r = int(math.floor(rho / res + 1e-9))", "r = int(math.floor(rho / res + 1e-9)) - 1", "wall run: search window"),
    ("p3_eval.py", "clutter = (lab == REF_FREE) & (h >= 1) & (n >= nmin)", "clutter = (lab == REF_FREE) & (h >= 0) & (n >= nmin)", "lil: hit_in_free needs a hit"),
    ("p3_eval.py", "clutter = (lab == REF_FREE) & (h >= 1) & (n >= nmin)", "clutter = ((lab == REF_FREE) | (lab == REF_FREE_NEAR)) & (h >= 1) & (n >= nmin)", "lil: near-wall cells excluded"),
    ("p3_eval.py", "clutter = (lab == REF_FREE) & (h >= 1) & (n >= nmin)", "clutter = (lab == REF_FREE) & (h >= 1) & (n > nmin)", "lil: nmin boundary (clutter)"),
    ("p3_eval.py", "structure = wall & (n >= nmin)", "structure = wall & (n > nmin)", "lil: nmin boundary (wall)"),
    ("p3_eval.py", "structure = wall & (n >= nmin)", "structure = occ & (n >= nmin)", "lil: wall runs only"),
    ("p3_eval.py", "block(structure, ver == VERDICTS[\"rejected\"], \"false_reject_on_wall_runs\")", "block(structure, ver == VERDICTS[\"undecided\"], \"false_reject_on_wall_runs\")", "lil: false reject is Rejected"),
    ("p3_eval.py", "block(clutter, ver == VERDICTS[\"static\"], \"false_static_on_clutter\")", "block(clutter, ver != VERDICTS[\"rejected\"], \"false_static_on_clutter\")", "lil: false static is Static"),
    ("p3_eval.py", "m = clutter & (r >= lo_b) & (r < hi_b)", "m = clutter & (r > lo_b) & (r < hi_b)", "lil: band lower edge"),
    ("p3_eval.py", "m = clutter & (r >= lo_b) & (r < hi_b)", "m = clutter & (r >= lo_b) & (r <= hi_b)", "lil: band upper edge"),
    ("p3_eval.py", "got = structure & (d[\"first_static\"] >= 0)", "got = structure & (d[\"first_static\"] > 0)", "lil: latency sentinel"),
    ("p3_eval.py", "x = origin + (d[\"ix\"] + 0.5) * res", "x = origin + d[\"ix\"] * res", "lil: cell centre"),
    ("p3_eval.py", "\"ref_occupied_cells_rejected\": int((occ & elig & (ver == VERDICTS[\"rejected\"])).sum()),", "\"ref_occupied_cells_rejected\": int((occ & elig & (ver != VERDICTS[\"static\"])).sum()),", "lil: qualitative rejected count"),
    ("p3_eval.py", "kc = c.get((clutter_label, 1, \"static\"), 0)", "kc = c.get((clutter_label, 0, \"static\"), 0)", "spires: eligible clutter"),
    ("p3_eval.py", "kr = c.get((lab, 1, \"rejected\"), 0)", "kr = c.get((lab, 1, \"undecided\"), 0)", "spires: rejected count"),
    ("p3_eval.py", "return sum(v for (l, e, _), v in c.items() if l == label and e == elig)", "return sum(v for (l, e, _), v in c.items() if l == label)", "spires: eligibility in totals"),
    ("p3_eval.py", "return (((ids >> 42) & mask) - _OFF, ((ids >> 21) & mask) - _OFF, (ids & mask) - _OFF)", "return (((ids >> 21) & mask) - _OFF, ((ids >> 21) & mask) - _OFF, (ids & mask) - _OFF)", "ids: x field"),
    ("p3_eval.py", "return (((ids >> 42) & mask) - _OFF, ((ids >> 21) & mask) - _OFF, (ids & mask) - _OFF)", "return (((ids >> 42) & mask) - _OFF, ((ids >> 21) & mask) - _OFF, ((ids >> 21) & mask) - _OFF)", "ids: z field"),
    ("p3_eval.py", "union = a.size + b.size - inter", "union = a.size + b.size", "jaccard: union"),
    ("p3_eval.py", "t = int(round(tile_m / res))", "t = int(round(tile_m / res)) * 2", "tiles: size"),
    ("p3_eval.py", "shared = np.intersect1d(ta, tb, assume_unique=True)", "shared = np.union1d(ta, tb)", "tiles: shared tiles"),
    ("p3_eval.py", "ta = np.unique(tiles_of_ids(all_a if all_a is not None else a, res, tile_m))", "ta = np.unique(tiles_of_ids(a, res, tile_m))", "tiles: observed set of A"),
    ("p3_eval.py", "ok = (a[\"SA\"] == 100.0 and abs(a[\"DA\"] - ceiling) < 1e-9 and b[\"SA\"] == 100.0 and b[\"DA\"] == 0.0)", "ok = (a[\"SA\"] == 100.0 and abs(a[\"DA\"] - ceiling) < 1e-9 and b[\"SA\"] == 100.0 and b[\"DA\"] == 100.0)", "selfcheck: remove nothing expects DA 0"),
    ("p3_eval.py", "ok = (a[\"SA\"] == 100.0 and abs(a[\"DA\"] - ceiling) < 1e-9 and b[\"SA\"] == 100.0 and b[\"DA\"] == 0.0)", "ok = (a[\"SA\"] == 100.0 and abs(a[\"DA\"] - ceiling) < 1e-9 and b[\"SA\"] == 0.0 and b[\"DA\"] == 0.0)", "selfcheck: remove nothing expects SA 100"),
    ("p3_eval.py", "ok = (a[\"SA\"] == 100.0 and abs(a[\"DA\"] - ceiling) < 1e-9 and b[\"SA\"] == 100.0 and b[\"DA\"] == 0.0)", "ok = (a[\"SA\"] == 100.0 and abs(a[\"DA\"] - 100.0) < 1e-9 and b[\"SA\"] == 100.0 and b[\"DA\"] == 0.0)", "selfcheck: DA against the ceiling"),
    ("p3_eval.py", "a = kth_scores(gt, dyn, gt[~dyn], args.tol)\n    b = kth_scores(gt, dyn, gt, args.tol)", "a = kth_scores(gt, dyn, gt[dyn], args.tol)\n    b = kth_scores(gt, dyn, gt, args.tol)", "selfcheck: GT static only"),
    ("p3_eval.py", "ceiling = 100.0 * (a[\"gt_dynamic\"] - shadowed) / a[\"gt_dynamic\"]", "ceiling = 100.0 * shadowed / a[\"gt_dynamic\"]", "selfcheck: ceiling"),
    ("p3_eval.py", "lo = np.searchsorted(xs, p[0] - tol, side=\"left\")", "lo = np.searchsorted(xs, p[0] - tol, side=\"right\")", "shadowed: window start"),
    ("p3_eval.py", "hi = np.searchsorted(xs, p[0] + tol, side=\"right\")", "hi = np.searchsorted(xs, p[0] + tol, side=\"left\")", "shadowed: window end"),
    ("p3_eval.py", "if ((d * d).sum(axis=1) <= tol * tol).any():\n                count += 1", "if ((d * d).sum(axis=1) < tol * tol).any():\n                count += 1", "shadowed: boundary"),
    ("p3_eval.py", "lat = (d[\"first_static\"] - d[\"first_touch\"] + 1)[got]", "lat = d[\"first_static\"][got]", "lil: latency from the first touch"),
    ("p3_eval.py", "lat = (d[\"first_static\"] - d[\"first_touch\"] + 1)[got]", "lat = (d[\"first_static\"] - d[\"first_touch\"])[got]", "lil: latency counts the first window"),
    ("p3_eval.py", "return {\"tiles\": int(d.size), \"median\": float(np.median(d)), \"p10\": float(np.percentile(d, 10)), \"p90\": float(np.percentile(d, 90))}", "return {\"tiles\": int(d.size), \"median\": float(np.mean(d)), \"p10\": float(np.percentile(d, 10)), \"p90\": float(np.percentile(d, 90))}", "zoffset: median"),
    ("p3_eval.py", "d = np.array([a[k] - b[k] for k in a if k in b])", "d = np.array([b[k] - a[k] for k in a if k in b])", "zoffset: sign"),
    ("p3_eval.py", "if int(r[\"count\"]) >= min_count}", "if int(r[\"count\"]) > min_count}", "zoffset: minimum count"),
]

EQUIVALENT = {
    "cli: lil track start": "differs only if a scan stamp plus the offset equals the first ground-truth time to the last bit; the stamps come from integer seconds and nanoseconds and cannot be set equal by a test",
    "cli: latency sentinel": "first_static is -1 (not yet) or a window index >= 1, never 0: `> 0` and `>= 0` select the same cells",
    "tally: first_static sentinel": "first_static is -1 (not yet) or a window index >= 1, never 0: `<= 0` and `< 0` select the same cells",
}


def read(p):
    with open(p) as f:
        return f.read()


def write(p, s):
    with open(p, "w") as f:
        f.write(s)


def run(cmd, cwd=None, timeout=3600):
    p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    return p.returncode, p.stdout.decode(errors="replace")


def prepare(scratch):
    src = os.path.join(scratch, "src")
    shutil.rmtree(src, ignore_errors=True)
    os.makedirs(src)
    arc = subprocess.Popen(["git", "-C", REPO, "archive", "HEAD"], stdout=subprocess.PIPE)
    subprocess.check_call(["tar", "-x", "-C", src], stdin=arc.stdout)
    arc.wait()
    for f in os.listdir(HERE):                       # the current sources over the committed ones
        if f.endswith((".hpp", ".cpp", ".py", ".txt", ".sh", ".md")) and os.path.isfile(os.path.join(HERE, f)):
            shutil.copy(os.path.join(HERE, f), os.path.join(src, RD, f))
    return src


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--only-notes", default="", help="regular expression: run only the mutants whose note matches")
    ap.add_argument("--jobs", default="2")
    a = ap.parse_args(argv)
    os.makedirs(a.scratch, exist_ok=True)
    src = prepare(a.scratch)
    rd = os.path.join(src, RD)
    build = os.path.join(a.scratch, "build")
    todo = [("cpp", m) for m in CPP] + [("py", m) for m in PY]
    if a.only:
        todo = [t for t in todo if t[0] == a.only]
    if a.only_notes:
        import re
        todo = [t for t in todo if re.search(a.only_notes, t[1][3])]
    bad = [(fn, note, read(os.path.join(rd, fn)).count(old)) for _, (fn, old, new, note) in todo if read(os.path.join(rd, fn)).count(old) != 1]
    for fn, note, c in bad:
        print("PATTERN %d times: %s: %s" % (c, fn, note))
    if a.check:
        print("%d mutants, %d patterns not unique" % (len(todo), len(bad)))
        return 1 if bad else 0
    if any(k == "cpp" for k, _ in todo):
        rc, out = run(["cmake", "-S", os.path.join(src, "paper/experiments"), "-B", build, "-DCMAKE_BUILD_TYPE=Release"])
        if rc != 0:
            print(out); return 2
        rc, out = run(["cmake", "--build", build, "-j", a.jobs, "--target", "test_p3"])
        if rc != 0:
            print(out[-3000:]); return 2
        rc, out = run([os.path.join(build, "realdata", "test_p3")], cwd="/tmp")
        if rc != 0:
            print("the unmutated tests fail:\n" + out[-3000:]); return 2
    rows = []
    for kind, (fn, old, new, note) in todo:
        path = os.path.join(rd, fn)
        text = read(path)
        if text.count(old) != 1:
            rows.append((kind, fn, note, "NOT APPLIED (pattern found %d times)" % text.count(old)))
            continue
        write(path, text.replace(old, new))
        try:
            if kind == "cpp":
                rc, out = run(["cmake", "--build", build, "-j", a.jobs, "--target", "test_p3"])
                if rc != 0:
                    rows.append((kind, fn, note, "BUILD ERROR"))
                    continue
                rc, out = run([os.path.join(build, "realdata", "test_p3")], cwd="/tmp")
            else:
                rc, out = run([sys.executable, "-I", "-m", "unittest", "discover", "-s", rd, "-p", "test_p3_*.py"], cwd=rd)
            res = "killed" if rc != 0 else ("SURVIVED (%s)" % EQUIVALENT[note] if note in EQUIVALENT else "SURVIVED")
            rows.append((kind, fn, note, res))
        finally:
            write(path, text)
        print("%-4s %-16s %-45s %s" % (kind, fn, note, rows[-1][3]), flush=True)
    killed = sum(1 for r in rows if r[3] == "killed")
    surv = [r for r in rows if r[3].startswith("SURVIVED")]
    other = [r for r in rows if r[3] != "killed" and not r[3].startswith("SURVIVED")]
    L = ["# Mutation record, phase 3 tests", "",
         "Generated by `mutation_p3.py` on %s from a copy of the sources (the worktree is not touched). One textual mutant at a time; the C++ tests are `test_p3` (including its command line tests), the Python tests `test_p3_eval.py` and `test_p3_runlog.py` (only `test_p3_*.py` are run for Python mutants)." % time.strftime("%Y-%m-%dT%H:%M:%S%z"),
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
