/**
 * @file test_cli_input.cpp
 * @brief Verify YAML/CSV path resolution, SI conversion, strict parsing, bounds, and replay fidelity.
 * @author watson
 */
#include <gtest/gtest.h>

#include <array>
#include <format>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>

#include "cli_input.hpp"
#include "cli_support.hpp"

namespace {
namespace fs = std::filesystem;
namespace cli = orbitfit::cli;
using Json = nlohmann::json;

class JobInput : public testing::Test {
 protected:
  cli::StagingDirectory root{fs::temp_directory_path() / "orbitfit-input-test"};
  cli::ExampleJob example = cli::make_example_job();
  std::size_t sequence{};

  static void replace(std::string& text, std::string_view from, std::string_view to) {
    const auto at = text.find(from);
    if (at == std::string::npos) throw std::logic_error("Test replacement is absent");
    text.replace(at, from.size(), to);
  }
  fs::path save(const cli::ExampleJob& input) {
    const auto directory = root.path() / std::to_string(sequence++);
    fs::create_directory(directory);
    cli::write_file(directory / "fit.yaml", input.yaml);
    cli::write_file(directory / "states.csv", input.csv);
    return directory / "fit.yaml";
  }
  std::string error(const cli::ExampleJob& input) {
    try {
      (void)cli::load_job(save(input));
      ADD_FAILURE() << "Invalid input was accepted";
      return {};
    } catch (const std::invalid_argument& failure) {
      return failure.what();
    }
  }
};

TEST_F(JobInput, ResolvesCsvRelativeToYamlAndKeepsAnIndependentReplaySnapshot) {
  fs::create_directories(root.path() / "jobs");
  fs::create_directories(root.path() / "data with spaces");
  replace(example.yaml, "path: states.csv", "path: ../data with spaces/trajectory.csv");
  cli::write_file(root.path() / "jobs" / "fit.yaml", example.yaml);
  cli::write_file(root.path() / "data with spaces" / "trajectory.csv", example.csv);
  const auto loaded = cli::load_job(root.path() / "jobs" / "fit.yaml");
  ASSERT_TRUE(loaded.source);
  EXPECT_EQ(loaded.source->data_path, root.path() / "data with spaces" / "trajectory.csv");
  EXPECT_EQ(loaded.source->yaml, example.yaml);
  EXPECT_EQ(loaded.source->csv, example.csv);
  ASSERT_EQ(loaded.request.samples.size(), 61U);
  const auto replay = orbitfit::read_request(loaded.snapshot_json);
  ASSERT_TRUE(replay);
  ASSERT_EQ(replay->samples.size(), loaded.request.samples.size());
  for (std::size_t i = 0; i < replay->samples.size(); ++i) {
    EXPECT_EQ(replay->samples[i].epoch, loaded.request.samples[i].epoch);
    EXPECT_EQ(replay->samples[i].state.position_m, loaded.request.samples[i].state.position_m);
    EXPECT_EQ(replay->samples[i].state.velocity_mps, loaded.request.samples[i].state.velocity_mps);
  }
}

TEST_F(JobInput, ConvertsExplicitKilometresAndVelocitiesToSi) {
  replace(example.yaml, "position_unit: m", "position_unit: km");
  replace(example.yaml, "velocity_unit: m/s", "velocity_unit: km/s");
  example.csv =
      "epoch,x,y,z,vx,vy,vz\n"
      "2026-01-01T00:00:00Z,+1.25,-2.5,3,4e-3,-5e-3,6e-3\n"
      "2026-01-01T00:01:00Z,2,3,4,5,6,7\n"
      "2026-01-01T00:02:00Z,3,4,5,6,7,8\n";
  const auto loaded = cli::load_job(save(example));
  EXPECT_EQ(loaded.request.samples[0].state.position_m, (orbitfit::Vector3{1250, -2500, 3000}));
  EXPECT_EQ(loaded.request.samples[0].state.velocity_mps, (orbitfit::Vector3{4, -5, 6}));
  const auto replay = orbitfit::read_request(loaded.snapshot_json);
  ASSERT_TRUE(replay);
  EXPECT_EQ(replay->samples[0].state.position_m, loaded.request.samples[0].state.position_m);
}

TEST_F(JobInput, AcceptsScalarQuotesBomCrLfWhitespaceAndCommentLines) {
  example.csv =
      "\xEF\xBB\xBF# file provenance\r\n\r\n"
      "\"epoch\", x, y, z, vx, vy, vz\r\n"
      "\"2026-01-01T00:00:00Z\",\"1.25\",-2.5,3,4e-3,-5e-3,6e-3\r\n"
      " # another comment\r\n"
      "2026-01-01T00:01:00Z,2,3,4,5,6,7\r\n"
      "2026-01-01T00:02:00Z,3,4,5,6,7,8";
  const auto loaded = cli::load_job(save(example));
  ASSERT_EQ(loaded.request.samples.size(), 3U);
  EXPECT_DOUBLE_EQ(loaded.request.samples[0].state.position_m[0], 1.25);
  EXPECT_EQ(loaded.source->csv, example.csv);
}

TEST_F(JobInput, AppliesTypesByFieldAndPreservesNumericLookingNames) {
  replace(example.yaml, "object_name: SYNTHETIC EXAMPLE", "object_name: 123");
  replace(example.yaml, "use_velocity: true", "use_velocity: false");
  const auto loaded = cli::load_job(save(example));
  EXPECT_EQ(loaded.request.metadata.object_name, "123");
  EXPECT_FALSE(loaded.request.options.use_velocity);
  EXPECT_EQ(loaded.request.metadata.catalog_number, 99999U);
  EXPECT_DOUBLE_EQ(loaded.request.options.maximum_rms_position_m, 10);
}

TEST_F(JobInput, RejectsDuplicateUnknownAndNonMappingConfiguration) {
  auto input = example;
  replace(input.yaml, "position_unit: m", "position_unit: m\n  position_unit: km");
  EXPECT_NE(error(input).find("Duplicate YAML key: position_unit"), std::string::npos);
  input = example;
  input.yaml += "typo: value\n";
  EXPECT_NE(error(input).find("Unknown job field: typo"), std::string::npos);
  input = example;
  replace(input.yaml, "fit_bstar: false", "fit_bstar: false\n  typo: 1");
  EXPECT_NE(error(input).find("Unknown field: typo"), std::string::npos);
  input = example;
  replace(input.yaml, "metadata:\n", "metadata: value\n#");
  EXPECT_THROW((void)cli::load_job(save(input)), std::invalid_argument);
}

TEST_F(JobInput, RejectsAliasesTagsMultipleDocumentsAndExcessiveNesting) {
  for (const auto yaml : {"schema: &ref orbit-fit.job.v1\ninput: *ref\n", "schema: !!str orbit-fit.job.v1\n",
                          "schema: [orbit-fit.job.v1]\n", "? {complex: key}\n: value\n",
                          "a: {b: {c: {d: {e: {f: {g: {h: {i: 1}}}}}}}}\n"}) {
    auto input = example;
    input.yaml = yaml;
    EXPECT_THROW((void)cli::load_job(save(input)), std::invalid_argument) << yaml;
  }
  auto input = example;
  input.yaml += "---\nschema: orbit-fit.job.v1\n";
  EXPECT_NE(error(input).find("exactly one YAML document"), std::string::npos);
}

TEST_F(JobInput, RejectsInvalidModesTypesUnitsAndUnsupportedCovarianceMode) {
  for (const auto& [from, to] : std::array{
           std::pair{"frame: TEME", "frame: GCRF"}, std::pair{"time_system: UTC", "time_system: TAI"},
           std::pair{"position_unit: m", "position_unit: feet"}, std::pair{"format: csv", "format: unknown"},
           std::pair{"fit_bstar: false", "fit_bstar: yes"},
           std::pair{"catalog_number: 99999", "catalog_number: 1.5"},
           std::pair{"maximum_rms_position_m: 10", "maximum_rms_position_m: nan"},
           std::pair{"weighting: approximation", "weighting: independent_measurements"}}) {
    auto input = example;
    replace(input.yaml, from, to);
    EXPECT_THROW((void)cli::load_job(save(input)), std::invalid_argument) << to;
  }
}

TEST_F(JobInput, ReportsCsvLineAndColumnForMalformedStates) {
  constexpr auto header = "epoch,x,y,z,vx,vy,vz\n";
  for (const auto value : {"", "nan", "inf", "1e9999", "12junk", "+-1"}) {
    auto input = example;
    input.csv = std::string(header) + "2026-01-01T00:00:00Z," + value + ",2,3,4,5,6\n";
    const auto message = error(input);
    EXPECT_NE(message.find("CSV line 2, column x"), std::string::npos) << message;
  }
  for (const auto row : {"bad-date,1,2,3,4,5,6\n", "2026-01-01T00:00:00Z,1,2,3,4,5\n",
                         "2026-01-01T00:00:00Z,1,2,3,4,5,6,7\n"}) {
    auto input = example;
    input.csv = std::string(header) + row;
    EXPECT_NE(error(input).find("CSV line 2"), std::string::npos);
  }
  auto input = example;
  replace(input.yaml, "position_unit: m", "position_unit: km");
  input.csv = std::string(header) + "2026-01-01T00:00:00Z,1e308,2,3,4,5,6\n";
  EXPECT_NE(error(input).find("SI conversion overflow"), std::string::npos);
}

TEST_F(JobInput, RejectsWrongHeadersMissingStatesAndNonIncreasingEpochs) {
  for (const auto csv : {"", "epoch,x,y,z,vx,vy,vz\n", "time,x,y,z,vx,vy,vz\n",
                         "epoch,x,y,z,vx,vy,vz\n2026-01-01T00:00:00Z,1,2,3,4,5,6\n"}) {
    auto input = example;
    input.csv = csv;
    EXPECT_THROW((void)cli::load_job(save(input)), std::invalid_argument);
  }
  auto input = example;
  replace(input.csv, "2026-01-01T00:02:00.000000Z", "2026-01-01T00:00:00.000000Z");
  EXPECT_NE(error(input).find("strictly increasing"), std::string::npos);
}

TEST_F(JobInput, EnforcesConfigurationRowAndSampleBounds) {
  auto input = example;
  input.yaml.append(256 * 1024, ' ');
  EXPECT_NE(error(input).find("byte limit"), std::string::npos);
  input = example;
  input.yaml = "field: " + std::string(4097, 'x') + "\n";
  EXPECT_NE(error(input).find("scalar exceeds 4096 bytes"), std::string::npos);
  input = example;
  for (int i = 0; i < 256; ++i) input.yaml += std::format("field{}: value\n", i);
  EXPECT_NE(error(input).find("exceeds 512 events"), std::string::npos);
  input = example;
  input.csv = "#" + std::string(4096, 'x') + "\n" + example.csv;
  EXPECT_NE(error(input).find("CSV line 1 exceeds 4096 bytes"), std::string::npos);
  input = example;
  input.csv = "epoch,x,y,z,vx,vy,vz\n";
  for (int i = 0; i <= 100000; ++i) input.csv += std::format("2026-01-01T00:00:00.{:06d}Z,1,2,3,4,5,6\n", i);
  EXPECT_NE(error(input).find("exceeds 100000 samples"), std::string::npos);
}

TEST_F(JobInput, RetainsCancellationAndRejectsCancelledLoadsBeforeOpeningFiles) {
  std::stop_source source;
  const auto loaded = cli::load_job(save(example), source.get_token());
  source.request_stop();
  EXPECT_TRUE(loaded.request.options.stop_token.stop_requested());
  EXPECT_THROW((void)cli::load_job(root.path() / "missing.yaml", source.get_token()), cli::Cancelled);
  EXPECT_THROW((void)cli::load_request(root.path() / "missing.json", source.get_token()), cli::Cancelled);
}

TEST_F(JobInput, LoadsRelativeOmmSeedAndSnapshotsItsValues) {
  const auto json = orbitfit::make_example_request();
  ASSERT_TRUE(json);
  const auto request = orbitfit::read_request(*json);
  ASSERT_TRUE(request);
  const auto fitted = orbitfit::fit(request->samples, request->options);
  ASSERT_TRUE(fitted);
  const auto seed = orbitfit::to_omm_json({fitted->elements, request->metadata});
  ASSERT_TRUE(seed);
  cli::write_file(root.path() / "seed.json", *seed);
  example.yaml += "\nseed_omm: ../seed.json\n";
  const auto loaded = cli::load_job(save(example));
  ASSERT_TRUE(loaded.request.options.seed);
  EXPECT_EQ(loaded.request.options.seed->epoch, fitted->elements.epoch);
  EXPECT_DOUBLE_EQ(loaded.request.options.seed->mean_motion_rev_per_day,
                   fitted->elements.mean_motion_rev_per_day);
  EXPECT_TRUE(Json::parse(loaded.snapshot_json).contains("seed_omm"));
}
}  // namespace
