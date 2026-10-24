/**
 * @file test_fit.cpp
 * @brief Verify objective semantics, fitting acceptance, observability, and numerical stopping.
 * @author watson
 */
#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "fit_internal.hpp"
#include "support.hpp"

using namespace orbitfit;

TEST(Fit, RecoversNearEarthMeanElementsWithoutSeed) {
  const auto e = test::truth();
  const auto samples = test::samples(e);
  const auto result = fit(samples, test::options(samples));
  ASSERT_TRUE(result) << result.error().message;
  EXPECT_EQ(result->termination, Termination::converged);
  EXPECT_TRUE(result->accepted);
  EXPECT_EQ(result->rank, 6);
  EXPECT_LT(result->metrics.max_position_m, 0.01);
}

TEST(Fit, RecoversDeepSpaceFromPerturbedSeed) {
  auto e = test::reference(8195).elements;
  const auto samples = test::samples(e, 49, 3600);
  auto o = test::options(samples);
  o.fixed_bstar = e.bstar;
  e.mean_anomaly_rad += 0.002;
  e.mean_motion_rev_per_day *= 1.00001;
  o.seed = e;
  const auto result = fit(samples, o);
  ASSERT_TRUE(result) << result.error().message;
  EXPECT_TRUE(result->accepted) << to_string(result->termination);
  EXPECT_LT(result->metrics.rms_position_m, 0.02);
}

TEST(Fit, NearCircularEquatorialAndRetrogradeCharts) {
  for (double inclination : {0.0001, std::numbers::pi - 0.0001}) {
    auto e = test::truth();
    e.inclination_rad = inclination;
    e.eccentricity = 0.0001;
    const auto samples = test::samples(e);
    auto o = test::options(samples);
    e.mean_anomaly_rad += 0.001;
    o.seed = e;
    const auto result = fit(samples, o);
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_TRUE(result->accepted) << inclination << " " << to_string(result->termination);
    EXPECT_LT(result->metrics.rms_position_m, 0.1);
  }
}

TEST(Fit, EstimatesBstarOnLongArc) {
  auto e = test::truth();
  e.bstar = 2e-4;
  const auto samples = test::samples(e, 97, 1800);
  auto o = test::options(samples);
  o.seed = e;
  o.seed->bstar = 1e-4;
  o.fit_bstar = true;
  const auto result = fit(samples, o);
  ASSERT_TRUE(result) << result.error().message;
  EXPECT_TRUE(result->accepted) << to_string(result->termination);
  EXPECT_EQ(result->rank, 7);
  EXPECT_NEAR(result->elements.bstar, e.bstar, 1e-7);
  EXPECT_LT(result->metrics.rms_position_m, 0.05);
}

TEST(Fit, WindowAndDecimationStillEvaluateEveryInWindowSample) {
  const auto e = test::truth();
  auto samples = test::samples(e);
  auto o = test::options(samples);
  o.start = samples[10].epoch;
  o.stop = samples[50].epoch;
  o.epoch = e.epoch;
  EXPECT_FALSE(fit(samples, o));  // Fit epoch must be inside the window.
  o.epoch = samples[10].epoch;
  samples[0].state.position_m[0] += 1e7;
  const auto result = fit(samples, o);
  ASSERT_TRUE(result) << result.error().message;
  EXPECT_EQ(result->metrics.sample_count, 41);
  EXPECT_EQ(result->residuals.front().epoch, o.start);
  EXPECT_LT(result->metrics.rms_position_m, 100);  // Re-epoched SGP4 is an approximation.

  samples = test::samples(e);
  o = test::options(samples);
  o.seed = e;
  o.maximum_samples = 3;
  samples[1].state.position_m[0] += 1000;
  const auto decimated = fit(samples, o);
  ASSERT_TRUE(decimated) << decimated.error().message;
  EXPECT_EQ(decimated->fitted_samples, 3);
  EXPECT_EQ(decimated->metrics.sample_count, samples.size());
  EXPECT_GT(decimated->metrics.rms_position_m, 100);
  EXPECT_FALSE(decimated->accepted);
}

