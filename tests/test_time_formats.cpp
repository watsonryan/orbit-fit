#include <gtest/gtest.h>

#include <limits>
#include <numbers>

#include "support.hpp"

using namespace orbitfit;

TEST(Time, CalendarValidationAndMicrosecondArithmetic) {
  auto epoch = test::take(parse_epoch("2056-02-29T23:59:59.999999Z"));
  EXPECT_EQ(format_epoch(epoch), "2056-02-29T23:59:59.999999Z");
  const auto next = test::take(add_seconds(epoch, 0.000001));
  EXPECT_EQ(format_epoch(next), "2056-03-01T00:00:00.000000Z");
  EXPECT_DOUBLE_EQ(seconds_between(next, epoch), 0.000001);
  for (auto bad : {"2025-02-29T00:00:00Z", "2016-12-31T23:59:60Z", "2026-01-01T24:00:00Z",
                   "2026-01-01T00:00:00", "2026-01-01T00:00:00.0000001Z", "1956-01-01T00:00:00Z"})
    EXPECT_FALSE(parse_epoch(bad)) << bad;
  EXPECT_FALSE(add_seconds(epoch, std::numeric_limits<double>::infinity()));
}

TEST(Tle, ReferenceParseSerializeAndChecksum) {
  const auto source = test::reference(5);
  EXPECT_EQ(source.metadata.international_designator, "1958-002B");
  auto tle = test::take(to_tle(source));
  EXPECT_EQ(tle.line1.size(), 69);
  EXPECT_EQ(tle.line2.size(), 69);
  const auto parsed = test::take(from_tle(tle));
  EXPECT_EQ(parsed.elements.epoch, source.elements.epoch);
  EXPECT_NEAR(parsed.elements.mean_motion_rev_per_day, source.elements.mean_motion_rev_per_day, 1e-14);
  auto unsupported = tle;
  unsupported.line1[62] = '4';
  unsupported.line1[68] = static_cast<char>('0' + (tle.line1[68] - '0' + 4) % 10);
  EXPECT_FALSE(from_tle(unsupported));
  tle.line1[25] = tle.line1[25] == '1' ? '2' : '1';
  EXPECT_FALSE(from_tle(tle));
}

TEST(Tle, EpochYearCarryAndAngularCarry) {
  auto e = test::truth();
  e.epoch = test::take(parse_epoch("2055-12-31T23:59:59.999900Z"));
  e.raan_rad = (360.0 - 0.00001) * std::numbers::pi / 180;
  e.bstar = 9.99996e-5;
  const auto parsed = test::take(from_tle(test::take(to_tle({e, test::metadata()}))));
  EXPECT_EQ(format_epoch(parsed.elements.epoch), "2056-01-01T00:00:00.000000Z");
  EXPECT_EQ(parsed.elements.raan_rad, 0);
  EXPECT_DOUBLE_EQ(parsed.elements.bstar, 0.0001);
  e.epoch = test::take(parse_epoch("2056-12-31T23:59:59.999900Z"));
  EXPECT_FALSE(to_tle({e, test::metadata()}));
}

TEST(Tle, RejectsUnrepresentableValuesRatherThanSaturating) {
  auto e = test::truth();
  auto m = test::metadata();
  m.catalog_number = 100001;
  EXPECT_FALSE(to_tle({e, m}));
  EXPECT_TRUE(to_omm_json({e, m}));
  m = test::metadata();
  e.eccentricity = 0.99999999;
  EXPECT_FALSE(to_tle({e, m}));
  e = test::truth();
  e.bstar = std::numeric_limits<double>::denorm_min();
  EXPECT_FALSE(to_tle({e, m}));
  e.bstar = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(to_tle({e, m}));
  e = test::truth();
  m.international_designator = "2057-001A";
  EXPECT_FALSE(to_tle({e, m}));
}

TEST(Omm, PrecisionAndLargeCatalogIdentity) {
  auto e = test::truth();
  auto m = test::metadata();
  m.catalog_number = 123456789;
  const auto text = test::take(to_omm_json({e, m}));
  const auto parsed = test::take(from_omm_json(text));
  EXPECT_EQ(parsed.metadata.catalog_number, m.catalog_number);
  EXPECT_EQ(parsed.elements.epoch, e.epoch);
  const auto metrics = test::take(evaluate(parsed.elements, test::samples(e)));
  EXPECT_LT(metrics.max_position_m, 1e-6);
  auto wrong = text;
  wrong.replace(wrong.find("TEME"), 4, "GCRF");
  EXPECT_FALSE(from_omm_json(wrong));
}

TEST(Request, ExplicitFramesUnknownFieldsAndDuplicateKeys) {
  const auto text = test::take(make_example_request());
  const auto parsed = read_request(text);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_EQ(parsed->samples.size(), 61);
  auto wrong = text;
  wrong.replace(wrong.find("TEME"), 4, "GCRF");
  EXPECT_FALSE(read_request(wrong));
  wrong = text;
  wrong.insert(1, "\"typo\":1,");
  EXPECT_FALSE(read_request(wrong));
  wrong = text;
  wrong.insert(1, "\"frame\":\"TEME\",");
  EXPECT_FALSE(read_request(wrong));
  EXPECT_FALSE(read_request("{}"));
}
