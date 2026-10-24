/**
 * @file cancellation.cpp
 * @brief Handle both early and partial cancellation through the synchronous C++ API.
 * @author watson
 */
#include <print>
#include <stop_token>

#include "orbitfit/io.hpp"

int main() {
  const auto text = orbitfit::make_example_request();
  if (!text) return 1;
  auto request = orbitfit::read_request(*text);
  if (!request) return 1;
  std::stop_source stop;
  request->options.stop_token = stop.get_token();
  // An application can request_stop() from another thread while fit() runs.
  // A pre-requested stop makes this documented example deterministic.
  stop.request_stop();
  const auto fitted = orbitfit::fit(request->samples, request->options);
  if (!fitted) {
    if (fitted.error().code != orbitfit::ErrorCode::cancelled) return 1;
    std::println("Cancelled before a candidate was available");
    return 0;
  }
  if (fitted->termination == orbitfit::Termination::cancelled) {
    std::println("Cancelled with an unaccepted partial candidate");
    return fitted->accepted ? 1 : 0;
  }
  return 1;
}
