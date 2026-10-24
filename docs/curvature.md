<!-- @brief Specify residual Hessians, objective curvature, validation, and measured cost. @author watson -->
# Second-order SGP4 derivatives

`orbitfit::residual_curvature` is an experimental, optional evaluation API. It returns the state residual, its Jacobian, and one Hessian for each residual component. The library computes these derivatives only when the API is called. Ordinary Ceres fitting continues to use first-order AD and its existing LM solver.

The implementation uses nested `ceres::Jet<ceres::Jet<double, 7>, 1>` values through the same scalar-templated chart, initialization, and SGP4 propagation as the first-order path. The inner Jet carries all first derivatives; the outer Jet selects one coordinate direction. Six or seven evaluations assemble all Hessian rows. This bounds working storage: carrying all outer directions simultaneously overflowed a worker thread's stack in the Debug/ASan test. The directional implementation is exercised by that same test without increasing its stack.

A private scalar adapter lifts constants into both Jet levels and supplies value-based branch comparisons and the local `fmod` derivative. All power exponents in the kernel are constant, so it uses Ceres' constant-exponent overload and checks that no exponent derivatives are discarded. It introduces no new dependency, Python runtime, generated source, or separate set of SGP4 derivative equations. The unmodified Vallado reference kernel is unchanged.

## API and coordinates

Include `orbitfit/curvature.hpp` and link `orbitfit::orbitfit`:

```cpp
std::array<double, 7> offset{};
auto curvature = orbitfit::residual_curvature(seed, sample, offset);
if (!curvature) {
  // Handle curvature.error().code and curvature.error().message.
}
```

The offset has **six or seven** finite, dimensionless entries. Six fixes BSTAR at `seed.bstar`; seven includes its sensitivity. All-zero offsets evaluate at the seed. The seed epoch, observation epoch, and prograde/retrograde chart choice remain fixed. Offsets are added to these chart coordinates:

| Index | Coordinate | Increment per unit offset |
|---|---|---|
| 0 | `log(mean_motion_rev_per_day)` | 1e-3 |
| 1 | `e / sqrt(1-e²) * sin(varpi)` | 1e-3 |
| 2 | `e / sqrt(1-e²) * cos(varpi)` | 1e-3 |
| 3 | `tan(plane_angle / 2) * sin(node)` | 1e-3 |
| 4 | `tan(plane_angle / 2) * cos(node)` | 1e-3 |
| 5 | `mean_anomaly + varpi` | 1e-3 |
| 6 | BSTAR, when enabled | 1e-4 |

For a prograde seed, `varpi = argument_of_perigee + node` and `plane_angle = inclination`. For a retrograde seed, `varpi = argument_of_perigee - node` and `plane_angle = pi - inclination`. These are the fitter's existing coordinates, not derivatives directly with respect to printed TLE fields. Hessians depend on the coordinate system; changing coordinates generally requires second derivatives of that transformation as well as a Jacobian.

`ResidualCurvature` uses fixed-capacity standard arrays with explicit active dimensions:

- `residual[row]`: predicted minus supplied TEME state, ordered `x,y,z,vx,vy,vz`, in metres and metres/second.
- `jacobian[row][i]`: first derivative with respect to offset `i`.
- `hessians[row][i][j]`: mixed second derivative with respect to offsets `i,j`.
- `parameter_count` is six or seven; `residual_count` is six, or three when `use_velocity=false`.

Inactive rows and columns are zero. The returned Hessians are not forcibly symmetrized, allowing numerical asymmetry to be inspected. Public headers expose no Ceres or Eigen types. Invalid dimensions, non-finite inputs, unsupported frames, invalid propagated models, and singular chart origins produce `std::unexpected` errors.

## From residual Hessians to objective curvature

This API returns **unwhitened residual derivatives**. It does not apply `Sample::covariance`, time weights, design scales, or a robust loss. For a fixed whitening matrix `W`, transform all three derivative orders:

```text
r = W * raw_residual
J = W * raw_jacobian
S_i = sum_k W[i,k] * raw_hessian_k
```

For the quadratic objective `F = 0.5 * rᵀr`, its exact local Hessian is:

```text
g = Jᵀr
H_GN = JᵀJ
H = H_GN + sum_i r_i * S_i
```

For one robust block `F = 0.5 * rho(s)`, `s = rᵀr`, the gradient and Hessian are:

```text
u = Jᵀr
g = rho'(s) * u
H = rho'(s) * (JᵀJ + sum_i r_i * S_i) + 2 * rho''(s) * u * uᵀ
```

Sum block contributions for an arc. Freezing robust weights would omit the last term. The tests validate these formulas with correlated whitening and both quadratic and Huber objectives against differences of the first-order objective gradient. Huber's second derivative is not defined at its threshold; that boundary is outside the smooth comparison.

Ceres' standard `CostFunction` interface accepts residuals and Jacobians. This API does not inject an exact Hessian into Ceres. An exact Hessian can be indefinite; using it for Newton refinement would require a separately validated safeguarded algorithm. Its inverse is not automatically a calibrated covariance, particularly for approximation weights and temporally correlated ephemerides.

## Validation and benchmark

The curvature tests compare values and Jacobians with the first-order path and check every Hessian column against differences of first-order AD Jacobians. They cover ten public Vallado objects at negative, zero, and positive times; six/seven parameters; nonzero offsets; position-only output; mixed BSTAR terms; symmetry; angular wraps; selected near-singular and branch-adjacent cases; and call-order/thread independence. Taylor tests check the expected cubic remainder of a second-order residual prediction. Objective Hessian-vector tests include robust-loss and whitening derivatives.

