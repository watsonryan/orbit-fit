<!-- @brief Record measured verification results and the limits of the evidence. @author watson -->
# Local validation

Updated 2026-10-24 after the delivery, package isolation, CLI, C++23 printing, spdlog, YAML/CSV input, and documentation passes. Local platforms: macOS arm64 / Apple M4 / AppleClang 21.0.0.21000334 / CMake 4.3.2, and an Ubuntu 24.04 arm64 container / GCC 14.2.0 / CMake 3.28.3. Dependencies are pinned in `cmake/dependencies.cmake`. Earlier delivery/logging checks predate the first GitHub push; the YAML/CSV follow-up ran afterward. This record reports local evidence and does not establish hosted GitHub or Windows results.

| Check | Result |
|---|---|
| macOS Release configure/build/CTest/package-check with benchmarks and qualification enabled | 139 checks passed after adding YAML/CSV input; relocated installation, all five independent header compilations, and separate consumer passed |
| macOS sanitizer configuration below | 139 checks passed with ASan/UBSan; relocated installation and sanitized consumer passed |
| Ubuntu 24.04 arm64 Release, ordinary pinned CPM dependencies | 138 checks passed; relocation/header/consumer checks passed; optional ERFA qualification executable not enabled |
| Separate Ubuntu container, only installed archive and consumer source | Configured with imported-path auditing, compiled all five headers, linked/ran the consumer, and generated/fitted YAML/CSV with the installed CLI; producing source/build/cache absent |
| CPM source consumer | Configured, built, and ran on macOS and Linux using the local source declaration; macOS rechecked after YAML/CSV input, with neither spdlog nor yaml-cpp in its dependency cache metadata |
| `ORBITFIT_FETCH_DEPS=OFF` (earlier qualification pass) | Installed-dependency CLI baseline and separate qualification/frame check using explicitly supplied ERFA |
| clang-format 18 | Authored C++ passes the checked-in formatting rules |
| Dependency provenance | Unmodified SGP4 and public fixture hashes match the documented immutable upstream revisions |
| Authored-file whitespace, shell syntax, preset JSON | Passed |
| Doxygen HTML/XML | Current documentation passes Linux 1.9.8 with zero warnings and offline link checks; macOS 1.18.0 has intermittent scanner crashes after an earlier successful run; all three examples compiled/tested |
| Independent forecast comparison | Eight NASA ISS cases against pinned Project Pluto eph2tle; continuous, OMM/TLE, and disjoint held-out checks passed |
| Fixture/frame integrity | 17 source/fixture hashes verified; published frame example within 0.15 m and 0.0001 m/s |
| Native parser fuzzing after cancellation API change | 98645 inputs in 61 s with ASan/UBSan; no crash, sanitizer finding, or round-trip violation; 433 MiB reported peak RSS |
| Purpose/author headers | Authored text and preset metadata checked; upstream/data exceptions recorded |

The final sanitizer run used bundled CPM dependencies, with qualification/benchmarks already enabled in the preset's local cache:

```sh
./local_ci.sh sanitize -DCPM_USE_LOCAL_PACKAGES=OFF -DCMAKE_PREFIX_PATH= \
  -DORBITFIT_BUILD_BENCHMARKS=ON -DORBITFIT_BUILD_QUALIFICATION=ON
```

Instrumentation covers authored code, the templated AD kernel, and the unmodified SGP4 kernel. Ceres, glog, compiled spdlog/yaml-cpp, and the optional ERFA C routines themselves were not sanitizer-instrumented. The ordinary sanitizer preset can build dependencies from cached sources, but `ORBITFIT_SANITIZERS` instruments project targets; full dependency instrumentation remains a separate qualification step. The Release installation was tested in a clean prefix with no GTSAM package present.

The logging regression checks all seven severity thresholds, UTC stderr records, absence of terminal color on redirected output, unchanged error/rejection exit codes, invalid-option handling before staging, and cleanup after failure. Successful default and debug runs produce byte-identical request, report, OMM, and TLE files. The relocated package contains spdlog's notice, but no spdlog headers, library, or CMake package; the executable links it statically. The separate Linux container ran that installed executable with debug logging and built the numerical consumer using only the new prefix. The existing real-signal cancellation checks also pass with logging enabled. Logs for this follow-up are retained locally under `build/qualification/logging-*`.

