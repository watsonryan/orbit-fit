<!-- @brief Track qualification milestones, research directions, and release criteria. @author watson -->
# Development and qualification roadmap

## 0.1: reproducible fitting baseline

Implemented: independent C++23 library and CLI; pinned CPM dependencies; Ceres observation factors and dense QR estimation; near-Earth/deep-space SGP4; native forward automatic derivatives with a numerical comparison mode; experimental second-order residual evaluation; C++ step-sweep, fitting, and curvature benchmarks; explicit TEME/UTC contract; full-precision OMM and validated TLE output; public reference and synthetic recovery tests; installation consumer and CI configuration.

The 2026-10-24 qualification pass adds an independent NASA ISS forecast, a pinned external eph2tle comparison, exported-product and held-out error checks, perturbed-seed/long-drag/cancellation regressions, native parser fuzzing, strict Doxygen, author/provenance checks, and a design refactor. See the [qualification results](../qualification/README.md), [validation record](validation.md), and [design review](design-review.md).

The software/documentation follow-up adds one shared OMM/TLE delivery decision, report v2, explicit product policies, bounded cancellable CLI I/O/parsing, exclusive staged publication, compiled forecast/cancellation examples, and full internal API documentation. Installation checks relocate/audit the package and compile every public header independently; CI consumes the archive on separate runners without a source checkout. Native console output uses C++23 `std::print`/`std::println`. Executed platform checks and configured but unrun hosted jobs are distinguished in the validation record.

Before a public release, run the GitHub matrix, review public API stability and binary distribution notices, and publish supported compiler/platform results. The current ISS forecast evidence covers one LEO object; it does not establish operational fitness across orbit regimes.

## 0.2: qualification and diagnostics

1. Extend the frozen NASA ISS non-SGP4 forecast corpus to independent MEO, GEO, and HEO data and observed trajectory uncertainty. Synthetic seed recovery now covers eight selected Vallado regimes, with a separate 72-hour high-drag/irregular-cadence regression; these do not substitute for independent truth.
2. Generalize the scoped, reference-tested EME2000-to-TEME fixture converter only when needed. A production frames layer needs explicit EOP/leap-second provenance, broader reference epochs, and errors on missing environmental data. Keep download policy outside the numerical core.
3. Add held-out epochs, RTN residuals, forward-prediction error curves, numerical Jacobian step sweeps, scaled singular vectors, and profile-cost diagnostics for BSTAR. Detect weak observability before trusting a seventh parameter.
4. Benchmark initialization, failure rates, evaluation counts, latency, and peak memory against a transparent ordinary least-squares baseline. Record units, tolerances, compiler, CPU, and dependency versions.
5. Continue the native parser fuzz campaign and active-fit cancellation regressions, add broader runtime/resource stress, run fully instrumented dependency builds, and qualify installed packages on all supported platforms.

The [second-order evaluator](curvature.md) supplies residual Hessians and exact-objective curvature diagnostics. Before adding a solver, measure whether the extra curvature improves difficult fits enough to justify its evaluation cost. Candidate experiments are safeguarded Newton refinement after LM, directional second derivatives for geodesic acceleration, and ranking representable TLE neighbors with a quadratic local model. Compare each against the current first-order solver, including failed steps and total runtime. An exact Hessian can be indefinite; it is not a drop-in replacement for the existing Ceres linearization.

## 0.3: connected arcs

Use one mean-element block per arc, state factors within each arc, and continuity factors that propagate neighboring blocks to the **same Cartesian epoch and frame**. Raw element differences across epochs are not continuity constraints. Explicit jump variables can describe maneuvers; robust or switchable factors can let evidence reject continuity. Ceres can express these connections as residual blocks over multiple parameter blocks. Evaluate a separate incremental inference framework only if a measured streaming workload requires it.

This is where factor graphs provide a clear architectural advantage: shared sensor biases, clock offsets, drag priors, multiple arcs, and mixed observations can join the same estimation problem. Each new factor needs an identifiability analysis and validated derivatives.

## 0.4: optimize the deliverable

**Accuracy-driven arc selection.** Choose the smallest set of arcs whose *exported and reparsed* TLEs satisfy a user-defined error target on a validation grid. Compare dynamic programming and greedy splitting under a maximum arc count, minimum duration, and explicit maneuver boundaries. Optimize a practical tradeoff between catalog size and delivered accuracy.

**Serialization-aware refinement.** After continuous fitting, search neighboring representable TLE values, jointly considering epoch ticks, angle ticks, eccentricity, mean motion, and BSTAR. The curvature evaluator can support candidate ranking, but its present chart fixes the epoch and differs from the printed-field coordinates; that mapping and any epoch derivative need separate treatment. Re-evaluate exact serialized candidates. Compare against straightforward rounding and report improvements and runtime. Local combinatorial refinement may recover accuracy otherwise lost during formatting.

**Observability-aware model complexity.** Let evidence decide whether BSTAR, sensor biases, or additional arcs are justified. Use held-out performance, singular-vector interpretation, and explicit priors, while separating approximation quality from statistical uncertainty.

These are useful research directions, not claims of academic novelty or priority. A literature review and comparative experiments should precede publication claims.

## Release criteria

An operational release needs published error budgets and supported regimes; independent truth and regression thresholds; trustworthy failure behavior; cross-platform and installation evidence; stable format/API versioning; complete license/provenance records; documented resource bounds; and a maintenance/release process. Automatic frame conversion, globally reliable initialization, covariance calibration, Alpha-5 parsing, and operational real-data qualification are not part of the initial implementation.
