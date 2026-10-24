/**
 * @file products.cpp
 * @brief Apply one serialized-product validation and acceptance policy for all callers.
 * @author watson
 */
#include "orbitfit/products.hpp"

#include <algorithm>

#include "fit_internal.hpp"
#include "orbitfit/io.hpp"

namespace orbitfit {
namespace {
// Serialization succeeds before this helper runs. Preserve the exact text even
// when parsing or physical evaluation fails so rejected deliveries are diagnosable.
/** @brief Score a reparsed product. @param parsed Round-trip parse result. @param window Validated fit-window
 * observations. @param options Physical gate and stop token. @return Per-format outcome; caller promotes stop
 * requests to cancellation. */
ProductValidation check_product(const Result<ElementSet>& parsed, std::span<const Sample> window,
                                const FitOptions& options) {
  if (!parsed) return {ProductStatus::parse_failed, {}, parsed.error().message};
  const auto physical = detail::evaluate_arc(parsed->elements, window, false, options.stop_token);
  if (!physical) return {ProductStatus::evaluation_failed, {}, physical.error().message};
  if (physical->metrics.rms_position_m > options.maximum_rms_position_m)
    return {ProductStatus::accuracy_rejected, physical->metrics, "Exported RMS position exceeds the target"};
  return {ProductStatus::accepted, physical->metrics, {}};
}
}  // namespace

Result<Delivery> prepare_products(const FitResult& fitted, std::span<const Sample> samples,
                                  const Metadata& metadata, const FitOptions& options, ProductPolicy policy) {
  if (options.stop_token.stop_requested() || fitted.termination == Termination::cancelled)
    return std::unexpected(Error{ErrorCode::cancelled, "Product validation cancelled"});
  if (policy != ProductPolicy::tle_required && policy != ProductPolicy::omm_sufficient)
    return std::unexpected(Error{ErrorCode::invalid_input, "Unknown product policy"});
  if (auto valid = detail::validate_request(samples, options); !valid) return std::unexpected(valid.error());
  const auto first = std::ranges::lower_bound(samples, options.start, {}, &Sample::epoch);
  const auto last = std::ranges::upper_bound(samples, options.stop, {}, &Sample::epoch);
  const std::span<const Sample> window(first, last);
  if (window.size() < 3 || fitted.elements.epoch != options.epoch ||
      fitted.metrics.sample_count != window.size())
    return std::unexpected(Error{ErrorCode::invalid_input, "Fit and product-validation window do not match"});

  Delivery delivery;
  delivery.policy = policy;
  delivery.continuous_accepted = fitted.accepted;
  delivery.continuous_metrics = fitted.metrics;
  const ElementSet set{fitted.elements, metadata};
  if (auto omm = to_omm_json(set)) {
    delivery.omm_json = std::move(*omm);
    delivery.omm = check_product(from_omm_json(*delivery.omm_json), window, options);
  } else {
    delivery.omm.message = omm.error().message;
  }
  if (options.stop_token.stop_requested())
    return std::unexpected(Error{ErrorCode::cancelled, "OMM validation cancelled"});
  if (auto tle = to_tle(set)) {
    delivery.tle_text = std::move(*tle);
    delivery.tle = check_product(from_tle(*delivery.tle_text), window, options);
  } else {
    delivery.tle.message = tle.error().message;
  }
  if (options.stop_token.stop_requested())
    return std::unexpected(Error{ErrorCode::cancelled, "TLE validation cancelled"});

  if (!delivery.continuous_accepted)
    delivery.reason = DeliveryReason::continuous_rejected;
  else if (delivery.omm.status != ProductStatus::accepted)
    delivery.reason = DeliveryReason::omm_rejected;
  else if (policy == ProductPolicy::tle_required && !delivery.tle_text)
    delivery.reason = DeliveryReason::tle_unavailable;
  else if (policy == ProductPolicy::tle_required && delivery.tle.status != ProductStatus::accepted)
    delivery.reason = DeliveryReason::tle_rejected;
  else
    delivery.reason = DeliveryReason::accepted;
  delivery.accepted = delivery.reason == DeliveryReason::accepted;
  return delivery;
}

std::string_view to_string(ProductPolicy policy) {
  switch (policy) {
    case ProductPolicy::tle_required:
      return "tle_required";
    case ProductPolicy::omm_sufficient:
      return "omm_sufficient";
  }
  return "unknown";
}
std::string_view to_string(ProductStatus status) {
  switch (status) {
    case ProductStatus::accepted:
      return "accepted";
    case ProductStatus::accuracy_rejected:
      return "accuracy_rejected";
    case ProductStatus::serialization_failed:
      return "serialization_failed";
    case ProductStatus::parse_failed:
      return "parse_failed";
    case ProductStatus::evaluation_failed:
      return "evaluation_failed";
  }
  return "unknown";
}
std::string_view to_string(DeliveryReason reason) {
  switch (reason) {
    case DeliveryReason::accepted:
      return "accepted";
    case DeliveryReason::continuous_rejected:
      return "continuous_rejected";
    case DeliveryReason::omm_rejected:
      return "omm_rejected";
    case DeliveryReason::tle_unavailable:
      return "tle_unavailable";
    case DeliveryReason::tle_rejected:
      return "tle_rejected";
  }
  return "unknown";
}
}  // namespace orbitfit