The YAML/CSV follow-up adds twelve input tests and one CLI integration check. They cover path resolution from an unrelated working directory, explicit kilometre/SI conversion, original-byte retention, relative OMM seeds, strict YAML structures and types, CSV line/column diagnostics, byte/depth/event/scalar/row/sample limits, ordering, and cancellation. Equivalent generated YAML/CSV and inline JSON inputs produce byte-identical OMM, TLE, and report outputs on each tested platform. The saved resolved request reproduces the report after the original CSV is moved. Missing data and conflicting input modes leave no published output or owned staging behind. The checked-in example is exercised separately from generation.

The installed CLI runs the two-file workflow in a separate Linux container, and the numerical package consumer requires neither yaml-cpp nor spdlog: only their notices are present in the installed prefix. Strict Linux Doxygen 1.9.8 and its offline link check pass the expanded input documentation. clang-format 18 accepts `Standard: Latest`; authored compilation remains C++23, verified in the generated compiler commands. A fresh replay/mutation campaign of the existing library parser corpus completed **55,393 executions in 61 seconds**, with **444 MiB reported peak RSS** and no finding. That harness does not cover YAML/CSV; its adapter is covered by the new Release/sanitizer tests, and upstream yaml-cpp is not instrumented. These concurrent verification runs are not performance measurements. Follow-up logs are retained locally under `build/qualification/yaml-*`.

The delivery tests exercise strict TLE versus explicit OMM policy, report agreement, catalog limits, full-window rescoring after decimation, and cancellation. Native CLI tests cover bounded reads, failed/short writes, directory races, competing publishers, stale staging ownership, permissions, and real SIGINT/SIGTERM subprocess cancellation. Linux tests ran as a non-root user so the permissions check executed. The CLI parser now checks a shared stop token during parsing and conversion, fixing the sanitizer cancellation timeout found in this pass.

Linux also exposed a genuine near-convergence defect: scalar-only residuals and Jet primal residuals differed at the last bits, making the objective depend on whether Ceres requested a Jacobian. Both paths now use the unmodified reference residual with AD derivatives; the noisy near-Earth/HEO/GEO comparison passes its original physical/cost tolerances. The iteration monitor retains accepted parameters/cost across Ceres aborts. A separate raw-Hessian symmetry discrepancy near an equatorial chart was traced to floating-point contraction/cancellation; the measured limits and narrowly scoped tolerance are documented in [curvature](curvature.md).

The locally extracted Homebrew Doxygen 1.18.0 binary intermittently crashed in its comment scanner. The official arm64 1.18.0 release (`8e760943e5d9581a444cf327f43a0b4d20d29482`) generated the earlier documentation successfully, but the logging follow-up also triggered `SIGBUS` in its `commentscanYYlex` scanner. Linux 1.9.8 generated the current documentation successfully with warnings fatal. The official macOS ZIP SHA-256 was `31d1c74467a9f6f456b4e6a4eff0c03e93d1ffa92da5fdf2d53a2f14ebaabbd7`, matching the release asset digest. See [integration troubleshooting](integration.md); no warning checks were disabled.

The suite covers ten selected public Vallado verification objects (near-Earth, deep-space, resonant, and negative-time cases), propagation call-order/thread independence, automatic and seeded fitting, near-circular/equatorial/retrograde charts, BSTAR recovery, Jacobian step/directional checks, fit-window selection, decimation validation, covariance requirements, robust outliers, iteration limits, pre-start and active-fit cancellation, weak drag observability, format boundaries, duplicate keys, and CLI acceptance/identity handling. Every published state exercised by the suite also checks the templated double and AD paths against the unmodified kernel.

Analytic objective tests independently check full covariance whitening, including position/velocity cross-correlation, position-only covariance blocks, block Huber loss, and irregular time weights after decimation. Noisy fits exercise relative cost convergence even when the absolute-cost and gradient tolerances cannot be met. The migration exposed a finite-difference GEO plateau that the initial adapter mislabeled as stalled; Ceres now owns relative cost convergence, including equal-cost floating-point trials. Exhausted trust-region radius remains a separate stalled result. Rank and physical RMS acceptance are still required; convergence alone cannot accept an inaccurate or unobservable candidate. See [numerical design](design.md) for the stopping semantics.

AD tests check individual position and velocity Jacobian columns, nonzero chart offsets, six/seven parameters, position-only residuals, angular wraps, and smooth sides of eccentricity and near-Earth/deep-space branch boundaries. Nearly equatorial and tiny-drag cases require different finite-difference perturbations; `orbitfit_benchmark --steps` exposes the truncation/roundoff tradeoff. Exactly circular or equatorial chart origins are explicitly rejected for AD, while ordinary propagation remains available. No derivative is claimed across a true model discontinuity.

