/**
 * @file iss.cpp
 * @brief Prepare NASA ISS reference arcs and compare continuous and serialized fits.
 * @author watson
 *
 * This offline qualification tool uses ERFA only for EME2000-to-TEME fixture
 * conversion. It is not a general OEM reader or part of the installed library.
 */
#include <erfa.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>

#include "orbitfit/io.hpp"

namespace {
using namespace orbitfit;
namespace fs = std::filesystem;

template <class T>
T take(Result<T> result) {
  if (!result) throw std::runtime_error(result.error().message);
  return std::move(*result);
}

std::ofstream create(const fs::path& path) {
  std::ofstream stream(path, std::ios::out | std::ios::noreplace);
  if (!stream) throw std::runtime_error("Cannot create a new file: " + path.string());
  stream.exceptions(std::ios::badbit | std::ios::failbit);
  return stream;
}

// The fixture lies in October 2026. TT-UTC = 32.184 + 37 seconds;
// leap-second provenance and the scoped frame convention are in README.md.
double tt_days(Epoch utc, double tt_minus_utc = 69.184) {
  const auto j2000 = take(parse_epoch("2000-01-01T12:00:00Z"));
  return (seconds_between(utc, j2000) + tt_minus_utc) / 86400;
}

State to_teme(const State& eme2000, Epoch utc, double tt_minus_utc = 69.184) {
  const double days = tt_days(utc, tt_minus_utc);
  double rotation[3][3], dpsi, deps;
  eraPnm80(2451545.0, days, rotation);
  eraNut80(2451545.0, days, &dpsi, &deps);
  // TEME uses the geometric equation of the equinoxes, matching the Vallado
  // convention. ERFA matrices act on column vectors; Rz left-multiplies N*P.
  eraRz(dpsi * std::cos(eraObl80(2451545.0, days)), rotation);
  State result;
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j) {
      result.position_m[i] += rotation[i][j] * eme2000.position_m[j];
      result.velocity_mps[i] += rotation[i][j] * eme2000.velocity_mps[j];
    }
  return result;
}

void check_frames() {
  // Published Vallado numerical example, as recorded in Orekit 13.1's
  // TEMEProviderTest. Only numerical reference data are used; see README.md.
  const auto epoch = take(add_seconds(take(parse_epoch("2000-06-30T00:00:00Z")), 0.78495062 * 86400));
  const State eme2000{{-9059941.522499937, 4659697.122583760, 813957.729476476},
                      {-2233.347693917930, -4110.136284940341, -3157.394196306019}};
  const State expected{{-9060473.73569, 4658709.52502, 813686.73153},
                       {-2232.832783, -4110.453490, -3157.345433}};
  const auto actual = to_teme(eme2000, epoch, 64.184);
  double position2 = 0, velocity2 = 0;
  for (std::size_t j = 0; j < 3; ++j) {
    position2 += std::pow(actual.position_m[j] - expected.position_m[j], 2);
    velocity2 += std::pow(actual.velocity_mps[j] - expected.velocity_mps[j], 2);
  }
  // The reference includes complementary equinox terms; our documented
  // geometric convention omits them (at most about 0.13 m at this radius).
  std::print("Published frame check: {:.9f} m, {:.9f} m/s\n", std::sqrt(position2), std::sqrt(velocity2));
  if (std::sqrt(position2) > 0.15 || std::sqrt(velocity2) > 1e-4)
    throw std::runtime_error("EME2000-to-TEME conversion disagrees with the published reference");
}

