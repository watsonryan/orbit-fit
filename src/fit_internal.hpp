/**
 * @file fit_internal.hpp
 * @brief Declare private validation, chart, estimation, and physical-evaluation contracts.
 * @author watson
 */
#pragma once

#include <Eigen/Core>
#include <stdexcept>

#include "orbitfit/curvature.hpp"
#include "orbitfit/orbitfit.hpp"

/** @brief Private numerical implementation; no ABI or source compatibility is promised. */
namespace orbitfit::detail {
/**
 * @brief Validate bounded, ordered TEME input and cross-field fit controls.
 * @param samples Entire caller-owned observation span, including outside-window samples.
 * @param options Requested window, numerical controls, seed, and cancellation token.
 * @return Success or an input/frame/cancellation error; no solver is constructed.
 * @note Selected covariance blocks are factored separately during estimation.
 */
[[nodiscard]] Result<void> validate_request(std::span<const Sample> samples, const FitOptions& options);
/**
 * @brief Obtain a coarse mean-element seed from a Cartesian state.
 * @param samples Sorted states bracketing epoch; validated before this call.
 * @param epoch Fixed fit epoch; Hermite interpolation is used between observations.
 * @param bstar Initial drag coefficient, in inverse Earth radii.
 * @return Keplerian-derived initial guess, or invalid_input for unusable states.
 * @note This is an initialization heuristic, not an osculating-to-mean conversion guarantee.
 */
[[nodiscard]] Result<Elements> initial_elements(std::span<const Sample> samples, Epoch epoch, double bstar);

/** @brief One-pass physical statistics and optional owned residual storage. */
struct ArcEvaluation {
  Metrics metrics;                  ///< Unweighted SI statistics at every supplied observation.
  std::vector<Residual> residuals;  ///< Empty unless retention was requested.
};
/**
 * @brief Propagate once per observation for both metrics and residuals.
 * @param elements Candidate mean elements.
 * @param samples Nonempty TEME observations in arbitrary order.
 * @param keep_residuals Retain predicted-minus-observed errors when true.
 * @param stop Stop request checked before each propagation.
 * @return Complete evaluation or input/frame/propagation/cancellation error.
 */
[[nodiscard]] Result<ArcEvaluation> evaluate_arc(const Elements& elements, std::span<const Sample> samples,
                                                 bool keep_residuals, std::stop_token stop = {});

/** @brief Internal exception for a trial outside the propagation/derivative domain. */
class InvalidModel : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/** @brief Dynamic residual vector and its derivatives with respect to chart offsets. */
struct Linearization {
  Eigen::VectorXd residual;  ///< Three position or six state residuals.
  Eigen::MatrixXd jacobian;  ///< Residual rows by six or seven coordinate columns.
};

/**
 * @brief Immutable scaled equinoctial-style chart selected from the seed.
 * @details Prograde/retrograde selection, epoch, and origin stay fixed through
 * estimation. The scalar model shares these coordinates across all derivative
 * orders. Copies are unnecessary inside observation factors.
 */
class Chart {
 public:
  /** @brief Select a chart. @param seed Valid mean elements. @param drag Include BSTAR offsets. */
  Chart(const Elements& seed, bool drag);
  /**
   * @brief Map normalized local coordinates to mean elements.
   * @param offset Exactly dimension() finite offsets.
   * @return Elements at the seed epoch.
   * @throws InvalidModel On invalid dimensions or non-finite offsets.
   * @note Mean-element validity is checked by the subsequent propagation call.
   */
  [[nodiscard]] Elements decode(const Eigen::VectorXd& offset) const;
  /**
   * @brief Evaluate raw SI residuals and native first derivatives.
   * @param offset Exactly dimension() finite offsets.
   * @param sample TEME observation with finite state.
   * @param velocity Include velocity residual rows.
   * @return Raw predicted-minus-observed residual and normalized-coordinate Jacobian.
   * @throws InvalidModel On singular chart, invalid trial, or failed propagation.
   */
  [[nodiscard]] Linearization linearize(const Eigen::VectorXd& offset, const Sample& sample,
                                        bool velocity) const;
  /**
   * @brief Evaluate raw SI residuals and their first and second derivatives.
   * @param offset Exactly dimension() finite offsets.
   * @param sample TEME observation with finite state.
   * @param velocity Include velocity residual rows.
   * @return Unsymmetrized residual Hessians and the common residual/Jacobian.
   * @throws InvalidModel On singular chart, invalid trial, or failed propagation.
   */
  [[nodiscard]] ResidualCurvature curvature(const Eigen::VectorXd& offset, const Sample& sample,
                                            bool velocity) const;
  /** @brief Get the active coordinate count. @return Six, or seven when drag is active. */
  [[nodiscard]] std::size_t dimension() const { return drag_ ? 7 : 6; }

 private:
  /** @brief Reject singular differentiation origins. @param offset Active offsets. @throws InvalidModel On
   * invalid dimensions, values, or zero chart radii. */
  void check_derivative_domain(const Eigen::VectorXd& offset) const;
  Elements seed_;         ///< Fixed epoch and seed BSTAR; never modified by a trial.
  Eigen::VectorXd base_;  ///< Six unscaled chart coordinates at the seed.
  bool retrograde_;       ///< Fixed hemisphere selection; never differentiated.
  bool drag_;             ///< Include the seventh BSTAR offset when true.
};

/**
 * @brief Evaluate a raw residual using the unmodified reference kernel.
 * @param chart Immutable coordinate system.
 * @param offset Active normalized coordinates.
 * @param sample TEME observation.
 * @param velocity Include velocity components.
 * @return Predicted-minus-observed SI vector.
 * @throws InvalidModel On invalid coordinates or propagation failure.
 */
[[nodiscard]] Eigen::VectorXd state_residual(const Chart& chart, const Eigen::VectorXd& offset,
                                             const Sample& sample, bool velocity);
/**
 * @brief Difference the unmodified reference propagator for independent AD checks.
 * @param chart Fixed coordinate system.
 * @param offset Active normalized coordinates.
 * @param sample TEME observation.
 * @param velocity Include velocity components.
 * @param step Positive coordinate increment, default 1e-4.
 * @return Central differences, or one-sided columns when one neighbor is invalid.
 * @throws InvalidModel When neither neighbor is valid or output is non-finite.
 */
[[nodiscard]] Eigen::MatrixXd finite_difference_jacobian(const Chart& chart, const Eigen::VectorXd& offset,
                                                         const Sample& sample, bool velocity,
                                                         double step = 1e-4);
/**
 * @brief Run the adapted scalar kernel with doubles for conformance testing.
 * @param elements Valid mean elements.
 * @param epoch Target observation epoch.
 * @return TEME SI state or propagation error, including invalid elements.
 */
[[nodiscard]] Result<State> propagate_native(const Elements& elements, Epoch epoch);
/**
 * @brief Estimate the selected factors and compute local numerical diagnostics.
 * @param window Validated in-window observations, at least three, in time order.
 * @param chart Immutable chart that outlives all factors in this synchronous call.
 * @param options Validated fit controls; selected covariance is checked here.
 * @return Candidate with cost/rank/stopping reason, or setup/solver/cancellation error.
 * @note The caller evaluates the full window and sets physical acceptance.
 */
[[nodiscard]] Result<FitResult> estimate(std::span<const Sample> window, const Chart& chart,
                                         const FitOptions& options);
}  // namespace orbitfit::detail
