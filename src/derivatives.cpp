/**
 * @file derivatives.cpp
 * @brief Evaluate chart coordinates and native first-order SGP4 derivatives.
 * @author watson
 */
#include <ceres/jet.h>

#include "sgp4_model.hpp"

namespace orbitfit::detail {
using Jet = ceres::Jet<double, 7>;  ///< Value and the maximum seven active first derivatives.

Elements Chart::decode(const Eigen::VectorXd& x) const {
  if (static_cast<std::size_t>(x.size()) != dimension() || !x.allFinite())
    throw InvalidModel("Invalid parameter vector");
  std::array<double, 7> coordinates{};
  for (Eigen::Index i = 0; i < x.size(); ++i) coordinates[static_cast<std::size_t>(i)] = x[i];
  return sgp4_model::values(sgp4_model::decode(seed_, base_, retrograde_, drag_, coordinates));
}

void Chart::check_derivative_domain(const Eigen::VectorXd& x) const {
  if (static_cast<std::size_t>(x.size()) != dimension() || !x.allFinite())
    throw InvalidModel("Invalid parameter vector");
  const Eigen::VectorXd u = base_ + x.head(6) * 1e-3;
  if (std::hypot(u[1], u[2]) == 0.0 || std::hypot(u[3], u[4]) == 0.0)
    throw InvalidModel(
        "Automatic derivatives are undefined at an exactly circular or equatorial chart origin");
}

Linearization Chart::linearize(const Eigen::VectorXd& x, const Sample& sample, bool velocity) const {
  check_derivative_domain(x);
  std::array<Jet, 7> coordinates{};
  for (Eigen::Index i = 0; i < x.size(); ++i)
    coordinates[static_cast<std::size_t>(i)] = Jet(x[i], static_cast<int>(i));
  const auto model = sgp4_model::decode(seed_, base_, retrograde_, drag_, coordinates);
  const auto state = sgp4_model::propagate(model, sample.epoch);
  Linearization result{Eigen::VectorXd(velocity ? 6 : 3), Eigen::MatrixXd(velocity ? 6 : 3, x.size())};
  for (Eigen::Index row = 0; row < result.residual.size(); ++row) {
    const auto index = static_cast<std::size_t>(row);
    const double observed = index < 3 ? sample.state.position_m[index] : sample.state.velocity_mps[index - 3];
    result.residual[row] = state[index].a - observed;
    result.jacobian.row(row) = state[index].v.head(x.size()).transpose();
  }
  if (!result.residual.allFinite() || !result.jacobian.allFinite())
    throw InvalidModel("SGP4 automatic differentiation returned a non-finite residual or Jacobian");
  return result;
}

Result<State> propagate_native(const Elements& e, Epoch epoch) {
  try {
    const auto state = sgp4_model::propagate(
        sgp4_model::MeanElements<double>{e.epoch, e.inclination_rad, e.raan_rad, e.eccentricity,
                                         e.argument_of_perigee_rad, e.mean_anomaly_rad,
                                         e.mean_motion_rev_per_day, e.bstar},
        epoch);
    return State{{state[0], state[1], state[2]}, {state[3], state[4], state[5]}};
  } catch (const InvalidModel& ex) {
    return std::unexpected(Error{ErrorCode::propagation, ex.what()});
  }
}
}  // namespace orbitfit::detail
