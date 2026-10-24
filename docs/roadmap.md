# Development and qualification roadmap

## 0.1: reproducible fitting baseline

Implemented: independent C++23 library and CLI; pinned CPM dependencies; GTSAM state factors; near-Earth/deep-space SGP4; explicit TEME/UTC contract; full-precision OMM and validated TLE output; public reference and synthetic recovery tests; installation consumer and CI configuration.

Before a public release, run the GitHub matrix, review public API stability and third-party distribution notices, exercise real public ephemerides, and publish compiler/platform results. A repository scaffold and passing synthetic tests do not establish operational fitness.

## 0.2: qualification and diagnostics

1. Build an independently sourced public benchmark corpus across LEO, MEO, GEO, HEO, short arcs, high drag, irregular sampling, and difficult circular/equatorial/retrograde cases. Include non-SGP4 truth with traceable licenses.
2. Add a separate frame-conversion layer with explicit EOP and leap-second provenance and independent reference tests. Missing data must remain an error. Avoid embedding an environmental-data download policy in the numerical core.
3. Add held-out epochs, RTN residuals, forward-prediction error curves, numerical Jacobian step sweeps, scaled singular vectors, and profile-cost diagnostics for BSTAR. Detect weak observability before trusting a seventh parameter.
4. Benchmark initialization, failure rates, evaluation counts, latency, and peak memory against a transparent ordinary least-squares baseline. Record units, tolerances, compiler, CPU, and dependency versions.
5. Fuzz parsers and serialization boundaries, test cancellation during long solves, run instrumented dependency builds, and qualify installed packages on all supported platforms.

## 0.3: connected arcs

Use one mean-element block per arc, state factors within each arc, and continuity factors that propagate neighboring blocks to the **same Cartesian epoch and frame**. Raw element differences across epochs are not continuity constraints. Explicit jump variables can describe maneuvers; robust or switchable factors can let evidence reject continuity. Start with batch optimization, then evaluate incremental updates with iSAM2 only if the workload benefits.

This is where factor graphs provide a clear architectural advantage: shared sensor biases, clock offsets, drag priors, multiple arcs, and mixed observations can join the same estimation problem. Each new factor needs an identifiability analysis and validated derivatives.

## 0.4: optimize the deliverable

**Accuracy-driven arc selection.** Choose the smallest set of arcs whose *exported and reparsed* TLEs satisfy a user-defined error target on a validation grid. Compare dynamic programming and greedy splitting under a maximum arc count, minimum duration, and explicit maneuver boundaries. Optimize a practical tradeoff between catalog size and delivered accuracy.

**Serialization-aware refinement.** After continuous fitting, search neighboring representable TLE values, jointly considering epoch ticks, angle ticks, eccentricity, mean motion, and BSTAR. Re-evaluate exact serialized candidates. Compare against straightforward rounding and report improvements and runtime. Local combinatorial refinement may recover accuracy otherwise lost during formatting.

**Observability-aware model complexity.** Let evidence decide whether BSTAR, sensor biases, or additional arcs are justified. Use held-out performance, singular-vector interpretation, and explicit priors, while separating approximation quality from statistical uncertainty.

These are useful research directions, not claims of academic novelty or priority. A literature review and comparative experiments should precede publication claims.

## Release criteria

An operational release needs published error budgets and supported regimes; independent truth and regression thresholds; trustworthy failure behavior; cross-platform and installation evidence; stable format/API versioning; complete license/provenance records; documented resource bounds; and a maintenance/release process. Automatic frame conversion, globally reliable initialization, covariance calibration, Alpha-5 parsing, and operational real-data qualification are not part of the initial implementation.
