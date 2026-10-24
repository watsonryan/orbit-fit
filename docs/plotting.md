<!-- @brief Document optional publication plots, input contracts, scientific interpretation, and reproducibility. @author watson -->
# Publication figures

`orbitfit-plot` is an optional Python tool using Matplotlib and SciencePlots. It reads the native C++23 CLI's completed output and the frozen ISS qualification CSV. Fitting, SGP4 propagation, derivatives, and product validation stay in C++. Python is not required to build, test, install, or use the numerical library and CLI, or to build the Doxygen reference with its checked-in figures.

## Quick start

Install [uv](https://docs.astral.sh/uv/getting-started/installation/) and use Python 3.12–3.14. Run these commands from the repository root; `uv` creates an isolated environment in `tools/plots/.venv`. The checked-in lock records exact package versions and archive hashes. The first synchronization needs access to package downloads; rendering itself is offline.

```sh
uv sync --locked --project tools/plots --python 3.12
build/release/orbit-fit fit --config examples/fit.yaml --output-dir results
uv run --locked --project tools/plots orbitfit-plot fit results \
  --qualification docs/benchmarks/2026-10-24-iss-comparison-macos-arm64.csv \
  --output-dir figures
```

This creates three figures in PDF, SVG, and PNG, plus `captions.md` and `manifest.json`. Without `--qualification`, the `fit` command creates the two result figures. To try the full gallery without compiling or running the fitter, replace `results` with `docs/figures/input`, the checked-in native result snapshot. See the [gallery and provenance on GitHub](https://github.com/watsonryan/orbit-fit/blob/main/docs/figures/README.md); all three figures are also included below for offline reading.

For the frozen campaign alone:

```sh
uv run --locked --project tools/plots orbitfit-plot qualification \
  docs/benchmarks/2026-10-24-iss-comparison-macos-arm64.csv \
  --output-dir iss-figures --width double --formats pdf svg
```

Output directories must be new. Existing files are never overwritten. The tool removes its newly created directory if a handled export error occurs and writes the manifest last to indicate completion. Export is not an atomic directory publication and is not crash durable; a killed process may leave an incomplete directory. Exit codes are 0 for completed export, 1 for input/I/O errors, and 2 for invalid command-line usage. Plotting a complete rejected fit can return 0: that status means the figures were written, not that the orbit was accepted.

## Figures and interpretation

| Output basename | What it shows | Interpretation |
|---|---|---|
| `residuals` | Continuous position residuals in reference RTN and velocity residual magnitude | All supplied samples in the inclusive fit window, including samples omitted from optimization by decimation |
| `serialization` | Position RMS and maximum for the continuous model and reparsed OMM/TLE | Quantization and delivered-product accuracy against the same input; the dashed acceptance gate applies to RMS only |
| `iss-prediction` | Reparsed TLE training and held-out prediction RMS for fixed/fitted BSTAR and different arc lengths | Frozen NASA forecast agreement; the 12–24 h prediction interval follows each arc's own end |

![Continuous residuals in the synthetic example](figures/residuals.svg)

Residuals are **predicted minus supplied**. At each supplied TEME state `(r, v)`, the unit axes are:

```text
R = r / |r|
N = (r × v) / |r × v|
T = N × R
delta-r_RTN = [dot(delta-r, R), dot(delta-r, T), dot(delta-r, N)]
```

T is transverse in the instantaneous orbital plane; velocity need not be parallel to T. The position plot is a vector projection. The velocity panel shows the norm of the TEME velocity difference, not the time derivative of position components in a rotating RTN frame. Zero position/velocity and nearly parallel vectors (normalized cross-product magnitude at most `1e-12`) do not define a usable RTN basis and are rejected.

All statistics use equal validation weight per sample: position RMS is `sqrt(mean(|delta-r|²))`, not per-axis RMS or the solver's weighted cost. Velocity RMS has the analogous definition. The residual panel chooses µm/mm/m/km display units, independently for position and velocity; stored data remain SI. Times are hours from the configured fit start under the native UTC calendar convention. Straight segments connect sample values; the plot does not evaluate propagation between samples.

The serialization panel uses native round-trip metrics, without reparsing or propagating products in Python. A logarithmic ordinate is used only if all displayed metrics and the gate are positive and span more than a factor of 100. Exact zeros stay zero on a linear axis. Unavailable evaluations are labelled and omitted, never replaced with zero. Complete rejected fits are visibly marked. Individual product labels report physical acceptance; the caption also reports the overall delivery decision.

![Continuous and serialized position errors](figures/serialization.svg)

The synthetic example demonstrates numerical self-consistency and serialization loss. Its tiny continuous residual is not evidence of spacecraft accuracy. The ISS figure uses the retained 2026-10-24 campaign: it selects only `orbit-fit` / `tle` rows and intervals `fit` and `12-24h`. Six parameters mean BSTAR fixed at zero; seven mean BSTAR estimated. Each selected arc must contain both parameter choices and both intervals. The displayed prediction interval is `(fit end + 12 h, fit end + 24 h]`; different arc lengths therefore compare different absolute prediction epochs. Lines connect tested cases, without asserting an interpolated optimum. See the [qualification report](../qualification/README.md) for epochs, counts, conversion conventions, comparator results, and limitations.

![ISS fit and prediction comparison](figures/iss-prediction.svg)

These are errors against a NASA forecast, not observed tracking truth. The 12-hour fitted-BSTAR case has approximately 276 m training RMS and 16.7 km prediction RMS. Do not use training acceptance alone to choose drag estimation or arc length.

## Publication settings

The base styles are `science`, `ieee`, and `no-latex`. Explicit overrides give reproducible dimensions and fonts:

| Option | Default | Behavior |
|---|---|---|
| `--width single` | 3.5 in | Single-column page width; `double` selects 7.16 in |
| `--formats pdf svg png` | All three | Select any nonrepeating subset |
| `--dpi 600` | 600 | PNG resolution, allowed range 150–1200; vector page dimensions stay unchanged |
| `--font STIXGeneral` | Bundled serif family | An explicit installed family is required; missing families cause an error |

Axis labels, ticks, titles, and legends use 9 pt type. PDF embeds TrueType font subsets. SVG outlines text so viewers do not need the font. PNG is a high-resolution preview. Colors are supplemented by distinct line styles or markers. The exporter preserves the exact page width instead of cropping the page to the artists. Figure heights are 4.35 in, 3.15 in, and 4.3 in respectively; selecting double width changes width only. No TeX installation is needed.

The sizes follow [IEEE figure guidance](https://proceedingsoftheieee.ieee.org/resources/guidelines-for-figures-and-tables/). SciencePlots' upstream IEEE preset is a starting point; it does not certify a submission. Check the target venue's [file and font requirements](https://journals.ieeeauthorcenter.ieee.org/create-your-ieee-journal-article/create-graphics-for-your-article/file-formatting/), inspect at final printed size, and include the caption. STIXGeneral is the portable default, not a claim that every venue accepts it. If Times New Roman is required and installed, add `--font "Times New Roman"`. Use the PDF for vector publication output and verify its actual fonts with `pdffonts`; SVG paths are not editable text. Review grayscale legibility and any journal-specific resolution requirements before submission.

## Input contract and reproducibility

The `fit` command expects `report.json` (`orbit-fit.report.v2`) and the resolved `request.json` (`orbit-fit.request.v1`) in the same directory. It does not accept YAML or raw state CSV directly: run the native CLI first. The snapshot has already resolved paths, converted units, and embedded the input samples. Keep the pair together.

Checks cover schema, TEME/UTC declarations, catalog identity, fit start/stop/element epoch, acceptance policy and RMS gate, finite values, strictly increasing sample epochs, exact in-window residual coverage, metric sample counts, and consistency between continuous metrics and residual arrays. Serialized metrics come from the native report. These checks catch common mismatched or damaged files; they do not authenticate a report or replace the native input validator. Unknown fields are allowed for forward-compatible metadata. Complete rejected candidates can be plotted; incomplete cancelled results cannot.

Reads are bounded to 64 MiB per file, 3–100,000 state/residual records, and 10,000 qualification CSV rows. JSON duplicate keys and nonfinite numbers are errors. Timestamps must be UTC `YYYY-MM-DDTHH:MM:SS[.ffffff]Z` with at most six fractional digits; timezone offsets and leap-second labels are rejected. Qualification mode is specific to the frozen ISS campaign vocabulary, not a general benchmark CSV reader.

`manifest.json` records consumed input basenames and SHA-256 hashes, plotting source hashes, package/Python/FreeType versions, platform, font hash, layout settings, native fit configuration and decision when present, captions, and exported artifact hashes. It does not record absolute input paths or volatile timestamps. Use the recorded input hashes and native provenance to identify an experiment; the manifest is not a signature. The checked-in gallery retains its exact input pair, so the figures can be regenerated independently of future numerical changes.

Repeated exports in the same environment are tested for byte equality. Different operating systems, font files, Python versions, or rendering libraries can change PDF/SVG/PNG bytes despite identical plotted values. The lock and manifest document those differences; cross-platform bit identity is not promised. The native input snapshot's original solver commit and compiler are recorded in the gallery README because the native report currently records a version, not a Git revision.

For read-only environments, set `MPLCONFIGDIR` to a writable cache directory before running the command. The Agg backend works without a display. Figure generation makes no network requests and imports no numerical orbit package.

## Development checks

```sh
uv sync --locked --project tools/plots --python 3.12
uv run --locked --project tools/plots ruff check tools/plots
uv run --locked --project tools/plots ruff format --check tools/plots
uv run --locked --project tools/plots python -m unittest discover -s tools/plots/tests -v
```

Install Poppler (`poppler-utils` on Ubuntu, `poppler` with Homebrew) for `pdffonts` and `pdfinfo`. The PDF audit is skipped locally if those tools are absent; CI installs them and checks embedded fonts and physical page size. Tests also cover RTN sign/norm/rotation invariants, malformed and mismatched inputs, missing versus zero metrics, both page widths, export failure cleanup, and repeatable artifact bytes. The optional plotting CI job runs Python 3.12 and 3.14 and renders fresh results from the native Linux package. The plot tests and dependency environment are separate from CMake/CTest.

The checked-in gallery is generated output. Regenerate it into a new directory, inspect all three figures, and replace its artifacts, captions, and manifest together when changing the renderer. Retain the source snapshots unless deliberately documenting a new numerical run. No new physical qualification is inferred from a rendering change.
