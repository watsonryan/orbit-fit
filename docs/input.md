<!-- @brief Specify TEME request schemas, units, identity, and output acceptance. @author watson -->
# Input contract

Start with [examples/fit.yaml](../examples/fit.yaml) and its separate [states.csv](../examples/states.csv), containing 61 synthetic states over two hours. The [example guide](../examples/README.md) explains their provenance. Generate a fresh pair with `orbit-fit example --output-dir example`.

## YAML configuration and CSV ephemeris

Run `orbit-fit fit --config fit.yaml --output-dir results`. The YAML schema is `orbit-fit.job.v1`:

```yaml
schema: orbit-fit.job.v1
input:
  path: data/states.csv
  format: csv
  frame: TEME
  time_system: UTC
  position_unit: km
  velocity_unit: km/s
metadata:
  catalog_number: 99999
  object_name: SYNTHETIC EXAMPLE
fit:
  start: 2026-01-01T00:00:00Z
  stop: 2026-01-01T02:00:00Z
  epoch: 2026-01-01T00:00:00Z
  maximum_rms_position_m: 10
# Optional seed at exactly fit.epoch, with matching catalog identity:
# seed_omm: seeds/initial.omm.json
```

`schema`, `input`, `metadata`, and `fit` are required. All six `input` fields shown above are required. Paths are literal UTF-8 strings: relative paths resolve against the directory containing the YAML file, absolute paths are allowed, and environment variables/`~` are not expanded. `seed_omm` is an optional path to an OMM GP JSON file. Input files must be regular files. Output location and product/logging policies remain CLI arguments.

The CSV has exactly these seven columns, in order:

```csv
epoch,x,y,z,vx,vy,vz
2026-01-01T00:00:00Z,-4456.769456228622,-4347.297273369452,3247.26730176436,1.8823924056269718,-5.535357951582687,-4.741714473987683
```

This single row illustrates the kilometre units declared above; a valid file needs at least three strictly increasing epochs, with at least three inside the fit window. Position units are `m` or `km`; velocity units are `m/s` or `km/s`. They are converted to SI before fitting. Fit scales and acceptance thresholds always use the units in their field names, regardless of CSV units. Both state vectors are required even for position-only residuals. Only TEME and UTC are supported; no frame/time conversion is implicit, and leap-second labels are rejected.

CSV numbers are finite decimal/scientific scalars with optional signs. Empty cells, nonfinite values, trailing text, repeated/out-of-order epochs, extra columns, and conversion overflow are errors. Leading/trailing spaces or tabs, CRLF, a leading UTF-8 BOM, blank lines, and full-line `#` comments are accepted. Fields may have one pair of surrounding double quotes; embedded quotes, commas, and multiline fields are unsupported. Row errors identify the file and physical line, with a column name for invalid values. Limits are 64 MiB per CSV, 4,096 bytes per physical line, and 100,000 states.

YAML configuration and seed files are limited to 256 KiB each. Jobs accept one mapping document with scalar values and nested mappings: aliases, anchors, type/custom tags, sequences, merge keys, duplicate keys, unknown fields, and null values are rejected. Non-specific YAML tags (`!`/`?`) do not override field types. The parser limits mapping depth to eight, document/map-start/scalar events to 512, and scalars to 4,096 bytes. Field types are explicit: booleans use `true`/`false`, integers use unsigned decimal, and dates/paths/names retain their scalar spelling. The numerical reader validates the same metadata and fit options used by JSON below.

The seven-column CSV carries no covariance; YAML jobs use `approximation` weighting. For `independent_measurements`, supply per-sample covariance using the inline JSON request or C++ API. The `seed_omm` file must have the same catalog identity and exactly the fit epoch; the reader never relabels an orbit's epoch.

A published result preserves the original bytes as `input.yaml` and `states.csv`. Its `request.json` contains the resolved settings, SI states, and any seed object, and can be replayed with `--input` without either source file. Archived YAML retains its original paths, so use the resolved JSON for portable replay. For inline JSON input, `request.json` is an unchanged copy of that input. These snapshots describe the bytes read; the CLI does not lock externally edited source files into a multi-file transaction.

## Compatible inline JSON request

Use `orbit-fit fit --input request.json --output-dir results`. [examples/request.json](../examples/request.json) contains the complete synthetic example; `orbit-fit example --output request.json` generates another. Exactly one of `--config` and `--input` is required.

The request schema is `orbit-fit.request.v1`; unknown request fields, duplicate JSON keys, unsupported frames/time systems, nonfinite values, and invalid dates are rejected. Input is limited to 64 MiB, nesting depth 32, and 100,000 samples. Timestamps have microsecond resolution. Top-level fields are:

| Field | Required content |
|---|---|
| `schema` | `"orbit-fit.request.v1"` |
| `frame` | `"TEME"` |
| `time_system` | `"UTC"` |
| `metadata` | `catalog_number`, with optional identity fields below |
| `fit` | UTC `start`, `stop`, and `epoch`, with optional controls below |
| `samples` | At least three strictly increasing epochs with both position and velocity vectors |

The first entry in the example's `samples` array is:

```json
{
  "epoch": "2026-01-01T00:00:00.000000Z",
  "position_m": [-4456769.456228622, -4347297.273369452, 3247267.30176436],
  "velocity_mps": [1882.3924056269718, -5535.357951582687, -4741.714473987683]
}
```

Populate `samples` with at least three strictly increasing epochs spanning the requested window. At least three samples must fall inside it. Each sample has `epoch`, `position_m: [x,y,z]`, and `velocity_mps: [vx,vy,vz]`. The fit epoch must lie within the window. Intermediate state coverage matters: passing the sampled RMS gate does not establish an error bound between samples or beyond the fit window.

## Shared metadata and fitting options

YAML `metadata`/`fit` and JSON `metadata`/`fit` accept the same fields and defaults, except covariance weighting requires inline JSON. Optional metadata: `object_name` (up to 128 bytes, default empty), `international_designator` (`YYYY-NNNPPP` or empty), `classification` (`U`, `C`, or `S`), `element_set_number`, and `revolution_number`. Never substitute a hash of a name for a real catalog number.

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

In inline JSON, optional top-level `seed_omm` is an OMM GP JSON object; in YAML it is a file path. It must describe the same catalog object and exactly the requested fit epoch. Changing a seed's epoch label changes the model: the API refuses to do that silently. Without a seed, a two-body Cartesian conversion supplies an initial guess; optimization then estimates SGP4 mean elements. A poor initial guess can still fail to converge.

## Delivery report

The `orbit-fit.report.v2` report records fit configuration, solver termination, rank, conditioning, continuous metrics/residuals, and reparsed OMM/TLE metrics. RMS position is `sqrt(mean(dx²+dy²+dz²))`, not a per-axis RMS. These metrics use equally weighted validation samples; the fitting objective may use time weights or covariance. Top-level `accepted`, `reason`, and `product_policy` carry the same decision used by the CLI. Each `serialized_omm`/`serialized_tle` block has availability, physical acceptance, a stable status, nullable metrics, and diagnostic text. The default `tle_required` policy requires both products and continuous acceptance; explicitly choose `omm_sufficient` to permit OMM-based delivery despite TLE failures. See [delivery workflows and the v1-to-v2 change](workflows.md).
