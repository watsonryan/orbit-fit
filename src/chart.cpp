/**
 * @file chart.cpp
 * @brief Encode the fixed mean-element chart and reference numerical derivatives.
 * @author watson
 */
#include <cmath>
#include <numbers>

#include "fit_internal.hpp"

namespace orbitfit::detail {
namespace {
constexpr double pi = std::numbers::pi;  ///< Half-turn used to select the fixed chart hemisphere.
}
Chart::Chart(const Elements& seed, bool drag)
    : seed_(seed), base_(6), retrograde_(seed.inclination_rad > pi / 2), drag_(drag) {
  const double sign = retrograde_ ? -1 : 1;
  const double varpi = seed.argument_of_perigee_rad + sign * seed.raan_rad;
  const double e_scale = seed.eccentricity / std::sqrt(1 - seed.eccentricity * seed.eccentricity);
  const double t = std::tan((retrograde_ ? pi - seed.inclination_rad : seed.inclination_rad) / 2);
  base_ << std::log(seed.mean_motion_rev_per_day), e_scale * std::sin(varpi), e_scale * std::cos(varpi),
      t * std::sin(seed.raan_rad), t * std::cos(seed.raan_rad), seed.mean_anomaly_rad + varpi;
}
Eigen::VectorXd state_residual(const Chart& chart, const Eigen::VectorXd& x, const Sample& s, bool velocity) {
  const auto predicted = propagate(chart.decode(x), s.epoch);
  if (!predicted) throw InvalidModel(predicted.error().message);
  Eigen::VectorXd residual(velocity ? 6 : 3);
  for (int j = 0; j < 3; ++j) {
    residual[j] =
        predicted->position_m[static_cast<std::size_t>(j)] - s.state.position_m[static_cast<std::size_t>(j)];
    if (velocity)
      residual[j + 3] = predicted->velocity_mps[static_cast<std::size_t>(j)] -
                        s.state.velocity_mps[static_cast<std::size_t>(j)];
  }
  return residual;
}
Eigen::MatrixXd finite_difference_jacobian(const Chart& chart, const Eigen::VectorXd& x, const Sample& s,
                                           bool velocity, double step) {
  if (!std::isfinite(step) || step <= 0) throw InvalidModel("Invalid finite-difference step");
  Eigen::MatrixXd H(velocity ? 6 : 3, x.size());
  const auto center = state_residual(chart, x, s, velocity);
  for (Eigen::Index j = 0; j < x.size(); ++j) {
    auto a = x, b = x;
    a[j] += step;
    b[j] -= step;
    std::optional<Eigen::VectorXd> plus, minus;
    try {
      plus = state_residual(chart, a, s, velocity);
    } catch (const InvalidModel&) {
    }
    try {
      minus = state_residual(chart, b, s, velocity);
    } catch (const InvalidModel&) {
    }
    if (plus && minus)
      H.col(j) = (*plus - *minus) / (2 * step);
    else if (plus)
      H.col(j) = (*plus - center) / step;
    else if (minus)
      H.col(j) = (center - *minus) / step;
    else
      throw InvalidModel("No valid finite-difference neighborhood");
  }
  if (!H.allFinite()) throw InvalidModel("Non-finite Jacobian");
  return H;
}
}  // namespace orbitfit::detail