Eighteen additional curvature tests compare nested-AD residual Hessians with differences of first-order AD Jacobians, check Hessian symmetry and cubic Taylor remainders, and verify exact-objective Hessian-vector products with correlated whitening and Huber loss. They include all ten selected Vallado objects, mixed BSTAR terms, nonzero offsets, six/seven parameters, position-only output, selected difficult geometries, and thread/call-order independence. The implementation computes one Hessian row per nested-AD evaluation to bound working storage; the worker-thread test passes in both Release and Debug/ASan without a larger stack. The installed-package consumer exercises the public curvature API. See [second-order derivatives](curvature.md) for coordinates, tolerances, and limits.

Noisy multi-epoch fits compare AD and finite differences on LEO, resonant HEO, and GEO cases, including extrapolated states after the fitting window. These are comparisons between derivative methods within this fitter, not comparisons against an independent orbit-determination package.

## Native C++ timing comparison

The sections below retain the earlier solver/curvature measurements. They predate the common-primal residual fix and are historical measurements, not fresh timings of this follow-up. Independent-data and robustness results are recorded separately at the end; no new performance improvement is inferred from the refactor.

Run with `ORBITFIT_BUILD_BENCHMARKS=ON` in Release mode. The [raw migration CSV](benchmarks/2026-10-24-ceres-migration-macos-arm64.csv) records three sequential pairs of GTSAM/Ceres runs. Each row is a median of five batches after warm-up: 1,940 residual/Jacobian evaluations per batch or five complete fits per batch. Tables below take the median across the three runs. No orbit-fit build ran concurrently with timing, but system scheduling and load were not controlled; the raw timings show noticeable variation.

Both backends use 97 samples over 48 hours, identical perturbed seeds and deterministic position/velocity noise, the same native AD kernel, weights, and physical acceptance gate. The LEO case estimates BSTAR; the two resonant cases hold it fixed. Input samples are generated with the unmodified reference kernel. The GTSAM baseline is the preserved local implementation before migration, using pinned GTSAM commit `71a25ca36c084cbad1f872e812d6d97fbadfdb05`, LM, and sequential QR. The current implementation uses Ceres 2.2.0 LM and Eigen dense QR. Trust-region updates and termination semantics differ; iteration counts are not directly interchangeable. This is an implementation comparison, not an isolated linear-solver microbenchmark.

Current Ceres AD versus finite differences:

| Case | Parameters | AD factor (µs) | FD factor (µs) | AD fit (ms) | FD fit (ms) | FD / AD fit time |
|---|---:|---:|---:|---:|---:|---:|
| LEO | 7 | 1.643 | 10.803 | 2.822 | 12.096 | 4.29× |
| Molniya 8195 | 6 | 3.349 | 18.760 | 5.523 | 19.781 | 3.58× |
| GEO 25954 | 6 | 2.899 | 13.575 | 4.516 | 18.631 | 4.13× |

Complete fits with AD, before and after migration:

| Case | GTSAM fit (ms) | Ceres fit (ms) | Ceres time increase | Reported iterations, GTSAM / Ceres |
|---|---:|---:|---:|---:|
| LEO | 2.620 | 2.822 | 8% | 4 / 4 |
| Molniya 8195 | 3.951 | 5.523 | 40% | 3 / 4 |
| GEO 25954 | 3.497 | 4.516 | 29% | 3 / 4 |

Every benchmark fit converged and passed acceptance, with approximately 2.12 m position RMS from the injected noise. Across backends, the AD position RMS values differ by less than 4e-7 m and final costs by less than 3e-12 in these cases. Ceres took an additional iteration for the two resonant AD fits. The migration consolidates estimation and differentiation into one dependency; these measurements do **not** show a solver speedup. Timings are machine/workload dependent, are not CI pass/fail thresholds, and do not establish physical orbit accuracy. An [earlier CSV](benchmarks/2026-10-24-macos-arm64.csv) is retained as a historical GTSAM AD/FD run; it is not a measurement of the current Ceres implementation.

## Second-order measurements

The optional `orbitfit_curvature_benchmark` measures residuals, Jacobians, and all residual Hessians through nested AD. Three sequential runs put full second-order factor latency at approximately 18 µs for LEO, 39 µs for Molniya, and 33 µs for GEO at the fitted solutions, about 16–17 times the corresponding first-order evaluation. The GEO seed has a 7.46% normalized residual-curvature correction and an indefinite exact Hessian; fitted-solution corrections are 1.6e-9 to 2.3e-8 across these synthetic cases. See [methodology and results](curvature.md) and the [raw CSV](benchmarks/2026-10-24-curvature-macos-arm64.csv). These are on-demand diagnostics; ordinary fitting still uses the first-order path.

