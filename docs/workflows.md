<!-- @brief Walk through real-data fitting, delivery decisions, prediction checks, and troubleshooting. @author watson -->
# Fitting workflows and troubleshooting

## From a real TEME request to a validated deliverable

1. Convert your source states to **TEME**, metres, metres per second, and supported civil-UTC timestamps before fitting. Record that conversion and the source's provenance. The library does not infer frames or perform a general frame conversion.
2. Follow the [request contract](input.md), supplying real catalog identity, strictly increasing epochs, both state vectors, and an explicit fit window. Keep independent prediction epochs outside the fitting sample span when testing extrapolation.
3. Fit and inspect the report:

```sh
build/release/orbit-fit fit --input your-teme-request.json --output-dir first-fit
```

The default `tle-required` policy requires numerical/continuous acceptance and passing round-trip physical gates for both OMM and TLE. The shared library function `prepare_products` produces the exact strings written to disk, reparses each, and evaluates every supplied sample inside the inclusive fit window, including samples omitted by solver decimation.

`report.json` uses `orbit-fit.report.v2`. Its top-level `accepted`, `reason`, and `product_policy` describe the delivery decision. `continuous_accepted` describes optimization/continuous accuracy. `serialized_omm` and `serialized_tle` each contain `available`, `accepted`, `status`, `metrics`, and `error`. A product's `accepted` field describes its physical gate only: an accurate product cannot override a rejected continuous fit. `available` means text was emitted, even if its evaluation failed. Missing metrics are `null`, not fabricated zeros.

| Delivery reason | Meaning |
|---|---|
| `accepted` | Continuous fit and every required product passed |
| `continuous_rejected` | Fit acceptance failed; inspect termination, rank, RMS, and diagnostics |
| `omm_rejected` | OMM serialization, parsing, evaluation, or RMS gate failed |
| `tle_unavailable` | Required TLE could not be serialized, such as an out-of-range catalog number |
| `tle_rejected` | Required TLE could be serialized but failed parsing, evaluation, or its RMS gate |

Reasons use that precedence. Per-format statuses are `accepted`, `accuracy_rejected`, `serialization_failed`, `parse_failed`, or `evaluation_failed`. Treat messages as human diagnostics, not machine-readable reason codes.

If your downstream system accepts OMM and does not require a passing TLE, select that policy explicitly:

```sh
build/release/orbit-fit fit --input your-teme-request.json --output-dir omm-fit \
  --product-policy omm-sufficient
```

Both formats are attempted. With `omm-sufficient`, a missing or inaccurate optional TLE remains visible in the report while continuous+OMM success can accept delivery. Only distribute optional products whose own status satisfies your needs. Identity is never changed to force a catalog number into a TLE field.

Exit **0** means the shared delivery decision is accepted. Exit **2** publishes the rejected candidate, report, original request, and any serializable products for diagnosis. Exit **1** means input/I/O/solver failure. Exit **130** means cooperative SIGINT/SIGTERM cancellation before publication. A signal after the final directory rename may arrive after the result has already been committed.

This changes the earlier development interface: report v1 had no overall decision or OMM validation record, and missing TLEs could implicitly allow OMM-only success. Code should now call `prepare_products`, pass its `Delivery` to `report_json`, and select `omm_sufficient` explicitly when wanted. The request format remains `orbit-fit.request.v1`.

## CLI diagnostics and log levels

The CLI owns a synchronous spdlog logger on stderr. Records contain UTC timestamps, the `orbit-fit` name, and severity. Color is automatic on supported terminals and absent when stderr is redirected. The default threshold is `warn`: accepted runs normally leave stderr empty; rejected deliveries and cancellation produce warnings; command failures produce errors. An accepted OMM-only delivery warns if its optional TLE did not pass.

The `fit` command accepts `--log-level trace|debug|info|warn|error|critical|off`. `info` adds reading, fitting, validation, and publication stages. `debug` adds input counts, fit controls, termination/rank/RMS, returned solver diagnostics, and product statuses. `trace` additionally identifies the staging directory. The logger does not dump raw requests or emit per-propagation traces. The setting controls application diagnostics; Ceres' separate glog configuration is unchanged. Argument errors are reported at the default threshold because configuration has not yet succeeded.

```sh
build/release/orbit-fit fit --input your-teme-request.json --output-dir debug-fit \
  --log-level debug 2>fit.log
```

Help and command results stay on stdout, independently of log level. Even `off` preserves result output, exit codes, report contents, and publication/cancellation rules. Log text is for human diagnosis; use the report schema and exit status for automation. There is no automatic file sink, rotation, or asynchronous logging queue in the CLI; redirect stderr or let your process supervisor collect it. The numerical library continues to return `std::expected` errors and owned diagnostics for callers to handle with their own logger.

## Compiled examples and a frozen independent forecast

