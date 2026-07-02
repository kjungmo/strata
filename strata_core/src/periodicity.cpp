#include "strata_core/periodicity.hpp"
#include <algorithm>
#include <cmath>
namespace strata_core {
PeriodicityModel::PeriodicityModel(PeriodicityParams p) : params_(p),
  omega_(2.0 * M_PI / std::max(1, p.period_windows)) {}

void PeriodicityModel::gather(CellId id, bool occupied, int window_index) {
  auto& cell = cells_[id];
  if (cell.c.empty()) { cell.c.assign(params_.n_harmonics, 0.0); cell.s.assign(params_.n_harmonics, 0.0); }
  const double v = occupied ? 1.0 : 0.0;
  cell.n += 1.0; cell.s0 += v;
  for (int k = 0; k < params_.n_harmonics; ++k) {
    const double a = (k + 1) * omega_ * window_index;
    cell.c[k] += v * std::cos(a);
    cell.s[k] += v * std::sin(a);
  }
}

double PeriodicityModel::predict(CellId id, int window_index) const {
  auto it = cells_.find(id);
  if (it == cells_.end() || it->second.n <= 0.0) return 0.5;
  const auto& cell = it->second;
  double val = cell.s0 / cell.n;
  for (int k = 0; k < params_.n_harmonics; ++k) {
    const double ak = 2.0 * cell.c[k] / cell.n, bk = 2.0 * cell.s[k] / cell.n;
    const double a = (k + 1) * omega_ * window_index;
    val += ak * std::cos(a) + bk * std::sin(a);
  }
  return std::min(1.0, std::max(0.0, val));
}

double PeriodicityModel::amplitude(CellId id) const {
  auto it = cells_.find(id);
  if (it == cells_.end() || it->second.n <= 0.0) return 0.0;
  // Fourier amplitude is only meaningful once a full period has been observed;
  // a truncated (partial-period) window gives a constant signal spurious amplitude.
  if (it->second.n < static_cast<double>(params_.period_windows)) return 0.0;
  const auto& cell = it->second;
  double amp = 0.0;
  for (int k = 0; k < params_.n_harmonics; ++k) {
    const double ak = 2.0 * cell.c[k] / cell.n, bk = 2.0 * cell.s[k] / cell.n;
    amp = std::max(amp, std::sqrt(ak * ak + bk * bk));
  }
  return amp;
}
}  // namespace strata_core
