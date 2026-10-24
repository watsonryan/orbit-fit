/**
 * @file types.hpp
 * @brief Public value types, fit controls, diagnostics, and explicit error results.
 * @author watson
 */
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

/** @brief Standalone SGP4 fitting, propagation, serialization, and diagnostics. */
namespace orbitfit {

/** @brief Machine-readable failure categories; inspect Error::message for context. */
enum class ErrorCode {
  invalid_input,      ///< Invalid values, schema, ordering, dimensions, or covariance.
  unsupported_frame,  ///< A sample or request declares a frame other than TEME.
  propagation,        ///< SGP4 or a derivative evaluation cannot represent the requested state.
  serialization,      ///< An element set cannot be encoded or a TLE cannot be decoded.
  solver,             ///< Estimation cannot initialize or produce a usable numerical result.
  io,                 ///< Reserved for library-level I/O errors; the CLI reports its own I/O exceptions.
  cancelled           ///< Cancellation before a fitted candidate is available.
};

/** @brief An actionable failure returned through Result. */
struct Error {
  ErrorCode code;       ///< Stable category suitable for control flow.
  std::string message;  ///< Human-readable detail; wording is not a stable interface.
};

/**
 * @brief Expected success value or explicit Error.
 * @tparam T Success type; void represents successful validation.
 * @note Allocation failures may still throw std::bad_alloc.
 */
template <class T>
using Result = std::expected<T, Error>;

/**
 * @brief UTC calendar timestamp at microsecond resolution.
 * @details SGP4 uses 86,400 model seconds per civil day. This is not elapsed TAI
 * time across a leap second. Parsers reject leap-second labels. Constructing an
 * Epoch directly does not validate its calendar range.
 */
struct Epoch {
  std::chrono::sys_time<std::chrono::microseconds> utc{};  ///< Civil UTC timestamp, using sys_time storage.
  /** @brief Compare stored timestamps. @param other Timestamp to compare. @return Chronological ordering. */
  auto operator<=>(const Epoch& other) const = default;
};

/** @brief Three Cartesian components; the containing field determines units. */
using Vector3 = std::array<double, 3>;

/** @brief Explicit reference-frame tag; the numerical API accepts only teme. */
enum class Frame {
  teme,  ///< True equator, mean equinox of date: the SGP4 output convention.
  gcrf,  ///< Geocentric celestial reference frame; requires external conversion.
  itrf   ///< International terrestrial reference frame; requires external conversion.
};

/** @brief Cartesian position and velocity; both must be finite for fitting. */
struct State {
  Vector3 position_m{};    ///< Cartesian position in metres.
  Vector3 velocity_mps{};  ///< Cartesian velocity in metres per second.
};

/** @brief One timestamped state observation or ephemeris sample. */
struct Sample {
  Epoch epoch;               ///< Observation epoch; fitting requires strictly increasing, unique epochs.
  State state;               ///< Observed position and velocity in the declared frame.
  Frame frame{Frame::teme};  ///< Must be TEME; no implicit frame transformation occurs.
  /**
   * @brief Optional row-major covariance of [x,y,z,vx,vy,vz].
   * @details Entries have the corresponding SI products (m², m²/s, m²/s²).
   * Independent-measurement fitting requires a finite symmetric positive-definite
   * active block at each selected epoch. Position-only fitting uses the leading
   * 3x3 block, retaining the row stride of six. Approximation fitting ignores it.
   * Different epochs are assumed independent in measurement mode.
   */
  std::optional<std::array<double, 36>> covariance;
};

/**
 * @brief SGP4 mean elements at one fixed epoch, using WGS-72 and improved mode.
 * @details These are mean model parameters, not osculating Keplerian elements.
 * Changing epoch without refitting does not produce the equivalent orbit.
 */
struct Elements {
  Epoch epoch;                       ///< Element epoch, supported calendar years 1957 through 9999.
  double inclination_rad{};          ///< Inclination in radians, inclusive range [0, pi].
  double raan_rad{};                 ///< Right ascension of ascending node in radians; any finite angle.
  double eccentricity{};             ///< Mean eccentricity, in [0, 1).
  double argument_of_perigee_rad{};  ///< Argument of perigee in radians; any finite angle.
  double mean_anomaly_rad{};         ///< Mean anomaly in radians; any finite angle.
  double mean_motion_rev_per_day{};  ///< Mean motion, finite and in (0, 100] revolutions/day.
  double bstar{};  ///< Finite SGP4 BSTAR coefficient in conventional inverse Earth radii; may be negative.
};

/** @brief Catalog identity and format metadata, independent of the fitted orbit. */
struct Metadata {
  std::uint32_t catalog_number{};  ///< OMM: 1..999999999; numeric TLE: 1..99999.
  std::string
      object_name;  ///< Optional object name, at most 128 bytes without ASCII control characters in JSON.
  std::string international_designator;  ///< YYYY-NNNPPP with 1..3 uppercase piece letters, or empty.
  char classification{'U'};              ///< Classification character: U, C, or S.
  std::uint32_t element_set_number{1};   ///< OMM: at most 999999999; TLE: at most 9999.
  std::uint32_t revolution_number{};     ///< Revolution at epoch; OMM: at most 999999999; TLE: at most 99999.
};

/** @brief Two numeric TLE lines, excluding line endings and an optional name line. */
struct Tle {
  std::string line1;  ///< Exactly 69 characters including the checksum.
  std::string line2;  ///< Exactly 69 characters including the checksum.
};

/** @brief Mean elements paired with their catalog identity. */
struct ElementSet {
  Elements elements;  ///< SGP4 mean elements.
  Metadata metadata;  ///< Identity and output-format metadata.
};

/** @brief Interpretation of residual weights. */
enum class Weighting {
  approximation,  ///< Normalized time-cell weights and user design scales; no statistical covariance claim.
  independent_measurements  ///< Supplied within-epoch covariance with independent epochs.
};

/** @brief Derivative method; invalid AD trials never silently switch methods. */
enum class Derivatives {
  automatic,         ///< Native forward Ceres Jets through chart, initialization, and SGP4.
  finite_difference  ///< Reference-kernel central differences, with one-sided differences at valid-domain
                     ///< edges.
};

/** @brief Why the optimizer stopped; convergence alone does not imply acceptance. */
enum class Termination {
  converged,         ///< A configured numerical convergence criterion was met.
  iteration_limit,   ///< The iteration budget was exhausted.
  stalled,           ///< The trust-region radius was exhausted without the monitored criteria.
  cancelled,         ///< A stop request interrupted an active fit; diagnostics may be incomplete.
  numerical_failure  ///< The optimizer or derivative evaluation failed numerically.
};

/**
 * @brief Controls for one fixed-epoch, six- or seven-parameter fit.
 * @details Samples must cover [start, stop], with at least three inside it.
 * All scales, tolerances, and the accuracy target must be finite and strictly
 * positive. Unknown enum values are rejected. The chart and element epoch stay
 * fixed during optimization. There is no parameter covariance estimate.
 */
struct FitOptions {
  Epoch start;                   ///< Inclusive fit-window start; must precede stop.
  Epoch stop;                    ///< Inclusive fit-window end.
  Epoch epoch;                   ///< Fixed element epoch, required to lie inside the fit window.
  std::optional<Elements> seed;  ///< Optional mean-element seed; its epoch must equal epoch exactly.
  bool fit_bstar{false};         ///< Estimate a seventh coordinate for BSTAR when true.
  double fixed_bstar{0.0};  ///< Finite fixed BSTAR, overriding seed BSTAR when fit_bstar is false; also the
                            ///< automatic seed value.
  bool use_velocity{true};  ///< Fit six state residuals when true, three position residuals otherwise.
  Weighting weighting{
      Weighting::approximation};  ///< Approximation scales or independent measurement covariance.
  Derivatives derivatives{Derivatives::automatic};  ///< Automatic derivatives by default.
  double position_scale_m{100.0};         ///< Positive approximation-mode position design scale in metres.
  double velocity_scale_mps{0.1};         ///< Positive approximation-mode velocity design scale in m/s.
  std::optional<double> huber_threshold;  ///< Positive block-Huber transition in unweighted scaled/whitened
                                          ///< residual norm; absent means quadratic loss.
  std::size_t maximum_samples{
      0};  ///< Zero selects all; otherwise 3..100000 evenly spaced indices, preserving endpoints.
  std::size_t maximum_iterations{80};     ///< Ceres iteration budget in [1, 10000].
  double relative_cost_tolerance{1e-10};  ///< Positive Ceres relative objective-change tolerance.
  double absolute_cost_tolerance{1e-12};  ///< Positive objective threshold accepted by the iteration monitor.
  double gradient_tolerance{
      1e-7};  ///< Positive infinity-norm threshold for gradient divided by whitened Jacobian column norms.
  double maximum_rms_position_m{
      1000.0};  ///< Positive continuous-solution RMS position gate over every in-window sample.
  std::stop_token stop_token;  ///< Cooperative cancellation checked during validation, setup, factor
                               ///< evaluations, and final evaluation.
};

/** @brief Unweighted predicted-minus-observed error at one supplied epoch. */
struct Residual {
  Epoch epoch;           ///< Epoch of the observation.
  Vector3 position_m;    ///< Cartesian position error in metres.
  Vector3 velocity_mps;  ///< Cartesian velocity error in metres per second.
};

/** @brief Unweighted physical error statistics, independent of optimization weights. */
struct Metrics {
  std::size_t sample_count{};  ///< Number of evaluated samples; zero means no metrics are available.
  double rms_position_m{};     ///< sqrt(mean(||position residual||²)), in metres; not per-component RMS.
  double max_position_m{};     ///< Maximum Euclidean position residual, in metres.
  double rms_velocity_mps{};   ///< sqrt(mean(||velocity residual||²)), in metres per second.
  double max_velocity_mps{};   ///< Maximum Euclidean velocity residual, in metres per second.
};

/**
 * @brief Candidate mean elements, numerical status, and full-window diagnostics.
 * @details An accepted result requires convergence, full numerical rank, and the
 * configured RMS position gate. This flag covers continuous elements only;
 * callers must serialize, reparse, and evaluate the delivered TLE separately.
 * Cancellation after estimation begins can retain the last candidate with
 * empty residuals, sample_count zero, and unavailable rank/conditioning.
 */
struct FitResult {
  Elements elements;  ///< Last candidate in the original fixed-epoch chart.
  Termination termination{Termination::numerical_failure};  ///< Numerical stopping reason.
  bool accepted{false};      ///< Continuous candidate passes convergence, rank, and physical accuracy gates.
  std::size_t iterations{};  ///< Last Ceres iteration index, excluding initialization.
  std::size_t fitted_samples{};   ///< Number of selected observation factors.
  std::size_t parameter_count{};  ///< Six, or seven when estimating BSTAR.
  std::size_t
      rank{};  ///< Rank of the final whitened/robust Jacobian at relative singular-value threshold 1e-8.
  double condition_number{};  ///< Largest/smallest singular-value ratio; infinity for a zero smallest value
                              ///< or cancelled final evaluation.
  double initial_cost{};      ///< Initial weighted objective, including the configured robust loss.
  double final_cost{};        ///< Final weighted objective, or the last Ceres cost if cancelled.
  Metrics metrics;  ///< Physical errors at every in-window sample, including those omitted by decimation.
  std::vector<Residual>
      residuals;  ///< Full-window residuals in input order; empty if final validation was cancelled.
  std::vector<std::string>
      diagnostics;  ///< Human-readable limitations and solver messages; not a stable machine interface.
};
}  // namespace orbitfit
