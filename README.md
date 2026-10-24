# orbit-fit

**SGP4 orbit fitting with factor graphs.** A standalone C++23 library and CLI using GTSAM, with TLE and OMM JSON output.

Status: experimental first implementation. The repository includes numerical tests and production-oriented build infrastructure; operational qualification is still a separate milestone. It has no dependency on astro-core and contains no code or fixtures copied from it.

## Build and try it

Use CMake 3.28+, Ninja, and a C++23 compiler and standard library with `std::expected`, `std::format`, and exclusive file creation. The presets use the compiler selected by your environment. AppleClang 21 is the initial local validation platform; CI also targets GCC 14 and MSVC.

```sh
cmake --preset release
cmake --build --preset release --parallel 4
ctest --preset release

build/release/orbit-fit example --output request.json
build/release/orbit-fit fit --input request.json --output-dir results
```

The example generates a synthetic two-hour orbit and fits it without a supplied element seed. Use your object's actual catalog identity for real data. Output paths must be new: the CLI protects existing files and publishes each result directory after writing all its files.

Results contain `elements.omm.json`, `elements.tle` when representable, `report.json`, and the original request. Exit code **0** means the continuous solution and available serialized products satisfy the configured acceptance gate; **2** retains a rejected candidate for diagnosis; **1** indicates an input, I/O, or solver error. Read the report to distinguish convergence, iteration limits, stalling, cancellation, and numerical failures.

## What is implemented

- Near-Earth and deep-space propagation through the unmodified upstream Vallado SGP4 kernel, WGS-72, improved operation mode.
- One scaled, nonsingular-style mean-element parameter block, with optional BSTAR estimation; GTSAM unary state factors, Levenberg–Marquardt, and QR linear solves.
- Position-only or position-and-velocity residuals; time-weighted approximation or independent measurements with within-epoch covariance; optional block Huber loss.
- Explicit fit windows, optional decimation, residual evaluation at **every supplied sample inside the window**, and rank/conditioning diagnostics.
- Full-precision OMM GP JSON, strict numeric TLE parsing, checked TLE field limits and checksums, and accuracy evaluation after parsing the exported TLE back, including epoch rounding.
- A C++ API using `std::expected`, `std::span`, UTC calendar types, and cooperative cancellation, with GTSAM and Eigen kept out of public headers.

Input must explicitly declare **TEME**, **UTC**, positions in **metres**, and velocities in **metres per second**. Both state vectors are required, even when velocity residuals are disabled, because automatic initialization uses a Cartesian state. No frame conversion is implicit. UTC uses SGP4's 86,400-second calendar-day convention; leap-second labels are rejected. See [the input contract](docs/input.md) and [numerical design](docs/design.md).

OMM uses the CelesTrak-style GP JSON field vocabulary, with explicit frame/time/theory metadata. It is not a complete CCSDS OMM XML/KVN implementation. Numeric catalog IDs through nine digits are supported in OMM; legacy TLE export supports 1–99999, epochs/launch years 1957–2056, and rejects field overflow. Alpha-5 identifiers are not implemented. Mean-motion derivative fields are not estimated or used by SGP4 and are emitted as zero.

## Repository and dependency conventions

```text
include/orbitfit/    public library API
src/                independently authored implementation
apps/               thin CLI
tests/              numerical, format, and CLI tests; public reference fixtures
third_party/vallado/ pinned, unmodified SGP4 kernel and upstream notice
cmake/              CPM bootstrap, dependency pins, installed package config
docs/               input contract, design, provenance, release roadmap
```

CPM resolves pinned, hash-checked GTSAM 4.3.0, JSON 3.12.0, and GoogleTest 1.17.0 archives. Presets share `.cache/cpm`; override `CPM_SOURCE_CACHE` with a CMake cache variable to share across repositories. Source caching avoids repeat downloads; it is not a binary package cache. The first GTSAM build takes longer than the fitter itself.

```sh
./local_ci.sh release
./local_ci.sh sanitize
# Use preinstalled dependencies without downloading:
cmake -S . -B build/system -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DORBITFIT_FETCH_DEPS=OFF -DCMAKE_PREFIX_PATH=/path/to/dependencies
```

The installed package supports `find_package(orbitfit CONFIG REQUIRED)` and `orbitfit::orbitfit`. GTSAM and JSON remain package dependencies when linking the static library. `local_ci.sh` checks a separate installed-package consumer. See [dependency provenance](docs/dependencies.md) before redistributing binaries.

## Where this becomes interesting

The current graph is intentionally a single orbit variable connected to state samples. The strongest next steps are connected arc graphs with continuity factors, automatic arc selection to a serialized-output accuracy target, and optimization over representable TLE neighbors. Each addresses a problem beyond replacing one least-squares solver with another. See the [development and qualification roadmap](docs/roadmap.md).

Original code: [Apache-2.0](LICENSE). Upstream components and fixtures retain their own notices; see [NOTICE](NOTICE).
