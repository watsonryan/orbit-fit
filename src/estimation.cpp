/**
 * @file estimation.cpp
 * @brief Own Ceres observation factors, whitening, cancellation, and solver diagnostics.
 * @author watson
 */
#include <ceres/ceres.h>

#include <Eigen/Cholesky>
#include <Eigen/SVD>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include "fit_internal.hpp"

namespace orbitfit::detail {
namespace {
using RowMatrix = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic,
                                Eigen::RowMajor>;      ///< Ceres Jacobian memory layout.
constexpr double minimum_trust_region_radius = 1e-12;  ///< Exhausted-radius threshold reported as stalled.

/**
 * @brief Build the left residual/Jacobian scaling matrix for one observation.
 * @param sample Observation with covariance when measurement weighting is selected.
 * @param options Validated weighting and active-row controls.
 * @param weight Positive normalized time-cell weight, or one in measurement mode.
 * @return Inverse covariance Cholesky factor, or diagonal sqrt(weight)/design-scale matrix.
 * @throws std::invalid_argument On missing, non-finite, asymmetric, or non-positive-definite covariance.
 */
Eigen::MatrixXd whitener(const Sample& sample, const FitOptions& options, double weight) {
  const int n = options.use_velocity ? 6 : 3;
  if (options.weighting == Weighting::independent_measurements) {
    if (!sample.covariance)
      throw std::invalid_argument("Measurement mode requires covariance for every fitted sample");
    Eigen::MatrixXd covariance(n, n);
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j)
        covariance(i, j) = (*sample.covariance)[static_cast<std::size_t>(6 * i + j)];
    if (!covariance.allFinite() || !covariance.isApprox(covariance.transpose(), 1e-12))
      throw std::invalid_argument("Measurement covariance must be finite, symmetric, and positive definite");
    const Eigen::LLT<Eigen::MatrixXd> llt(covariance);
    if (llt.info() != Eigen::Success)
      throw std::invalid_argument("Measurement covariance must be finite, symmetric, and positive definite");
    // C = L L^T, so ||L^-1 r||^2 = r^T C^-1 r, including cross-correlations.
    return llt.matrixL().solve(Eigen::MatrixXd::Identity(n, n));
  }
  Eigen::VectorXd scale(n);
  scale.head(3).setConstant(std::sqrt(weight) / options.position_scale_m);
  if (options.use_velocity) scale.tail(3).setConstant(std::sqrt(weight) / options.velocity_scale_mps);
  return scale.asDiagonal();
}

/** @brief One owned observation/whitener/loss attached to the common borrowed immutable orbit chart. */
class StateCost final : public ceres::CostFunction {
 public:
  /** @brief Construct one factor. @param chart Chart outliving this factor. @param sample Owned observation
   * copy. @param options Validated numerical controls. @param weight Positive time weight, or one for
   * measurements. @throws std::invalid_argument On invalid whitening. */
  StateCost(const Chart& chart, Sample sample, const FitOptions& options, double weight)
      : chart_(chart),
        sample_(std::move(sample)),
        stop_(options.stop_token),
        velocity_(options.use_velocity),
        derivatives_(options.derivatives),
        whitening_(whitener(sample_, options, weight)) {
    if (!whitening_.allFinite()) throw std::invalid_argument("Non-finite residual whitening matrix");
    set_num_residuals(velocity_ ? 6 : 3);
    mutable_parameter_block_sizes()->push_back(static_cast<int>(chart_.dimension()));
    if (options.huber_threshold) loss_.emplace(*options.huber_threshold * std::sqrt(weight));
  }

  /** @brief Ceres evaluation callback. @param parameters One active coordinate block. @param residuals
   * Writable active residual rows. @param jacobians Optional row-major Jacobian storage. @return False on
   * cancellation, invalid model, or non-finite residuals; true otherwise. */
  bool Evaluate(double const* const* parameters, double* residuals, double** jacobians) const override {
    if (stop_.stop_requested()) return false;
    try {
      const Eigen::Map<const Eigen::VectorXd> x(parameters[0], static_cast<Eigen::Index>(chart_.dimension()));
      Eigen::Map<Eigen::VectorXd> residual(residuals, num_residuals());
      if (jacobians && jacobians[0]) {
        const auto evaluation = linearize(x);
        residual = evaluation.residual;
        Eigen::Map<RowMatrix> jacobian(jacobians[0], num_residuals(), x.size());
        jacobian = evaluation.jacobian;
      } else {
        residual = whitening_ * state_residual(chart_, x, sample_, velocity_);
      }
      return residual.allFinite();
    } catch (const InvalidModel&) {
      // Ceres rejects invalid trial points and contracts its trust region.
      return false;
    }
  }

