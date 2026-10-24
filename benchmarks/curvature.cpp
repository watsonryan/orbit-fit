/**
 * @file curvature.cpp
 * @brief Measure second-order evaluation and quantify objective curvature corrections.
 * @author watson
 */
#include <Eigen/Eigenvalues>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <numbers>
#include <print>
#include <string_view>

#include "fit_internal.hpp"
#include "support.hpp"

using namespace orbitfit;
namespace {
template <class Function>
double median_microseconds(Function function, int repetitions) {
  std::array<double, 5> timings{};
  function();
  for (auto& timing : timings) {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < repetitions; ++i) function();
    timing = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() /
             repetitions;
  }
  std::ranges::sort(timings);
  return timings[2];
}

double checksum(const ResidualCurvature& curvature) {
  double sum = 0;
  for (std::size_t row = 0; row < curvature.residual_count; ++row) {
    sum += curvature.residual[row] * curvature.residual[row];
    for (std::size_t i = 0; i < curvature.parameter_count; ++i) {
      sum += curvature.jacobian[row][i] * curvature.jacobian[row][i];
      for (std::size_t j = 0; j < curvature.parameter_count; ++j)
        sum += curvature.hessians[row][i][j] * curvature.hessians[row][i][j];
    }
  }
  return sum;
}

void measure(std::string_view name, std::string_view location, const Elements& origin,
             const std::vector<Sample>& samples, bool drag) {
  const detail::Chart chart(origin, drag);
  const Eigen::VectorXd x = Eigen::VectorXd::Zero(drag ? 7 : 6);
  const std::span<const double> offsets(x.data(), static_cast<std::size_t>(x.size()));
  double sum = 0;
  std::size_t index = 0;
  const double first_us = median_microseconds(
      [&] {
        const auto first = chart.linearize(x, samples[index++ % samples.size()], true);
        sum += first.residual.squaredNorm() + first.jacobian.squaredNorm();
      },
      1940);
  index = 0;
  const double second_us = median_microseconds(
      [&] { sum += checksum(chart.curvature(x, samples[index++ % samples.size()], true)); }, 1940);
  index = 0;
  const double public_us = median_microseconds(
      [&] {
        sum += checksum(test::take(residual_curvature(origin, samples[index++ % samples.size()], offsets)));
      },
      1940);

  // Quadratic approximation objective, matching the existing benchmark: time
  // weights, 100 m position scale, 0.1 m/s velocity scale, and no robust loss.
  Eigen::MatrixXd gn = Eigen::MatrixXd::Zero(x.size(), x.size());
  Eigen::MatrixXd correction = gn;
  double cost = 0;
  for (std::size_t sample = 0; sample < samples.size(); ++sample) {
    const auto current = chart.curvature(x, samples[sample], true);
    const double weight =
        (sample == 0 || sample + 1 == samples.size() ? 0.5 : 1.0) / static_cast<double>(samples.size() - 1);
    for (std::size_t row = 0; row < 6; ++row) {
      const double scale = row < 3 ? 100.0 : 0.1;
      const double w = weight / (scale * scale);
      const double r = current.residual[row];
      const Eigen::Map<const Eigen::VectorXd> gradient(current.jacobian[row].data(), x.size());
      cost += 0.5 * w * r * r;
      gn += w * gradient * gradient.transpose();
      for (Eigen::Index i = 0; i < x.size(); ++i)
        for (Eigen::Index j = 0; j < x.size(); ++j)
          correction(i, j) +=
              w * r * current.hessians[row][static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
    }
  }
  if ((gn.diagonal().array() <= 0).any()) throw std::runtime_error("Unobservable benchmark coordinate");
  const Eigen::VectorXd scaling = gn.diagonal().array().sqrt().inverse();
  const Eigen::MatrixXd scaled_gn = scaling.asDiagonal() * gn * scaling.asDiagonal();
  const Eigen::MatrixXd scaled_correction = scaling.asDiagonal() * correction * scaling.asDiagonal();
  const Eigen::MatrixXd exact = scaled_gn + scaled_correction;
  // Report asymmetry before averaging roundoff for the eigenvalue calculation.
  const double asymmetry = (exact - exact.transpose()).norm() / exact.norm();
  const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> gn_eigen(scaled_gn);
  const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> exact_eigen(0.5 * (exact + exact.transpose()));
  if (gn_eigen.info() != Eigen::Success || exact_eigen.info() != Eigen::Success)
    throw std::runtime_error("Benchmark eigendecomposition failed");
  std::print("{},{},{},{:.3f},{:.3f},{:.3f},{:.3f},{:.9e},{:.9e},{:.9e},{:.9e},{:.9e}\n", name, x.size(),
             location, first_us, second_us, public_us, second_us / first_us, cost,
             scaled_correction.norm() / scaled_gn.norm(), gn_eigen.eigenvalues()[0],
             exact_eigen.eigenvalues()[0], asymmetry);
  std::print(stderr, "{} {} checksum: {:.12e}\n", name, location, sum);
}

void run(std::string_view name, const Elements& truth, bool drag) {
  auto samples = test::samples(truth, 97, 1800);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    samples[i].state.position_m[0] += 3 * std::sin(static_cast<double>(i));
    samples[i].state.velocity_mps[2] += 0.002 * std::cos(static_cast<double>(i));
  }
  auto options = test::options(samples);
  options.maximum_rms_position_m = 10;
  options.fixed_bstar = truth.bstar;
  options.fit_bstar = drag;
  options.seed = truth;
  options.seed->mean_anomaly_rad += 0.002;
  options.seed->mean_motion_rev_per_day *= 1.00001;
  if (drag) options.seed->bstar *= 0.8;
  const auto fitted = test::take(fit(samples, options));
  if (!fitted.accepted) throw std::runtime_error("Benchmark fit rejected");
  measure(name, "seed", *options.seed, samples, drag);
  measure(name, "fitted", fitted.elements, samples, drag);
}

