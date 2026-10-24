#include <gtsam/linear/NoiseModel.h>
#include <gtsam/nonlinear/LevenbergMarquardtOptimizer.h>
#include <gtsam/nonlinear/NoiseModelFactorN.h>
#include <gtsam/nonlinear/NonlinearFactorGraph.h>

#include <Eigen/Cholesky>
#include <Eigen/Geometry>
#include <Eigen/SVD>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "fit_internal.hpp"
#include "orbitfit/orbitfit.hpp"

namespace orbitfit {
namespace {
constexpr double pi = std::numbers::pi;
constexpr double tau = 2 * pi;
constexpr gtsam::Key orbit_key = 0;
double wrap(double x) {
  x = std::fmod(x, tau);
  return x < 0 ? x + tau : x;
}
class InvalidModel : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

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

gtsam::SharedNoiseModel noise_model(const Sample& s, const FitOptions& o, double weight) {
  const int n = o.use_velocity ? 6 : 3;
  gtsam::SharedNoiseModel noise;
  if (o.weighting == Weighting::independent_measurements) {
    if (!s.covariance)
      throw std::invalid_argument("Measurement mode requires covariance for every fitted sample");
    gtsam::Matrix covariance(n, n);
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j) covariance(i, j) = (*s.covariance)[static_cast<std::size_t>(6 * i + j)];
    if (!covariance.allFinite() || !covariance.isApprox(covariance.transpose(), 1e-12) ||
        Eigen::LLT<gtsam::Matrix>(covariance).info() != Eigen::Success)
      throw std::invalid_argument("Measurement covariance must be finite, symmetric, and positive definite");
    noise = gtsam::noiseModel::Gaussian::Covariance(covariance);
  } else {
    gtsam::Vector sigmas(n);
    sigmas.head(3).setConstant(o.position_scale_m / std::sqrt(weight));
    if (o.use_velocity) sigmas.tail(3).setConstant(o.velocity_scale_mps / std::sqrt(weight));
    noise = gtsam::noiseModel::Diagonal::Sigmas(sigmas);
  }
  if (o.huber_threshold) {
    const double threshold = *o.huber_threshold * std::sqrt(weight);
    noise = gtsam::noiseModel::Robust::Create(gtsam::noiseModel::mEstimator::Huber::Create(threshold), noise);
  }
  return noise;
}

class StateFactor final : public gtsam::NoiseModelFactorN<gtsam::Vector> {
 public:
  using Base = gtsam::NoiseModelFactorN<gtsam::Vector>;
  StateFactor(detail::Chart chart, Sample sample, bool velocity, gtsam::SharedNoiseModel noise)
      : Base(std::move(noise), orbit_key),
        chart_(std::move(chart)),
        sample_(std::move(sample)),
        velocity_(velocity) {}
  gtsam::Vector evaluateError(const gtsam::Vector& x, gtsam::OptionalMatrixType H = nullptr) const override {
    if (H) *H = detail::state_jacobian(chart_, x, sample_, velocity_);
    return detail::state_residual(chart_, x, sample_, velocity_);
  }
  double error(const gtsam::Values& values) const override {
    try {
      return Base::error(values);
    } catch (const InvalidModel&) {
      return std::numeric_limits<double>::infinity();
    }
  }

 private:
  detail::Chart chart_;
  Sample sample_;
  bool velocity_;
};

Result<void> validate_request(std::span<const Sample> samples, const FitOptions& o) {
  if (samples.size() < 3 || o.start >= o.stop || o.epoch < o.start || o.epoch > o.stop ||
      o.maximum_iterations == 0 || o.maximum_iterations > 10000 ||
      (o.maximum_samples != 0 && o.maximum_samples < 3))
    return std::unexpected(
        Error{ErrorCode::invalid_input, "Invalid window, fit epoch, sample count, or iteration limit"});
  for (double x : {o.position_scale_m, o.velocity_scale_mps, o.relative_cost_tolerance,
                   o.absolute_cost_tolerance, o.gradient_tolerance, o.maximum_rms_position_m})
    if (!std::isfinite(x) || x <= 0)
      return std::unexpected(Error{ErrorCode::invalid_input,
                                   "Scales, tolerances, and accuracy target must be finite and positive"});
  if (!std::isfinite(o.fixed_bstar) ||
      (o.huber_threshold && (!std::isfinite(*o.huber_threshold) || *o.huber_threshold <= 0)))
    return std::unexpected(Error{ErrorCode::invalid_input, "Invalid BSTAR or robust threshold"});
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const auto& s = samples[i];
    if (s.frame != Frame::teme)
      return std::unexpected(Error{ErrorCode::unsupported_frame, "Convert samples to TEME before fitting"});
    if (i && samples[i - 1].epoch >= s.epoch)
      return std::unexpected(
          Error{ErrorCode::invalid_input, "Sample epochs must be strictly increasing and unique"});
    for (const auto& vector : {s.state.position_m, s.state.velocity_mps})
      if (!std::ranges::all_of(vector, [](double x) { return std::isfinite(x); }))
        return std::unexpected(Error{ErrorCode::invalid_input, "Samples must contain finite states"});
  }
  if (samples.front().epoch > o.start || samples.back().epoch < o.stop)
    return std::unexpected(Error{ErrorCode::invalid_input, "Samples must cover the requested fit window"});
  if (o.seed) {
    if (auto valid = validate(*o.seed); !valid) return valid;
    if (o.seed->epoch != o.epoch)
      return std::unexpected(Error{ErrorCode::invalid_input,
                                   "Seed epoch must equal fit epoch; elements cannot simply be re-tagged"});
  }
  return {};
}
}  // namespace