  /** @brief Compute derivatives and whiten exactly once. @param x Active normalized coordinates. @return
   * Whitened residual and Jacobian before robust reweighting. @throws InvalidModel On invalid trial or
   * non-finite results. */
  Linearization linearize(const Eigen::VectorXd& x) const {
    auto evaluation = derivatives_ == Derivatives::automatic
                          ? chart_.linearize(x, sample_, velocity_)
                          : Linearization{state_residual(chart_, x, sample_, velocity_),
                                          finite_difference_jacobian(chart_, x, sample_, velocity_)};
    // Use the reference primal in both Ceres callback paths. Last-bit differences
    // between scalar and Jet arithmetic otherwise change the objective depending
    // on whether derivatives were requested, corrupting near-solution gain ratios.
    if (derivatives_ == Derivatives::automatic)
      evaluation.residual = state_residual(chart_, x, sample_, velocity_);
    evaluation.residual = (whitening_ * evaluation.residual).eval();
    evaluation.jacobian = (whitening_ * evaluation.jacobian).eval();
    if (!evaluation.residual.allFinite() || !evaluation.jacobian.allFinite())
      throw InvalidModel("Non-finite whitened residual or Jacobian");
    return evaluation;
  }

  /** @brief Borrow the optional loss. @return Factor-owned Huber loss, or nullptr for quadratic loss. */
  ceres::LossFunction* loss() { return loss_ ? &*loss_ : nullptr; }
  /** @brief Get the first-order robust weight. @param squared_norm Whitened residual squared norm. @return
   * rho-prime, or one without a robust loss. */
  double robust_weight(double squared_norm) const {
    if (!loss_) return 1.0;
    double rho[3];
    loss_->Evaluate(squared_norm, rho);
    return rho[1];
  }

 private:
  const Chart& chart_;                    ///< Borrowed immutable chart, alive through the synchronous solve.
  Sample sample_;                         ///< Owned observation including optional covariance.
  std::stop_token stop_;                  ///< Cooperative cancellation shared with the request.
  bool velocity_;                         ///< Include velocity rows when true.
  Derivatives derivatives_;               ///< Explicit AD or reference-difference selection.
  Eigen::MatrixXd whitening_;             ///< Fixed left multiplier for residuals and Jacobians.
  std::optional<ceres::HuberLoss> loss_;  ///< Owned optional loss; Ceres borrows it.
};

/** @brief Monitor accepted Ceres iterations without calling Problem::Evaluate during a solve. */
class FitMonitor final : public ceres::IterationCallback {
 public:
  /** @brief Borrow synchronous solve state. @param options Fit controls. @param factors Owned immutable
   * factors. @param x Updated accepted parameter block; all references outlive the monitor.
   * @param initial_cost Cost at the initial parameter block. */
  FitMonitor(const FitOptions& options, const std::vector<std::unique_ptr<StateCost>>& factors,
             const Eigen::VectorXd& x, double initial_cost)
      : last_parameters(x), last_cost(initial_cost), options_(options), factors_(factors), x_(x) {}

  /** @brief Check cancellation, stationarity, cost, and trust-radius exhaustion. @param iteration Current
   * Ceres summary. @return Continue, successful termination, or abort with an explicit reason. */
  ceres::CallbackReturnType operator()(const ceres::IterationSummary& iteration) override {
    if (iteration.step_is_successful) {
      last_parameters = x_;
      last_cost = iteration.cost;
    }
    if (options_.stop_token.stop_requested()) {
      termination = Termination::cancelled;
      return ceres::SOLVER_ABORT;
    }
    try {
      if (iteration.step_is_successful && (iteration.cost <= options_.absolute_cost_tolerance ||
                                           scaled_gradient() <= options_.gradient_tolerance)) {
        termination = Termination::converged;
        return ceres::SOLVER_TERMINATE_SUCCESSFULLY;
      }
      // Ceres also labels an exhausted trust region as CONVERGENCE. Keep
      // that distinct from its function/gradient/parameter convergence.
      if (iteration.trust_region_radius <= minimum_trust_region_radius) {
        termination = Termination::stalled;
        diagnostic = "Minimum trust-region radius reached without satisfying convergence criteria";
        return ceres::SOLVER_ABORT;
      }
    } catch (const InvalidModel& ex) {
      termination = Termination::numerical_failure;
      diagnostic = ex.what();
      return ceres::SOLVER_ABORT;
    }
    if (options_.stop_token.stop_requested()) {
      termination = Termination::cancelled;
      return ceres::SOLVER_ABORT;
    }
    return ceres::SOLVER_CONTINUE;
  }