void sweep(std::string_view name, const Elements& seed, double seconds) {
  const detail::Chart chart(seed, true);
  const Eigen::VectorXd x = Eigen::VectorXd::Zero(7);
  const auto epoch = test::take(add_seconds(seed.epoch, seconds));
  const Sample sample{epoch, test::take(propagate(seed, epoch)), Frame::teme, std::nullopt};
  const auto second = chart.curvature(x, sample, true);
  for (double h : {1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7, 1e-8}) {
    for (Eigen::Index j = 0; j < 7; ++j) {
      Eigen::VectorXd d = Eigen::VectorXd::Zero(7);
      d[j] = h;
      const Eigen::MatrixXd central =
          (chart.linearize(x + d, sample, true).jacobian - chart.linearize(x - d, sample, true).jacobian) /
          (2 * h);
      const Eigen::MatrixXd five_point = (8 * (chart.linearize(x + d, sample, true).jacobian -
                                               chart.linearize(x - d, sample, true).jacobian) -
                                          (chart.linearize(x + 2 * d, sample, true).jacobian -
                                           chart.linearize(x - 2 * d, sample, true).jacobian)) /
                                         (12 * h);
      for (Eigen::Index i = 0; i < 7; ++i) {
        for (std::size_t start : {0u, 3u}) {
          Eigen::Vector3d actual;
          for (std::size_t k = 0; k < 3; ++k)
            actual[static_cast<Eigen::Index>(k)] =
                second.hessians[start + k][static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
          const double central_error =
              (actual - central.block<3, 1>(static_cast<Eigen::Index>(start), i)).norm();
          const double five_error =
              (actual - five_point.block<3, 1>(static_cast<Eigen::Index>(start), i)).norm();
          std::print("{},{},{},{},{},{:.0e},{:.9e},{:.9e},{:.9e}\n", name, seconds, i, j, start, h,
                     actual.norm(), central_error, five_error);
        }
      }
    }
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--steps") {
      std::println("case,seconds,i,j,start,step,hessian_norm,central_error,five_point_error");
      for (int id : {25954, 26900})
        for (double seconds : {-43200.0, 0.0, 86400.0})
          sweep(std::to_string(id), test::reference(id).elements, seconds);
      for (double inclination : {1e-4, std::numbers::pi - 1e-4}) {
        auto seed = test::truth();
        seed.inclination_rad = inclination;
        seed.eccentricity = 2e-4;
        sweep(inclination < 1 ? "equatorial" : "retrograde", seed, 3600);
      }
      for (double eccentricity : {0.5e-6, 2e-6, 0.9e-4, 1.1e-4}) {
        auto seed = test::truth();
        seed.eccentricity = eccentricity;
        sweep(std::format("eccentricity-{:.1e}", eccentricity), seed, 1200);
      }
      return 0;
    }
    if (argc != 1) throw std::invalid_argument("Usage: orbitfit_curvature_benchmark [--steps]");
    std::println(
        "case,parameters,location,first_order_us,second_order_us,public_api_us,second_over_first,"
        "cost,scaled_correction_ratio,min_scaled_gn_eigenvalue,min_scaled_exact_eigenvalue,"
        "relative_asymmetry");
    auto leo = test::truth();
    leo.bstar = 2e-4;
    run("LEO", leo, true);
    run("Molniya-8195", test::reference(8195).elements, false);
    run("GEO-25954", test::reference(25954).elements, false);
    return 0;
  } catch (const std::exception& ex) {
    std::println(stderr, "{}", ex.what());
    return 1;
  }
}
