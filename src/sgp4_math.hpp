/**
 * @file sgp4_math.hpp
 * @brief Preserve SGP4 remainder values while differentiating their local branches.
 * @author watson
 */
#pragma once

#include <ceres/jet.h>

#include <cmath>

namespace orbitfit::detail {
/** @brief Preserve libm remainder semantics. @param x Dividend. @param y Divisor. @return std::fmod(x,y). */
inline double sgp4_fmod(double x, double y) { return std::fmod(x, y); }

// fmod is locally x - trunc(x/y)*y, away from quotient changes. Keep the
// reference library's value (including its rounding and signed-zero behavior).
// Every SGP4 use has a constant, positive 2*pi divisor. A wrap boundary has
// no two-sided scalar derivative; the periodic physical state is tested across it.
/**
 * @brief Differentiate a fixed remainder quotient while preserving the libm primal value.
 * @tparam N Number of tangent directions.
 * @param x Dividend with derivatives.
 * @param y Divisor with derivatives; SGP4 uses constant positive 2 pi.
 * @return Remainder and its derivative on the executed quotient branch.
 * @note No two-sided derivative exists where trunc(x/y) changes; periodic state continuity is tested
 * separately.
 */
template <int N>
ceres::Jet<double, N> sgp4_fmod(const ceres::Jet<double, N>& x, const ceres::Jet<double, N>& y) {
  return {std::fmod(x.a, y.a), x.v - std::trunc(x.a / y.a) * y.v};
}
}  // namespace orbitfit::detail
