/**
 * @file support.hpp
 * @brief Provide shared synthetic states and public verification fixture helpers.
 * @author watson
 */
#pragma once
#include <fstream>
#include <stdexcept>
#include <string>

#include "orbitfit/io.hpp"

namespace test {
template <class T>
T take(orbitfit::Result<T> result) {
  if (!result) throw std::runtime_error(result.error().message);
  return std::move(*result);
}
inline orbitfit::Elements truth() {
  return {take(orbitfit::parse_epoch("2026-01-01T00:00:00.123456Z")), 0.9, 1.2, 0.005, 0.4, 2.1, 14.8, 0};
}
inline orbitfit::Metadata metadata() { return {99999, "SYNTHETIC", "", 'U', 1, 0}; }
inline std::vector<orbitfit::Sample> samples(const orbitfit::Elements& e, int count = 61, double dt = 120) {
  std::vector<orbitfit::Sample> result;
  for (int i = 0; i < count; ++i) {
    const auto t = take(orbitfit::add_seconds(e.epoch, i * dt));
    result.push_back({t, take(orbitfit::propagate(e, t)), orbitfit::Frame::teme, std::nullopt});
  }
  return result;
}
inline orbitfit::FitOptions options(const std::vector<orbitfit::Sample>& samples) {
  orbitfit::FitOptions result;
  result.start = samples.front().epoch;
  result.stop = samples.back().epoch;
  result.epoch = result.start;
  result.maximum_rms_position_m = 1.0;
  return result;
}
inline orbitfit::ElementSet reference(int id) {
  std::ifstream file(std::string(ORBITFIT_TEST_DATA) + "/SGP4-VER.TLE");
  if (!file) throw std::runtime_error("Missing public verification fixture");
  std::string line;
  while (std::getline(file, line)) {
    if (line.starts_with("1 ") && std::stoi(line.substr(2, 5)) == id) {
      const auto first = line.substr(0, 69);
      std::getline(file, line);
      return take(orbitfit::from_tle({first, line.substr(0, 69)}));
    }
  }
  throw std::runtime_error("Reference satellite not found");
}
}  // namespace test
