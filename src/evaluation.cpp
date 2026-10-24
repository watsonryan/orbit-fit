/**
 * @file evaluation.cpp
 * @brief Evaluate physical errors and optional residuals in one propagation pass.
 * @author watson
 */
#include <algorithm>
#include <cmath>

#include "fit_internal.hpp"

namespace orbitfit::detail {
Result<ArcEvaluation> evaluate_arc(const Elements& elements, std::span<const Sample> samples,
                                   bool keep_residuals, std::stop_token stop) {
  if (samples.empty())
    return std::unexpected(Error{ErrorCode::invalid_input, "Cannot evaluate an empty sample set"});
  ArcEvaluation result;
  if (keep_residuals) result.residuals.reserve(samples.size());
  auto& metrics = result.metrics;
  for (const auto& sample : samples) {
    if (stop.stop_requested())
      return std::unexpected(Error{ErrorCode::cancelled, "Arc evaluation cancelled"});
    if (sample.frame != Frame::teme)
      return std::unexpected(Error{ErrorCode::unsupported_frame, "Expected TEME samples"});
    const auto predicted = propagate(elements, sample.epoch);
    if (!predicted) return std::unexpected(predicted.error());
    Residual residual{.epoch = sample.epoch, .position_m = {}, .velocity_mps = {}};
    double r2 = 0, v2 = 0;
    for (std::size_t j = 0; j < 3; ++j) {
      const double dr = predicted->position_m[j] - sample.state.position_m[j];
      const double dv = predicted->velocity_mps[j] - sample.state.velocity_mps[j];
      residual.position_m[j] = dr;
      residual.velocity_mps[j] = dv;
      r2 += dr * dr;
      v2 += dv * dv;
    }
    if (!std::isfinite(r2) || !std::isfinite(v2))
      return std::unexpected(Error{ErrorCode::invalid_input, "Non-finite state residual"});
    ++metrics.sample_count;
    const auto count = static_cast<double>(metrics.sample_count);
    metrics.rms_position_m += (r2 - metrics.rms_position_m) / count;
    metrics.rms_velocity_mps += (v2 - metrics.rms_velocity_mps) / count;
    metrics.max_position_m = std::max(metrics.max_position_m, std::sqrt(r2));
    metrics.max_velocity_mps = std::max(metrics.max_velocity_mps, std::sqrt(v2));
    if (keep_residuals) result.residuals.push_back(residual);
  }
  metrics.rms_position_m = std::sqrt(metrics.rms_position_m);
  metrics.rms_velocity_mps = std::sqrt(metrics.rms_velocity_mps);
  return result;
}
}  // namespace orbitfit::detail

namespace orbitfit {
Result<Metrics> evaluate(const Elements& elements, std::span<const Sample> samples) {
  const auto evaluated = detail::evaluate_arc(elements, samples, false);
  if (!evaluated) return std::unexpected(evaluated.error());
  return evaluated->metrics;
}
}  // namespace orbitfit
