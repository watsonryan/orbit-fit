/**
 * @file sgp4_model.hpp
 * @brief Share scalar-generic mean-element decoding and fresh-record SGP4 propagation.
 * @author watson
 */
#pragma once

#include <array>
#include <cmath>
#include <numbers>

#include "SGP4.hpp"
#include "fit_internal.hpp"
#include "sgp4_math.hpp"

/** @brief Shared value/first-order/second-order chart and adapted propagation path. */
namespace orbitfit::detail::sgp4_model {
inline constexpr double pi = std::numbers::pi;  ///< Half-turn in radians.
inline constexpr double tau = 2.0 * pi;         ///< Full-turn in radians.

/** @brief Derivative-carrying mean elements. @tparam Scalar Double, first-order Jet, or SecondOrder. */
template <class Scalar>
struct MeanElements {
  Epoch epoch;          ///< Fixed civil-UTC element epoch, never differentiated.
  Scalar inclination;   ///< Inclination in radians.
  Scalar node;          ///< Right ascension of ascending node in radians.
  Scalar eccentricity;  ///< Mean eccentricity, dimensionless.
  Scalar perigee;       ///< Argument of perigee in radians.
  Scalar anomaly;       ///< Mean anomaly in radians.
  Scalar motion;        ///< Mean motion in revolutions per day.
  Scalar bstar;         ///< BSTAR in inverse Earth radii.
};

/** @brief Extract a scalar value. @param x Real scalar. @return x unchanged. */
inline double value(double x) { return x; }
/** @brief Recursively extract the primal value. @tparam T Inner scalar. @tparam N Tangent width. @param x
 * Jet. @return Real value without derivatives. */
template <class T, int N>
double value(const ceres::Jet<T, N>& x) {
  return value(x.a);
}

/** @brief Strip derivatives for domain validation. @tparam Scalar Arithmetic scalar. @param e Mean elements.
 * @return Public value-only elements. */
template <class Scalar>
Elements values(const MeanElements<Scalar>& e) {
  return {e.epoch,          value(e.inclination), value(e.node),   value(e.eccentricity),
          value(e.perigee), value(e.anomaly),     value(e.motion), value(e.bstar)};
}

/** @brief Wrap a local angle branch to [0, 2 pi). @tparam Scalar Arithmetic scalar. @param angle Radians.
 * @return Wrapped value with local branch derivatives. */
template <class Scalar>
Scalar wrap(const Scalar& angle) {
  Scalar result = sgp4_fmod(angle, Scalar(tau));
  return value(result) < 0.0 ? result + tau : result;
}

/**
 * @brief Decode the same normalized chart for every derivative order.
 * @tparam Scalar Double, first-order Jet, or SecondOrder.
 * @param seed Fixed epoch and baseline BSTAR.
 * @param base Six unscaled seed chart coordinates.
 * @param retrograde Fixed hemisphere selected at initialization.
 * @param drag Include the seventh coordinate when true.
 * @param x Seven storage slots; first six scale by 1e-3, BSTAR by 1e-4.
 * @return Mean elements carrying all active derivatives.
 * @pre Differentiated inputs have nonzero eccentricity and plane-vector magnitudes.
 * @note No derivative passes through the fixed hemisphere selection.
 */
template <class Scalar>
MeanElements<Scalar> decode(const Elements& seed, const Eigen::VectorXd& base, bool retrograde, bool drag,
                            const std::array<Scalar, 7>& x) {
  using std::atan;
  using std::atan2;
  using std::exp;
  using std::hypot;
  std::array<Scalar, 6> u;
  for (std::size_t i = 0; i < u.size(); ++i) u[i] = base[static_cast<Eigen::Index>(i)] + x[i] * 1e-3;
  const Scalar radius = hypot(u[1], u[2]);
  const Scalar eccentricity = radius / hypot(Scalar(1.0), radius);
  const Scalar varpi = atan2(u[1], u[2]);
  const Scalar node = atan2(u[3], u[4]);
  const Scalar tilt = 2.0 * atan(hypot(u[3], u[4]));
  return {seed.epoch,
          retrograde ? pi - tilt : tilt,
          wrap(node),
          eccentricity,
          wrap(varpi - (retrograde ? -1.0 : 1.0) * node),
          wrap(u[5] - varpi),
          exp(u[0]),
          seed.bstar + (drag ? x[6] * 1e-4 : Scalar(0.0))};
}

/**
 * @brief Initialize a fresh derivative-carrying record and propagate once.
 * @tparam Scalar Double, first-order Jet, or SecondOrder.
 * @param e Valid mean elements with optional derivatives.
 * @param epoch Target civil-UTC observation epoch, treated as constant.
 * @return TEME [x,y,z,vx,vy,vz] in metres and metres per second.
 * @throws InvalidModel On invalid elements, offset beyond ten years, or SGP4 failure/non-finite values.
 * @details Each call owns resonance integrator state; call order and concurrent calls cannot reuse it.
 */
template <class Scalar>
std::array<Scalar, 6> propagate(const MeanElements<Scalar>& e, Epoch epoch) {
  if (const auto check = validate(values(e)); !check) throw InvalidModel(check.error().message);
  const double minutes = seconds_between(epoch, e.epoch) / 60.0;
  if (!std::isfinite(minutes) || std::abs(minutes) > 5259600)
    throw InvalidModel("Propagation offset exceeds the ten-year guard");
  const Epoch origin{std::chrono::sys_days{std::chrono::year{1949} / 12 / 31}};
  const Scalar epoch_days(seconds_between(e.epoch, origin) / 86400.0);
  // Initialization carries the same derivatives as propagation. Fresh records
  // keep deep-space integrator state local to each factor evaluation.
  vallado_ad::Record<Scalar> record{};
  const bool initialized = vallado_ad::sgp4init(
      vallado_ad::wgs72, 'i', "00000", epoch_days, e.bstar, Scalar(0.0), Scalar(0.0), e.eccentricity,
      e.perigee, e.inclination, e.anomaly, e.motion * tau / 1440.0, e.node, record);
  if (!initialized || record.error != 0)
    throw InvalidModel("SGP4 initialization failed: " + std::to_string(record.error));
  Scalar r[3]{}, v[3]{};
  if (!vallado_ad::sgp4(record, Scalar(minutes), r, v) || record.error != 0)
    throw InvalidModel("SGP4 propagation failed: " + std::to_string(record.error));
  std::array<Scalar, 6> state;
  for (std::size_t i = 0; i < 3; ++i) {
    state[i] = r[i] * 1000.0;
    state[i + 3] = v[i] * 1000.0;
    if (!std::isfinite(value(state[i])) || !std::isfinite(value(state[i + 3])))
      throw InvalidModel("SGP4 returned a non-finite state");
  }
  return state;
}
}  // namespace orbitfit::detail::sgp4_model
