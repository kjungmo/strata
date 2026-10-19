#pragma once
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include "strata_core/layered_map.hpp"
namespace strata {

// The text and the values of the "<node>: evidence model" status on /diagnostics (and of the start-up log
// line). ROS-free so every row of the table below can be unit-tested.
//
//   evidence_model  calibration         backend   message starts with                      evidence_calibration value
//   sprt            measured_2d         grid2d    "provisional (PREREG not frozen: ...)"   "measured_2d, provisional (...)"
//   sprt            fallback_2d_pair    voxel3d   "NOT calibrated for voxel3d (...)"       "fallback_2d_pair, provisional (...)"
//                                                  and then the provisional note
//   sprt            user                any       "sprt: ..."                              "user"
//   sprt            any, with m0, m1, alpha or beta set away from the library default: the node relabels it
//                   "user" (and logs a warning) before this function runs, so it reads like the "user" row
//   legacy          any                 any       "legacy (v0.1 arithmetic): ..."          the label as given (no pair is used)
//
// The provisional note is there because PREREG.md marks the 2D pair NOT FROZEN (gate G1 failed on all three
// calibration sequences) while the shipped defaults are its conditional values. The text is the constant
// kEvidenceProvisional; check_defaults.py fails if this file carries it while PREREG.json says the 2D pair is
// FROZEN, or does not carry it while PREREG.json says it is not.
inline constexpr const char* kEvidenceProvisional = "provisional (PREREG not frozen: G1 failed)";

struct EvidenceStatus {
  std::string message;
  std::vector<std::pair<std::string, std::string>> values;
};

inline EvidenceStatus evidenceStatus(const strata_core::LayeredMapParams& p, const std::string& evidence_model,
                                     const std::string& calibration, const std::string& backend) {
  auto num = [](double v) { char b[32]; std::snprintf(b, sizeof(b), "%.4g", v); return std::string(b); };
  EvidenceStatus s;
  s.values.emplace_back("evidence_model", evidence_model);
  const bool sprt = p.evidence_model == strata_core::EvidenceModel::Sprt;
  if (sprt) {
    const strata_core::SprtDerived d = strata_core::deriveSprt(p);
    s.values.emplace_back("m0", num(p.m0));
    s.values.emplace_back("m1", num(p.m1));
    s.values.emplace_back("alpha", num(p.alpha));
    s.values.emplace_back("beta", num(p.beta));
    s.values.emplace_back("windows_to_graduate_always_hit", std::to_string(d.latency));
    s.values.emplace_back("all_miss_windows_to_demote", std::to_string(d.demote_misses));
    s.values.emplace_back("n_max_undecided", std::to_string(p.n_max_undecided));
    s.message = "sprt: clutter m0=" + num(p.m0) + ", structure m1=" + num(p.m1) + ", alpha=" + num(p.alpha) +
        ", beta=" + num(p.beta) + "; an always-hit cell graduates after " + std::to_string(d.latency) +
        " windows; the error rates hold for independent windows";
  } else {
    s.values.emplace_back("min_observations", std::to_string(p.min_observations));
    s.message = "legacy (v0.1 arithmetic): a cell graduates once its smoothed log-odds reach graduate_prob after "
        "min_observations=" + std::to_string(p.min_observations) + " windows; no error rate is stated";
  }
  const bool provisional = sprt && calibration != "user";
  s.values.emplace_back("evidence_calibration", provisional ? calibration + ", " + kEvidenceProvisional : calibration);
  if (provisional) s.message = std::string(kEvidenceProvisional) + "; " + s.message;
  if (sprt && calibration == "fallback_2d_pair" && backend != "grid2d") {
    s.message = "NOT calibrated for " + backend + " (fallback: the 2D pair); on single-pass 3D data most "
        "structure stays undecided; " + s.message;
  }
  return s;
}

}  // namespace strata
