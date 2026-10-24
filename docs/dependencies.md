# Dependency provenance

All authored orbit-fit source is independent of astro-core. Similar directory and build conventions do not introduce a code dependency. No astro-core source, private fixtures, environment model, or deployment scripts are included.

## Build dependencies

`cmake/dependencies.cmake` records archive URLs and SHA-256 pins; `cmake/CPM.cmake` records the CPM bootstrap hash. GTSAM uses its bundled Eigen 3.4 to keep the headers and compiled library consistent. Native CPU tuning, Boost serialization, optional Python/MATLAB modules, TBB, MKL, and nested dissection are disabled in the default build.

| Component | Pin | License |
|---|---|---|
| CPM.cmake | 0.40.2 | MIT |
| GTSAM | 4.3.0, `71a25ca36c084cbad1f872e812d6d97fbadfdb05` | BSD; bundled dependencies have separate notices |
| nlohmann/json | 3.12.0 | MIT |
| GoogleTest | 1.17.0, tests only | BSD |

The `ORBITFIT_FETCH_DEPS=OFF` path discovers preinstalled packages; their provenance and build compatibility belong to the consuming environment. Do not mix Eigen configurations or ABI-incompatible GTSAM binaries. The installed static library still requires GTSAM and JSON CMake packages.

## Vendored SGP4 kernel

Upstream: [CelesTrak/fundamentals-of-astrodynamics](https://github.com/CelesTrak/fundamentals-of-astrodynamics/tree/49df0479c950917c0a2c5ac31f2db6477bba57f6/software/cpp/SGP4/SGP4), commit `49df0479c950917c0a2c5ac31f2db6477bba57f6`. Header version: `SGP4 Version 2025-12-30`.

Only `software/cpp/SGP4/SGP4/SGP4.cpp`, `SGP4.h`, and the upstream root `NOTICE` are vendored. They are byte-for-byte unmodified, including upstream line endings. The root notice explicitly exempts this C++ SGP4 directory from the upstream repository's general AGPL license and grants unrestricted use. Preserve the source attribution and notice; do not assume this exception covers other upstream directories.

| File | SHA-256 |
|---|---|
| SGP4.cpp | `7e984e29a66f75a2b5ad3881f0e5dc0f8fbf9ee8de675ee39be1c9092bc3f183` |
| SGP4.h | `0563e6e8027607452e451d2342da96e5b376a1c119675c40940866f2e04c0bd6` |
| NOTICE | `3b90f5a746be2899b444e3794e74cf13ffe6183ea32f52498ba16522e5ca56bd` |

## Public verification fixtures

`tests/data/SGP4-VER.TLE` and `tcppver.out` are the public Vallado verification fixtures distributed by [python-sgp4](https://github.com/brandon-rhodes/python-sgp4/tree/8126f773923c2902719d45aab8bdb5cce86cf39d/sgp4), commit `8126f773923c2902719d45aab8bdb5cce86cf39d`. Its MIT license is preserved in `tests/data/python-sgp4-LICENSE`. Tests use selected valid near-Earth and deep-space cases, including negative propagation times and resonances. The historical corpus also contains intentionally invalid or pathological cases that are not all part of the current acceptance suite.

| File | SHA-256 |
|---|---|
| SGP4-VER.TLE | `d246d1d9d768ace445a38a965713fa9ba52d80fd8a41a0502ff83d7acffe2881` |
| tcppver.out | `687bf28dbe52df86e8e60ab5cb4a08d1aa3dbcaf4e63b1f7ab95f044fbe3833b` |
| python-sgp4-LICENSE | `4474ea9eccbb829c6bc3652381bfe78b1c815de021d4a284a2a5b383a374d030` |

## Binary distributions

Before publishing a binary bundle, collect all notices for the components actually linked, including GTSAM's enabled third-party components and Eigen. The repository's top-level Apache license does not replace those obligations. This initial project does not generate a public binary release or claim a completed distribution audit.
