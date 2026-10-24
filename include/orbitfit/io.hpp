/**
 * @file io.hpp
 * @brief Strict JSON request parsing, GP OMM JSON, and diagnostic reports.
 * @author watson
 */
#pragma once

#include <stop_token>
#include <string_view>

#include "orbitfit/products.hpp"

namespace orbitfit {
/** @brief Parsed request; invoke fit() for cross-field and numerical validation. */
struct FitRequest {
  Metadata metadata;            ///< Catalog identity required for output products.
  FitOptions options;           ///< Fit configuration, with defaults applied to omitted optional fields.
  std::vector<Sample> samples;  ///< Supplied SI TEME states and optional covariances.
};

/**
 * @brief Parse orbit-fit.request.v1 with explicit TEME and UTC declarations.
 * @param json Request text, at most 64 MiB and nesting depth at most 32.
 * @param stop Cooperative cancellation checked during parsing and sample conversion.
 * @return Parsed request or invalid_input/unsupported_frame/cancelled.
 * @details Rejects duplicate keys, unknown request fields, non-finite values, and
 * malformed identity. Requires 3..100000 samples. This parses the schema;
 * fit() additionally checks time ordering, coverage, covariance, and solver controls.
 * The returned options retain the supplied stop token for subsequent fitting.
 */
[[nodiscard]] Result<FitRequest> read_request(std::string_view json, std::stop_token stop = {});

/**
 * @brief Parse a GP OMM JSON object with explicit SGP4, EARTH, TEME, and UTC metadata.
 * @param json A single object, at most 64 MiB and nesting depth at most 32.
 * @return Mean elements and metadata, or invalid_input.
 * @details Allows additional OMM properties, rejects duplicate keys, and accepts
 * EPOCH with or without its final Z because TIME_SYSTEM explicitly declares UTC.
 * This is the documented GP JSON profile, not a complete CCSDS XML/KVN reader.
 */
[[nodiscard]] Result<ElementSet> from_omm_json(std::string_view json);

/**
 * @brief Emit a GP OMM JSON object at floating-point round-trip precision.
 * @param set Valid mean elements and catalog identity.
 * @return Formatted JSON ending in a newline, or invalid_input/serialization.
 * @details Includes explicit frame/time/theory metadata. Angular fields are
 * degrees; mean-motion derivatives are zero. It does not estimate covariance.
 */
[[nodiscard]] Result<std::string> to_omm_json(const ElementSet& set);

/**
 * @brief Encode orbit-fit.report.v2 diagnostics, delivery decision, and reproducibility information.
 * @param result Candidate and diagnostics returned by fit().
 * @param metadata Identity associated with the request.
 * @param options Options used for that fit.
 * @param delivery Unmodified result of prepare_products() for this fit and these options.
 * @return Formatted report JSON ending in a newline.
 * @details Serializes the shared decision without recomputing acceptance. Product
 * acceptance means the format passed its physical gate; overall acceptance also
 * requires the continuous fit and the selected policy. Does not infer covariance.
 */
[[nodiscard]] std::string report_json(const FitResult& result, const Metadata& metadata,
                                      const FitOptions& options, const Delivery& delivery);

/**
 * @brief Generate a self-contained synthetic two-hour SGP4 fitting request.
 * @return orbit-fit.request.v1 JSON, or a propagation error.
 * @details Uses catalog 99999 solely as a synthetic example. The states share
 * the fitter's propagation model and do not constitute independent accuracy evidence.
 */
[[nodiscard]] Result<std::string> make_example_request();
}  // namespace orbitfit
