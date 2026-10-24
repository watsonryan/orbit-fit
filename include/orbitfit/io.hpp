#pragma once

#include <string_view>

#include "orbitfit/orbitfit.hpp"

namespace orbitfit {
struct FitRequest {
  Metadata metadata;
  FitOptions options;
  std::vector<Sample> samples;
};
[[nodiscard]] Result<FitRequest> read_request(std::string_view json);
[[nodiscard]] Result<ElementSet> from_omm_json(std::string_view json);
[[nodiscard]] Result<std::string> to_omm_json(const ElementSet& set);
[[nodiscard]] std::string report_json(const FitResult& result, const Metadata& metadata,
                                      const FitOptions& options,
                                      const std::optional<Metrics>& serialized_tle_metrics,
                                      std::string_view tle_error = {});
[[nodiscard]] Result<std::string> make_example_request();
}  // namespace orbitfit
