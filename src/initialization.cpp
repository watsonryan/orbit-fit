/**
 * @file initialization.cpp
 * @brief Construct a coarse bound-orbit seed from Cartesian samples.
 * @author watson
 */
#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <numbers>

#include "fit_internal.hpp"

namespace orbitfit::detail {
namespace {
constexpr double tau = 2 * std::numbers::pi;  ///< Full-turn in radians.
/** @brief Normalize a seed angle. @param x Angle in radians. @return Angle in [0,2 pi). */
double wrap(double x) {
  x = std::fmod(x, tau);
  return x < 0 ? x + tau : x;
}
}  // namespace

Result<Elements> initial_elements(std::span<const Sample> samples, Epoch epoch, double bstar) {
  auto right = std::ranges::lower_bound(samples, epoch, {}, &Sample::epoch);
  State state;
  if (right == samples.end())
    state = samples.back().state;
  else if (right == samples.begin() || right->epoch == epoch)
    state = right->state;
  else {
    const auto& a = *(right - 1);
    const auto& b = *right;
    const double dt = seconds_between(b.epoch, a.epoch);
    const double u = seconds_between(epoch, a.epoch) / dt;
    for (std::size_t j = 0; j < 3; ++j) {
      state.position_m[j] = (2 * u * u * u - 3 * u * u + 1) * a.state.position_m[j] +
                            (u * u * u - 2 * u * u + u) * dt * a.state.velocity_mps[j] +
                            (-2 * u * u * u + 3 * u * u) * b.state.position_m[j] +
                            (u * u * u - u * u) * dt * b.state.velocity_mps[j];
      state.velocity_mps[j] = (6 * u * u - 6 * u) / dt * a.state.position_m[j] +
                              (3 * u * u - 4 * u + 1) * a.state.velocity_mps[j] +
                              (-6 * u * u + 6 * u) / dt * b.state.position_m[j] +
                              (3 * u * u - 2 * u) * b.state.velocity_mps[j];
    }
  }
  const Eigen::Vector3d r = Eigen::Map<const Eigen::Vector3d>(state.position_m.data());
  const Eigen::Vector3d v = Eigen::Map<const Eigen::Vector3d>(state.velocity_mps.data());
  constexpr double mu = 398600.8e9;  // WGS-72, matching the propagator.
  const Eigen::Vector3d h = r.cross(v);
  const double hn = h.norm(), rn = r.norm();
  if (!(rn > 1) || !(hn > 1))
    return std::unexpected(Error{ErrorCode::invalid_input, "Degenerate seed state"});
  const Eigen::Vector3d ev = v.cross(h) / mu - r / rn;
  const double ecc = ev.norm();
  const double a = 1.0 / (2.0 / rn - v.squaredNorm() / mu);
  if (!(a > 0) || ecc >= 1)
    return std::unexpected(Error{ErrorCode::invalid_input, "Seed state is not a bound orbit"});
  const double incl = std::acos(std::clamp(h.z() / hn, -1.0, 1.0));
  Eigen::Vector3d p{-h.y(), h.x(), 0};
  const double node_norm = p.norm();
  double node = 0;
  if (node_norm > 1e-12 * hn) {
    p /= node_norm;
    node = std::atan2(p.y(), p.x());
  } else
    p = Eigen::Vector3d::UnitX();
  const Eigen::Vector3d q = (h / hn).cross(p);
  double argp = 0, mean = 0;
  if (ecc > 1e-10) {
    argp = std::atan2(ev.dot(q), ev.dot(p));
    const double cosnu = std::clamp(ev.dot(r) / (ecc * rn), -1.0, 1.0);
    const double sinnu = ev.cross(r).dot(h) / (ecc * rn * hn);
    const double eccentric = std::atan2(std::sqrt(1 - ecc * ecc) * sinnu, ecc + cosnu);
    mean = eccentric - ecc * std::sin(eccentric);
  } else
    mean = std::atan2(r.dot(q), r.dot(p));
  return Elements{
      epoch, incl, wrap(node), ecc, wrap(argp), wrap(mean), std::sqrt(mu / (a * a * a)) * 86400 / tau, bstar};
}

}  // namespace orbitfit::detail
