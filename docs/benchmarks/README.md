<!-- @brief Provenance for retained benchmark and qualification CSV measurements. @author watson -->
# Recorded measurements

All CSV campaigns here were configured, run, and curated by watson on macOS arm64. They are raw numerical artifacts, so descriptions and authorship live in this sidecar instead of changing their column layout.

- `2026-10-24-macos-arm64.csv`: historical GTSAM AD/FD derivative and fit timings before the Ceres migration.
- `2026-10-24-ceres-migration-macos-arm64.csv`: paired historical GTSAM/current Ceres measurements; methodology in [validation](../validation.md).
- `2026-10-24-curvature-macos-arm64.csv`: native nested-AD evaluation costs and objective curvature; methodology in [curvature](../curvature.md).
- `2026-10-24-iss-comparison-macos-arm64.csv`: NASA forecast approximation, exported TLEs, and disjoint prediction intervals; methodology and provenance in [qualification](../../qualification/README.md).
- `2026-10-24-parser-fuzz.txt`: final sanitizer-backed native parser/serialization campaign counters; reproduction in [fuzzing](../../fuzz/README.md).

Timings are observational, not performance guarantees or CI thresholds. Accuracy rows describe agreement with the specified input model/forecast; they do not measure tracking truth. Historical backends remain historical evidence rather than supported implementation choices.
