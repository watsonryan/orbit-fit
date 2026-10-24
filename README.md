<!-- @brief Introduce the standalone fitter, build workflow, and supported behavior. @author watson -->
# orbit-fit

**SGP4 orbit fitting with factor graphs.** A standalone C++23 library and CLI using Ceres Solver, with TLE and OMM JSON output.

Status: engineering alpha with public reference tests, a reproducible NASA ISS forecast comparison against Project Pluto eph2tle, sanitizer-backed robustness checks, and documented qualification limits. Operational qualification remains a separate milestone. It has no dependency on astro-core and contains no code or fixtures copied from it.

## Build and try it

Use CMake 3.28+, Ninja, and a C++23 compiler and standard library with `std::expected`, `std::format`, `std::print`, and exclusive file creation. Configuration checks the required facilities. The presets use the compiler selected by your environment. See the [compiler and integration guide](docs/integration.md) for tested platforms and CI targets.

Open [examples/fit.yaml](examples/fit.yaml) for the job settings and [examples/states.csv](examples/states.csv) for the time, position, and velocity data. The YAML declares the data path, frame, units, object identity, and fit controls. The example contains 61 synthetic TEME states at two-minute intervals over two hours. The [example guide](examples/README.md) explains its fields and provenance.

```sh
cmake --preset release
cmake --build --preset release --parallel 4
ctest --preset release

build/release/orbit-fit fit --config examples/fit.yaml --output-dir results
```

The example fits without a supplied element seed. Data paths resolve relative to the YAML file. Use your object's actual catalog identity for real data. Output paths must be new: the CLI protects existing files and publishes each result directory after writing all its files. To generate a fresh job and CSV in a new directory:

```sh
build/release/orbit-fit example --output-dir example
```

Results contain the serializable `elements.omm.json` and `elements.tle`, `report.json`, the original YAML/CSV, and a self-contained SI `request.json` snapshot for replay. Inline JSON remains supported with `fit --input examples/request.json`; see [the input contract](docs/input.md). Exit code **0** means continuous fit, OMM, and TLE all pass by default; select `--product-policy omm-sufficient` explicitly when an accepted OMM is enough. Exit **2** retains a rejected candidate for diagnosis; **1** indicates input/I/O/solver failure; **130** indicates cooperative SIGINT/SIGTERM cancellation before publication. The v2 report records the shared overall decision and each format's round-trip accuracy. See [workflows and troubleshooting](docs/workflows.md).

Command results and help use C++23 `std::println`/`std::print` on stdout. The CLI uses spdlog for UTC-stamped diagnostics on stderr, with warning/error output by default. Add `--log-level debug` to `fit` for stages and numerical diagnostics. Logging verbosity never changes exit codes or delivered products; the numerical library returns errors and diagnostics to its caller without adding application logging.

## What is implemented

- Near-Earth and deep-space propagation through the unmodified upstream Vallado SGP4 kernel, WGS-72, improved operation mode.
- Native C++ forward-mode automatic derivatives through the mean-element chart, SGP4 initialization, and propagation. A scalar-templated adaptation retains the unmodified kernel as its reference; finite differences remain an explicit comparison mode.
- An experimental [second-order evaluation API](docs/curvature.md) returns residuals, Jacobians, and residual Hessians using nested Ceres Jets. Evaluation is on demand and leaves the fitting algorithm unchanged.
- One scaled, nonsingular-style mean-element parameter block, with optional BSTAR estimation; Ceres observation residual blocks, Levenberg–Marquardt, and dense QR linear solves.
- Position-only or position-and-velocity residuals; time-weighted approximation or independent measurements with within-epoch covariance; optional block Huber loss.
- Explicit fit windows, optional decimation, residual evaluation at **every supplied sample inside the window**, and rank/conditioning diagnostics.
- Full-precision OMM GP JSON, strict numeric TLE parsing, checked TLE field limits and checksums, and accuracy evaluation after parsing the exported TLE back, including epoch rounding.
- A C++ API using `std::expected`, `std::span`, UTC calendar types, and cooperative cancellation, with Ceres and Eigen kept out of public headers.

Input must explicitly declare **TEME** and **UTC**. YAML jobs declare position units (`m` or `km`) and velocity units (`m/s` or `km/s`); the loader converts to the library's SI units. Inline JSON uses `position_m` and `velocity_mps`. Both state vectors are required, even when velocity residuals are disabled, because automatic initialization uses a Cartesian state. No frame conversion is implicit. UTC uses SGP4's 86,400-second calendar-day convention; leap-second labels are rejected. See [the input contract](docs/input.md) and [numerical design](docs/design.md).

