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

TEST(Jacobian, DirectionalDerivativeAndStepStability) {
  const auto e = test::truth();
  const auto sample = test::samples(e, 2, 3600).back();
  const detail::Chart chart(e, true);
  const gtsam::Vector x = gtsam::Vector::Zero(7);
  const auto a = detail::state_jacobian(chart, x, sample, true, 1e-4);
  const auto b = detail::state_jacobian(chart, x, sample, true, 2e-4);
  EXPECT_LT((a - b).norm() / a.norm(), 1e-6);
  gtsam::Vector direction(7);
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
