/**
 * @file test_independent.cpp
 * @brief Regress fits and held-out exported TLE errors against a NASA forecast and eph2tle.
 * @author watson
 */
#include <gtest/gtest.h>

#include <format>
#include <sstream>

#include "support.hpp"

namespace {
using namespace orbitfit;

std::vector<Sample> independent_samples() {
  std::ifstream file(std::string(ORBITFIT_TEST_DATA) + "/independent/iss-teme.csv");
  if (!file) throw std::runtime_error("Missing independent TEME fixture");
  std::vector<Sample> result;
  for (std::string line; std::getline(file, line);) {
    if (line.starts_with('#') || line.starts_with("epoch,")) continue;
    for (char& c : line)
      if (c == ',') c = ' ';
    std::istringstream row(line);
    std::string stamp, extra;
    Sample sample;
    if (!(row >> stamp >> sample.state.position_m[0] >> sample.state.position_m[1] >>
          sample.state.position_m[2] >> sample.state.velocity_mps[0] >> sample.state.velocity_mps[1] >>
          sample.state.velocity_mps[2]) ||
        (row >> extra))
      throw std::runtime_error("Malformed independent TEME fixture");
    sample.epoch = test::take(parse_epoch(stamp));
    sample.covariance.emplace();
    for (std::size_t j = 0; j < 6; ++j) (*sample.covariance)[7 * j] = 1;
    result.push_back(sample);
  }
  if (result.size() != 1081) throw std::runtime_error("Incomplete independent fixture");
  return result;
}

class IndependentIss : public testing::TestWithParam<std::tuple<int, bool>> {};

TEST_P(IndependentIss, FitsForecastAndChecksReparsedTleOnDisjointHoldouts) {
  const auto [hours, drag] = GetParam();
  const auto samples = independent_samples();
  const auto count = static_cast<std::size_t>(hours * 15 + 1);
  const std::span<const Sample> training(samples.data(), count);
  FitOptions options;
  options.start = training.front().epoch;
  options.stop = training.back().epoch;
  options.epoch = training[count / 2].epoch;
  options.use_velocity = false;
  options.weighting = Weighting::independent_measurements;
  options.fit_bstar = drag;
  options.maximum_rms_position_m = 2000;
  const auto fitted = test::take(fit(training, options));
  ASSERT_TRUE(fitted.accepted);
  EXPECT_EQ(fitted.rank, drag ? 7 : 6);
  const Metadata metadata{25544, "ISS", "1998-067A", 'U', 1, 0};
  const auto encoded = test::take(to_tle({fitted.elements, metadata}));
  const auto delivered = test::take(from_tle(encoded));
  std::ifstream file(std::format("{}/independent/iss-{}h-{}p.tle", ORBITFIT_TEST_DATA, hours, drag ? 7 : 6));
  Tle reference;
  ASSERT_TRUE(static_cast<bool>(std::getline(file, reference.line1)));
  ASSERT_TRUE(static_cast<bool>(std::getline(file, reference.line2)));
  const auto baseline = test::take(from_tle(reference));
  const auto physical = test::take(evaluate(delivered.elements, training));
  const auto comparator = test::take(evaluate(baseline.elements, training));
  // Regression budgets, not promised orbital accuracy: allow TLE quantization
  // and platform rounding while detecting a material loss against eph2tle.
  EXPECT_LE(physical.rms_position_m, comparator.rms_position_m * 1.02 + 10);
  EXPECT_LT(physical.rms_position_m, 2000);
  EXPECT_LT(std::abs(physical.rms_position_m - fitted.metrics.rms_position_m), 20);
  const auto omm = test::take(from_omm_json(test::take(to_omm_json({fitted.elements, metadata}))));
  EXPECT_NEAR(test::take(evaluate(omm.elements, training)).rms_position_m, fitted.metrics.rms_position_m,
              1e-5);
  for (const auto& [offset, size] : {std::pair{0u, 90u}, {90u, 90u}, {180u, 180u}}) {
    const auto holdout = std::span(samples).subspan(count + offset, size);
    const auto actual = test::take(evaluate(delivered.elements, holdout));
    const auto expected = test::take(evaluate(baseline.elements, holdout));
    EXPECT_LE(actual.rms_position_m, expected.rms_position_m * 1.15 + 50);
  }
  // A short training fit can pass while its prediction fails the same target.
  if (hours == 12 && drag) {
    const auto holdout = std::span(samples).subspan(count + 180, 180);
    EXPECT_GT(test::take(evaluate(delivered.elements, holdout)).rms_position_m, 10000);
  }
}
INSTANTIATE_TEST_SUITE_P(Nasa, IndependentIss,
                         testing::Combine(testing::Values(6, 12, 24, 48), testing::Bool()));
}  // namespace
