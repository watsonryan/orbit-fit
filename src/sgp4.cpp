/**
 * @file sgp4.cpp
 * @brief Validate mean elements and propagate with the unmodified Vallado reference kernel.
 * @author watson
 */
#include "SGP4.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "orbitfit/orbitfit.hpp"

namespace orbitfit {
Result<void> validate(const Elements& e) {
  const std::array values{
      e.inclination_rad,         e.raan_rad, e.eccentricity, e.argument_of_perigee_rad, e.mean_anomaly_rad,
      e.mean_motion_rev_per_day, e.bstar};
  const auto minimum = std::chrono::sys_days{std::chrono::year{1957} / 1 / 1};
  const auto maximum = std::chrono::sys_days{std::chrono::year{9999} / 12 / 31} + std::chrono::days{1};
  if (!std::ranges::all_of(values, [](double x) { return std::isfinite(x); }) || e.inclination_rad < 0 ||
      e.inclination_rad > std::numbers::pi || e.eccentricity < 0 || e.eccentricity >= 1 ||
      e.mean_motion_rev_per_day <= 0 || e.mean_motion_rev_per_day > 100 || e.epoch.utc < minimum ||
      e.epoch.utc >= maximum)
    return std::unexpected(Error{ErrorCode::invalid_input, "Invalid SGP4 mean elements or epoch"});
  return {};
}

Result<State> propagate(const Elements& e, Epoch epoch) {
  if (const auto check = validate(e); !check) return std::unexpected(check.error());
  const double minutes = seconds_between(epoch, e.epoch) / 60.0;
  if (!std::isfinite(minutes) || std::abs(minutes) > 5259600)
    return std::unexpected(Error{ErrorCode::propagation, "Propagation offset exceeds the ten-year guard"});
  // Fresh records make deep-space integration independent of evaluation order and threads.
  elsetrec record{};
  constexpr double tau = 2.0 * std::numbers::pi;
  const Epoch origin{std::chrono::sys_days{std::chrono::year{1949} / 12 / 31}};
  const double epoch_days = seconds_between(e.epoch, origin) / 86400.0;
  const bool initialized = SGP4Funcs::sgp4init(
      wgs72, 'i', "00000", epoch_days, e.bstar, 0, 0, e.eccentricity, e.argument_of_perigee_rad,
      e.inclination_rad, e.mean_anomaly_rad, e.mean_motion_rev_per_day * tau / 1440.0, e.raan_rad, record);
  if (!initialized || record.error != 0)
    return std::unexpected(
        Error{ErrorCode::propagation, "SGP4 initialization failed: " + std::to_string(record.error)});
  double r[3]{};
  double v[3]{};
  if (!SGP4Funcs::sgp4(record, minutes, r, v) || record.error != 0)
    return std::unexpected(
        Error{ErrorCode::propagation, "SGP4 propagation failed: " + std::to_string(record.error)});
  State state;
  for (std::size_t i = 0; i < 3; ++i) {
    if (!std::isfinite(r[i]) || !std::isfinite(v[i]))
      return std::unexpected(Error{ErrorCode::propagation, "SGP4 returned a non-finite state"});
    state.position_m[i] = r[i] * 1000;
    state.velocity_mps[i] = v[i] * 1000;
  }
  return state;
}

}  // namespace orbitfit
