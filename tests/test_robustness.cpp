/**
 * @file test_robustness.cpp
 * @brief Exercise difficult fits, cancellation, malformed inputs, and review regressions.
 * @author watson
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <random>
#include <thread>

#include "fit_internal.hpp"
#include "support.hpp"

using namespace orbitfit;
namespace {
void recheck(std::string& line) {
  int sum = 0;
  for (std::size_t i = 0; i < 68; ++i)
    if (line[i] >= '0' && line[i] <= '9')
      sum += line[i] - '0';
    else if (line[i] == '-')
      ++sum;
  line[68] = static_cast<char>('0' + sum % 10);
}
}  // namespace

TEST(Robustness, RejectsInvalidEnumsBeforeEstimation) {
  const auto samples = test::samples(test::truth(), 5, 600);
  for (bool weighting : {false, true}) {
    auto options = test::options(samples);
    if (weighting)
      options.weighting = static_cast<Weighting>(99);
    else
      options.derivatives = static_cast<Derivatives>(99);
    const auto result = fit(samples, options);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::invalid_input);
  }
}

TEST(Robustness, LibraryAndJsonShareResourceBounds) {
  auto samples = test::samples(test::truth(), 3, 60);
  auto options = test::options(samples);
  options.maximum_samples = 100001;
  EXPECT_FALSE(fit(samples, options));
  options.maximum_samples = 0;
  samples.resize(100001, samples.back());
  const auto result = fit(samples, options);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::invalid_input);
}

TEST(Robustness, JsonByteAndNestingLimitsApplyToBothReaders) {
  const std::string oversized(64 * 1024 * 1024 + 1, ' ');
  const auto request = read_request(oversized);
  const auto omm = from_omm_json(oversized);
  ASSERT_FALSE(request);
  ASSERT_FALSE(omm);
  EXPECT_NE(request.error().message.find("64 MiB"), std::string::npos);
  EXPECT_NE(omm.error().message.find("64 MiB"), std::string::npos);
  const std::string nested = std::string(34, '[') + "0" + std::string(34, ']');
  const auto deep_request = read_request(nested);
  const auto deep_omm = from_omm_json(nested);
  ASSERT_FALSE(deep_request);
  ASSERT_FALSE(deep_omm);
  EXPECT_NE(deep_request.error().message.find("32 levels"), std::string::npos);
  EXPECT_NE(deep_omm.error().message.find("32 levels"), std::string::npos);
}

TEST(Robustness, OmmRejectsMetadataItCannotReadBack) {
  for (int scenario = 0; scenario < 3; ++scenario) {
    auto metadata = test::metadata();
    if (scenario == 0) metadata.element_set_number = 1000000000;
    if (scenario == 1) metadata.revolution_number = 1000000000;
    if (scenario == 2) metadata.object_name = std::string(1, '\x7f');
    EXPECT_FALSE(to_omm_json({test::truth(), metadata}));
  }
}

TEST(Robustness, RejectsDegenerateHyperbolicAndNonfiniteStates) {
  const auto original = test::samples(test::truth(), 3, 60);
  for (int scenario = 0; scenario < 5; ++scenario) {
    auto samples = original;
    for (auto& sample : samples) {
      if (scenario == 0) sample.state.position_m = {};
      if (scenario == 1) sample.state.velocity_mps = {};
      if (scenario == 2) sample.state.velocity_mps = {50000, 50000, 50000};
      if (scenario == 3) sample.state.position_m[0] = std::numeric_limits<double>::quiet_NaN();
      if (scenario == 4) sample.state.velocity_mps[2] = std::numeric_limits<double>::infinity();
    }
    const auto result = fit(samples, test::options(samples));
    EXPECT_FALSE(result) << scenario;
  }
}

TEST(Robustness, InvalidEpochStorageNeverReachesCalendarFormatting) {
  const Epoch minimum{std::chrono::sys_time<std::chrono::microseconds>{
      std::chrono::microseconds{std::numeric_limits<std::int64_t>::min()}}};
  const Epoch maximum{std::chrono::sys_time<std::chrono::microseconds>{
      std::chrono::microseconds{std::numeric_limits<std::int64_t>::max()}}};
  EXPECT_THROW(static_cast<void>(format_epoch(minimum)), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(format_epoch(maximum)), std::invalid_argument);
  auto samples = test::samples(test::truth(), 3, 60);
  const auto options = test::options(samples);
  samples.back().epoch = maximum;
  const auto result = fit(samples, options);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::invalid_input);
  EXPECT_TRUE(std::isfinite(seconds_between(maximum, minimum)));
}

TEST(Robustness, ActiveFitHonorsCancellationBeforeFinalDiagnostics) {
  const auto truth = test::truth();
  const auto state = test::take(propagate(truth, truth.epoch));
  std::vector<Sample> samples;
  for (int i = 0; i < 12000; ++i)
    samples.push_back({test::take(add_seconds(truth.epoch, i * 30.0)), state, Frame::teme, std::nullopt});
  auto options = test::options(samples);
  options.seed = truth;
  std::stop_source stop;
  options.stop_token = stop.get_token();
  std::promise<void> started;
  auto running = std::async(std::launch::async, [&] {
    started.set_value();
    return fit(samples, options);
  });
  started.get_future().wait();
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  stop.request_stop();
  const auto result = running.get();
  if (result) {
    EXPECT_EQ(result->termination, Termination::cancelled);
    EXPECT_FALSE(result->accepted);
  } else
    EXPECT_EQ(result.error().code, ErrorCode::cancelled);
}

class SeedRecovery : public testing::TestWithParam<int> {};
TEST_P(SeedRecovery, RecoversAfterJointElementPerturbations) {
  const auto truth = test::reference(GetParam()).elements;
  const double duration = truth.mean_motion_rev_per_day > 6.4 ? 86400 : 172800;
  const auto samples = test::samples(truth, 97, duration / 96);
  auto options = test::options(samples);
  options.fixed_bstar = truth.bstar;
  options.maximum_iterations = 150;
  options.seed = truth;
  options.seed->mean_motion_rev_per_day *= 1.001;
  options.seed->mean_anomaly_rad += 0.03;
  options.seed->argument_of_perigee_rad -= 0.02;
  options.seed->raan_rad += 0.01;
  // Limit the perigee decrease for highly eccentric objects so the seed stays
  // above Earth. A 15% relative eccentricity increase makes 8195 subterranean.
  options.seed->eccentricity += std::min(0.15 * truth.eccentricity, 0.1 * (1 - truth.eccentricity));
  const auto result = test::take(fit(samples, options));
  EXPECT_TRUE(result.accepted) << to_string(result.termination) << " RMS=" << result.metrics.rms_position_m;
  const auto held_out = test::samples(truth, 121, duration / 96);
  const auto predicted = test::take(evaluate(result.elements, std::span(held_out).subspan(97)));
  EXPECT_LT(predicted.rms_position_m, 5);
}
INSTANTIATE_TEST_SUITE_P(Vallado, SeedRecovery,
                         testing::Values(5, 6251, 8195, 9880, 14128, 24208, 25954, 26900));

TEST(Robustness, SeedThatDecaysInsideWindowFailsExplicitly) {
  const auto truth = test::reference(8195).elements;
  const auto samples = test::samples(truth, 97, 1800);
  auto options = test::options(samples);
  options.seed = truth;
  options.fixed_bstar = truth.bstar;
  options.seed->mean_motion_rev_per_day *= 1.001;
  options.seed->mean_anomaly_rad += 0.03;
  options.seed->argument_of_perigee_rad -= 0.02;
  options.seed->raan_rad += 0.01;
  options.seed->eccentricity *= 1.15;
  EXPECT_TRUE(std::ranges::any_of(
      samples, [&](const auto& sample) { return !propagate(*options.seed, sample.epoch); }));
  const auto result = fit(samples, options);
  ASSERT_FALSE(result);
  EXPECT_TRUE(result.error().code == ErrorCode::solver || result.error().code == ErrorCode::propagation);
}

TEST(Robustness, LongArcHighDragAndIrregularCadence) {
  auto truth = test::truth();
  truth.mean_motion_rev_per_day = 15.6;
  truth.eccentricity = 0.001;
  truth.bstar = 5e-4;
  std::vector<Sample> samples;
  for (int i = 0; i <= 180; ++i) {
    const double u = static_cast<double>(i) / 180;
    const auto epoch = test::take(add_seconds(truth.epoch, 259200 * u * u));
    samples.push_back({epoch, test::take(propagate(truth, epoch)), Frame::teme, std::nullopt});
  }
  auto options = test::options(samples);
  options.fit_bstar = true;
  options.maximum_samples = 45;
  options.seed = truth;
  options.seed->bstar = 0;
  options.seed->mean_anomaly_rad += 0.01;
  const auto result = test::take(fit(samples, options));
  EXPECT_TRUE(result.accepted);
  EXPECT_EQ(result.fitted_samples, 45);
  EXPECT_EQ(result.metrics.sample_count, samples.size());
  EXPECT_NEAR(result.elements.bstar, truth.bstar, 1e-8);
}

TEST(Robustness, RobustLossCannotHideFailureOfPhysicalAcceptance) {
  const auto truth = test::truth();
  auto samples = test::samples(truth);
  for (std::size_t i = 10; i < samples.size(); i += 10) samples[i].state.position_m[0] += 50000;
  auto options = test::options(samples);
  options.seed = truth;
  options.huber_threshold = 1;
  options.maximum_rms_position_m = 100;
  const auto result = test::take(fit(samples, options));
  EXPECT_FALSE(result.accepted);
  EXPECT_GT(result.metrics.rms_position_m, options.maximum_rms_position_m);
  EXPECT_EQ(result.residuals.size(), samples.size());
}

TEST(Robustness, SinglePassEvaluationAgreesWithRetainedResiduals) {
  const auto truth = test::truth();
  auto samples = test::samples(truth, 7, 600);
  samples[3].state.position_m[0] += 12;
  samples[3].state.velocity_mps[1] += 0.2;
  const auto evaluated = test::take(detail::evaluate_arc(truth, samples, true));
  const auto metrics = test::take(evaluate(truth, samples));
  ASSERT_EQ(evaluated.residuals.size(), samples.size());
  EXPECT_DOUBLE_EQ(metrics.rms_position_m, evaluated.metrics.rms_position_m);
  EXPECT_NEAR(metrics.rms_position_m, 12 / std::sqrt(7.0), 1e-8);
  EXPECT_NEAR(evaluated.residuals[3].position_m[0], -12, 1e-8);
}

TEST(Robustness, TleRejectsLayoutAndIgnoredFieldCorruptionWithValidChecksums) {
  const auto original = test::take(to_tle({test::truth(), test::metadata()}));
  for (std::size_t column : {8u, 17u, 32u, 43u, 52u, 61u, 63u, 34u, 37u, 45u, 50u}) {
    auto changed = original;
    changed.line1[column] = 'X';
    recheck(changed.line1);
    EXPECT_FALSE(from_tle(changed)) << column;
  }
  for (std::size_t column : {7u, 16u, 25u, 33u, 42u, 51u, 11u, 20u, 37u, 46u, 54u}) {
    auto changed = original;
    changed.line2[column] = 'X';
    recheck(changed.line2);
    EXPECT_FALSE(from_tle(changed)) << column;
  }
  for (std::string_view id : {"26XYZAAA", "26001a  ", "26001   "}) {
    auto changed = original;
    changed.line1.replace(9, 8, id);
    recheck(changed.line1);
    EXPECT_FALSE(from_tle(changed));
  }
}

TEST(Robustness, VeryLargeFiniteAnglesSerializeWithoutOverflow) {
  auto elements = test::truth();
  elements.raan_rad = 1e308;
  elements.argument_of_perigee_rad = -1e308;
  elements.mean_anomaly_rad = 1e308;
  const auto tle = test::take(to_tle({elements, test::metadata()}));
  EXPECT_TRUE(from_tle(tle));
  const auto omm = test::take(to_omm_json({elements, test::metadata()}));
  EXPECT_TRUE(from_omm_json(omm));
  EXPECT_EQ(omm.find("null"), std::string::npos);
}

TEST(Robustness, DeterministicTleMutationCampaignPreservesAcceptedRoundTrips) {
  const auto original = test::take(to_tle({test::truth(), test::metadata()}));
  std::mt19937 generator(0x6f726269);
  for (int trial = 0; trial < 2000; ++trial) {
    auto changed = original;
    auto& line = (generator() & 1) ? changed.line1 : changed.line2;
    line[generator() % 68] = static_cast<char>(generator() % 128);
    recheck(line);
    const auto parsed = from_tle(changed);
    if (parsed) {
      const auto encoded = to_tle(*parsed);
      ASSERT_TRUE(encoded) << parsed->metadata.international_designator;
      EXPECT_TRUE(from_tle(*encoded));
      EXPECT_TRUE(from_omm_json(test::take(to_omm_json(*parsed))));
    }
  }
}
