/**
 * @file products.hpp
 * @brief Serialize, reparse, and validate delivered orbit products under an explicit policy.
 * @author watson
 */
#pragma once

#include "orbitfit/orbitfit.hpp"

namespace orbitfit {
/** @brief Products required for a successful delivery; both formats are always attempted. */
enum class ProductPolicy {
  tle_required,   ///< Continuous fit, OMM, and TLE must all pass; the default for a TLE fitter.
  omm_sufficient  ///< Continuous fit and OMM must pass; TLE failures remain visible but do not reject
                  ///< delivery.
};

/** @brief Stable outcome of serializing and checking one format on the fit window. */
enum class ProductStatus {
  accepted,              ///< Reparsed product passes the unweighted RMS position gate.
  accuracy_rejected,     ///< Evaluation succeeded but exceeds the RMS position gate.
  serialization_failed,  ///< Candidate or identity cannot be represented by this format.
  parse_failed,          ///< The emitted product could not be read back.
  evaluation_failed      ///< The reparsed elements could not be evaluated on the window.
};

/** @brief Stable overall delivery reason, in the precedence used by prepare_products(). */
enum class DeliveryReason {
  accepted,             ///< Every product required by the selected policy passed.
  continuous_rejected,  ///< The supplied fit was not accepted, regardless of product accuracy.
  omm_rejected,         ///< OMM serialization, round-trip evaluation, or accuracy failed.
  tle_unavailable,      ///< Required TLE serialization failed, for example due to catalog field limits.
  tle_rejected          ///< Required TLE round-trip evaluation or accuracy failed.
};

/** @brief Physical validation of one serialized format, independent of optimizer convergence. */
struct ProductValidation {
  ProductStatus status{ProductStatus::serialization_failed};  ///< Stable outcome; never infer from message.
  std::optional<Metrics> metrics;  ///< Present only after complete evaluation of every in-window sample.
  std::string message;             ///< Human-readable detail; wording is not a stable interface.
};

/**
 * @brief Owned products and the single delivery decision shared by library, CLI, and reporting.
 * @details A product can pass its accuracy gate while the overall delivery is rejected
 * because estimation did not converge. Text can be present for rejected products;
 * inspect accepted before operational use. No filesystem I/O is performed.
 */
struct Delivery {
  ProductPolicy policy{ProductPolicy::tle_required};  ///< Required output policy.
  bool continuous_accepted{};                         ///< Acceptance of the original continuous fit.
  Metrics continuous_metrics;                         ///< Full-window metrics copied from the fit.
  bool accepted{};  ///< Overall decision, equivalent to reason == DeliveryReason::accepted.
  DeliveryReason reason{DeliveryReason::continuous_rejected};  ///< Stable overall reason.
  ProductValidation omm;                                       ///< Validation of omm_json after reparsing.
  ProductValidation tle;                                       ///< Validation of tle_text after reparsing.
  std::optional<std::string> omm_json;  ///< Exact emitted OMM text, or absent if serialization failed.
  std::optional<Tle> tle_text;          ///< Exact emitted TLE lines, or absent if serialization failed.
};

/**
 * @brief Prepare both output formats and decide whether the delivery satisfies policy.
 * @param fitted Candidate returned by fit() with the same samples and options.
 * @param samples Entire original ordered TEME input; only [options.start, options.stop] is evaluated.
 * @param metadata Catalog identity; format-specific failures are retained in Delivery.
 * @param options Original fit controls, including RMS gate and cooperative stop token.
 * @param policy Require a TLE, or allow delivery based on OMM alone.
 * @return Owned products and diagnostics, or invalid_input/unsupported_frame/cancelled.
 * @details Validates input bounds and window, and rejects inconsistent epoch/sample counts.
 * Serialization, reparsing, and propagation failures are product outcomes, not unexpected errors.
 * Cancellation returns no delivery, including for a fit with termination=cancelled.
 * This checks training-window accuracy only; evaluate disjoint epochs separately for prediction.
 */
[[nodiscard]] Result<Delivery> prepare_products(const FitResult& fitted, std::span<const Sample> samples,
                                                const Metadata& metadata, const FitOptions& options,
                                                ProductPolicy policy = ProductPolicy::tle_required);

/** @brief Stable report spelling. @param policy Delivery policy. @return Name, or "unknown" for invalid
 * enums. */
[[nodiscard]] std::string_view to_string(ProductPolicy policy);
/** @brief Stable report spelling. @param status Product outcome. @return Name, or "unknown" for invalid
 * enums. */
[[nodiscard]] std::string_view to_string(ProductStatus status);
/** @brief Stable report spelling. @param reason Delivery outcome. @return Name, or "unknown" for invalid
 * enums. */
[[nodiscard]] std::string_view to_string(DeliveryReason reason);
}  // namespace orbitfit
