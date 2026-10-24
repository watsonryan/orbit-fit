/**
 * @file test_curvature.cpp
 * @brief Check residual Hessians, Taylor errors, objective curvature, and thread behavior.
 * @author watson
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <future>
#include <limits>
#include <numbers>

#include "fit_internal.hpp"
#include "orbitfit/curvature.hpp"
#include "support.hpp"

using namespace orbitfit;
namespace {
Eigen::MatrixXd hessian(const ResidualCurvature& curvature, std::size_t row) {
  const auto n = static_cast<Eigen::Index>(curvature.parameter_count);
  Eigen::MatrixXd result(n, n);
  for (Eigen::Index i = 0; i < n; ++i)
    for (Eigen::Index j = 0; j < n; ++j)
      result(i, j) = curvature.hessians[row][static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
  return result;
}

void check_curvature(const Elements& seed, double seconds, const Eigen::VectorXd& offset, double step = 1e-3,
                     double relative_tolerance = 2e-5, double symmetry_tolerance = 1e-9) {
  const detail::Chart chart(seed, offset.size() == 7);
  const auto epoch = test::take(add_seconds(seed.epoch, seconds));
  const Sample sample{epoch, test::take(propagate(seed, epoch)), Frame::teme, std::nullopt};
  const auto first = chart.linearize(offset, sample, true);
  const auto second = test::take(
      residual_curvature(seed, sample, std::span(offset.data(), static_cast<std::size_t>(offset.size()))));
  ASSERT_EQ(second.parameter_count, offset.size());
  ASSERT_EQ(second.residual_count, 6);
  for (std::size_t row = 0; row < 6; ++row) {
    const double absolute = row < 3 ? 5e-7 : 5e-10;
    EXPECT_NEAR(second.residual[row], first.residual[static_cast<Eigen::Index>(row)], absolute);
    for (std::size_t j = 0; j < second.parameter_count; ++j) {
      const double expected = first.jacobian(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(j));
      EXPECT_NEAR(second.jacobian[row][j], expected, absolute + 1e-10 * std::abs(expected));
    }
    const auto H = hessian(second, row);
    EXPECT_LE((H - H.transpose()).norm(), symmetry_tolerance * std::max(1.0, H.norm())) << "row=" << row;
  }

  // Check every column, including mixed BSTAR derivatives, using the independent
  // first-order AD path at neighboring points, using a fourth-order stencil.
  // A larger step suppresses subtraction roundoff; cap the eccentricity and
  // plane perturbations relative to their chart radii to remain local.
  // The benchmark's --steps mode exposes this truncation/roundoff balance.
  // Group SI position/velocity
  // separately so a large position sensitivity cannot hide a velocity error.
  for (Eigen::Index j = 0; j < offset.size(); ++j) {
    const auto current = chart.decode(offset);
    double h = j == 6 ? 20 * step : step;
    if (j == 1 || j == 2)
      h = std::min(
          h, 0.03 * current.eccentricity / std::sqrt(1 - current.eccentricity * current.eccentricity) / 1e-3);
    if (j == 3 || j == 4)
      h = std::min(
          h, 0.005 *
                 std::tan(std::min(current.inclination_rad, std::numbers::pi - current.inclination_rad) / 2) /
                 1e-3);
    Eigen::VectorXd d = Eigen::VectorXd::Zero(offset.size());
    d[j] = h;
    const auto numerical = ((8 * (chart.linearize(offset + d, sample, true).jacobian -
                                  chart.linearize(offset - d, sample, true).jacobian) -
                             (chart.linearize(offset + 2 * d, sample, true).jacobian -
                              chart.linearize(offset - 2 * d, sample, true).jacobian)) /
                            (12 * h))
                               .eval();
    for (Eigen::Index i = 0; i < offset.size(); ++i) {
      for (std::size_t start : {0u, 3u}) {
        Eigen::Vector3d actual;
        for (std::size_t row = 0; row < 3; ++row)
          actual[static_cast<Eigen::Index>(row)] =
              second.hessians[start + row][static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
        const auto expected = numerical.block<3, 1>(static_cast<Eigen::Index>(start), i);
        const double absolute = start == 0 ? 2e-5 : 2e-8;
        EXPECT_LE((actual - expected).norm(),
                  absolute + relative_tolerance * std::max(actual.norm(), expected.norm()))
            << "seconds=" << seconds << " i=" << i << " j=" << j << " start=" << start;
      }
    }
  }
}
}  // namespace

class CurvatureVerification : public testing::TestWithParam<int> {};
TEST_P(CurvatureVerification, MatchesFirstOrderAndJacobianDifferences) {
  const auto seed = test::reference(GetParam()).elements;
  for (double seconds : {-43200.0, 0.0, 86400.0}) {
    SCOPED_TRACE(testing::Message() << "object=" << GetParam() << " seconds=" << seconds);
    check_curvature(seed, seconds, Eigen::VectorXd::Zero(7));
  }
}
INSTANTIATE_TEST_SUITE_P(Vallado, CurvatureVerification,
                         testing::Values(5, 4632, 6251, 8195, 9880, 14128, 24208, 25954, 26900, 28623));

TEST(Curvature, NonzeroOffsetsSixAndSevenParameters) {
  auto seed = test::truth();
  seed.bstar = 2e-4;
  for (int n : {6, 7}) check_curvature(seed, 172800, Eigen::VectorXd::LinSpaced(n, -0.02, 0.03));
}

TEST(Curvature, PositionOnlyAndInactiveStorage) {
  const auto seed = test::truth();
  const auto sample = test::samples(seed, 2, 86400).back();
  for (std::size_t n : {6u, 7u}) {
    std::array<double, 7> x{};
    const auto full = test::take(residual_curvature(seed, sample, std::span(x.data(), n)));
    const auto position = test::take(residual_curvature(seed, sample, std::span(x.data(), n), false));
    EXPECT_EQ(position.parameter_count, n);
    EXPECT_EQ(position.residual_count, 3);
    for (std::size_t row = 0; row < 6; ++row) {
      EXPECT_EQ(position.residual[row], row < 3 ? full.residual[row] : 0);
      for (std::size_t i = 0; i < 7; ++i) {
        EXPECT_EQ(position.jacobian[row][i], row < 3 && i < n ? full.jacobian[row][i] : 0);
        if (i >= n) {
          EXPECT_EQ(full.jacobian[row][i], 0);
        }
        for (std::size_t j = 0; j < 7; ++j) {
          EXPECT_EQ(position.hessians[row][i][j], row < 3 && i < n && j < n ? full.hessians[row][i][j] : 0);
          if (i >= n || j >= n) {
            EXPECT_EQ(full.hessians[row][i][j], 0);
          }
        }
      }
    }
  }
}

TEST(Curvature, TaylorPredictionHasCubicRemainder) {
  for (int id : {5, 8195}) {
    SCOPED_TRACE(id);
    const auto seed = test::reference(id).elements;
    const detail::Chart chart(seed, true);
    const auto epoch = test::take(add_seconds(seed.epoch, 43200));
    const Sample sample{epoch, test::take(propagate(seed, epoch)), Frame::teme, std::nullopt};
    const Eigen::VectorXd x = Eigen::VectorXd::Zero(7);
    const auto first = chart.linearize(x, sample, true);
    const auto second = test::take(residual_curvature(seed, sample, std::span(x.data(), 7)));
    const Eigen::VectorXd direction = Eigen::VectorXd::LinSpaced(7, -0.4, 0.6);
    Eigen::VectorXd acceleration(6);
    for (std::size_t row = 0; row < 6; ++row)
      acceleration[static_cast<Eigen::Index>(row)] = direction.dot(hessian(second, row) * direction);
    std::array<double, 2> previous{};
    for (double h : {0.2, 0.1, 0.05}) {
      const auto actual = detail::state_residual(chart, x + h * direction, sample, true);
      const Eigen::VectorXd linear = first.residual + h * first.jacobian * direction;
      const Eigen::VectorXd quadratic = linear + 0.5 * h * h * acceleration;
      for (Eigen::Index start : {0, 3}) {
        const double error = (actual - quadratic).segment<3>(start).norm();
        EXPECT_LT(error, 0.1 * (actual - linear).segment<3>(start).norm());
        auto& before = previous[static_cast<std::size_t>(start / 3)];
        if (before > 0) {
          EXPECT_GT(before / error, 6);
          EXPECT_LT(before / error, 10);
        }
        before = error;
      }
    }
  }
}

TEST(Curvature, ObjectiveHessianVectorAfterCorrelatedWhiteningAndHuberLoss) {
  auto seed = test::truth();
  seed.bstar = 2e-4;
  const detail::Chart chart(seed, true);
  auto sample = test::samples(seed, 2, 86400).back();
  sample.state.position_m[0] += 1000;
  sample.state.velocity_mps[2] -= 0.5;
  const Eigen::VectorXd x = Eigen::VectorXd::Constant(7, 0.01);
  const auto second = test::take(residual_curvature(seed, sample, std::span(x.data(), 7)));
  Eigen::MatrixXd W = Eigen::MatrixXd::Identity(6, 6);
  W.topLeftCorner(3, 3) /= 100;
  W.bottomRightCorner(3, 3) /= 0.1;
  W(3, 0) = 0.002;  // A nonsingular whitener mixing position and velocity.
  const auto first = chart.linearize(x, sample, true);
  const Eigen::VectorXd r = W * first.residual;
  const Eigen::MatrixXd J = W * first.jacobian;
  for (bool robust : {false, true}) {
    const double q = r.squaredNorm();
    const double threshold = 0.75;
    ASSERT_GT(q, threshold * threshold);  // Stay on one smooth Huber branch.
    const double rho1 = robust ? threshold / std::sqrt(q) : 1;
    const double rho2 = robust ? -rho1 / (2 * q) : 0;
    const Eigen::VectorXd g = J.transpose() * r;
    Eigen::MatrixXd H = rho1 * J.transpose() * J + 2 * rho2 * g * g.transpose();
    for (Eigen::Index row = 0; row < 6; ++row) {
      Eigen::MatrixXd whitened = Eigen::MatrixXd::Zero(7, 7);
      for (Eigen::Index k = 0; k < 6; ++k)
        whitened += W(row, k) * hessian(second, static_cast<std::size_t>(k));
      H += rho1 * r[row] * whitened;
    }
    const auto gradient = [&](const Eigen::VectorXd& at) {
      const auto linear = chart.linearize(at, sample, true);
      const Eigen::VectorXd residual = W * linear.residual;
      const double weight = robust ? threshold / residual.norm() : 1;
      return (weight * linear.jacobian.transpose() * W.transpose() * residual).eval();
    };
    for (const Eigen::VectorXd& direction :
         {Eigen::VectorXd::LinSpaced(7, -0.4, 0.6).eval(), Eigen::VectorXd::Unit(7, 6).eval()}) {
      const Eigen::VectorXd predicted = H * direction;
      for (double step : {1e-4, 5e-5}) {
        const Eigen::VectorXd numerical =
            (8 * (gradient(x + step * direction) - gradient(x - step * direction)) -
             (gradient(x + 2 * step * direction) - gradient(x - 2 * step * direction))) /
            (12 * step);
        for (Eigen::Index j = 0; j < 7; ++j)
          EXPECT_NEAR(predicted[j], numerical[j],
                      1e-6 + 2e-5 * std::max(std::abs(predicted[j]), std::abs(numerical[j])))
              << "robust=" << robust << " step=" << step << " coordinate=" << j;
      }
    }
  }
}

TEST(Curvature, NearSingularChartsAndSmoothSidesOfModelBranches) {
  for (double inclination : {1e-4, std::numbers::pi - 1e-4}) {
    auto seed = test::truth();
    seed.eccentricity = 2e-4;
    seed.inclination_rad = inclination;
    SCOPED_TRACE(inclination);
    // Near the plane origin, cancellation amplifies contraction-dependent
    // roundoff. GCC 14/arm64 measures 6.1e-9 relative raw asymmetry here;
    // disabling FMA moves the error between components. Keep the raw Hessian
    // and the independent Jacobian-difference check rather than symmetrizing.
    check_curvature(seed, 3600, Eigen::VectorXd::Zero(6), 1e-3, 2e-4, 1e-8);
  }
  for (double eccentricity : {0.5e-6, 2e-6, 0.9e-4, 1.1e-4}) {
    auto seed = test::truth();
    seed.eccentricity = eccentricity;
    SCOPED_TRACE(eccentricity);
    // Near the 1e-6 clamp, finite differences of the chart's cancelling
    // Jacobian terms have a narrower useful step range (see --steps).
    check_curvature(seed, 1200, Eigen::VectorXd::Zero(7), 1e-3, eccentricity < 1e-5 ? 1e-3 : 2e-4);
  }
  for (double motion : {6.39, 6.41}) {
    auto seed = test::truth();
    seed.mean_motion_rev_per_day = motion;
    SCOPED_TRACE(motion);
    check_curvature(seed, 43200, Eigen::VectorXd::Zero(7));
  }
}

TEST(Curvature, PhysicalHessianAcrossAngleWrap) {
  auto seed = test::truth();
  seed.raan_rad = seed.argument_of_perigee_rad = seed.mean_anomaly_rad = 0;
  check_curvature(seed, 0, Eigen::VectorXd::Zero(7));
  check_curvature(seed, -86400, Eigen::VectorXd::Zero(7));
}

TEST(Curvature, ExplicitErrorsForInvalidInputAndSingularChart) {
  const auto seed = test::truth();
  const Sample sample{seed.epoch, test::take(propagate(seed, seed.epoch)), Frame::teme, std::nullopt};
  const std::array<double, 7> zero{};
  auto bad_sample = sample;
  bad_sample.frame = Frame::gcrf;
  const auto frame = residual_curvature(seed, bad_sample, zero);
  ASSERT_FALSE(frame);
  EXPECT_EQ(frame.error().code, ErrorCode::unsupported_frame);
  EXPECT_FALSE(residual_curvature(seed, sample, std::span<const double>{}));
  EXPECT_FALSE(residual_curvature(seed, sample, std::span(zero.data(), 5)));
  auto bad_offset = zero;
  bad_offset[2] = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(residual_curvature(seed, sample, bad_offset));
  bad_sample = sample;
  bad_sample.state.position_m[0] = std::numeric_limits<double>::infinity();
  EXPECT_FALSE(residual_curvature(seed, bad_sample, zero));
  for (int axis : {0, 1, 2}) {
    auto bad_seed = seed;
    if (axis == 0) bad_seed.eccentricity = 0;
    if (axis == 1) bad_seed.inclination_rad = 0;
    if (axis == 2) bad_seed.inclination_rad = std::numbers::pi;
    EXPECT_FALSE(residual_curvature(bad_seed, sample, zero));
  }
  auto bad_seed = seed;
  bad_seed.mean_motion_rev_per_day = 50;
  EXPECT_FALSE(residual_curvature(bad_seed, sample, zero));
}

TEST(Curvature, DeepSpaceCallsAreIndependentOfOrderAndThread) {
  const auto seed = test::reference(8195).elements;
  const auto sample = test::samples(seed, 2, 172800).back();
  const std::array<double, 7> x{};
  const auto a = test::take(residual_curvature(seed, sample, x));
  auto thread =
      std::async(std::launch::async, [&] { return test::take(residual_curvature(seed, sample, x)); });
  auto earlier = sample;
  earlier.epoch = test::take(add_seconds(seed.epoch, -86400));
  EXPECT_TRUE(residual_curvature(seed, earlier, x));
  const auto b = thread.get();
  const auto c = test::take(residual_curvature(seed, sample, x));
  EXPECT_EQ(a.residual, b.residual);
  EXPECT_EQ(a.jacobian, b.jacobian);
  EXPECT_EQ(a.hessians, b.hessians);
  EXPECT_EQ(a.hessians, c.hessians);
}