The [basic example](../examples/fit.cpp) generates synthetic observations, fits them, prepares products, and prints the shared report. It tests integration; its model-generated observations are not independent accuracy evidence.

The [forecast example](../examples/forecast.cpp) reads the frozen NASA ISS forecast already converted to TEME under the [qualified fixture convention](../tests/data/independent/README.md). It fits the first day, reparses the delivered TLE, and evaluates the remaining disjoint epochs:

```sh
cmake --build --preset release --target orbitfit_forecast_example
build/release/orbitfit_forecast_example tests/data/independent/iss-teme.csv
```

The CSV columns are `epoch,x_m,y_m,z_m,vx_mps,vy_mps,vz_mps`; the example uses ISS identity explicitly. Change the identity when adapting it to another object. This bounded example reader is not a general OEM/CSV import facility. Both examples and the [cancellation example](../examples/cancellation.cpp) are built and exercised by CTest.

Prediction errors are separate from delivery acceptance. Report RMS **and maximum** position error on disjoint epochs and compare them with your application's prediction budget. A lower training RMS does not establish better extrapolation: the [ISS campaign](../qualification/README.md) demonstrates substantial short-arc BSTAR overfitting. The fixture is an independent forecast, not observed tracking truth.

For per-epoch diagnosis, inspect `residuals` in the report: predicted-minus-supplied TEME Cartesian position and velocity errors with timestamps. These are unweighted SI errors. Compare their time trend with the fit window, cadence, and any suspected maneuver. The application does not automatically infer maneuvers or provide RTN residuals.

## Choose weights and BSTAR deliberately

Use `weighting: "approximation"` to approximate an ephemeris. Position and velocity scales specify the relative design objective; they do not assert measurement uncertainty. Time-cell weights reduce sensitivity to irregular sampling. Keep the unweighted physical acceptance budget separate from these optimization scales.

Use `independent_measurements` only with meaningful supplied covariance for each selected observation. The active covariance block must be finite, symmetric, and positive definite. Position-only fits use the top-left 3x3 block of the row-major 6x6 storage. Cross-epoch correlation is not modeled. Do not interpret a forecast's arbitrary weights or an inverse numerical Hessian as calibrated parameter covariance.

Start with fixed BSTAR for short arcs or weak drag information. Fit BSTAR when the arc contains useful sensitivity and held-out validation supports the added parameter. Inspect rank and conditioning and compare fixed/estimated cases on the same holdout. A full-rank numerical Jacobian is necessary for acceptance, but does not guarantee statistically reliable BSTAR.

## Diagnose common failures

| Symptom | Action |
|---|---|
| Invalid/unbound initial state or invalid initial model | Check frame, units, velocity, epoch, coverage, and orbit validity. Try a physically plausible SGP4 mean-element seed **at the requested element epoch**; do not simply relabel another epoch |
| Exactly circular/equatorial derivative origin | The current chart's AD domain excludes the singular origin. Use a physically justified nonsingular seed if available; finite differences are an explicit diagnostic mode, not an automatic repair |
| Rank deficiency or high condition number | Inspect arc length/cadence, observability, scales, and extra parameters. Try fixed BSTAR and compare validation accuracy; relaxing the rank threshold is not a supported option |
| `iteration_limit`, `stalled`, or `numerical_failure` | Inspect the seed and residuals first. A larger iteration budget helps only a progressing solve; convergence alone does not pass physical gates |
| Continuous/OMM pass, TLE fails | Inspect reparsed TLE metrics and field limits. Quantization can consume a tight error budget. Use OMM explicitly if acceptable or revisit the required deliverable/budget |
| Strong training fit, poor prediction | Shorten the claimed prediction horizon, compare fixed BSTAR, and investigate maneuvers/model mismatch using held-out data |
| Publication fails | Choose a new output directory, check parent permissions/filesystem support, and inspect stale staging left by hard termination. Never overwrite an existing run implicitly |

## Cancellation and output lifecycle

Pass a `std::stop_token` in `FitOptions`, or to `read_request(json, token)` to cover parsing and retain the token in the parsed options. An early stop returns `ErrorCode::cancelled`; an active stop can retain a candidate with `termination=cancelled`, `accepted=false`, and incomplete diagnostics. `prepare_products` returns cancellation instead of turning that candidate into a delivery. Do not evaluate missing cancellation metrics as zeros.

The CLI's signal handler only sets a lock-free atomic flag. A normal monitor thread requests stop; main-thread checkpoints also transfer pending signals before publication. Reads and writes use bounded chunks. Cancellation remains cooperative and cannot interrupt a single blocking I/O, SGP4, or linear-algebra operation.

Each fit writes into its own unique sibling staging directory, closes and checks every file, then publishes using one no-replace directory rename. Failure/cancellation cleans only the owned unpublished directory. Existing output, empty directories, and another process's staging are preserved. A hard kill/power loss can leave staging for manual inspection; atomic publication does not promise filesystem durability after a power loss.
