/**
 * @file forecast.cpp
 * @brief Fit a TEME forecast CSV, validate delivered products, and score disjoint prediction epochs.
 * @author watson
 */
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <print>
#include <sstream>
#include <stdexcept>

#include "orbitfit/io.hpp"

namespace {
template <class T>
T take(orbitfit::Result<T> result) {
  if (!result) throw std::runtime_error(result.error().message);
  return std::move(*result);
}

// This example reads the documented seven-column TEME/SI fixture vocabulary.
// It does not infer frames or turn arbitrary external ephemerides into TEME.
std::vector<orbitfit::Sample> read_csv(const char* path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Cannot open TEME CSV");
  std::vector<orbitfit::Sample> samples;
  for (std::string line; std::getline(input, line);) {
    if (line.starts_with('#') || line.starts_with("epoch,")) continue;
    std::replace(line.begin(), line.end(), ',', ' ');
    std::istringstream row(line);
    std::string stamp, extra;
    orbitfit::Sample sample;
    if (!(row >> stamp >> sample.state.position_m[0] >> sample.state.position_m[1] >>
          sample.state.position_m[2] >> sample.state.velocity_mps[0] >> sample.state.velocity_mps[1] >>
          sample.state.velocity_mps[2]) ||
        (row >> extra))
      throw std::runtime_error("Expected epoch,x_m,y_m,z_m,vx_mps,vy_mps,vz_mps");
    sample.epoch = take(orbitfit::parse_epoch(stamp));
    if (samples.size() == 100000) throw std::runtime_error("Example is limited to 100000 samples");
    samples.push_back(sample);
  }
  if (input.bad() || samples.size() < 4) throw std::runtime_error("Incomplete TEME CSV");
  if (!std::ranges::is_sorted(samples, {}, &orbitfit::Sample::epoch))
    throw std::runtime_error("CSV epochs must be ordered");
  return samples;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Usage: orbitfit_forecast_example TEME.csv");
    const auto samples = read_csv(argv[1]);
    orbitfit::FitOptions options;
    options.start = samples.front().epoch;
    options.stop = take(orbitfit::add_seconds(options.start, 86400));
    options.epoch = take(orbitfit::add_seconds(options.start, 43200));
    options.use_velocity = false;
    options.fit_bstar = false;
    options.fixed_bstar = 0;
    options.maximum_rms_position_m = 2000;
    const auto end = std::ranges::upper_bound(samples, options.stop, {}, &orbitfit::Sample::epoch);
    const std::span<const orbitfit::Sample> training(samples.begin(), end);
    const std::span<const orbitfit::Sample> holdout(end, samples.end());
    if (holdout.empty())
      throw std::runtime_error("Provide independent prediction epochs after the first day");
    const auto fitted = take(orbitfit::fit(training, options));
    // This fixture is ISS. Replace the identity when using another object's data.
    const orbitfit::Metadata identity{25544, "ISS", "1998-067A", 'U', 1, 0};
    const auto delivery = take(orbitfit::prepare_products(fitted, training, identity, options));
    std::println("Delivery: {}", orbitfit::to_string(delivery.reason));
    if (!delivery.accepted) {
      std::print("{}", orbitfit::report_json(fitted, identity, options, delivery));
      return 2;
    }
    const auto model = take(orbitfit::from_tle(*delivery.tle_text));
    const auto prediction = take(orbitfit::evaluate(model.elements, holdout));
    std::println("{}\n{}", delivery.tle_text->line1, delivery.tle_text->line2);
    std::println("Exported training RMS: {:.6f} m", delivery.tle.metrics->rms_position_m);
    std::println("Disjoint prediction RMS: {:.6f} m", prediction.rms_position_m);
    std::println("Disjoint prediction maximum: {:.6f} m", prediction.max_position_m);
    // Delivery acceptance is the training-window contract. Prediction has a
    // separate application-specific budget; do not reuse training acceptance.
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "{}", error.what());
    return 1;
  }
}