  /** @brief Evaluate column-normalized stationarity. @return Infinity norm of robust J-transpose-r divided by
   * weighted column norms, or infinity if cancelled. @throws InvalidModel On invalid factor or non-finite
   * gradient. */
  double scaled_gradient() const {
    Eigen::VectorXd gradient = Eigen::VectorXd::Zero(x_.size());
    Eigen::VectorXd column_squares = gradient;
    // Problem::Evaluate is forbidden inside a Ceres iteration callback. These
    // immutable factors can be evaluated directly at the accepted parameters.
    for (const auto& factor : factors_) {
      if (options_.stop_token.stop_requested()) return std::numeric_limits<double>::infinity();
      const auto evaluation = factor->linearize(x_);
      const double weight = factor->robust_weight(evaluation.residual.squaredNorm());
      gradient += weight * evaluation.jacobian.transpose() * evaluation.residual;
      column_squares += weight * evaluation.jacobian.colwise().squaredNorm().transpose();
    }
    for (Eigen::Index j = 0; j < gradient.size(); ++j)
      gradient[j] = column_squares[j] > 0 ? gradient[j] / std::sqrt(column_squares[j]) : 0.0;
    if (!gradient.allFinite()) throw InvalidModel("Non-finite normalized gradient");
    return gradient.lpNorm<Eigen::Infinity>();
  }

  std::optional<Termination> termination;  ///< Set only when the monitor terminates the solve.
  std::string diagnostic;                  ///< Optional human-readable stalled/failure explanation.
  Eigen::VectorXd last_parameters;         ///< Owned last accepted iterate, retained across Ceres abort.
  double last_cost;  ///< Objective at last_parameters; usable if evaluation is cancelled.

 private:
  const FitOptions& options_;                               ///< Borrowed request controls.
  const std::vector<std::unique_ptr<StateCost>>& factors_;  ///< Borrowed factor owners, stable during solve.
  const Eigen::VectorXd& x_;  ///< Borrowed parameter block updated by Ceres each iteration.
};
}  // namespace

