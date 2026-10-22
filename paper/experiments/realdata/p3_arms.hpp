// The five arms of the phase 3 test evaluation (PREREG_P3.md section 2) as engine parameters.
// One table, one function: armSpec(name) -> ArmSpec, toParams(spec, W, periodicity) -> LayeredMapParams.
// An arm is an evidence rule, not a program: every arm runs through the same LayeredMap, windows and read-out.
//
//   shipped_v0_1  the v0.1 shipped defaults (the legacy rule, unchanged)
//   symmetric     shipped_v0_1 with l_miss = -l_hit (the only change)
//   octomap       OctoMap defaults: p_hit 0.7, p_miss 0.4, clamps 0.12 / 0.97, occupied iff l > 0, no leak, never pruned
//   elite_like    on-ray odds factors 9 and 1/9 (ELite Eq. 5-6 at zero distance), no leak, occupied iff l > 0
//   sprt          Wald's test; (m0, m1, alpha, beta) from PREREG_P3.md section 2, derived by the engine
// Extra arms (not part of the five; the pair is not frozen, PREREG.md): sprt_low_end, sprt_high_end.
//
// "binary" arms have no undecided state: a cell is Static or it is not, and "not Static" is their Rejected
// (PREREG_P3.md section 3.2). The other arms read Rejected as "the engine pruned the cell".
#pragma once
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include "strata_core/layered_map.hpp"

namespace realdata {
namespace p3 {

struct ArmSpec {
  std::string name;
  bool sprt{false};
  bool binary{false};
  // evidence_model = legacy
  double l_hit{0.85}, l_miss{-0.4}, l_min{-5.0}, l_max{5.0}, decay{0.97};
  double graduate_prob{0.8}, demote_prob{0.45}, prune_prob{0.05};
  int min_observations{3};
  // evidence_model = sprt
  double m0{0}, m1{0}, alpha{0.01}, beta{0.01};
  int n_max_undecided{80};
  std::string source;
};

inline std::vector<std::string> armNames() { return {"shipped_v0_1", "symmetric", "octomap", "elite_like", "sprt"}; }
inline std::vector<std::string> extraArmNames() { return {"sprt_low_end", "sprt_high_end"}; }

inline double logit(double p) { return std::log(p / (1.0 - p)); }

inline ArmSpec armSpec(const std::string& name) {
  ArmSpec a; a.name = name;
  if (name == "shipped_v0_1") {
    a.source = "strata_core v0.1 LayeredMapParams defaults";
  } else if (name == "symmetric") {
    a.l_miss = -a.l_hit;
    a.source = "shipped_v0_1 with l_miss = -l_hit; every other field unchanged";
  } else if (name == "octomap") {
    a.l_hit = logit(0.7); a.l_miss = logit(0.4); a.l_min = logit(0.12); a.l_max = logit(0.97); a.decay = 1.0;
    a.graduate_prob = 0.5 + 1e-12; a.demote_prob = 0.5; a.min_observations = 1; a.prune_prob = 0.0; a.binary = true;
    a.source = "OctoMap defaults (Hornung et al. 2013): prob_hit 0.7, prob_miss 0.4, clamping 0.12 / 0.97, occupancy threshold 0.5";
  } else if (name == "elite_like") {
    a.l_hit = std::log(9.0); a.l_miss = -std::log(9.0); a.l_min = -50.0; a.l_max = 50.0; a.decay = 1.0;
    a.graduate_prob = 0.5 + 1e-12; a.demote_prob = 0.5; a.min_observations = 1; a.prune_prob = 0.0; a.binary = true;
    a.source = "ELite Eq. 5-6 on the ray: odds factors 9 and 1/9, tau_l 0.5; no clamp found in the paper";
  } else if (name == "sprt" || name == "sprt_low_end" || name == "sprt_high_end") {
    a.sprt = true; a.alpha = 0.01; a.beta = 0.01; a.n_max_undecided = 80;
    if (name == "sprt") { a.m0 = 0.20; a.m1 = 0.70; }
    if (name == "sprt_low_end") { a.m0 = 0.20; a.m1 = 0.65; }
    if (name == "sprt_high_end") { a.m0 = 0.30; a.m1 = 0.75; }
    a.source = "PREREG.json conditional pair of the 2D backend (point pair and both interval ends); not frozen";
  } else {
    throw std::runtime_error("unknown arm '" + name + "'");
  }
  return a;
}

namespace armdetail {
// The engine of the phase 2 branch has `evidence_model`; the phase 1 engine does not. Both build.
template <class P> auto setLegacy(P& p, int) -> decltype((void)p.evidence_model, void()) {
  p.evidence_model = std::decay_t<decltype(p.evidence_model)>::Legacy;
}
template <class P> void setLegacy(P&, long) {}
template <class P> auto setSprt(P& p, const ArmSpec& a, int) -> decltype((void)p.evidence_model, void()) {
  p.evidence_model = std::decay_t<decltype(p.evidence_model)>::Sprt;
  p.m0 = a.m0; p.m1 = a.m1; p.alpha = a.alpha; p.beta = a.beta; p.n_max_undecided = a.n_max_undecided;
}
template <class P> void setSprt(P&, const ArmSpec& a, long) {
  throw std::runtime_error("arm '" + a.name + "' needs evidence_model = sprt, which this strata_core does not have (phase 2 engine missing)");
}
template <class P> constexpr auto hasSprt(int) -> decltype((void)std::declval<P&>().evidence_model, true) { return true; }
template <class P> constexpr bool hasSprt(long) { return false; }
}  // namespace armdetail

// True if this build of strata_core can run the sprt arm.
inline constexpr bool engineHasSprt() { return armdetail::hasSprt<strata_core::LayeredMapParams>(0); }

// Engine parameters of an arm. W is the window length in integration steps (layer_interval).
// periodicity: the Periodic veto on or off, the same for every arm of one run (PREREG_P3.md section 2.2).
inline strata_core::LayeredMapParams toParams(const ArmSpec& a, int window_steps, bool periodicity) {
  if (window_steps < 1) throw std::runtime_error("window length must be at least 1 step");
  strata_core::LayeredMapParams p;
  p.layer_interval = window_steps;
  p.enable_periodicity = periodicity;
  if (a.sprt) {
    armdetail::setSprt(p, a, 0);
    return p;
  }
  armdetail::setLegacy(p, 0);
  p.l_hit = a.l_hit; p.l_miss = a.l_miss; p.l_min = a.l_min; p.l_max = a.l_max; p.survival_decay = a.decay;
  p.graduate_prob = a.graduate_prob; p.demote_prob = a.demote_prob; p.min_observations = a.min_observations;
  p.prune_prob = a.prune_prob;
  return p;
}

// One line per arm, the text that PREREG_P3.md section 2 must contain (a test compares them).
inline std::string describe(const ArmSpec& a) {
  std::ostringstream o;
  o.precision(6);
  o << a.name << ": ";
  if (a.sprt) {
    o << "sprt m0=" << a.m0 << " m1=" << a.m1 << " alpha=" << a.alpha << " beta=" << a.beta << " n_max_undecided=" << a.n_max_undecided;
  } else {
    o << "legacy l_hit=" << a.l_hit << " l_miss=" << a.l_miss << " l_min=" << a.l_min << " l_max=" << a.l_max << " decay=" << a.decay
      << " graduate_prob=" << a.graduate_prob << " demote_prob=" << a.demote_prob << " min_observations=" << a.min_observations
      << " prune_prob=" << a.prune_prob;
  }
  o << " binary=" << (a.binary ? 1 : 0);
  return o.str();
}

}  // namespace p3
}  // namespace realdata