std::vector<Sample> read_oem(const fs::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Cannot read the NASA OEM");
  bool frame = false, time = false, center = false, version = false;
  std::vector<Sample> samples;
  for (std::string line; std::getline(input, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.starts_with("REF_FRAME")) frame = line.ends_with("EME2000");
    if (line.starts_with("TIME_SYSTEM")) time = line.ends_with("UTC");
    if (line.starts_with("CENTER_NAME")) center = line.ends_with("Earth");
    if (line.starts_with("CCSDS_OEM_VERS")) version = line.ends_with("2.0");
    if (!line.starts_with("2026-")) continue;
    if (!frame || !time || !center || !version) throw std::runtime_error("Unexpected NASA OEM metadata");
    std::istringstream row(line);
    std::string stamp, extra;
    State state;
    if (!(row >> stamp >> state.position_m[0] >> state.position_m[1] >> state.position_m[2] >>
          state.velocity_mps[0] >> state.velocity_mps[1] >> state.velocity_mps[2]) ||
        (row >> extra))
      throw std::runtime_error("Malformed NASA OEM state row");
    const auto epoch = take(parse_epoch(stamp + "Z"));
    if (!samples.empty() && seconds_between(epoch, samples.front().epoch) > 72 * 3600) break;
    if (!samples.empty() && seconds_between(epoch, samples.back().epoch) != 240)
      throw std::runtime_error("The qualification excerpt requires a regular four-minute coasting arc");
    for (auto& value : state.position_m) value *= 1000;
    for (auto& value : state.velocity_mps) value *= 1000;
    samples.push_back({epoch, to_teme(state, epoch), Frame::teme, std::nullopt});
  }
  if (samples.size() != 1081 || format_epoch(samples.front().epoch) != "2026-10-21T12:00:00.000000Z")
    throw std::runtime_error("Expected the pinned 2026-10-21 ISS ephemeris and a 72-hour excerpt");
  return samples;
}

std::vector<Sample> read_csv(const fs::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Cannot read TEME fixture");
  std::vector<Sample> samples;
  for (std::string line; std::getline(input, line);) {
    if (line.empty() || line.starts_with('#') || line.starts_with("epoch,")) continue;
    for (char& c : line)
      if (c == ',') c = ' ';
    std::istringstream row(line);
    std::string stamp;
    Sample sample;
    if (!(row >> stamp >> sample.state.position_m[0] >> sample.state.position_m[1] >>
          sample.state.position_m[2] >> sample.state.velocity_mps[0] >> sample.state.velocity_mps[1] >>
          sample.state.velocity_mps[2]))
      throw std::runtime_error("Malformed TEME fixture row");
    sample.epoch = take(parse_epoch(stamp));
    samples.push_back(sample);
  }
  if (samples.size() != 1081) throw std::runtime_error("Incomplete TEME fixture");
  return samples;
}

std::string case_name(int hours, bool drag) { return std::format("iss-{}h-{}p", hours, drag ? 7 : 6); }

void prepare(const fs::path& oem, const fs::path& directory) {
  fs::create_directories(directory);
  const auto samples = read_oem(oem);
  auto csv = create(directory / "iss-teme.csv");
  csv << "# @brief NASA ISS forecast converted from EME2000; see qualification/README.md.\n"
         "# @author watson (conversion); NASA/JSC/FOD/TOPO (source data).\n"
         "epoch,x_m,y_m,z_m,vx_mps,vy_mps,vz_mps\n";
  for (const auto& s : samples)
    csv << std::format("{},{:.17g},{:.17g},{:.17g},{:.17g},{:.17g},{:.17g}\n", format_epoch(s.epoch),
                       s.state.position_m[0], s.state.position_m[1], s.state.position_m[2],
                       s.state.velocity_mps[0], s.state.velocity_mps[1], s.state.velocity_mps[2]);
  for (int hours : {6, 12, 24, 48}) {
    const auto count = static_cast<std::size_t>(hours * 15 + 1);
    auto vectors = create(directory / std::format("iss-{}h.eph", hours));
    // eph2tle's -1 frame code disables its internal precession. The numbers
    // supplied here are already TEME. Dates are TT, distances m, velocities m/s.
    vectors << std::format("{:.15f} {:.17g} {} -1,149597870700,86400\n",
                           2451545.0 + tt_days(samples.front().epoch), 240.0 / 86400, count);
    for (std::size_t i = 0; i < count; ++i) {
      const auto& s = samples[i];
      vectors << std::format("{:.15f} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g}\n",
                             2451545.0 + tt_days(s.epoch), s.state.position_m[0], s.state.position_m[1],
                             s.state.position_m[2], s.state.velocity_mps[0], s.state.velocity_mps[1],
                             s.state.velocity_mps[2]);
    }
  }
}

