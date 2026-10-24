<!-- @brief Library guide, error semantics, and a compiled C++23 usage example. @author watson -->
# orbit-fit C++23 reference

orbit-fit fits one fixed-epoch SGP4 mean-element model to TEME state samples and exports numeric TLE or GP OMM JSON. Ceres supplies first-order automatic differentiation and Levenberg–Marquardt estimation; the optional curvature evaluator uses nested Jets. The library has no astro-core dependency and exposes no Ceres or Eigen types.

## Start with the contracts

Include `orbitfit/orbitfit.hpp` for time, propagation, fitting, TLE, and physical-error evaluation; `orbitfit/io.hpp` adds JSON requests, OMM, and reports. Include `orbitfit/curvature.hpp` only when second-order residual information is needed. Install and link with `find_package(orbitfit CONFIG REQUIRED)` and `orbitfit::orbitfit`. The target propagates the C++23 requirement.

All positions and velocities use metres and metres per second in **TEME**. Angles in the C++ API use radians, mean motion uses revolutions/day, and BSTAR uses the SGP4 convention of inverse Earth radii. Element epochs are fixed during optimization. They cannot be changed by simply assigning a new timestamp. These are SGP4 mean elements, not osculating Cartesian-to-Keplerian elements.

`Epoch` stores civil UTC at microsecond precision. Calendar parsing accepts years 1957–9999 and requires a trailing `Z`; leap-second labels are rejected. Differences follow SGP4's 86,400-second civil-day convention. The qualification converter separately accounts for TT−UTC for precession/nutation. The fitting API performs no hidden frame conversion, EOP lookup, clock correction, or network access.

Read [the input contract](input.md) before choosing weights. Approximation weights express design scales; they do not imply a measurement covariance. Independent-measurement weights require a valid covariance at each selected observation and assume independence across epochs. A single Huber loss applies to the entire position or state residual block. Final physical errors are always unweighted.

## Fit and validate the delivered product

The [basic example source](../examples/fit.cpp) is compiled as `orbitfit_example` and exercised by CTest. It checks continuous acceptance and both serialized formats through `prepare_products`. Its synthetic observations test the workflow, not independent orbital accuracy. See also the [forecast and cancellation walkthroughs](workflows.md).

\include fit.cpp

For your own data, construct `std::vector<orbitfit::Sample>` or call `read_request`. The samples must be finite, strictly increasing, unique, and cover the inclusive fit window. Both state vectors are required, even for position-only residuals. Set `FitOptions::epoch` inside the window. An optional seed must have exactly that epoch. The coarse automatic seed is useful but not globally reliable.

`fit` returns `Result<FitResult>`. An unexpected error means no usable candidate is returned; inspect `Error::code` and `Error::message`. A successful `Result` can contain an unaccepted candidate. Inspect `FitResult::accepted`, `termination`, `rank`, physical metrics, and diagnostics. Accepted means numerical convergence, full local rank at the documented threshold, and the continuous RMS position gate. It says nothing about unobserved epochs or TLE rounding.

Call `prepare_products(fitted, samples, metadata, options)` with the original inputs. It selects the same full fit window, emits and reparses both formats, and applies their physical gates. Inspect `Delivery::accepted` and `reason`; pass the unmodified delivery to `report_json` for the identical decision. The default `tle_required` policy requires continuous, OMM, and TLE acceptance. Select `ProductPolicy::omm_sufficient` explicitly to make TLE optional. Low-level `to_tle`, `from_tle`, and `evaluate` remain available for other workflows. For prediction, evaluate disjoint held-out observations; a lower training error can accompany worse prediction. The [ISS qualification](../qualification/README.md) demonstrates this with estimated BSTAR.

## Errors, cancellation, and ownership

`Result<T>` is `std::expected<T, Error>`; resource allocation may still throw. `format_epoch` throws `std::invalid_argument` for an invalid directly constructed calendar epoch. Expected input/propagation/serialization failures are returned through `Result`. Human-readable messages are diagnostic text, not a stable machine interface.

Pass a `std::stop_token` through `FitOptions` for cooperative cancellation, or to `read_request(json, token)` to cover parsing and retain it in the returned options. Early cancellation returns an error; cancellation during estimation may return the last candidate with `accepted=false`, `termination=cancelled`, no residuals, and `metrics.sample_count=0`. Rank and conditioning can be incomplete. Cancellation is checked during parsing, validation, factor construction/evaluation, iteration monitoring, and final physical evaluation; it is not a hard real-time deadline and cannot interrupt a single SGP4 call or a linear algebra operation.

Calls are synchronous. Input spans and referenced options must remain valid and unmodified until return. Returned objects own their data. Independent calls can run concurrently; each propagation evaluation owns its deep-space record. Ceres uses one evaluation thread per fit. No mutable propagation cache or global solver state is exposed.

## Numerical and format boundaries

Fitting accepts 3–100000 samples and 1–10000 iterations. JSON input is limited to 64 MiB, depth 32, and 100000 samples. Propagation has a ten-year offset guard. Numeric TLEs support IDs 1–99999 and epoch/launch years 1957–2056; OMM supports larger IDs and later dates. Alpha-5 and complete CCSDS XML/KVN are outside the implemented profile. Unknown request properties and duplicate JSON keys are errors.

SGP4 is piecewise smooth. AD differentiates the executed branch and does not establish a derivative across a true discontinuity. Exactly circular/equatorial chart origins are explicitly unsupported by the derivative path. The [numerical design](design.md) and [curvature guide](curvature.md) specify the normalized chart, scaling, branch behavior, objective Hessian construction, and its limitations. Curvature output is raw residual curvature, not an objective Hessian or statistical covariance.

## Build this reference

Install Doxygen 1.9.8 or newer, then run:

```sh
cmake --preset release -DORBITFIT_BUILD_DOCS=ON
cmake --build --preset release --target docs
```

Open `build/release/docs/html/index.html`. XML is generated alongside HTML. Missing declarations/parameters and Doxygen warnings fail the target across all authored library and CLI sources, including private members and anonymous namespaces. The reference is offline and does not require Graphviz or a Python package. Source file headers identify purpose and author; original upstream notices and data attribution are preserved under the [authorship policy](authorship.md). See [integration](integration.md) and [internal contracts](internals.md).

Maintainers should read the [design review](design-review.md), [validation evidence](validation.md), [dependency provenance](dependencies.md), and [release roadmap](roadmap.md).