## Serialization and remaining qualification

### Independent forecast and external estimator

The [raw 96-row CSV](benchmarks/2026-10-24-iss-comparison-macos-arm64.csv) contains the measured continuous and serialized errors. The [ISS qualification report](../qualification/README.md) records a frozen NASA/JSC/FOD/TOPO forecast, scoped ERFA conversion, published frame reference check, and eight cases against pinned Project Pluto eph2tle. The ordinary suite contains eight offline regressions for 6/12/24/48-hour arcs with fixed/estimated BSTAR, checking full-precision OMM, reparsed TLE training error, and disjoint 0–6/6–12/12–24-hour prediction intervals. Reproduction commands and all source/archive hashes are retained. Forecast approximation is not observed spacecraft accuracy, and the common Vallado scoring engine is not an independent propagation cross-check.

All eight candidates converge with full rank. Four yield the same rounded orbital fields as the external fitter. The result that matters most for model selection is the 12-hour drag fit: about 276 m training RMS and 16.7 km RMS in the 12–24-hour prediction interval. The regression deliberately retains this failure of prediction despite successful training acceptance. The 48-hour drag fit reaches about 484 m training RMS and 782 m in that final prediction interval. See the report's complete table before choosing arc length or drag parameters.

### Robustness, parsing, and cancellation

The added recovery campaign perturbs mean motion, anomaly, perigee, node, and eccentricity jointly for eight public Vallado objects, over 24-hour near-Earth or 48-hour deep-space arcs. Physically admissible seeds recover accepted fits and predict an additional quarter-arc within a 5 m RMS regression gate. A distinct seed that fails SGP4 inside the fitting window must return an explicit error. A 72-hour high-drag irregular-cadence case estimates BSTAR from a zero-drag seed using 45 selected factors while validating all 181 observations.

Additional tests cover active cancellation of a 12000-observation fit, direct-library resource caps and invalid enums, degenerate/hyperbolic/non-finite states, extreme stored epochs, robust loss versus unweighted acceptance, OMM metadata round-trip limits, huge finite angles, ignored TLE fields with valid checksums, JSON byte/depth limits, and 2000 deterministic TLE mutations. Cancellation may return an early error or an explicitly cancelled partial candidate; the test does not claim a hard latency bound. Existing derivative/curvature near-singular and branch-adjacent cases remain active.

The native libFuzzer campaign uses AppleClang 21 ASan/UBSan instrumentation and LLVM 18.1.8's separately supplied libFuzzer archive. The earlier metadata-fix campaign ran a fresh four-file seed corpus for **333718 inputs in 91 seconds**, with **529 MiB peak reported RSS** and no crash, sanitizer report, timeout, or violated round-trip invariant. A preceding campaign ran 436224 inputs without a finding. After adding parser cancellation, another fresh four-file corpus completed **98645 inputs in 61 seconds**, with **433 MiB peak reported RSS** and no finding. That run overlapped other validation work and is not a throughput benchmark. Counts are executions, not unique inputs or a coverage percentage. The [earlier statistics](benchmarks/2026-10-24-parser-fuzz.txt), [follow-up statistics](benchmarks/2026-10-24-delivery-parser-fuzz.txt), and [reproduction guide](../fuzz/README.md) record bounds and instrumentation. Fitting is covered by numerical regressions, not invoked from this parser harness.

The [design review](design-review.md) records the module split, shared immutable chart, one-pass final diagnostics, cancellation changes, parser/metadata corrections, and retained numerical decisions. Doxygen warnings fail the documentation target; the installed consumer and compiled example verify that the documented public APIs link and execute.

### Serialization example and release gaps

The current AD synthetic example converged in three iterations with approximately **4.131e-5 m** continuous position RMS and **9.025 m** exported-TLE position RMS. The CLI regression checks that its 10 m target passes and a 1 m target rejects the serialized deliverable while retaining the continuous candidate. These are self-consistency/serialization results for this example, not a spacecraft accuracy claim; automatic differentiation does not eliminate TLE quantization.

GitHub Actions is configured for Linux, macOS, and Windows, with strict Doxygen on the Linux Release job. Hosted results are outside this local evidence record. The NASA/Project Pluto corpus, scoped frame reference check, and bounded fuzz campaigns strengthen local evidence. Independent MEO/GEO/HEO truth, observed spacecraft uncertainty, general frame conversion, globally reliable initialization, complete pathological-orbit coverage, long-running fuzz campaigns, and operational qualification remain on the roadmap.
