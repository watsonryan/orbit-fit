/**
 * @file test_derivatives.cpp
 * @brief Check native Jacobians, branch neighborhoods, and AD fit equivalence.
 * @author watson
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <future>
#include <limits>
#include <numbers>

#include "fit_internal.hpp"
#include "support.hpp"

using namespace orbitfit;
namespace {
// Compare each column, and both positions and velocities, so a large mean-
// motion column cannot hide a missing BSTAR derivative or a velocity error.
void check_derivatives(const Elements& e, double seconds, bool drag, bool velocity, double step = 1e-4,
                       double tolerance = 3e-5) {
  const detail::Chart chart(e, drag);
  const Eigen::VectorXd x = Eigen::VectorXd::Zero(drag ? 7 : 6);
  const auto epoch = test::take(add_seconds(e.epoch, seconds));
  const Sample sample{epoch, test::take(propagate(e, epoch)), Frame::teme, std::nullopt};
  const auto automatic = chart.linearize(x, sample, velocity);
  EXPECT_LT(automatic.residual.head(3).norm(), 2e-5);
  if (velocity) {
    EXPECT_LT(automatic.residual.tail(3).norm(), 2e-8);
  }
  auto numerical = detail::finite_difference_jacobian(chart, x, sample, velocity, step);
  // A tiny orbital-element perturbation can erase a small drag signal through
  // subtraction roundoff. BSTAR has its own scale and does not cross those
  // eccentricity/plane branches, so check it with a larger, independent step.
  if (drag) numerical.col(6) = detail::finite_difference_jacobian(chart, x, sample, velocity, 1e-3).col(6);
  for (Eigen::Index col = 0; col < x.size(); ++col) {
    for (Eigen::Index row = 0; row < automatic.residual.size(); row += 3) {
      const auto a = automatic.jacobian.block(row, col, 3, 1);
      const auto b = numerical.block(row, col, 3, 1);
      // Absolute tolerances cover roundoff in negligible/zero sensitivities.
      const double absolute = row == 0 ? 5e-4 : 5e-7;
      EXPECT_LE((a - b).norm(), absolute + tolerance * std::max(a.norm(), b.norm()))
          << "seconds=" << seconds << " col=" << col << " row=" << row;
    }
  }
}
}  // namespace

class DerivativeVerification : public testing::TestWithParam<int> {};

TEST_P(DerivativeVerification, MatchesReferenceValuesAndFiniteDifferences) {
  const auto e = test::reference(GetParam()).elements;
  for (double seconds : {-43200.0, 0.0, 7200.0, 86400.0}) {
    SCOPED_TRACE(testing::Message() << "object=" << GetParam() << " seconds=" << seconds);
    const auto epoch = test::take(add_seconds(e.epoch, seconds));
    const auto reference = test::take(propagate(e, epoch));
    const auto native = test::take(detail::propagate_native(e, epoch));
    for (std::size_t j = 0; j < 3; ++j) {
      EXPECT_NEAR(reference.position_m[j], native.position_m[j], 1e-6);
      EXPECT_NEAR(reference.velocity_mps[j], native.velocity_mps[j], 1e-9);
    }
    // This almost-equatorial GEO has a plane-vector magnitude near 1e-6;
    // the legacy default step is too large to resolve its local derivative.
    check_derivatives(e, seconds, true, true, GetParam() == 25954 ? 1e-6 : 1e-4);
  }
}
INSTANTIATE_TEST_SUITE_P(Vallado, DerivativeVerification,
                         testing::Values(5, 4632, 6251, 8195, 9880, 14128, 24208, 25954, 26900, 28623));

TEST(AutomaticDerivatives, SixAndSevenParametersAndPositionOnlyAtNonzeroOffsets) {
  auto e = test::truth();
  e.bstar = 2e-4;
  for (bool drag : {false, true}) {
    const detail::Chart chart(e, drag);
    const Eigen::VectorXd x = Eigen::VectorXd::Constant(drag ? 7 : 6, 0.01);
    const auto epoch = test::take(add_seconds(e.epoch, 86400));
    const Sample sample{epoch, test::take(propagate(e, epoch)), Frame::teme, std::nullopt};
    const auto full = chart.linearize(x, sample, true);
    const auto position = chart.linearize(x, sample, false);
    EXPECT_EQ(position.jacobian.cols(), drag ? 7 : 6);
    EXPECT_EQ(position.jacobian.rows(), 3);
    EXPECT_TRUE(position.residual.isApprox(full.residual.head(3), 1e-12));
    EXPECT_TRUE(position.jacobian.isApprox(full.jacobian.topRows(3), 1e-12));
    Eigen::VectorXd direction = Eigen::VectorXd::LinSpaced(x.size(), -0.4, 0.6);
    const auto predicted = (full.jacobian * direction).eval();
    for (double h : {1e-3, 1e-4, 1e-5}) {
      const auto numerical = ((detail::state_residual(chart, x + h * direction, sample, true) -
                               detail::state_residual(chart, x - h * direction, sample, true)) /
                              (2 * h))
                                 .eval();
      EXPECT_LT((predicted.head(3) - numerical.head(3)).norm() / predicted.head(3).norm(), 1e-5);
      EXPECT_LT((predicted.tail(3) - numerical.tail(3)).norm() / predicted.tail(3).norm(), 1e-5);
    }
  }
}

TEST(AutomaticDerivatives, NearlyCircularEquatorialAndRetrograde) {
  for (double inclination : {1e-4, std::numbers::pi - 1e-4}) {
    auto e = test::truth();
    e.inclination_rad = inclination;
    e.eccentricity = 2e-4;
    // Smaller steps lose accuracy in the reference kernel's 1+cos(i) term.
    check_derivatives(e, 3600, false, true, 1e-4, 1e-4);
  }
}

TEST(AutomaticDerivatives, SmoothSidesOfEccentricityClampAndCoefficientThreshold) {
  for (double eccentricity : {0.5e-6, 2e-6, 0.9e-4, 1.1e-4}) {
    auto e = test::truth();
    e.eccentricity = eccentricity;
    // The perturbation stays within the selected branch. No central-derivative
    // claim is made at the actual clamp/switch discontinuity.
    check_derivatives(e, 1200, true, true, 1e-7, 5e-4);
  }
}

TEST(AutomaticDerivatives, SmoothSidesOfNearEarthDeepSpaceBoundary) {
  for (double motion : {6.39, 6.41}) {
    auto e = test::truth();
    e.mean_motion_rev_per_day = motion;
    check_derivatives(e, 43200, true, true);
  }
}

TEST(AutomaticDerivatives, AngularWrapPreservesPhysicalSensitivity) {
  auto e = test::truth();
  e.raan_rad = 0;
  e.argument_of_perigee_rad = 0;
  e.mean_anomaly_rad = 0;
  check_derivatives(e, 0, true, true);
  check_derivatives(e, -86400, true, true);
}

TEST(AutomaticDerivatives, RejectsSingularChartAndInvalidModelsExplicitly) {
  for (int axis : {0, 1, 2}) {
    auto e = test::truth();
    if (axis == 0) e.eccentricity = 0;
    if (axis == 1) e.inclination_rad = 0;
    if (axis == 2) e.inclination_rad = std::numbers::pi;
    const detail::Chart chart(e, false);
    const Sample sample{e.epoch, {}, Frame::teme, std::nullopt};
    EXPECT_THROW((void)chart.linearize(Eigen::VectorXd::Zero(6), sample, true), detail::InvalidModel);
    EXPECT_TRUE(detail::propagate_native(e, e.epoch));  // Propagation itself remains defined.
  }
  const auto e = test::truth();
  const detail::Chart chart(e, false);
  const Sample sample{e.epoch, {}, Frame::teme, std::nullopt};
  EXPECT_THROW((void)chart.linearize(Eigen::VectorXd::Zero(7), sample, true), detail::InvalidModel);
  EXPECT_THROW((void)chart.linearize(Eigen::VectorXd::Constant(6, std::numeric_limits<double>::quiet_NaN()),
                                     sample, true),
               detail::InvalidModel);
  auto invalid = e;
  invalid.mean_motion_rev_per_day = 50;
  EXPECT_FALSE(detail::propagate_native(invalid, e.epoch));
}

TEST(AutomaticDerivatives, DeepSpaceEvaluationsAreIndependentOfOrderAndThread) {
  const auto e = test::reference(8195).elements;
  const detail::Chart chart(e, true);
  const Eigen::VectorXd x = Eigen::VectorXd::Zero(7);
  Sample sample{test::take(add_seconds(e.epoch, 172800)), {}, Frame::teme, std::nullopt};
  const auto a = chart.linearize(x, sample, true);
  auto thread = std::async(std::launch::async, [&] { return chart.linearize(x, sample, true); });
  auto earlier = sample;
  earlier.epoch = test::take(add_seconds(e.epoch, -86400));
  EXPECT_TRUE(chart.linearize(x, earlier, true).jacobian.allFinite());
  const auto b = thread.get();
  const auto c = chart.linearize(x, sample, true);
  EXPECT_EQ((a.residual - b.residual).norm(), 0);
  EXPECT_EQ((a.jacobian - b.jacobian).norm(), 0);
  EXPECT_EQ((a.jacobian - c.jacobian).norm(), 0);
}

TEST(Fit, AutomaticAndNumericalDerivativesAgreeOnNoisyNearEarthAndResonantArcs) {
  for (int id : {5, 8195, 25954}) {
    SCOPED_TRACE(id);
    const auto truth = test::reference(id).elements;
    auto samples = test::samples(truth, 49, 3600);
    for (std::size_t i = 0; i < samples.size(); ++i) {
      samples[i].state.position_m[0] += 3.0 * std::sin(static_cast<double>(i));
      samples[i].state.velocity_mps[2] += 0.002 * std::cos(static_cast<double>(i));
    }
    auto o = test::options(samples);
    o.maximum_rms_position_m = 10;
    o.fixed_bstar = truth.bstar;
    o.seed = truth;
    o.seed->mean_anomaly_rad += 0.002;
    o.seed->mean_motion_rev_per_day *= 1.00001;
    const auto automatic = test::take(fit(samples, o));
    o.derivatives = Derivatives::finite_difference;
    const auto numerical = test::take(fit(samples, o));
    EXPECT_TRUE(automatic.accepted) << to_string(automatic.termination) << " "
                                    << testing::PrintToString(automatic.diagnostics);
    EXPECT_TRUE(numerical.accepted) << to_string(numerical.termination) << " "
                                    << testing::PrintToString(numerical.diagnostics);
    EXPECT_NEAR(automatic.final_cost, numerical.final_cost, 1e-7);
    EXPECT_NEAR(automatic.metrics.rms_position_m, numerical.metrics.rms_position_m, 1e-3);
    for (double seconds : {0.0, 172800.0, 259200.0}) {
      const auto epoch = test::take(add_seconds(truth.epoch, seconds));
      const auto a = test::take(propagate(automatic.elements, epoch));
      const auto b = test::take(propagate(numerical.elements, epoch));
      for (std::size_t j = 0; j < 3; ++j) EXPECT_NEAR(a.position_m[j], b.position_m[j], 0.02);
    }
  }
}
