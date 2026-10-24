<!-- @brief Design review of estimation, validation, ownership, derivatives, and release boundaries. @author watson -->
# Design review — 2026-10-24

Scope: the final Ceres/AD/curvature implementation, public contracts, parsing, cancellation, and qualification infrastructure. This is a code review performed during development, not an independent certification or external audit.

## Boundaries and ownership

The public API uses value types, `std::span`, `std::expected`, and `std::stop_token`. Ceres/Eigen remain private implementation dependencies. The single orbit block and per-epoch residual blocks are a sufficient factor graph for the present problem; a generic graph framework or additional solver abstraction would add interfaces without a second supported use case.

The review separated `fit.cpp` into orchestration, `validation.cpp` for request checks, `initialization.cpp` for coarse seeding/interpolation, `chart.cpp` for coordinate construction and reference differences, `estimation.cpp` for Ceres ownership and stopping, and `evaluation.cpp` for physical diagnostics. `derivatives.cpp` and `curvature.cpp` share the scalar chart/propagation implementation in `sgp4_model.hpp`.

The fit window is a contiguous span selected with binary search from validated sorted input. Factors own selected samples and whitening matrices, but reference the single immutable chart for the synchronous solve. `ceres::Problem` explicitly does not own the cost/loss objects. Destruction order keeps factors and their optional Huber losses alive until the problem is destroyed. No borrowed chart survives `fit`.

## Findings corrected

| Finding | Change and evidence |
|---|---|
| Final metrics and residual retention propagated the same arc twice | One shared evaluator optionally retains residuals; a physical 12 m perturbation test checks the RMS independently |
| The full fit window and each chart were unnecessarily copied | Window span and shared immutable chart; existing objective and recovery tests retained |
| JSON limits did not protect direct C++ callers | Public sample/iteration/selection bounds and invalid-enum checks before estimation |
| Extreme stored epochs could reach calendar formatting | Calendar-range checks before conversion; direct storage-boundary regression tests |
| Finite radian angles could overflow while converting to degrees | Reduce modulo 2π before conversion; TLE and OMM round-trip tests at ±1e308 |
| TLE checksums did not ensure valid ignored fields or separators | Validate fixed-column layout, mean-motion derivative fields, exponent mantissas, and designator characters |
| C++ callers could emit OMM metadata that the reader rejects | Apply the same catalog/element/revolution bounds and reject ASCII DEL in object names; round-trip boundary regressions |
| Cancellation could continue into expensive final work | Stop checks in validation/setup/factors and final evaluation; cancelled candidates skip full diagnostics |

## Numerical decisions retained

Ceres LM with dense QR is appropriate for six or seven unknowns. A sparse solver would not benefit this single-block graph. The final SVD measures local rank in the documented scaled coordinates; it is not a covariance or global identifiability proof. Both continuous and serialized physical gates remain necessary, including when robust loss suppresses outliers in the objective.

The unmodified Vallado kernel remains the propagation/reference path. The scalar-templated adaptation carries derivatives through initialization and propagation, with fresh deep-space state. The original source is immutable and retains its notice. Adaptation conformance and numerical derivative checks are independent execution paths but share SGP4 lineage; they cannot establish spacecraft accuracy.

Second-order evaluation remains an explicit diagnostic API. It computes one outer Jet direction per pass, bounding stack storage on worker threads. The power adapter enforces constant exponents, and tests check symmetry, Taylor remainders, and objective Hessian-vector products. The exact objective Hessian can be indefinite. Replacing LM with unsafeguarded Newton updates is not justified by the current evidence.

## Remaining limits

The ISS corpus adds a non-SGP4 NASA forecast and an external estimation implementation. Both products are scored through the same unmodified Vallado propagator. This establishes forecast approximation and comparative fitting behavior for one LEO object; it does not cover independent MEO/GEO/HEO truth, tracking uncertainty, maneuvers, or operational prediction guarantees. Short-arc BSTAR extrapolation is a demonstrated failure mode.