Finite-difference validation uses a fourth-order stencil. Eccentricity and plane perturbations are limited by their local chart radii, while BSTAR uses a larger independent step. The comparison uses separate position and velocity absolute floors of 2e-5 and 2e-8, with a 2e-5 relative tolerance in ordinary cases. Selected near-singular cases use 2e-4, or 1e-3 near the eccentricity clamp, where subtraction of cancelling Jacobian terms is poorly conditioned. The tolerances describe these numerical comparisons, not a general error bound on Hessians.

Raw symmetry checks use `||H-Hᵀ|| <= 1e-9 * max(1, ||H||)` in ordinary cases and a 1e-8 multiplier for the two nearly equatorial/retrograde cases at inclination 1e-4 radians from the chart origin. GCC 14 on Linux arm64 measured a maximum 6.05e-9 relative asymmetry in that geometry. A diagnostic build disabling fused multiply-add contraction moved the roundoff between components and reduced the position asymmetry to about 3.3e-12; it did not remove cancellation in velocity components. The production build retains the compiler's ordinary floating-point behavior, the unsymmetrized output, and the separate Jacobian-difference checks. This conditioning limit is distinct from the much smaller asymmetry in the ordinary benchmark scenarios below.

Build the optional native benchmarks and run:

```sh
cmake --preset release -DORBITFIT_BUILD_BENCHMARKS=ON
cmake --build --preset release --parallel 4
build/release/orbitfit_curvature_benchmark
build/release/orbitfit_curvature_benchmark --steps
```

The main benchmark reports first-order and second-order factor latency, public API latency, and exact versus Gauss–Newton objective curvature at perturbed seeds and fitted solutions. It uses the existing noisy 97-sample/48-hour LEO, Molniya, and GEO scenarios with quadratic time-weighted residuals. Timings are medians of five warmed batches of 1,940 evaluations. Factor timings reuse a chart; the public API timing also includes validation and chart construction. The Hessian timings include extraction of every active entry and a checksum of the results.

For diagnostics, `D = diag(1 / sqrt(diag(H_GN)))` normalizes Jacobian column magnitudes. `scaled_correction_ratio` is `||D(H-H_GN)D||_F / ||D H_GN D||_F`. The benchmark also reports minimum eigenvalues of the scaled Gauss–Newton and exact Hessians and relative asymmetry. Eigenvalue analysis averages the exact matrix with its transpose after reporting asymmetry; the API returns the original entries. A small Frobenius ratio does not guarantee that weakly observed directions have small curvature corrections.

`--steps` compares three-point and five-point differences over a range of perturbations for difficult geometries. Some deliberately large perturbations cross branches, so this output is a diagnostic rather than a test that every step should agree. Small steps can magnify roundoff by orders of magnitude.

## Local measurements

Measured 2026-10-24 on Apple M4/macOS arm64 with AppleClang 21, Release, and the pinned dependencies. The [raw CSV](benchmarks/2026-10-24-curvature-macos-arm64.csv) retains three sequential runs, each using the five-batch timing procedure above. No project build ran concurrently; system load was not controlled. Each table entry is the median of its metric across those runs. Timings are per observation, including initialization and propagation, and are not CI thresholds.

At fitted solutions:

| Case | Parameters | First order (µs) | Full second order (µs) | Public API (µs) | Second / first |
|---|---:|---:|---:|---:|---:|
| LEO | 7 | 1.144 | 18.000 | 17.549 | 15.79× |
| Molniya 8195 | 6 | 2.357 | 39.294 | 40.121 | 16.67× |
| GEO 25954 | 6 | 2.039 | 32.860 | 32.318 | 16.06× |

The full Hessian costs approximately 16–17 first-order evaluations here. A single directional evaluation is an available implementation building block, but a dedicated directional public API and geodesic-acceleration algorithm have not been implemented or benchmarked. Public and internal timings are separate measurements; their small differences should not be treated as a reliable overhead estimate.

Curvature diagnostics, using the normalization defined above:

| Case | Correction ratio at seed | Correction ratio after fit | Minimum GN eigenvalue at seed | Minimum exact eigenvalue at seed |
|---|---:|---:|---:|---:|
| LEO | 6.304e-3 | 1.595e-9 | 9.530e-3 | 9.110e-3 |
| Molniya 8195 | 3.668e-4 | 1.365e-8 | 9.489e-2 | 9.489e-2 |
| GEO 25954 | 7.461e-2 | 2.265e-8 | 3.568e-4 | -1.065e-3 |

The GEO seed has a 7.46% normalized curvature correction and an indefinite exact Hessian in this chart, while its Gauss–Newton matrix is positive definite. This illustrates why an exact-Newton extension needs safeguards. The seed is not stationary, so this is not evidence of a saddle point or a different optimum. After fitting, all three corrections are small and the exact Hessians are positive definite. Maximum reported relative asymmetry is 1.59e-13.

These are noisy SGP4-generated arcs; they do not establish behavior for real ephemerides or large model mismatch. The results support keeping full Hessian evaluation optional and investigating directional curvature or TLE candidate ranking before adding it to every solver iteration. They demonstrate derivative evaluation and diagnostics, with no claim of an optimizer speedup or better fitted accuracy.

## Scope

Derivatives follow the executed branches and bounded iterations of the SGP4 implementation. They are local derivatives, not a proof of smoothness across resonance selection, eccentricity clamps, decay boundaries, or iteration-count changes. Exactly circular or equatorial chart origins remain explicit errors. Near-boundary tests do not establish universal validity. Rounded TLE fields and epoch ticks are discrete; their optimization would need a suitable local coordinate model and exact validation of serialized candidates.

The next decisions are whether measured curvature justifies safeguarded refinement, directional second derivatives, or improved ranking of representable TLE candidates. This milestone supplies the evaluator and measurements; those optimization algorithms are not enabled by it.
