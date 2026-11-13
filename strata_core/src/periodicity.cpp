#include "strata_core/periodicity.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
namespace strata_core {
PeriodicityKernel::PeriodicityKernel(PeriodicityParams p) : params_(p),
  omega_(2.0 * M_PI / std::max(1, p.period_windows)), H_(std::max(0, p.n_harmonics)) {}

// Read-only view of a slot: n and s0 are copied, the arrays are read in place.
struct PeriodicityKernel::View {
  double n, s0;
  const double *c, *s, *ec, *es, *ec2, *es2;
};
PeriodicityKernel::View PeriodicityKernel::view(const double* slot) const {
  View v;
  v.n = slot[0]; v.s0 = slot[1];
  const std::size_t H = static_cast<std::size_t>(H_);
  v.c = slot + 2; v.s = v.c + H; v.ec = v.s + H; v.es = v.ec + H; v.ec2 = v.es + H; v.es2 = v.ec2 + H;
  return v;
}

void PeriodicityKernel::gather(double* slot, bool occupied, int window_index) const {
  const std::size_t H = static_cast<std::size_t>(H_);
  double* const c = slot + 2; double* const s = c + H; double* const ec = s + H;
  double* const es = ec + H; double* const ec2 = es + H; double* const es2 = ec2 + H;
  const double v = occupied ? 1.0 : 0.0;
  slot[0] += 1.0; slot[1] += v;
  for (int k = 0; k < params_.n_harmonics; ++k) {
    const double a = (k + 1) * omega_ * window_index;
    const double ca = std::cos(a), sa = std::sin(a);
    c[k] += v * ca;  s[k] += v * sa;   // sum of o_w e^{i(k+1)wt}
    ec[k] += ca;     es[k] += sa;      // sum of e^{i(k+1)wt} over touched windows
    ec2[k] += std::cos(2.0 * a); es2[k] += std::sin(2.0 * a);   // doubled phase
  }
}

// Centred (mean-removed) FreMEn coefficient, as in Krajnik et al. (T-RO 2017):
//   gamma_k = (1/n) sum_w (o_w - mean) e^{i(k+1) omega w}
//           = (1/n) [ sum_w o_w e^{...} - mean * sum_w e^{...} ].
// Subtracting the mean times the phase sum of the *touched* windows makes the
// coefficient exactly zero for any constant cell under any sampling pattern.
void PeriodicityKernel::centred(const View& cell, int k, double& ak, double& bk) const {
  const double mean = cell.s0 / cell.n;
  ak = 2.0 * (cell.c[k] - mean * cell.ec[k]) / cell.n;
  bk = 2.0 * (cell.s[k] - mean * cell.es[k]) / cell.n;
}

bool PeriodicityKernel::gated(const View& cell) const {
  // A harmonic estimate needs at least one period's worth of touched windows.
  return cell.n <= 0.0 || cell.n < static_cast<double>(params_.period_windows);
}

double PeriodicityKernel::harmonicAmplitude(const View& cell, int k) const {
  double ak, bk; centred(cell, k, ak, bk);
  const double amp = std::sqrt(ak * ak + bk * bk);
  // A constant cell cancels to rounding error, not exactly zero; report 0.
  return amp < 1e-12 ? 0.0 : amp;
}

// Noise-calibrated test quantities of harmonic k.
// Least-squares fit of o_w = mu + A cos(theta_w) + B sin(theta_w) over the touched
// windows (the floating-mean periodogram of Zechmeister & Kuerster 2009). With the
// centred phase sums y = sum (o - mean) (cos, sin) and the centred phase-design
// matrix M = sum (cos - cbar, sin - sbar)(cos - cbar, sin - sbar)^T, the variance
// explained by the harmonic is dchi = y^T M^{-1} y. Because the centred phases sum
// to zero, y = sum (o_w - m) (cos - cbar, sin - sbar) for ANY constant m, so under
// an iid Bernoulli(m) null dchi is a quadratic form in the independent, 1/4-sub-
// Gaussian variables o_w - m, whatever m is and whatever the sampling pattern.
HarmonicStat PeriodicityKernel::stat(const View& cell, int k) const {
  HarmonicStat h;
  h.n = cell.n;
  h.amplitude = harmonicAmplitude(cell, k);
  const double n = cell.n;
  const double mean = cell.s0 / n;
  h.chi0 = n * mean * (1.0 - mean);
  if (h.chi0 <= 1e-12) return h;                       // constant cell: nothing to explain
  const double cb = cell.ec[k] / n, sb = cell.es[k] / n;
  const double scc = 0.5 * (n + cell.ec2[k]) - n * cb * cb;
  const double sss = 0.5 * (n - cell.ec2[k]) - n * sb * sb;
  const double scs = 0.5 * cell.es2[k] - n * cb * sb;
  const double yc = cell.c[k] - mean * cell.ec[k];
  const double ys = cell.s[k] - mean * cell.es[k];
  const double tr = scc + sss;
  if (tr <= 1e-9 * n) return h;                        // all touches at one phase
  const double det = scc * sss - scs * scs;
  if (det <= 1e-9 * tr * tr) {
    // Rank-1 phase design (e.g. the Nyquist harmonic, or two phases only): project
    // onto the one identifiable direction. The rank-2 bound stays valid (it is
    // larger than the rank-1 one), so this is conservative.
    h.dchi = (yc * yc + ys * ys) / tr;
  } else {
    h.dchi = (sss * yc * yc - 2.0 * scs * yc * ys + scc * ys * ys) / det;
  }
  h.dchi = std::min(h.dchi, h.chi0);                   // guard rounding
  return h;
}

// Chernoff tail bound for dchi under the iid-Bernoulli null (proof in the paper,
// Proposition "calibrated test"): whitening gives dchi = |w|^2 with w a sum of
// independent terms (o_w - m) b_w, sum b_w b_w^T = I_2, and o_w - m is 1/4-sub-
// Gaussian (Hoeffding's lemma), so E exp(lambda |w|^2) <= (1 - lambda/2)^{-1}
// (Gaussian decoupling). Optimising Chernoff over lambda gives, with x = 2 dchi,
//   P(dchi >= r) <= x e^{1 - x}   for x > 1,
// for ANY m and ANY (non-adaptive) set of touched phases.
double PeriodicityKernel::tailBound(double dchi) {
  const double x = 2.0 * dchi;
  if (!(x > 1.0)) return 1.0;
  return std::min(1.0, x * std::exp(1.0 - x));
}

HarmonicStat PeriodicityKernel::harmonic(const double* slot, int k) const {
  if (!slot) return HarmonicStat{};
  const View cell = view(slot);
  if (cell.n <= 0.0 || k < 0 || k >= params_.n_harmonics) return HarmonicStat{};
  return stat(cell, k);
}

double PeriodicityKernel::predict(const double* slot, int window_index) const {
  if (!slot) return 0.5;
  const View cell = view(slot);
  if (cell.n <= 0.0) return 0.5;
  double val = cell.s0 / cell.n;
  for (int k = 0; k < params_.n_harmonics; ++k) {
    double ak, bk; centred(cell, k, ak, bk);
    const double a = (k + 1) * omega_ * window_index;
    val += ak * std::cos(a) + bk * std::sin(a);
  }
  return std::min(1.0, std::max(0.0, val));
}

double PeriodicityKernel::amplitude(const double* slot) const {
  if (!slot) return 0.0;
  const View cell = view(slot);
  if (gated(cell)) return 0.0;
  double amp = 0.0;
  for (int k = 0; k < params_.n_harmonics; ++k)
    amp = std::max(amp, harmonicAmplitude(cell, k));
  return amp;
}

double PeriodicityKernel::falseAlarm(const double* slot) const {
  if (!slot) return 1.0;
  const View cell = view(slot);
  if (gated(cell)) return 1.0;
  double best = 1.0;
  for (int k = 0; k < params_.n_harmonics; ++k)
    best = std::min(best, tailBound(stat(cell, k).dchi));
  return std::min(1.0, best * std::max(1, params_.n_harmonics));
}

double PeriodicityKernel::spentLevel(double alpha, double n, int period_windows) {
  const double T = std::max(1, period_windows);
  if (n < T) return 0.0;
  return alpha * T / (n * (n + 1.0));
}

bool PeriodicityKernel::isPeriodic(const double* slot, double a_min, double alpha, bool spend) const {
  if (!slot) return false;
  const View cell = view(slot);
  if (gated(cell)) return false;
  const double H = std::max(1, params_.n_harmonics);
  const double level = spend ? spentLevel(alpha, cell.n, params_.period_windows) : alpha;
  for (int k = 0; k < params_.n_harmonics; ++k) {
    const double a = harmonicAmplitude(cell, k);
    if (a <= 0.0 || a < a_min) continue;               // effect-size screen
    if (alpha >= 1.0) return true;                     // significance test disabled
    if (H * tailBound(stat(cell, k).dchi) <= level) return true;
  }
  return false;
}

// ---- CoeffPool ------------------------------------------------------------------------------
// Slots live in blocks that are never moved or copied, so growth costs one block and no
// reallocation of existing slots. A freed slot keeps the next free index in its first 4 bytes, so
// release() allocates nothing and cannot throw.
CoeffPool::CoeffPool(std::size_t slot_doubles) : stride_(std::max<std::size_t>(slot_doubles, 2)) {}

std::uint32_t CoeffPool::acquire() {
  std::uint32_t idx;
  if (free_head_ != kNone) {
    idx = free_head_;
    std::memcpy(&free_head_, at(idx), sizeof free_head_);
  } else {
    if (next_ >= kNone) throw std::length_error("CoeffPool: more than 2^32 - 2 live slots");
    idx = next_;
    if (idx / kBlockSlots >= blocks_.size()) {
      std::unique_ptr<double[]> b(new double[kBlockSlots * stride_]);   // may throw; nothing changed yet
      blocks_.push_back(std::move(b));                                  // may throw; b frees itself
    }
    ++next_;
  }
  double* p = at(idx);
  std::fill(p, p + stride_, 0.0);
  ++in_use_;
  return idx;
}

void CoeffPool::release(std::uint32_t idx) noexcept {
  std::memcpy(at(idx), &free_head_, sizeof free_head_);
  free_head_ = idx;
  --in_use_;
}

// ---- PeriodicityModel: the id-keyed wrapper -------------------------------------------------
// A cell has an entry iff it was gathered at least once: one std::vector<double> in the slot layout,
// one allocation. A missing entry is a null slot, which the kernel answers as it always answered one.
PeriodicityModel::PeriodicityModel(PeriodicityParams p) : kernel_(p) {}

void PeriodicityModel::gather(CellId id, bool occupied, int window_index) {
  auto& e = cells_[id];
  if (e.empty()) e.assign(PeriodicityKernel::slotDoubles(kernel_.params().n_harmonics), 0.0);
  kernel_.gather(e.data(), occupied, window_index);
}

const double* PeriodicityModel::slot(CellId id) const {
  auto it = cells_.find(id);
  return it == cells_.end() ? nullptr : it->second.data();
}
HarmonicStat PeriodicityModel::harmonic(CellId id, int k) const { return kernel_.harmonic(slot(id), k); }
double PeriodicityModel::predict(CellId id, int window_index) const { return kernel_.predict(slot(id), window_index); }
double PeriodicityModel::amplitude(CellId id) const { return kernel_.amplitude(slot(id)); }
double PeriodicityModel::falseAlarm(CellId id) const { return kernel_.falseAlarm(slot(id)); }
bool PeriodicityModel::isPeriodic(CellId id, double a_min, double alpha, bool spend) const {
  return kernel_.isPeriodic(slot(id), a_min, alpha, spend);
}
}  // namespace strata_core
