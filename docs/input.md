<!-- @brief Specify TEME request schemas, units, identity, and output acceptance. @author watson -->
# Input contract

Generate a complete input with `orbit-fit example --output request.json`. The request schema is `orbit-fit.request.v1`; unknown request fields, duplicate JSON keys, unsupported frames/time systems, nonfinite values, and invalid dates are rejected. Input is limited to 64 MiB, nesting depth 32, and 100,000 samples. Timestamps have microsecond resolution.

```json
{
  "schema": "orbit-fit.request.v1",
  "frame": "TEME",
  "time_system": "UTC",
  "metadata": {"catalog_number": 99999, "object_name": "SYNTHETIC EXAMPLE"},
  "fit": {
    "start": "2026-01-01T00:00:00Z",
    "stop": "2026-01-01T02:00:00Z",
    "epoch": "2026-01-01T00:00:00Z",
    "weighting": "approximation",
    "position_scale_m": 100,
    "velocity_scale_mps": 0.1,
    "maximum_rms_position_m": 10
  },
  "samples": []
}
```

Populate `samples` with at least three strictly increasing epochs spanning the requested window. At least three samples must fall inside it. Each sample has `epoch`, `position_m: [x,y,z]`, and `velocity_mps: [vx,vy,vz]`. The fit epoch must lie within the window. Intermediate state coverage matters: passing the sampled RMS gate does not establish an error bound between samples or beyond the fit window.

Optional metadata: `international_designator` (`YYYY-NNNPPP` or empty), `classification` (`U`, `C`, or `S`), `element_set_number`, and `revolution_number`. Never substitute a hash of a name for a real catalog number.

Optional fit fields and defaults:

| Field | Default | Meaning |
|---|---:|---|
| `fit_bstar` | false | Estimate a seventh parameter; requires sufficient drag information |
| `fixed_bstar` | 0 | Used whenever BSTAR is fixed, including with a supplied seed |
| `use_velocity` | true | Include velocity residuals |
| `weighting` | approximation | Or `independent_measurements` |
| `derivatives` | automatic | Native forward AD; `finite_difference` retains the numerical comparison mode |
| `position_scale_m` | 100 | Approximation design scale |
| `velocity_scale_mps` | 0.1 | Approximation design scale |
| `huber_threshold` | absent | Block robust threshold in scaled residual units |
| `maximum_samples` | 0 | 0 uses all samples; otherwise at least 3 |
| `maximum_iterations` | 80 | Maximum Ceres trial iterations, including rejected steps but excluding initialization; at most 10,000 |
| `relative_cost_tolerance` | 1e-10 | Ceres relative cost-change stopping criterion, including floating-point plateaus |
| `absolute_cost_tolerance` | 1e-12 | Absolute whitened objective stopping criterion |
| `gradient_tolerance` | 1e-7 | Maximum absolute gradient component divided by its whitened Jacobian column norm |
| `maximum_rms_position_m` | 1000 | Acceptance gate over every in-window input sample |

For measurement mode, each sample also needs `covariance`, a flat row-major 6×6 matrix of `[x,y,z,vx,vy,vz]` in SI units. The active block must be symmetric positive definite. Position-only fitting uses its 3×3 position block. Within-epoch correlation is modeled; temporal correlation is not. Decimation intentionally discards measurements from the objective, although they remain in validation metrics.

Optional top-level `seed_omm` is an OMM GP JSON object. It must describe the same catalog object and exactly the requested fit epoch. Changing a seed's epoch label changes the model: the API refuses to do that silently. Without a seed, a two-body Cartesian conversion supplies an initial guess; optimization then estimates SGP4 mean elements. A poor initial guess can still fail to converge.

The `orbit-fit.report.v2` report records fit configuration, solver termination, rank, conditioning, continuous metrics/residuals, and reparsed OMM/TLE metrics. RMS position is `sqrt(mean(dx²+dy²+dz²))`, not a per-axis RMS. These metrics use equally weighted validation samples; the fitting objective may use time weights or covariance. Top-level `accepted`, `reason`, and `product_policy` carry the same decision used by the CLI. Each `serialized_omm`/`serialized_tle` block has availability, physical acceptance, a stable status, nullable metrics, and diagnostic text. The default `tle_required` policy requires both products and continuous acceptance; explicitly choose `omm_sufficient` to permit OMM-based delivery despite TLE failures. See [delivery workflows and the v1-to-v2 change](workflows.md).
