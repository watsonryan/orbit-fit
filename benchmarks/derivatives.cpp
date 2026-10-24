/**
 * @file derivatives.cpp
 * @brief Measure native derivative methods and complete fits on selected orbit regimes.
 * @author watson
 */
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
using Clock = std::chrono::steady_clock;

template <class Function>
double median_microseconds(Function function, int repetitions) {
  std::array<double, 5> timings{};
  function();  // Warm the code and allocator before timing.
  for (auto& duration : timings) {
    const auto begin = Clock::now();
    for (int i = 0; i < repetitions; ++i) function();
    duration = std::chrono::duration<double, std::micro>(Clock::now() - begin).count() / repetitions;
  }
  std::ranges::sort(timings);
  return timings[timings.size() / 2];
}

void run(std::string_view name, Elements truth, bool drag) {
  auto samples = test::samples(truth, 97, 1800);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    samples[i].state.position_m[0] += 3.0 * std::sin(static_cast<double>(i));
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
  const detail::Chart chart(*options.seed, drag);
  const Eigen::VectorXd x = Eigen::VectorXd::Zero(drag ? 7 : 6);
  double checksum = 0;
  for (auto method : {Derivatives::automatic, Derivatives::finite_difference}) {
    options.derivatives = method;
    const auto label = method == Derivatives::automatic ? "automatic" : "finite_difference";
    std::size_t index = 0;
    const auto factor_us = median_microseconds(
        [&] {
          const auto& sample = samples[index++ % samples.size()];
          if (method == Derivatives::automatic) {
            const auto result = chart.linearize(x, sample, true);
            checksum += result.residual.squaredNorm() + result.jacobian.squaredNorm();
          } else {
            checksum += detail::state_residual(chart, x, sample, true).squaredNorm() +
                        detail::finite_difference_jacobian(chart, x, sample, true).squaredNorm();
          }
        },
        1940);
    FitResult result;
    const auto fit_us = median_microseconds([&] { result = test::take(fit(samples, options)); }, 5);
    if (!result.accepted)
      throw std::runtime_error("Benchmark fit rejected: " + std::string(name) + " / " + label + " / " +
                               std::string(to_string(result.termination)));
    const detail::Chart final_chart(result.elements, drag);
    Eigen::VectorXd gradient = Eigen::VectorXd::Zero(x.size());
    Eigen::VectorXd column_squares = gradient;
    for (std::size_t i = 0; i < samples.size(); ++i) {
      const auto residual = detail::state_residual(final_chart, x, samples[i], true);
      auto jacobian = method == Derivatives::automatic
                          ? final_chart.linearize(x, samples[i], true).jacobian
                          : detail::finite_difference_jacobian(final_chart, x, samples[i], true);
      const double weight =
          (i == 0 || i + 1 == samples.size() ? 0.5 : 1.0) / static_cast<double>(samples.size() - 1);
      auto whitened = residual;
      whitened.head(3) /= options.position_scale_m;
      whitened.tail(3) /= options.velocity_scale_mps;
      jacobian.topRows(3) /= options.position_scale_m;
      jacobian.bottomRows(3) /= options.velocity_scale_mps;
      gradient += weight * jacobian.transpose() * whitened;
      column_squares += weight * jacobian.colwise().squaredNorm().transpose();
    }
    const double raw_gradient = gradient.lpNorm<Eigen::Infinity>();
    for (Eigen::Index i = 0; i < gradient.size(); ++i)
      if (column_squares[i] > 0) gradient[i] /= std::sqrt(column_squares[i]);
    std::print(stderr, "{} {} final gradient raw={:.9e} column-scaled={:.9e}\n", name, label, raw_gradient,
               gradient.lpNorm<Eigen::Infinity>());
    std::print("factor,{},{},{},{:.3f},,,,\n", name, drag ? 7 : 6, label, factor_us);
    std::print("fit,{},{},{},{:.3f},{},{:.9f},{:.9e},{}\n", name, drag ? 7 : 6, label, fit_us,
               result.iterations, result.metrics.rms_position_m, result.final_cost,
               to_string(result.termination));
  }
  // Keep all measured derivative work observable without volatile arithmetic.
  std::print(stderr, "{} checksum: {:.12e}\n", name, checksum);
}

void step_sweep(std::string_view name, const Elements& e, double seconds) {
  const detail::Chart chart(e, true);
  const Eigen::VectorXd x = Eigen::VectorXd::Zero(7);
  const auto epoch = test::take(add_seconds(e.epoch, seconds));
  const Sample sample{epoch, test::take(propagate(e, epoch)), Frame::teme, std::nullopt};
  const auto ad = chart.linearize(x, sample, true).jacobian;
  for (double h : {1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7, 1e-8}) {
    const auto fd = detail::finite_difference_jacobian(chart, x, sample, true, h);
    for (Eigen::Index col = 0; col < 7; ++col)
      std::print("{},{},{:.0e},{:.9e},{:.9e},{:.9e},{:.9e}\n", name, col, h, ad.col(col).head(3).norm(),
                 (ad - fd).col(col).head(3).norm(), ad.col(col).tail(3).norm(),
                 (ad - fd).col(col).tail(3).norm());
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--steps") {
      std::println(
          "case,column,step,position_ad_norm,position_difference,velocity_ad_norm,velocity_difference");
      step_sweep("GEO-25954", test::reference(25954).elements, 0);
      auto retro = test::truth();
      retro.inclination_rad = std::numbers::pi - 1e-4;
      retro.eccentricity = 2e-4;
      step_sweep("retrograde", retro, 3600);
      auto circular = test::truth();
      circular.eccentricity = 0.5e-6;
      step_sweep("eccentricity-clamp", circular, 1200);
      return 0;
    }
    if (argc != 1) throw std::invalid_argument("Usage: orbitfit_benchmark [--steps]");
    std::println(
        "operation,case,parameters,derivatives,median_microseconds,iterations,rms_position_m,final_"
        "cost,termination");
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
