<!-- @brief Record dependency revisions, hashes, licensing, and adaptation provenance. @author watson -->
# Dependency provenance

All authored orbit-fit source is independent of astro-core. Similar directory and build conventions do not introduce a code dependency. No astro-core source, private fixtures, environment model, or deployment scripts are included.

## Build dependencies

`cmake/dependencies.cmake` records archive URLs and SHA-256 pins; `cmake/CPM.cmake` records the CPM bootstrap hash. Ceres and orbit-fit share the same Eigen target. The default Ceres build is static and disables SuiteSparse, Eigen sparse solvers, METIS, Accelerate sparse solvers, LAPACK, CUDA, gflags, examples, tests, documentation, and Schur specializations. The fitted parameter block uses dense QR. A pinned glog build supplies logging, with its optional gflags, GoogleTest, and external unwind dependencies disabled. No GTSAM package is required.

| Component | Pin | License |
|---|---|---|
| CPM.cmake | 0.40.2 | MIT |
| Ceres Solver | 2.2.0 | BSD-3-Clause, notice in `third_party/ceres-LICENSE` |
| Eigen | 3.4.0 | MPL-2.0 and upstream notices |
| glog | 0.7.1 | BSD-3-Clause, notice in `third_party/glog-LICENSE` |
| spdlog | 1.17.0, CLI only | MIT, notice in `third_party/spdlog-LICENSE` |
| nlohmann/json | 3.12.0 | MIT |
| GoogleTest | 1.17.0, tests only | BSD |

The `ORBITFIT_FETCH_DEPS=OFF` path discovers preinstalled Ceres, Eigen, JSON, spdlog when the CLI is enabled, and GoogleTest when tests are enabled; Ceres discovers its own transitive dependencies, including glog. Their provenance and build compatibility belong to the consuming environment. Use matching Eigen headers and Ceres binaries. The installed static library finds Ceres, Eigen, and JSON through its package configuration; public orbit-fit headers expose none of their types. Ceres, glog, and Eigen MPL-2.0 license texts are installed alongside orbit-fit's notices.

The CLI links spdlog privately for application diagnostics. The pinned archive SHA-256 is `d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744`. The CPM build uses a static spdlog with `SPDLOG_USE_STD_FORMAT=ON`, without bundled/external fmt, and disables its tests, examples, benchmarks, and package installation. Only its license is installed beside the executable. Library-only builds do not resolve spdlog, and the installed numerical library does not require it. Preinstalled spdlog packages retain their own formatting backend and runtime-link requirements through their imported target. Ceres' glog dependency remains internal to Ceres; application logs do not redirect it or alter its global configuration.

First-order and second-order derivatives use the same pinned Ceres Jet headers. The second-order evaluator nests those Jets behind a private scalar adapter; it adds no dependency or generated-code step.

## Vendored SGP4 kernel

Upstream: [CelesTrak/fundamentals-of-astrodynamics](https://github.com/CelesTrak/fundamentals-of-astrodynamics/tree/49df0479c950917c0a2c5ac31f2db6477bba57f6/software/cpp/SGP4/SGP4), commit `49df0479c950917c0a2c5ac31f2db6477bba57f6`. Header version: `SGP4 Version 2025-12-30`.

Only `software/cpp/SGP4/SGP4/SGP4.cpp`, `SGP4.h`, and the upstream root `NOTICE` are vendored. They are byte-for-byte unmodified, including upstream line endings. The root notice explicitly exempts this C++ SGP4 directory from the upstream repository's general AGPL license and grants unrestricted use. Preserve the source attribution and notice; do not assume this exception covers other upstream directories.

| File | SHA-256 |
|---|---|
| SGP4.cpp | `7e984e29a66f75a2b5ad3881f0e5dc0f8fbf9ee8de675ee39be1c9092bc3f183` |
| SGP4.h | `0563e6e8027607452e451d2342da96e5b376a1c119675c40940866f2e04c0bd6` |
| NOTICE | `3b90f5a746be2899b444e3794e74cf13ffe6183ea32f52498ba16522e5ca56bd` |

`third_party/vallado_ad/SGP4.hpp` is a separately maintained C++ adaptation of the same pinned numerical routines and record. It retains the upstream attribution and unrestricted-use notice. Changes introduce a scalar template, explicit scalar constants, AD-compatible math dispatch, and a local-branch derivative for `fmod`; parsing, unused utility functions, and mutable global debug state are omitted. Initialization and near-Earth/deep-space branch logic are retained. The original `third_party/vallado` files remain byte-for-byte unchanged. The adaptation is checked in as C++ source and requires no generation step.

No dSGP4 code is included. Its Python/PyTorch implementation was reviewed as a reference; orbit-fit's AD path is derived from the explicitly permitted Vallado C++ source above.

## Public verification fixtures

`tests/data/SGP4-VER.TLE` and `tcppver.out` are the public Vallado verification fixtures distributed by [python-sgp4](https://github.com/brandon-rhodes/python-sgp4/tree/8126f773923c2902719d45aab8bdb5cce86cf39d/sgp4), commit `8126f773923c2902719d45aab8bdb5cce86cf39d`. Its MIT license is preserved in `tests/data/python-sgp4-LICENSE`. Tests use selected valid near-Earth and deep-space cases, including negative propagation times and resonances. The historical corpus also contains intentionally invalid or pathological cases that are not all part of the current acceptance suite.

| File | SHA-256 |
|---|---|
| SGP4-VER.TLE | `d246d1d9d768ace445a38a965713fa9ba52d80fd8a41a0502ff83d7acffe2881` |
| tcppver.out | `687bf28dbe52df86e8e60ab5cb4a08d1aa3dbcaf4e63b1f7ab95f044fbe3833b` |
| python-sgp4-LICENSE | `4474ea9eccbb829c6bc3652381bfe78b1c815de021d4a284a2a5b383a374d030` |

## Qualification and documentation tools

Optional qualification adds ERFA 2.0.1 (BSD), downloaded only when `ORBITFIT_BUILD_QUALIFICATION=ON`: archive SHA256 `d5469fbd0b212b3c7270c1da15c9bd82f37da9218fc89627f98283d27b416cbf`. Only the fixture converter links its unmodified precession/nutation routines; the installed orbit-fit library does not. With `ORBITFIT_FETCH_DEPS=OFF`, qualification instead requires preinstalled `erfa.h` and `liberfa`; no CPM fetch is attempted. Validate the version and conventions of an externally supplied ERFA before regenerating fixtures.

Doxygen 1.9.8+ is a documentation-only tool. Clang/libFuzzer is optional for parser campaigns. Neither is an installed library dependency. The local Doxygen HTML/XML check used 1.18.0.

The [ISS qualification report](../qualification/README.md) records NASA/IANA source hashes, the derived TEME fixture, a published Vallado frame example consulted through Orekit, and the pinned Project Pluto comparator commits. Project Pluto's GPL reference code is downloaded/built separately under ignored build directories, never vendored or linked into orbit-fit. This report distinguishes independent estimation, shared propagation scoring, and forecast accuracy from tracking truth.

## Binary distributions

Before publishing a binary bundle, collect all notices for the components actually linked, including Ceres' bundled components and Eigen. The repository's top-level Apache license does not replace those obligations. This initial project does not generate a public binary release or claim a completed distribution audit.