TEST(Fit, PositionOnlyAndCovarianceValidation) {
  const auto e = test::truth();
  auto samples = test::samples(e);
  auto o = test::options(samples);
  o.use_velocity = false;
  auto result = fit(samples, o);
  ASSERT_TRUE(result) << result.error().message;
  EXPECT_LT(result->metrics.rms_position_m, 0.1);
  o.weighting = Weighting::independent_measurements;
  EXPECT_FALSE(fit(samples, o));
  for (auto& s : samples) s.covariance = std::array<double, 36>{};
  EXPECT_FALSE(fit(samples, o));
  for (auto& s : samples)
    for (std::size_t j = 0; j < 6; ++j) (*s.covariance)[j * 6 + j] = j < 3 ? 10000 : 0.01;
  o.seed = e;
  result = fit(samples, o);
  ASSERT_TRUE(result) << result.error().message;
  EXPECT_TRUE(result->accepted);
}

TEST(Fit, RobustLossReducesOutlierInfluence) {
  const auto e = test::truth();
  const auto clean = test::samples(e);
  auto dirty = clean;
  dirty[20].state.position_m[0] += 10000;
  auto o = test::options(dirty);
  o.seed = e;
  const auto ordinary = test::take(fit(dirty, o));
  o.huber_threshold = 1.5;
  const auto robust = test::take(fit(dirty, o));
  const auto a = test::take(evaluate(ordinary.elements, clean));
  const auto b = test::take(evaluate(robust.elements, clean));
  EXPECT_LT(b.rms_position_m, 0.1 * a.rms_position_m);
  EXPECT_FALSE(robust.accepted);  // Robust fitting does not hide an accuracy-gate failure.
}

TEST(Fit, MeasurementObjectiveUsesFullCovarianceAndBlockHuberLoss) {
  const auto truth = test::truth();
  auto samples = test::samples(truth);
  for (auto& sample : samples) {
    sample.state.position_m[0] += 100;
    sample.state.position_m[1] += 50;
    sample.state.position_m[2] -= 25;
    sample.state.velocity_mps[0] += 0.2;
    sample.state.velocity_mps[1] -= 0.05;
    sample.state.velocity_mps[2] += 0.1;
    sample.covariance = std::array<double, 36>{};
    for (std::size_t i = 0; i < 6; ++i) (*sample.covariance)[i * 6 + i] = i < 3 ? 10000 : 0.01;
    // Correlation 0.25 between x position and x velocity.
    (*sample.covariance)[3] = (*sample.covariance)[18] = 2.5;
  }
  for (bool velocity : {false, true}) {
    for (bool robust : {false, true}) {
      auto options = test::options(samples);
      options.seed = truth;
      options.use_velocity = velocity;
      options.weighting = Weighting::independent_measurements;
      options.gradient_tolerance = 1e100;  // Inspect the objective at the supplied seed.
      if (robust) options.huber_threshold = 0.75;
      const auto result = test::take(fit(samples, options));
      // Analytic inverse of the correlated 2x2 block, plus the independent axes.
      const double q = velocity ? (1.0 - 2 * 0.25 * 1 * 2 + 4) / (1 - 0.25 * 0.25) + 0.25 + 0.0625 + 0.25 + 1
                                : 1 + 0.25 + 0.0625;
      const double cost =
          0.5 * static_cast<double>(samples.size()) * (robust ? 2 * 0.75 * std::sqrt(q) - 0.75 * 0.75 : q);
      EXPECT_EQ(result.iterations, 0);
      EXPECT_NEAR(result.initial_cost, cost, 1e-6);
      EXPECT_NEAR(result.final_cost, cost, 1e-6);
    }
  }
}

TEST(Fit, ApproximationObjectivePreservesTimeWeightsAndRobustThresholdAfterDecimation) {
  const auto truth = test::truth();
  std::vector<Sample> samples;
  for (double seconds : {0, 10, 25, 100, 400}) {
    const auto epoch = test::take(add_seconds(truth.epoch, seconds));
    auto state = test::take(propagate(truth, epoch));
    state.position_m[0] += 100 * static_cast<double>(samples.size() + 1);
    samples.push_back({epoch, state, Frame::teme, std::nullopt});
  }
  for (bool robust : {false, true}) {
    auto options = test::options(samples);
    options.epoch = truth.epoch;
    options.seed = truth;
    options.use_velocity = false;
    options.position_scale_m = 100;
    options.maximum_samples = 3;
    options.gradient_tolerance = 1e100;
    if (robust) options.huber_threshold = 1.5;
    const auto result = test::take(fit(samples, options));
    // Selected epochs 0, 25, 400 have cells [0,12.5], [12.5,212.5], [212.5,400].
    const auto loss = [robust](double r) { return robust && r > 1.5 ? 3 * r - 2.25 : r * r; };
    const double cost = 0.5 * (0.03125 * loss(1) + 0.5 * loss(3) + 0.46875 * loss(5));
    EXPECT_EQ(result.iterations, 0);
    EXPECT_EQ(result.fitted_samples, 3);
    EXPECT_EQ(result.metrics.sample_count, 5);
    EXPECT_NEAR(result.initial_cost, cost, 1e-7);
    EXPECT_NEAR(result.final_cost, cost, 1e-7);
  }
}

