# Contributing

Use C++23 with target-scoped CMake settings. Keep GTSAM/Eigen implementation types out of public headers, input units explicit, and error states observable. Prefer a small tested feature over a generic framework without a demonstrated use.

Run `./local_ci.sh release` for configure, build, tests, installation, and an external consumer. Run `./local_ci.sh sanitize` for ASan/UBSan instrumentation of authored code and the SGP4 adapter/kernel. Dependency instrumentation is a separate qualification task. Format authored C++ with clang-format 18 using `.clang-format`; never reformat vendored files or reference fixtures.

Numerical changes need evidence: an independently sourced reference or an invariant, meaningful tolerances in physical units, failure cases, and an explanation of any behavior change. Synthetic self-recovery alone is insufficient for changes to physical-model correctness. Do not lower thresholds just to accommodate an unexplained regression.

Record source URLs, immutable revisions, hashes, licenses, and reproduction instructions for new fixtures or vendored code. Do not contribute proprietary source or private operational data. Keep dependency updates separate from numerical changes where practical.

Pull requests should state the concrete behavior change and validation, including compiler/platform and any remaining limitations. Contributions use the repository's Apache-2.0 license unless a separately attributed upstream component says otherwise.
