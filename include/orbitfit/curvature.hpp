/**
 * @file curvature.hpp
 * @brief Experimental residual Jacobians and Hessians in the normalized mean-element chart.
 * @author watson
 */
#pragma once

#include <array>
#include <span>

#include "orbitfit/types.hpp"

namespace orbitfit {
/**
 * @brief Raw state-residual derivatives with fixed-capacity standard arrays.
 * @details Only leading parameter_count and residual_count entries are active;
 * inactive storage is zero. Derivatives use normalized chart offsets, not printed
 * TLE fields. Hessians are returned without forced symmetrization. The evaluator
 * follows executed SGP4 branches and does not differentiate across discontinuities.
 */
struct ResidualCurvature {
  /** @brief One residual's gradient, with capacity for seven coordinates. */
  using Gradient = std::array<double, 7>;
  /** @brief One residual's Hessian, indexed [coordinate][coordinate]. */
  using Hessian = std::array<Gradient, 7>;
  std::size_t parameter_count{};       ///< Six with fixed BSTAR, or seven with its derivatives.
  std::size_t residual_count{};        ///< Three for position-only, otherwise six.
  std::array<double, 6> residual{};    ///< Predicted-minus-observed [x,y,z,vx,vy,vz], in SI units.
  std::array<Gradient, 6> jacobian{};  ///< First derivatives per dimensionless chart offset.
  std::array<Hessian, 6>
      hessians{};  ///< Mixed second derivatives, indexed [residual][coordinate][coordinate].
};

/**
 * @brief Evaluate residuals, Jacobians, and residual Hessians using nested Ceres Jets.
 * @param seed Valid mean elements defining the fixed epoch and prograde/retrograde chart.
 * @param sample Finite TEME observation; covariance is ignored.
 * @param offset Six or seven finite, dimensionless local coordinates; zero evaluates the seed.
 * @param use_velocity Include velocity residuals and derivatives when true.
 * @return Raw derivatives or an input/frame/propagation error, including singular chart origins.
 * @details Coordinates are log(mean motion), two eccentricity-vector components,
 * two inclination-vector components, mean longitude, and optional BSTAR. The
 * first six increments scale by 1e-3; BSTAR by 1e-4. Six entries hold BSTAR at
 * seed.bstar. See docs/curvature.md for exact chart definitions and whitening.
 * The least-squares objective Hessian is J^T J + sum_i r_i Hessian(r_i).
 * Covariance whitening must transform all derivative orders; robust objectives
 * require loss derivatives as well. No optimizer or covariance estimate is enabled.
 * @note Thread-safe for immutable input; evaluated only on explicit request.
 */
[[nodiscard]] Result<ResidualCurvature> residual_curvature(const Elements& seed, const Sample& sample,
                                                           std::span<const double> offset,
                                                           bool use_velocity = true);
}  // namespace orbitfit
