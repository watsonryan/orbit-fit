/**
 * @file fit.cpp
 * @brief Coordinate validation, seeding, estimation, and full-window acceptance.
 * @author watson
 */
#include <algorithm>

#include "fit_internal.hpp"

namespace orbitfit {
Result<FitResult> fit(std::span<const Sample> samples, const FitOptions& o) {
  if (auto v = detail::validate_request(samples, o); !v) return std::unexpected(v.error());
  if (o.stop_token.stop_requested())
    return std::unexpected(Error{ErrorCode::cancelled, "Fit cancelled before initialization"});
  const auto first = std::ranges::lower_bound(samples, o.start, {}, &Sample::epoch);
  const auto last = std::ranges::upper_bound(samples, o.stop, {}, &Sample::epoch);
  const std::span<const Sample> window(first, last);
  if (window.size() < 3)
    return std::unexpected(
        Error{ErrorCode::invalid_input, "At least three samples must lie inside the fit window"});
  auto seed_result =
      o.seed ? Result<Elements>{*o.seed} : detail::initial_elements(samples, o.epoch, o.fixed_bstar);
  if (!seed_result) return std::unexpected(seed_result.error());
  auto seed = *seed_result;
  if (!o.fit_bstar) seed.bstar = o.fixed_bstar;
  if (auto check = propagate(seed, o.epoch); !check) return std::unexpected(check.error());

  const detail::Chart chart(seed, o.fit_bstar);
  auto estimate = detail::estimate(window, chart, o);
  if (!estimate) return std::unexpected(estimate.error());
  auto result = std::move(*estimate);
  try {
    if (o.weighting == Weighting::approximation)
      result.diagnostics.emplace_back(
          "Approximation weights are design scales; no statistical covariance is inferred");
    else
      result.diagnostics.emplace_back(
          "Measurement mode assumes independent epochs and supplied within-epoch covariance; temporal "
          "correlations are not modeled");
    if (result.termination == Termination::cancelled) return result;
    auto evaluation = detail::evaluate_arc(result.elements, window, true, o.stop_token);
    if (!evaluation) {
      if (evaluation.error().code != ErrorCode::cancelled) return std::unexpected(evaluation.error());
      result.termination = Termination::cancelled;
      result.diagnostics.emplace_back("Cancelled during full-window validation; metrics are unavailable");
      return result;
    }
    result.metrics = evaluation->metrics;
    result.residuals = std::move(evaluation->residuals);
    result.accepted = result.termination == Termination::converged && result.rank == result.parameter_count &&
                      result.metrics.rms_position_m <= o.maximum_rms_position_m;
  } catch (const std::exception& ex) {
    return std::unexpected(Error{ErrorCode::solver, ex.what()});
  }
  return result;
}
}  // namespace orbitfit
