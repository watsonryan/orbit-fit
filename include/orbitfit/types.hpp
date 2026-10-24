#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace orbitfit {

enum class ErrorCode { invalid_input, unsupported_frame, propagation, serialization, solver, io, cancelled };
struct Error {
  ErrorCode code;
  std::string message;
};
template <class T>
using Result = std::expected<T, Error>;

// SGP4's UTC calendar convention: every civil day has 86400 model seconds.
// This deliberately is not TAI elapsed time. Leap-second timestamps are rejected.
struct Epoch {
  std::chrono::sys_time<std::chrono::microseconds> utc{};
  auto operator<=>(const Epoch&) const = default;
};
using Vector3 = std::array<double, 3>;
enum class Frame { teme, gcrf, itrf };

struct State {
  Vector3 position_m{};
  Vector3 velocity_mps{};
};
struct Sample {
  Epoch epoch;
  State state;
  Frame frame{Frame::teme};
  // Row-major covariance of [x,y,z,vx,vy,vz], in m and m/s. Its position
  // block is used when velocities are excluded. Required in measurement mode.
  std::optional<std::array<double, 36>> covariance;
};
struct Elements {
  Epoch epoch;
  double inclination_rad{};
  double raan_rad{};
  double eccentricity{};
  double argument_of_perigee_rad{};
  double mean_anomaly_rad{};
  double mean_motion_rev_per_day{};
  double bstar{};
};
struct Metadata {
  std::uint32_t catalog_number{};
  std::string object_name;
  std::string international_designator;  // YYYY-NNNPPP, or empty for synthetic objects.
  char classification{'U'};
  std::uint32_t element_set_number{1};
  std::uint32_t revolution_number{};
};
struct Tle {
  std::string line1;
  std::string line2;
};
struct ElementSet {
  Elements elements;
  Metadata metadata;
};

enum class Weighting { approximation, independent_measurements };
enum class Termination { converged, iteration_limit, stalled, cancelled, numerical_failure };
struct FitOptions {
  Epoch start;
  Epoch stop;
  Epoch epoch;
  std::optional<Elements> seed;
  bool fit_bstar{false};
  double fixed_bstar{0.0};
  bool use_velocity{true};
  Weighting weighting{Weighting::approximation};
  double position_scale_m{100.0};
  double velocity_scale_mps{0.1};
  std::optional<double> huber_threshold;
  std::size_t maximum_samples{0};
  std::size_t maximum_iterations{80};
  double relative_cost_tolerance{1e-10};
  double absolute_cost_tolerance{1e-12};
  double gradient_tolerance{1e-7};
  double maximum_rms_position_m{1000.0};
  std::stop_token stop_token;
};
struct Residual {
  Epoch epoch;
  Vector3 position_m;
  Vector3 velocity_mps;
};
struct Metrics {
  std::size_t sample_count{};
  double rms_position_m{};
  double max_position_m{};
  double rms_velocity_mps{};
  double max_velocity_mps{};
};
struct FitResult {
  Elements elements;
  Termination termination{Termination::numerical_failure};
  bool accepted{false};
  std::size_t iterations{};
  std::size_t fitted_samples{};
  std::size_t parameter_count{};
  std::size_t rank{};
  double condition_number{};
  double initial_cost{};
  double final_cost{};
  // All samples in the requested window, including samples excluded by decimation.
  Metrics metrics;
  std::vector<Residual> residuals;
  std::vector<std::string> diagnostics;
};

}  // namespace orbitfit