namespace detail {
Chart::Chart(const Elements& seed, bool drag)
    : seed_(seed), base_(6), retrograde_(seed.inclination_rad > pi / 2), drag_(drag) {
  const double sign = retrograde_ ? -1 : 1;
  const double varpi = seed.argument_of_perigee_rad + sign * seed.raan_rad;
  const double e_scale = seed.eccentricity / std::sqrt(1 - seed.eccentricity * seed.eccentricity);
  const double t = std::tan((retrograde_ ? pi - seed.inclination_rad : seed.inclination_rad) / 2);
  base_ << std::log(seed.mean_motion_rev_per_day), e_scale * std::sin(varpi), e_scale * std::cos(varpi),
      t * std::sin(seed.raan_rad), t * std::cos(seed.raan_rad), seed.mean_anomaly_rad + varpi;
}
Elements Chart::decode(const gtsam::Vector& x) const {
  if (static_cast<std::size_t>(x.size()) != dimension() || !x.allFinite())
    throw InvalidModel("Invalid parameter vector");
  const gtsam::Vector u = base_ + x.head(6) * 1e-3;
  const double radius = std::hypot(u[1], u[2]);
  const double ecc = radius / std::hypot(1.0, radius);
  const double varpi = std::atan2(u[1], u[2]);
  const double node = std::atan2(u[3], u[4]);
  const double tilt = 2 * std::atan(std::hypot(u[3], u[4]));
  return Elements{seed_.epoch,
                  retrograde_ ? pi - tilt : tilt,
                  wrap(node),
                  ecc,
                  wrap(varpi - (retrograde_ ? -1 : 1) * node),
                  wrap(u[5] - varpi),
                  std::exp(u[0]),
                  seed_.bstar + (drag_ ? x[6] * 1e-4 : 0)};
}
gtsam::Vector state_residual(const Chart& chart, const gtsam::Vector& x, const Sample& s, bool velocity) {
  const auto predicted = propagate(chart.decode(x), s.epoch);
  if (!predicted) throw InvalidModel(predicted.error().message);
  gtsam::Vector residual(velocity ? 6 : 3);
  for (int j = 0; j < 3; ++j) {
    residual[j] =
        predicted->position_m[static_cast<std::size_t>(j)] - s.state.position_m[static_cast<std::size_t>(j)];
    if (velocity)
      residual[j + 3] = predicted->velocity_mps[static_cast<std::size_t>(j)] -
                        s.state.velocity_mps[static_cast<std::size_t>(j)];
  }
  return residual;
}
gtsam::Matrix state_jacobian(const Chart& chart, const gtsam::Vector& x, const Sample& s, bool velocity,
                             double step) {
  gtsam::Matrix H(velocity ? 6 : 3, x.size());
  const auto center = state_residual(chart, x, s, velocity);
  for (Eigen::Index j = 0; j < x.size(); ++j) {
    auto a = x, b = x;
    a[j] += step;
    b[j] -= step;
    std::optional<gtsam::Vector> plus, minus;
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
}  // namespace detail

Result<FitResult> fit(std::span<const Sample> samples, const FitOptions& o) {
  if (auto v = validate_request(samples, o); !v) return std::unexpected(v.error());
  if (o.stop_token.stop_requested())
    return std::unexpected(Error{ErrorCode::cancelled, "Fit cancelled before initialization"});
  std::vector<Sample> window;
  for (const auto& s : samples)
    if (s.epoch >= o.start && s.epoch <= o.stop) window.push_back(s);
  if (window.size() < 3)
    return std::unexpected(
        Error{ErrorCode::invalid_input, "At least three samples must lie inside the fit window"});
  auto seed_result = o.seed ? Result<Elements>{*o.seed} : initial_elements(samples, o.epoch, o.fixed_bstar);
  if (!seed_result) return std::unexpected(seed_result.error());
  auto seed = *seed_result;
  if (!o.fit_bstar) seed.bstar = o.fixed_bstar;
  if (auto check = propagate(seed, o.epoch); !check) return std::unexpected(check.error());

  std::vector<std::size_t> selected;
  const auto count = o.maximum_samples == 0 ? window.size() : std::min(o.maximum_samples, window.size());
  for (std::size_t i = 0; i < count; ++i) selected.push_back(i * (window.size() - 1) / (count - 1));
  const detail::Chart chart(seed, o.fit_bstar);
  gtsam::NonlinearFactorGraph graph;
  try {
    for (std::size_t i = 0; i < selected.size(); ++i) {
      double weight = 1;
      if (o.weighting == Weighting::approximation) {
        // Voronoi cells in time prevent dense portions of an ephemeris from dominating the fit.
        const auto t = [&](std::size_t j) { return seconds_between(window[selected[j]].epoch, o.start); };
        const double left = i == 0 ? 0 : 0.5 * (t(i - 1) + t(i));
        const double right =
            i + 1 == selected.size() ? seconds_between(o.stop, o.start) : 0.5 * (t(i) + t(i + 1));
        weight = (right - left) / seconds_between(o.stop, o.start);
      }
      graph.emplace_shared<StateFactor>(chart, window[selected[i]], o.use_velocity,
                                        noise_model(window[selected[i]], o, weight));
    }
  } catch (const std::exception& ex) {
    return std::unexpected(Error{ErrorCode::invalid_input, ex.what()});
  }

  gtsam::Values values;
  values.insert(orbit_key, gtsam::Vector::Zero(static_cast<Eigen::Index>(chart.dimension())).eval());
  gtsam::LevenbergMarquardtParams params;
  params.setLinearSolverType("SEQUENTIAL_QR");
  params.setDiagonalDamping(true);
  params.lambdaInitial = 1e-3;
  params.lambdaUpperBound = 1e12;
  params.maxIterations = o.maximum_iterations;
  FitResult result;
  result.fitted_samples = selected.size();
  result.parameter_count = chart.dimension();
  result.termination = Termination::iteration_limit;
  try {
    gtsam::LevenbergMarquardtOptimizer optimizer(graph, values, params);
    result.initial_cost = optimizer.error();
    if (!std::isfinite(result.initial_cost))
      return std::unexpected(Error{ErrorCode::solver, "Initial model is invalid over the fit window"});
    for (std::size_t k = 0; k < o.maximum_iterations; ++k) {
      if (o.stop_token.stop_requested()) {
        result.termination = Termination::cancelled;
        break;
      }
      const auto [A, b] = graph.linearize(optimizer.values())->jacobian();
      const double gradient = (A.transpose() * b).lpNorm<Eigen::Infinity>();
      if (gradient <= o.gradient_tolerance || optimizer.error() <= o.absolute_cost_tolerance) {
        result.termination = Termination::converged;
        break;
      }
      const double before = optimizer.error();
      optimizer.iterate();
      ++result.iterations;
      values = optimizer.values();
      const double after = optimizer.error();
      if (!std::isfinite(after)) throw InvalidModel("Optimizer returned non-finite cost");
      if (after <= o.absolute_cost_tolerance) {
        result.termination = Termination::converged;
        break;
      }
      if (after >= before) {
        result.termination = Termination::stalled;
        break;
      }
      if (before - after <= o.relative_cost_tolerance * std::max(before, 1e-20)) {
        result.termination = Termination::converged;
        break;
      }
    }
    values = optimizer.values();
  } catch (const std::exception& ex) {
    result.termination = Termination::numerical_failure;
    result.diagnostics.emplace_back(ex.what());
  }
  try {
    result.elements = chart.decode(values.at<gtsam::Vector>(orbit_key));
    result.final_cost = graph.error(values);
    const auto [A, b] = graph.linearize(values)->jacobian();
    (void)b;
    const Eigen::JacobiSVD<gtsam::Matrix> svd(A);
    const auto& singular = svd.singularValues();
    const double threshold = singular[0] * 1e-8;
    result.rank = static_cast<std::size_t>((singular.array() > threshold).count());
    result.condition_number = singular[singular.size() - 1] > 0 ? singular[0] / singular[singular.size() - 1]
                                                                : std::numeric_limits<double>::infinity();
    if (result.rank != result.parameter_count)
      result.diagnostics.emplace_back(
          "Parameters are weakly observable at relative singular-value threshold 1e-8");
    if (o.weighting == Weighting::approximation)
      result.diagnostics.emplace_back(
          "Approximation weights are design scales; no statistical covariance is inferred");
    else
      result.diagnostics.emplace_back(
          "Measurement mode assumes independent epochs and supplied within-epoch covariance; temporal "
          "correlations are not modeled");
    const auto metrics = evaluate(result.elements, window);
    if (!metrics) return std::unexpected(metrics.error());
    result.metrics = *metrics;
    for (const auto& s : window) {
      const auto state = propagate(result.elements, s.epoch);
      if (!state) return std::unexpected(state.error());
      Residual residual{.epoch = s.epoch, .position_m = {}, .velocity_mps = {}};
      for (std::size_t j = 0; j < 3; ++j) {
        residual.position_m[j] = state->position_m[j] - s.state.position_m[j];
        residual.velocity_mps[j] = state->velocity_mps[j] - s.state.velocity_mps[j];
      }
      result.residuals.push_back(residual);
    }
    result.accepted = result.termination == Termination::converged && result.rank == result.parameter_count &&
                      result.metrics.rms_position_m <= o.maximum_rms_position_m;
  } catch (const std::exception& ex) {
    return std::unexpected(Error{ErrorCode::solver, ex.what()});
  }
  return result;
}
}  // namespace orbitfit
