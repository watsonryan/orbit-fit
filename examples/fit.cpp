/**
 * @file fit.cpp
 * @brief Fit a synthetic request and verify the accuracy of the delivered TLE.
 * @author watson
 */
#include <cstdio>
#include <print>
#include <stdexcept>

#include "orbitfit/io.hpp"

namespace {
template <class T>
T take(orbitfit::Result<T> result) {
  if (!result) throw std::runtime_error(result.error().message);
  return std::move(*result);
}
}  // namespace

int main() {
  try {
    // Replace this synthetic request with read_request(your_json_text) for use
    // with observed or independently propagated TEME state samples.
    const auto request = take(orbitfit::read_request(take(orbitfit::make_example_request())));
    const auto fitted = take(orbitfit::fit(request.samples, request.options));
    if (!fitted.accepted) {
      std::println(stderr, "Continuous fit rejected: {}", orbitfit::to_string(fitted.termination));
      return 2;
    }
    const auto delivery =
        take(orbitfit::prepare_products(fitted, request.samples, request.metadata, request.options));
    std::print("{}", orbitfit::report_json(fitted, request.metadata, request.options, delivery));
    if (delivery.tle_text) std::println("{}\n{}", delivery.tle_text->line1, delivery.tle_text->line2);
    return delivery.accepted ? 0 : 2;
  } catch (const std::exception& error) {
    std::println(stderr, "{}", error.what());
    return 1;
  }
}
