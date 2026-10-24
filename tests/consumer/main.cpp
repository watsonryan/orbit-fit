/**
 * @file main.cpp
 * @brief Exercise fitting and curvature through the separately installed package.
 * @author watson
 */
#include <orbitfit/curvature.hpp>
#include <orbitfit/io.hpp>

int main() {
  const auto example = orbitfit::make_example_request();
  if (!example) return 1;
  const auto request = orbitfit::read_request(*example);
  if (!request) return 2;
  const auto fit = orbitfit::fit(request->samples, request->options);
  if (!fit || !fit->accepted) return 3;
  const auto delivery =
      orbitfit::prepare_products(*fit, request->samples, request->metadata, request->options);
  if (!delivery || !delivery->accepted || !delivery->omm_json || !delivery->tle_text) return 4;
  if (orbitfit::report_json(*fit, request->metadata, request->options, *delivery).empty()) return 5;
  const std::array<double, 6> offset{};
  const auto curvature = orbitfit::residual_curvature(fit->elements, request->samples.back(), offset);
  return curvature && curvature->parameter_count == 6 && curvature->residual_count == 6 ? 0 : 6;
}