ElementSet read_tle(const fs::path& path) {
  std::ifstream input(path);
  for (std::string line; std::getline(input, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.starts_with("1 ")) continue;
    Tle tle{line, {}};
    if (!std::getline(input, tle.line2)) throw std::runtime_error("Missing reference TLE line 2");
    if (!tle.line2.empty() && tle.line2.back() == '\r') tle.line2.pop_back();
    return take(from_tle(tle));
  }
  throw std::runtime_error("Missing reference TLE: " + path.string());
}

void compare(const fs::path& csv, const fs::path& references) {
  auto samples = read_csv(csv);
  // Uniform unit position weights match the comparator's least-squares stage.
  // This is a computational comparison, not a statement of measurement noise.
  for (auto& sample : samples) {
    sample.covariance.emplace();
    for (std::size_t j = 0; j < 6; ++j) (*sample.covariance)[7 * j] = 1;
  }
  const Metadata metadata{25544, "ISS", "1998-067A", 'U', 1, 0};
  std::println(
      "case,backend,product,interval,samples,rms_position_m,max_position_m,rms_velocity_mps,"
      "max_velocity_mps,continuous_fit_accepted,iterations,rank,fit_ms");
  for (int hours : {6, 12, 24, 48}) {
    const auto count = static_cast<std::size_t>(hours * 15 + 1);
    const std::span<const Sample> training(samples.data(), count);
    for (bool drag : {false, true}) {
      FitOptions options;
      options.start = training.front().epoch;
      options.stop = training.back().epoch;
      options.epoch = training[count / 2].epoch;
      options.use_velocity = false;
      options.weighting = Weighting::independent_measurements;
      options.fit_bstar = drag;
      options.maximum_rms_position_m = 2000;
      const auto start = std::chrono::steady_clock::now();
      const auto fitted = take(fit(training, options));
      const double milliseconds =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      const auto serialized = take(from_tle(take(to_tle({fitted.elements, metadata}))));
      const auto reference = read_tle(references / (case_name(hours, drag) + ".tle"));
      const auto report = [&](std::string_view backend, std::string_view product, const Elements& elements) {
        for (int interval = 0; interval < 4; ++interval) {
          // The holdout excludes the fit endpoint, and reports disjoint horizons.
          const std::size_t first = interval == 0 ? 0 : count + (interval == 3 ? 180 : (interval - 1) * 90);
          const std::size_t size = interval == 0 ? count : interval == 3 ? 180 : 90;
          const auto metrics = take(evaluate(elements, std::span(samples.data() + first, size)));
          const std::array<std::string_view, 4> labels{"fit", "0-6h", "6-12h", "12-24h"};
          std::print("{},{},{},{},{},{:.9f},{:.9f},{:.9f},{:.9f},{},{},{},{}\n", case_name(hours, drag),
                     backend, product, labels[interval], metrics.sample_count, metrics.rms_position_m,
                     metrics.max_position_m, metrics.rms_velocity_mps, metrics.max_velocity_mps,
                     backend == "orbit-fit" ? (fitted.accepted ? "true" : "false") : "",
                     backend == "orbit-fit" ? std::to_string(fitted.iterations) : "",
                     backend == "orbit-fit" ? std::to_string(fitted.rank) : "",
                     backend == "orbit-fit" ? std::format("{:.3f}", milliseconds) : "");
        }
      };
      report("orbit-fit", "continuous", fitted.elements);
      report("orbit-fit", "tle", serialized.elements);
      report("eph2tle", "tle", reference.elements);
      if (!fitted.accepted) throw std::runtime_error("Independent-data fit did not satisfy acceptance");
    }
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "check-frames") {
      check_frames();
      return 0;
    }
    if (argc != 4)
      throw std::invalid_argument("Usage: orbitfit_qualification prepare OEM DIR | compare CSV REFDIR");
    if (std::string_view(argv[1]) == "prepare")
      prepare(argv[2], argv[3]);
    else if (std::string_view(argv[1]) == "compare")
      compare(argv[2], argv[3]);
    else
      throw std::invalid_argument("Unknown qualification command");
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "{}", error.what());
    return 1;
  }
}
