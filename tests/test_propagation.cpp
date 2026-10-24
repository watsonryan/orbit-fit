/**
 * @file test_propagation.cpp
 * @brief Check public SGP4 reference vectors, deterministic records, and frame handling.
 * @author watson
 */
#include <gtest/gtest.h>

#include <future>
#include <sstream>

#include "fit_internal.hpp"
#include "support.hpp"

using namespace orbitfit;
class PublicVerification : public testing::TestWithParam<int> {};

TEST_P(PublicVerification, MatchesPublishedTemeStates) {
  const auto set = test::reference(GetParam());
  std::ifstream file(std::string(ORBITFIT_TEST_DATA) + "/tcppver.out");
  ASSERT_TRUE(file);
  std::string line;
  int current = 0, checked = 0;
  while (std::getline(file, line)) {
    if (line.find(" xx") != std::string::npos) {
      current = std::stoi(line);
      continue;
    }
    if (current != GetParam()) continue;
    std::istringstream row(line);
    double minutes;
    State expected;
    if (!(row >> minutes >> expected.position_m[0] >> expected.position_m[1] >> expected.position_m[2] >>
          expected.velocity_mps[0] >> expected.velocity_mps[1] >> expected.velocity_mps[2]))
      continue;
    const auto epoch = test::take(add_seconds(set.elements.epoch, minutes * 60));
    const auto state = propagate(set.elements, epoch);
    ASSERT_TRUE(state) << state.error().message;
    const auto native = test::take(detail::propagate_native(set.elements, epoch));
    const detail::Chart chart(set.elements, true);
    const auto automatic =
        chart.linearize(Eigen::VectorXd::Zero(7), Sample{epoch, *state, Frame::teme, std::nullopt}, true);
    EXPECT_LT(automatic.residual.head(3).norm(), 2e-5) << "minutes=" << minutes;
    EXPECT_LT(automatic.residual.tail(3).norm(), 2e-8) << "minutes=" << minutes;
    for (std::size_t j = 0; j < 3; ++j) {
      EXPECT_NEAR(native.position_m[j], state->position_m[j], 1e-6) << "minutes=" << minutes;
      EXPECT_NEAR(native.velocity_mps[j], state->velocity_mps[j], 1e-9) << "minutes=" << minutes;
      EXPECT_NEAR(state->position_m[j], expected.position_m[j] * 1000, 0.02) << "minutes=" << minutes;
      EXPECT_NEAR(state->velocity_mps[j], expected.velocity_mps[j] * 1000, 2e-5) << "minutes=" << minutes;
    }
    ++checked;
  }
  EXPECT_GE(checked, GetParam() == 26900 ? 4 : 5);  // 26900 contains four published rows.
}
INSTANTIATE_TEST_SUITE_P(Vallado2006, PublicVerification,
                         testing::Values(5, 4632, 6251, 8195, 9880, 14128, 24208, 25954, 26900, 28623));

TEST(Propagation, DeepSpaceIsIndependentOfCallOrderAndThread) {
  const auto e = test::reference(8195).elements;
  const auto t = test::take(add_seconds(e.epoch, 172800));
  const auto a = test::take(propagate(e, t));
  auto other = std::async(std::launch::async, [&] { return test::take(propagate(e, t)); });
  EXPECT_TRUE(propagate(e, test::take(add_seconds(e.epoch, -86400))));
  const auto b = test::take(propagate(e, t));
  EXPECT_EQ(a.position_m, b.position_m);
  EXPECT_EQ(a.position_m, other.get().position_m);
}

TEST(Propagation, RejectsInvalidModelsAndFrames) {
  auto e = test::truth();
  e.eccentricity = 1;
  EXPECT_FALSE(propagate(e, e.epoch));
  e = test::truth();
  e.mean_motion_rev_per_day = 50;  // Inside Earth.
  EXPECT_FALSE(propagate(e, e.epoch));
  auto samples = test::samples(test::truth());
  samples[0].frame = Frame::itrf;
  EXPECT_FALSE(evaluate(test::truth(), samples));
}