Automatic initialization remains local and can fail from unusable or decaying seeds. AD at exactly singular chart origins is rejected explicitly. Input bounds and bounded fuzzing reduce accidental failures but do not establish adversarial-service isolation or a runtime deadline. The SVD and individual SGP4 calls are not interruptible. Hosted Linux/Windows/macOS jobs and fully instrumented dependencies still need release evidence.

The internal structure is ready for further qualification without another solver/framework rewrite. Future additions should be driven by measured requirements: observability-aware BSTAR selection, independent orbit-regime datasets, arc segmentation, and then connected arc factors. See the [roadmap](roadmap.md).

## Delivery and integration review — 2026-10-24 follow-up

`products.cpp` now owns serialization, reparsing, full-window physical validation, and the final delivery policy. `Delivery` is the owned result used by the API, CLI, and report. Product physical accuracy and continuous optimization acceptance remain explicit, independent fields. `tle_required` is the default; `omm_sufficient` is an explicit choice. The report is v2 because overall acceptance and OMM validation are now represented. Request v1 remains unchanged.

The CLI support module owns bounded stream I/O, unique staging, and scoped signal cancellation. The OS no-replace rename is the publication commit point. Tests cover an empty destination appearing after staging, competing publishers, short writes, permissions, stale staging ownership, and cancelled publication. A POSIX subprocess check sends real signals to the executable; cross-platform unit checks exercise both signal handlers and the stop-token bridge. These changes do not introduce filesystem dependencies into the numerical library.

The install consumer no longer reads the producing CMake cache. A package target installs dependencies, relocates the prefix, audits exported/imported paths, and compiles each public header independently. CI passes the archive to a separate runner without source checkout, including Windows. A separate CPM source consumer verifies embedding. Clean GCC 14 compilation exposed missing braces around GoogleTest macros and a copied structured binding; these were corrected. Repeated CMake 3.28 configuration exposed transient glog package discovery; a private build-tree config adapter and explicit Ceres legacy metadata now handle cached source reuse and case-insensitive lookup without changing upstream files.

Doxygen now includes all authored library/CLI implementation files, private members, and anonymous namespaces. Internal contracts cover chart invariants, AD branches, nested-Jet storage, factor/loss lifetimes, and cancellation. Compiled examples cover the shared delivery API, a frozen independent forecast with prediction checks, and both cancellation outcomes. Console output uses native C++23 `std::print`/`std::println`, included in the toolchain feature probe.

Hosted CI results and Windows execution were pending at the time of this local review. Local platform evidence is recorded in [validation](validation.md), separately from configured CI targets and orbital qualification.

Linux execution exposed a near-convergence failure hidden by the original macOS run: value-only factors used reference propagation while derivative callbacks used Jet primal values. Both callbacks now use reference residuals with AD Jacobians, restoring a consistent objective without changing the fit comparison tolerance. The monitor also retains accepted parameters/cost because Ceres 2.2 restores the initial block after `USER_FAILURE`. A separate nearly equatorial Hessian asymmetry was traced to contraction-dependent cancellation; its scoped tolerance and measured limits are recorded in the curvature guide. ASan subprocess cancellation exposed a gap during JSON parsing; the request parser now accepts a stop token and checks parser events and sample conversion.

## Application logging review

The CLI owns one synchronous spdlog logger, writes diagnostics to stderr, and leaves help and command results on the C++23 printing path. The logger is passed explicitly to command execution without global registration. Logging occurs only in ordinary execution context; signal handlers retain their existing cancellation bridge. The numerical API still returns errors and diagnostics to its caller and introduces no logging configuration or side effects.

The fit-only `--log-level` option validates all arguments before applying the threshold or creating staging. Default `warn` reports rejected delivery, optional TLE failure, cancellation, and errors. Debug diagnostics summarize stages and existing fit metrics; no extra propagation, residual formatting, or iteration callback is added. A cancelled partial fit exits before reading metrics that were never computed. The CPM build pins static spdlog with the standard formatting backend, and the numerical library's installed dependency interface is unchanged. The CLI regression checks stderr filtering, redirected color, error/rejection status, staging cleanup, and byte-identical delivered products with logging enabled.