TEST(Fit, ExplicitIterationLimitCancellationAndInvalidInput) {
  const auto e = test::truth();
  auto samples = test::samples(e);
  auto o = test::options(samples);
  o.maximum_iterations = 1;
  const auto limited = fit(samples, o);
  ASSERT_TRUE(limited) << limited.error().message;
  EXPECT_EQ(limited->termination, Termination::iteration_limit);
  EXPECT_FALSE(limited->accepted);
  std::stop_source stop;
  stop.request_stop();
  o.stop_token = stop.get_token();
  EXPECT_FALSE(fit(samples, o));
  o = test::options(samples);
  o.seed = e;
  o.seed->epoch = test::take(add_seconds(e.epoch, 1));
  EXPECT_FALSE(fit(samples, o));
  o.seed.reset();
  samples[1].epoch = samples[0].epoch;
  EXPECT_FALSE(fit(samples, o));
}

TEST(Fit, RelativeCostConvergenceOnNoisyArc) {
  const auto truth = test::truth();
  auto samples = test::samples(truth, 49, 3600);
  for (std::size_t i = 0; i < samples.size(); ++i)
    samples[i].state.position_m[0] += 3 * std::sin(static_cast<double>(i));
  auto options = test::options(samples);
  options.seed = truth;
  options.seed->mean_anomaly_rad += 0.002;
  options.maximum_rms_position_m = 10;
  // Cost convergence must remain available when noisy data cannot satisfy
  // absolute-zero cost or an unrealistically tight gradient tolerance.
  options.absolute_cost_tolerance = 1e-20;
  options.gradient_tolerance = 1e-20;
  for (auto derivatives : {Derivatives::automatic, Derivatives::finite_difference}) {
    options.derivatives = derivatives;
    const auto result = test::take(fit(samples, options));
    EXPECT_EQ(result.termination, Termination::converged) << testing::PrintToString(result.diagnostics);
    EXPECT_TRUE(result.accepted);
    EXPECT_GT(result.final_cost, options.absolute_cost_tolerance);
    EXPECT_GT(result.metrics.rms_position_m, 1);
    EXPECT_LT(result.metrics.rms_position_m, 3);
  }
}

TEST(Jacobian, DirectionalDerivativeAndStepStability) {
  const auto e = test::truth();
  const auto sample = test::samples(e, 2, 3600).back();
  const detail::Chart chart(e, true);
  const Eigen::VectorXd x = Eigen::VectorXd::Zero(7);
  const auto a = chart.linearize(x, sample, true).jacobian;
  const auto b = detail::finite_difference_jacobian(chart, x, sample, true, 2e-4);
  EXPECT_LT((a - b).norm() / a.norm(), 1e-6);
  Eigen::VectorXd direction(7);
  direction << 0.3, -0.2, 0.4, 0.1, -0.4, 0.2, 0.3;
  const double step = 1e-3;
  const auto numerical = ((detail::state_residual(chart, x + step * direction, sample, true) -
                           detail::state_residual(chart, x - step * direction, sample, true)) /
                          (2 * step))
                             .eval();
  EXPECT_LT((a * direction - numerical).norm() / numerical.norm(), 1e-6);
}

TEST(Fit, RejectsUnobservableDragEvenWhenResidualsAreZero) {
  const auto e = test::truth();
  const auto samples = test::samples(e, 3, 0.000001);
  auto o = test::options(samples);
  o.seed = e;
  o.fit_bstar = true;
  const auto result = fit(samples, o);
  ASSERT_TRUE(result) << result.error().message;
  EXPECT_LT(result->metrics.rms_position_m, 0.001);
  EXPECT_LT(result->rank, result->parameter_count);
  EXPECT_FALSE(result->accepted);
}
