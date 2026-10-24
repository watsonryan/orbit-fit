# Initial local validation

Platform: macOS arm64, AppleClang 21.0.0.21000334, CMake 4.3.2. Dependencies are pinned in `cmake/dependencies.cmake`.

| Check | Result |
|---|---|
| `./local_ci.sh release` | 30 tests passed; installation and separate consumer passed |
| `./local_ci.sh sanitize` | 30 tests passed with ASan/UBSan; installation and separate consumer passed |
| `ORBITFIT_FETCH_DEPS=OFF` | Configured, built, and ran the CLI against installed dependencies |
| clang-format 18 | Authored C++ passes the checked-in formatting rules |
| Dependency provenance | Vendored SGP4 hashes verified; fixtures match the documented immutable upstream revision |
| Authored-file whitespace, shell syntax, preset JSON | Passed |

The sanitizer configuration reused GTSAM and JSON from the locally installed Release build:

```sh
cmake --preset sanitize -DCPM_USE_LOCAL_PACKAGES=ON \
  -DCMAKE_PREFIX_PATH="$PWD/build/release/install"
./local_ci.sh sanitize
```

Instrumentation covers authored code and the vendored SGP4 kernel. GTSAM itself was not sanitizer-instrumented. The ordinary sanitizer preset can build dependencies from their cached sources; full dependency instrumentation remains a separate qualification step.

The suite covers ten selected public Vallado verification objects (near-Earth, deep-space, resonant, and negative-time cases), propagation call-order/thread independence, automatic and seeded fitting, near-circular/equatorial/retrograde charts, BSTAR recovery, Jacobian step/directional checks, fit-window selection, decimation validation, covariance requirements, robust outliers, iteration limits, pre-start cancellation, weak drag observability, format boundaries, duplicate keys, and CLI acceptance/identity handling.

The generated synthetic example converged in three iterations with approximately **1.12e-6 m** continuous position RMS and **9.025 m** exported-TLE position RMS. Its 10 m target passed. A 1 m target correctly rejected the serialized deliverable while retaining the diagnostic candidate. These are self-consistency/serialization results for this example, not a spacecraft accuracy claim.

GitHub Actions is configured for Linux, macOS, and Windows. Those hosted jobs have not yet run. Independent non-SGP4 ephemerides, frame conversion, broadly qualified initialization, complete pathological-orbit coverage, parser fuzzing, and operational qualification remain on the roadmap.
