/**
 * @file curvature.cpp
 * @brief Evaluate residual Hessians with bounded-storage nested automatic differentiation.
 * @author watson
 */
#include "orbitfit/curvature.hpp"

#include <algorithm>
#include <cmath>

#include "second_order.hpp"
#include "sgp4_model.hpp"

namespace orbitfit::detail {
ResidualCurvature Chart::curvature(const Eigen::VectorXd& x, const Sample& sample, bool velocity) const {
  check_derivative_domain(x);
  ResidualCurvature result;
  result.parameter_count = dimension();
  result.residual_count = velocity ? 6 : 3;
  for (std::size_t i = 0; i < result.parameter_count; ++i) {
    std::array<SecondOrder, 7> coordinates{};
    for (Eigen::Index j = 0; j < x.size(); ++j)
      coordinates[static_cast<std::size_t>(j)] =
          SecondOrder::variable(x[j], static_cast<int>(j), static_cast<int>(i));
    const auto model = sgp4_model::decode(seed_, base_, retrograde_, drag_, coordinates);
    const auto state = sgp4_model::propagate(model, sample.epoch);
    for (std::size_t row = 0; row < result.residual_count; ++row) {
      const auto& jet = state[row].jet;
      if (i == 0) {
        const double observed = row < 3 ? sample.state.position_m[row] : sample.state.velocity_mps[row - 3];
        result.residual[row] = jet.a.a - observed;
        if (!std::isfinite(result.residual[row])) throw InvalidModel("Non-finite second-order residual");
        for (std::size_t j = 0; j < result.parameter_count; ++j) {
          result.jacobian[row][j] = jet.a.v[static_cast<Eigen::Index>(j)];
          if (!std::isfinite(result.jacobian[row][j])) throw InvalidModel("Non-finite second-order Jacobian");
        }
      }
      for (std::size_t j = 0; j < result.parameter_count; ++j) {
        const double h = jet.v[0].v[static_cast<Eigen::Index>(j)];
        if (!std::isfinite(h)) throw InvalidModel("Non-finite residual Hessian");
        result.hessians[row][i][j] = h;
      }
    }
  }
  return result;
}
}  // namespace orbitfit::detail

namespace orbitfit {
Result<ResidualCurvature> residual_curvature(const Elements& seed, const Sample& sample,
                                             std::span<const double> offset, bool use_velocity) {
  if (auto valid = validate(seed); !valid) return std::unexpected(valid.error());
  if (sample.frame != Frame::teme)
    return std::unexpected(Error{ErrorCode::unsupported_frame, "Curvature requires a TEME sample"});
  if ((offset.size() != 6 && offset.size() != 7) ||
      !std::ranges::all_of(offset, [](double x) { return std::isfinite(x); }))
    return std::unexpected(Error{ErrorCode::invalid_input, "Curvature requires six or seven finite offsets"});
  for (const auto& vector : {sample.state.position_m, sample.state.velocity_mps})
    if (!std::ranges::all_of(vector, [](double x) { return std::isfinite(x); }))
      return std::unexpected(Error{ErrorCode::invalid_input, "Curvature requires a finite sample state"});
  try {
    const detail::Chart chart(seed, offset.size() == 7);
    return chart.curvature(
        Eigen::Map<const Eigen::VectorXd>(offset.data(), static_cast<Eigen::Index>(offset.size())), sample,
        use_velocity);
  } catch (const detail::InvalidModel& ex) {
    return std::unexpected(Error{ErrorCode::propagation, ex.what()});
  }
}
}  // namespace orbitfit
