/**
 * @file validation.cpp
 * @brief Check fitting inputs before constructing an estimation problem.
 * @author watson
 */
#include <algorithm>
#include <cmath>

#include "fit_internal.hpp"

namespace orbitfit::detail {
Result<void> validate_request(std::span<const Sample> samples, const FitOptions& o) {
  if (samples.size() < 3 || samples.size() > 100000 || o.start >= o.stop || o.epoch < o.start ||
      o.epoch > o.stop || o.maximum_iterations == 0 || o.maximum_iterations > 10000 ||
      o.maximum_samples > 100000 || (o.maximum_samples != 0 && o.maximum_samples < 3))
    return std::unexpected(
        Error{ErrorCode::invalid_input, "Invalid window, fit epoch, sample count, or iteration limit"});
  if ((o.weighting != Weighting::approximation && o.weighting != Weighting::independent_measurements) ||
      (o.derivatives != Derivatives::automatic && o.derivatives != Derivatives::finite_difference))
    return std::unexpected(Error{ErrorCode::invalid_input, "Unknown weighting or derivative selection"});
  for (double x : {o.position_scale_m, o.velocity_scale_mps, o.relative_cost_tolerance,
                   o.absolute_cost_tolerance, o.gradient_tolerance, o.maximum_rms_position_m})
    if (!std::isfinite(x) || x <= 0)
      return std::unexpected(Error{ErrorCode::invalid_input,
                                   "Scales, tolerances, and accuracy target must be finite and positive"});
  if (!std::isfinite(o.fixed_bstar) ||
      (o.huber_threshold && (!std::isfinite(*o.huber_threshold) || *o.huber_threshold <= 0)))
    return std::unexpected(Error{ErrorCode::invalid_input, "Invalid BSTAR or robust threshold"});
  for (std::size_t i = 0; i < samples.size(); ++i) {
    if (o.stop_token.stop_requested())
      return std::unexpected(Error{ErrorCode::cancelled, "Fit cancelled during input validation"});
    const auto& s = samples[i];
    const auto minimum = std::chrono::sys_days{std::chrono::year{1957} / 1 / 1};
    const auto maximum = std::chrono::sys_days{std::chrono::year{10000} / 1 / 1};
    if (s.epoch.utc < minimum || s.epoch.utc >= maximum)
      return std::unexpected(Error{ErrorCode::invalid_input, "Sample epochs require years 1957..9999"});
    if (s.frame != Frame::teme)
      return std::unexpected(Error{ErrorCode::unsupported_frame, "Convert samples to TEME before fitting"});
    if (i && samples[i - 1].epoch >= s.epoch)
      return std::unexpected(
          Error{ErrorCode::invalid_input, "Sample epochs must be strictly increasing and unique"});
    for (const auto& vector : {s.state.position_m, s.state.velocity_mps})
      if (!std::ranges::all_of(vector, [](double x) { return std::isfinite(x); }))
        return std::unexpected(Error{ErrorCode::invalid_input, "Samples must contain finite states"});
  }
  if (samples.front().epoch > o.start || samples.back().epoch < o.stop)
    return std::unexpected(Error{ErrorCode::invalid_input, "Samples must cover the requested fit window"});
  if (o.seed) {
    if (auto valid = validate(*o.seed); !valid) return valid;
    if (o.seed->epoch != o.epoch)
      return std::unexpected(Error{ErrorCode::invalid_input,
                                   "Seed epoch must equal fit epoch; elements cannot simply be re-tagged"});
  }
  return {};
}
}  // namespace orbitfit::detail