Result<FitResult> estimate(std::span<const Sample> window, const Chart& chart, const FitOptions& options) {
  Eigen::VectorXd x = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(chart.dimension()));
  std::vector<std::unique_ptr<StateCost>> factors;
  // Explicit ownership keeps factors and losses alive until after Problem.
  ceres::Problem::Options problem_options;
  problem_options.cost_function_ownership = ceres::DO_NOT_TAKE_OWNERSHIP;
  problem_options.loss_function_ownership = ceres::DO_NOT_TAKE_OWNERSHIP;
  ceres::Problem problem(problem_options);
  const auto count =
      options.maximum_samples == 0 ? window.size() : std::min(options.maximum_samples, window.size());
  std::vector<std::size_t> selected;
  for (std::size_t i = 0; i < count; ++i) selected.push_back(i * (window.size() - 1) / (count - 1));
  try {
    for (std::size_t i = 0; i < selected.size(); ++i) {
      if (options.stop_token.stop_requested())
        return std::unexpected(Error{ErrorCode::cancelled, "Fit cancelled while constructing factors"});
      double weight = 1.0;
      if (options.weighting == Weighting::approximation) {
        const auto t = [&](std::size_t j) {
          return seconds_between(window[selected[j]].epoch, options.start);
        };
        const double left = i == 0 ? 0 : 0.5 * (t(i - 1) + t(i));
        const double right =
            i + 1 == selected.size() ? seconds_between(options.stop, options.start) : 0.5 * (t(i) + t(i + 1));
        weight = (right - left) / seconds_between(options.stop, options.start);
      }
      auto& factor =
          factors.emplace_back(std::make_unique<StateCost>(chart, window[selected[i]], options, weight));
      problem.AddResidualBlock(factor.get(), factor->loss(), x.data());
    }
  } catch (const std::exception& ex) {
    return std::unexpected(Error{ErrorCode::invalid_input, ex.what()});
  }

  FitResult result;
  result.fitted_samples = selected.size();
  result.parameter_count = chart.dimension();
  const bool initial_valid = problem.Evaluate({}, &result.initial_cost, nullptr, nullptr, nullptr);
  if (options.stop_token.stop_requested())
    return std::unexpected(Error{ErrorCode::cancelled, "Fit cancelled during initial evaluation"});
  if (!initial_valid || !std::isfinite(result.initial_cost))
    return std::unexpected(Error{ErrorCode::solver, "Initial model is invalid over the fit window"});

  FitMonitor monitor(options, factors, x, result.initial_cost);
  ceres::Solver::Options solver;
  solver.linear_solver_type = ceres::DENSE_QR;
  solver.dense_linear_algebra_library_type = ceres::EIGEN;
  solver.trust_region_strategy_type = ceres::LEVENBERG_MARQUARDT;
  solver.initial_trust_region_radius = 1e3;
  solver.min_trust_region_radius = minimum_trust_region_radius;
  solver.max_num_iterations = static_cast<int>(options.maximum_iterations);
  // Ceres owns relative cost convergence, including floating-point plateaus.
  // The callback adds absolute cost, column-scaled gradient, and cancellation.
  solver.function_tolerance = options.relative_cost_tolerance;
  solver.gradient_tolerance = 0;
  solver.parameter_tolerance = 0;
  solver.update_state_every_iteration = true;
  solver.callbacks.push_back(&monitor);
  solver.num_threads = 1;
  solver.logging_type = ceres::SILENT;
  ceres::Solver::Summary summary;
  ceres::Solve(solver, &problem, &summary);
  // Ceres 2.2 restores the initial parameter blocks on USER_FAILURE, even with
  // update_state_every_iteration. Retain our accepted snapshot for diagnostics.
  if (!summary.IsSolutionUsable()) x = monitor.last_parameters;
  const double retained_cost = summary.IsSolutionUsable() ? summary.final_cost : monitor.last_cost;
  result.iterations =
      summary.iterations.empty() ? 0 : static_cast<std::size_t>(summary.iterations.back().iteration);
  if (options.stop_token.stop_requested())
    result.termination = Termination::cancelled;
  else if (monitor.termination)
    result.termination = *monitor.termination;
  else if (summary.termination_type == ceres::NO_CONVERGENCE)
    result.termination = Termination::iteration_limit;
  else if (summary.termination_type == ceres::CONVERGENCE)
    result.termination = Termination::converged;
  else
    result.termination = Termination::numerical_failure;
  if (!monitor.diagnostic.empty()) result.diagnostics.push_back(monitor.diagnostic);
  if (!monitor.termination) result.diagnostics.push_back(summary.message);

  try {
    result.elements = chart.decode(x);
    if (options.stop_token.stop_requested()) {
      result.termination = Termination::cancelled;
      result.final_cost = retained_cost;
      result.condition_number = std::numeric_limits<double>::infinity();
      result.diagnostics.emplace_back("Cancelled; final rank and full-window metrics are unavailable");
      return result;
    }
    ceres::CRSMatrix jacobian;
    const bool final_valid = problem.Evaluate({}, &result.final_cost, nullptr, nullptr, &jacobian);
    if (options.stop_token.stop_requested()) {
      result.termination = Termination::cancelled;
      result.final_cost = retained_cost;
      result.condition_number = std::numeric_limits<double>::infinity();
      result.diagnostics.emplace_back("Cancelled during final linearization; diagnostics are incomplete");
      return result;
    }
    if (!final_valid || !std::isfinite(result.final_cost))
      throw InvalidModel("Final model has a non-finite residual or Jacobian");
    Eigen::MatrixXd A = Eigen::MatrixXd::Zero(jacobian.num_rows, jacobian.num_cols);
    for (int row = 0; row < jacobian.num_rows; ++row)
      for (int k = jacobian.rows[static_cast<std::size_t>(row)];
           k < jacobian.rows[static_cast<std::size_t>(row + 1)]; ++k)
        A(row, jacobian.cols[static_cast<std::size_t>(k)]) = jacobian.values[static_cast<std::size_t>(k)];
    const Eigen::JacobiSVD<Eigen::MatrixXd> svd(A);
    const auto& singular = svd.singularValues();
    result.rank = static_cast<std::size_t>((singular.array() > singular[0] * 1e-8).count());
    result.condition_number = singular[singular.size() - 1] > 0 ? singular[0] / singular[singular.size() - 1]
                                                                : std::numeric_limits<double>::infinity();
    if (result.rank != result.parameter_count)
      result.diagnostics.emplace_back(
          "Parameters are weakly observable at relative singular-value threshold 1e-8");
  } catch (const std::exception& ex) {
    return std::unexpected(Error{ErrorCode::solver, ex.what()});
  }
  return result;
}
}  // namespace orbitfit::detail
