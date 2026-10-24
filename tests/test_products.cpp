/**
 * @file test_products.cpp
 * @brief Verify serialized-product policies, report agreement, windowing, and cancellation.
 * @author watson
 */
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "support.hpp"

namespace {
using namespace orbitfit;
using Json = nlohmann::json;

class Products : public testing::Test {
 protected:
  FitRequest request = test::take(read_request(test::take(make_example_request())));
  FitResult fitted = test::take(fit(request.samples, request.options));

  Delivery prepare(ProductPolicy policy = ProductPolicy::tle_required) {
    return test::take(prepare_products(fitted, request.samples, request.metadata, request.options, policy));
  }
};

TEST_F(Products, SharedDecisionIncludesOmmAndActualTleMetrics) {
  const auto delivery = prepare();
  ASSERT_TRUE(delivery.accepted);
  EXPECT_EQ(delivery.reason, DeliveryReason::accepted);
  ASSERT_TRUE(delivery.omm.metrics);
  ASSERT_TRUE(delivery.tle.metrics);
  const auto parsed = test::take(from_tle(*delivery.tle_text));
  const auto independently_scored = test::take(evaluate(parsed.elements, request.samples));
  EXPECT_DOUBLE_EQ(delivery.tle.metrics->rms_position_m, independently_scored.rms_position_m);
  EXPECT_NEAR(delivery.omm.metrics->rms_position_m, fitted.metrics.rms_position_m, 1e-6);
  const auto report = Json::parse(report_json(fitted, request.metadata, request.options, delivery));
  EXPECT_EQ(report.at("schema"), "orbit-fit.report.v2");
  EXPECT_EQ(report.at("accepted"), delivery.accepted);
  EXPECT_EQ(report.at("reason"), to_string(delivery.reason));
  EXPECT_EQ(report.at("product_policy"), to_string(delivery.policy));
  EXPECT_EQ(report.at("serialized_omm").at("status"), "accepted");
  EXPECT_EQ(report.at("serialized_tle").at("metrics").at("rms_position_m"),
            independently_scored.rms_position_m);
}

TEST_F(Products, TightGateRejectsRequiredTleButAllowsExplicitOmmPolicy) {
  request.options.maximum_rms_position_m = 1;
  fitted = test::take(fit(request.samples, request.options));
  ASSERT_TRUE(fitted.accepted);
  const auto required = prepare();
  EXPECT_FALSE(required.accepted);
  EXPECT_EQ(required.reason, DeliveryReason::tle_rejected);
  EXPECT_EQ(required.omm.status, ProductStatus::accepted);
  EXPECT_EQ(required.tle.status, ProductStatus::accuracy_rejected);
  const auto optional = prepare(ProductPolicy::omm_sufficient);
  EXPECT_TRUE(optional.accepted);
  EXPECT_EQ(optional.tle.status, ProductStatus::accuracy_rejected);
  const auto report = Json::parse(report_json(fitted, request.metadata, request.options, optional));
  EXPECT_EQ(report.at("accepted"), true);
  EXPECT_EQ(report.at("serialized_tle").at("accepted"), false);
}

TEST_F(Products, CatalogOutsideNumericTleRangeRequiresExplicitOmmPolicy) {
  request.metadata.catalog_number = 100001;
  const auto required = prepare();
  EXPECT_FALSE(required.accepted);
  EXPECT_EQ(required.reason, DeliveryReason::tle_unavailable);
  EXPECT_EQ(required.tle.status, ProductStatus::serialization_failed);
  EXPECT_FALSE(required.tle_text);
  EXPECT_TRUE(required.omm_json);
  EXPECT_TRUE(prepare(ProductPolicy::omm_sufficient).accepted);
}

TEST_F(Products, ProductAccuracyCannotOverrideRejectedContinuousFit) {
  fitted.accepted = false;
  fitted.termination = Termination::iteration_limit;
  for (auto policy : {ProductPolicy::tle_required, ProductPolicy::omm_sufficient}) {
    const auto delivery = prepare(policy);
    EXPECT_FALSE(delivery.accepted);
    EXPECT_EQ(delivery.reason, DeliveryReason::continuous_rejected);
    EXPECT_EQ(delivery.omm.status, ProductStatus::accepted);
    const auto report = Json::parse(report_json(fitted, request.metadata, request.options, delivery));
    EXPECT_EQ(report.at("accepted"), false);
    EXPECT_EQ(report.at("continuous_accepted"), false);
    EXPECT_EQ(report.at("serialized_omm").at("accepted"), true);
  }
}

TEST_F(Products, BothPoliciesRequireSuccessfulOmmDelivery) {
  request.metadata.catalog_number = 0;
  for (auto policy : {ProductPolicy::tle_required, ProductPolicy::omm_sufficient}) {
    const auto delivery = prepare(policy);
    EXPECT_FALSE(delivery.accepted);
    EXPECT_EQ(delivery.reason, DeliveryReason::omm_rejected);
    EXPECT_FALSE(delivery.omm_json);
    EXPECT_EQ(delivery.omm.status, ProductStatus::serialization_failed);
  }
}

TEST_F(Products, WindowIncludesEveryObservationDespiteFitDecimation) {
  request.options.start = request.samples[10].epoch;
  request.options.stop = request.samples[50].epoch;
  request.options.epoch = request.samples[30].epoch;
  request.options.maximum_samples = 7;
  // Out-of-window states must not enter any delivery accuracy gate.
  request.samples.front().state.position_m[0] += 1e9;
  request.samples.back().state.position_m[0] -= 1e9;
  fitted = test::take(fit(request.samples, request.options));
  const auto delivery = prepare();
  ASSERT_TRUE(delivery.accepted);
  EXPECT_EQ(fitted.fitted_samples, 7);
  EXPECT_EQ(delivery.continuous_metrics.sample_count, 41);
  EXPECT_EQ(delivery.omm.metrics->sample_count, 41);
  EXPECT_EQ(delivery.tle.metrics->sample_count, 41);
}

TEST_F(Products, RejectsMismatchedResultsAndInvalidPolicy) {
  --fitted.metrics.sample_count;
  auto result = prepare_products(fitted, request.samples, request.metadata, request.options);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::invalid_input);
  ++fitted.metrics.sample_count;
  result = prepare_products(fitted, request.samples, request.metadata, request.options,
                            static_cast<ProductPolicy>(99));
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::invalid_input);
}

TEST_F(Products, CancellationNeverProducesAnAcceptedDelivery) {
  std::stop_source source;
  source.request_stop();
  request.options.stop_token = source.get_token();
  auto result = prepare_products(fitted, request.samples, request.metadata, request.options);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::cancelled);
  request.options.stop_token = {};
  fitted.termination = Termination::cancelled;
  result = prepare_products(fitted, request.samples, request.metadata, request.options);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::cancelled);
}
}  // namespace