OMM uses the CelesTrak-style GP JSON field vocabulary, with explicit frame/time/theory metadata. It is not a complete CCSDS OMM XML/KVN implementation. Numeric catalog IDs through nine digits are supported in OMM; legacy TLE export supports 1–99999, epochs/launch years 1957–2056, and rejects field overflow. Alpha-5 identifiers are not implemented. Mean-motion derivative fields are not estimated or used by SGP4 and are emitted as zero.

## Repository and dependency conventions

```text
include/orbitfit/    public library API
src/                independently authored implementation
apps/               thin CLI
tests/              numerical, format, and CLI tests; public reference fixtures
third_party/vallado/ pinned, unmodified SGP4 kernel and upstream notice
third_party/vallado_ad/ scalar-templated C++ adaptation for derivatives
benchmarks/         native C++ derivative and fitting comparisons
cmake/              CPM bootstrap, dependency pins, installed package config
docs/               input contract, design, provenance, release roadmap
```

CPM resolves pinned, hash-checked Ceres 2.2.0, Eigen 3.4.0, glog 0.7.1, and JSON 3.12.0 archives, plus spdlog 1.17.0 and yaml-cpp 0.9.0 for the CLI and GoogleTest 1.17.0 for tests. Ceres supplies both automatic differentiation and optimization. The default build uses Eigen dense QR; optional sparse solvers, BLAS/LAPACK, and GPU dependencies are disabled. All authored implementation, tests, and benchmarks are native C++23. Presets share `.cache/cpm`; override `CPM_SOURCE_CACHE` with a CMake cache variable to share across repositories. Source caching avoids repeat downloads; it is not a binary package cache.

```sh
./local_ci.sh release
./local_ci.sh sanitize
# Use preinstalled dependencies without downloading:
cmake -S . -B build/system -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DORBITFIT_FETCH_DEPS=OFF -DCMAKE_PREFIX_PATH=/path/to/dependencies
```

The installed package supports `find_package(orbitfit CONFIG REQUIRED)` and `orbitfit::orbitfit`. Ceres, Eigen, and JSON remain package dependencies when linking the static library. `local_ci.sh` relocates the installation and checks its dependency paths, standalone consumer, and each public header. CI also consumes the archive on a separate runner without a source checkout. See [CPM and installed-package integration](docs/integration.md) and [dependency provenance](docs/dependencies.md).

The optional C++ benchmarks compare residual/Jacobian evaluations and complete fits using both derivative methods, and measure second-order evaluation cost and objective curvature. Their `--steps` modes print finite-difference step sweeps for difficult geometries:

```sh
cmake --preset release -DORBITFIT_BUILD_BENCHMARKS=ON
cmake --build --preset release --parallel 4
build/release/orbitfit_benchmark
build/release/orbitfit_benchmark --steps
build/release/orbitfit_curvature_benchmark
build/release/orbitfit_curvature_benchmark --steps
```

See [validation results and limits](docs/validation.md). Exactly circular or equatorial chart origins currently produce an explicit AD error; the existing near-singular tests do not establish universal robustness.

## Reference and qualification

The [C++ API guide](docs/api.md), [real-data workflows](docs/workflows.md), and [internal contracts](docs/internals.md) cover units, ownership, cancellation, output policy, and diagnosis. Synthetic, independent-forecast, and cancellation examples compile and run under CTest. Build the HTML/XML reference with Doxygen 1.9.8 or newer:

```sh
cmake --preset release -DORBITFIT_BUILD_DOCS=ON
cmake --build --preset release --target docs
```

Open `build/release/docs/html/index.html`. Documentation warnings fail the target for public API and authored library/CLI internals, including private and anonymous-namespace members. Authored files carry watson's author and purpose headers; [upstream/data exceptions](docs/authorship.md) preserve original attribution and hashes.

The [independent qualification report](qualification/README.md) compares 6–48 hour ISS fits and exported TLE predictions against a pinned external fitter. The ordinary test suite uses frozen fixtures offline. Rebuilding the optional converter uses CPM-pinned ERFA; the separate reference script reproduces Project Pluto outputs. [Parser fuzzing](fuzz/README.md) and the [design review](docs/design-review.md) document additional robustness work and implementation decisions.

## Where this becomes interesting

The current graph is intentionally a single orbit variable connected to state samples. The strongest next steps are connected arc graphs with continuity factors, automatic arc selection to a serialized-output accuracy target, and optimization over representable TLE neighbors. Each addresses a problem beyond replacing one least-squares solver with another. See the [development and qualification roadmap](docs/roadmap.md).

Original code: [Apache-2.0](LICENSE). Upstream components and fixtures retain their own notices; see [NOTICE](NOTICE).
