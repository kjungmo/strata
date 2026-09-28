#include "strata_core/periodicity.hpp"
#include <algorithm>
#include <cmath>
namespace strata_core {
PeriodicityModel::PeriodicityModel(PeriodicityParams p) : params_(p),
  omega_(2.0 * M_PI / std::max(1, p.period_windows)) {}

void PeriodicityModel::gather(CellId id, bool occupied, int window_index) {
  auto& cell = cells_[id];
  if (cell.c.empty()) {
    cell.c.assign(params_.n_harmonics, 0.0); cell.s.assign(params_.n_harmonics, 0.0);
    cell.ec.assign(params_.n_harmonics, 0.0); cell.es.assign(params_.n_harmonics, 0.0);
  }
  const double v = occupied ? 1.0 : 0.0;
  cell.n += 1.0; cell.s0 += v;
  for (int k = 0; k < params_.n_harmonics; ++k) {
    const double a = (k + 1) * omega_ * window_index;
    const double ca = std::cos(a), sa = std::sin(a);
    cell.c[k] += v * ca;  cell.s[k] += v * sa;   // sum of o_w e^{i(k+1)wt}
    cell.ec[k] += ca;     cell.es[k] += sa;      // sum of e^{i(k+1)wt} over touched windows
  }
}

// Centred (mean-removed) FreMEn coefficient, as in Krajnik et al. (T-RO 2017):
//   gamma_k = (1/n) sum_w (o_w - mean) e^{i(k+1) omega w}
//           = (1/n) [ sum_w o_w e^{...} - mean * sum_w e^{...} ].
// Subtracting the mean times the phase sum of the *touched* windows makes the
// coefficient exactly zero for any constant cell under any sampling pattern.
void PeriodicityModel::centred(const Coeff& cell, int k, double& ak, double& bk) const {
  const double mean = cell.s0 / cell.n;
  ak = 2.0 * (cell.c[k] - mean * cell.ec[k]) / cell.n;
  bk = 2.0 * (cell.s[k] - mean * cell.es[k]) / cell.n;
}

double PeriodicityModel::predict(CellId id, int window_index) const {
  auto it = cells_.find(id);
  if (it == cells_.end() || it->second.n <= 0.0) return 0.5;
  const auto& cell = it->second;
  double val = cell.s0 / cell.n;
  for (int k = 0; k < params_.n_harmonics; ++k) {
    double ak, bk; centred(cell, k, ak, bk);
    const double a = (k + 1) * omega_ * window_index;
    val += ak * std::cos(a) + bk * std::sin(a);
  }
  return std::min(1.0, std::max(0.0, val));
}

double PeriodicityModel::amplitude(CellId id) const {
  auto it = cells_.find(id);
  if (it == cells_.end() || it->second.n <= 0.0) return 0.0;
  // A harmonic estimate needs at least one period's worth of touched windows.
  if (it->second.n < static_cast<double>(params_.period_windows)) return 0.0;
  const auto& cell = it->second;
  double amp = 0.0;
  for (int k = 0; k < params_.n_harmonics; ++k) {
    double ak, bk; centred(cell, k, ak, bk);
    amp = std::max(amp, std::sqrt(ak * ak + bk * bk));
  }
  // A constant cell cancels to rounding error, not exactly zero; report 0.
  return amp < 1e-12 ? 0.0 : amp;
}
}  // namespace strata_core
