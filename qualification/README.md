<!-- @brief Reproducible NASA ISS forecast qualification against Project Pluto eph2tle. @author watson -->
# Independent ephemeris qualification

This campaign fits an independently produced **NASA ISS forecast**, compares the result with Project Pluto's established `eph2tle` estimator, and scores both exported TLEs on disjoint prediction intervals. It measures approximation of that forecast, **not spacecraft tracking accuracy**. The numerical core stays native C++23 and independent of astro-core. ERFA and the external comparator are qualification-only tools.

## Dataset and frame convention

The unmodified [NASA ISS OEM](https://nasa-public-data.s3.amazonaws.com/iss-coords/current/ISS_OEM/ISS.OEM_J2K_EPH.txt) was retrieved on 2026-10-24 and is frozen at `tests/data/independent/iss-oem-20261021.txt`. NASA/JSC/FOD/TOPO created it at 2026-10-21T16:27:10.430, with states from October 21 through November 5. Its metadata declares Earth, EME2000, UTC, kilometres, and kilometres/second. NASA describes this public product on its [trajectory data page](https://spotthestation.nasa.gov/trajectory_data.cfm). The experiment uses the first 72 hours starting 2026-10-21T12:00:00Z: 1081 states at four-minute cadence. This excerpt has no listed maneuver annotations; absence of annotations is not proof of force-model fidelity or maneuver-free truth.

`iss.cpp prepare` converts the excerpt to SI TEME using unmodified ERFA 2.0.1 IAU-1976 precession and IAU-1980 nutation. For column vectors the rotation is `R3(dpsi*cos(mean_obliquity)) * N * P`, with ERFA's passive `R3` convention. This is the geometric equation-of-equinoxes TEME-of-date convention described by Vallado's [teme2eci companion routine](https://github.com/CelesTrak/fundamentals-of-astrodynamics/blob/49df0479c950917c0a2c5ac31f2db6477bba57f6/software/matlab/teme2eci.m). The routine was consulted for the convention; its implementation is not copied into orbit-fit.

Position and velocity components receive the same celestial rotation, as in that convention; this is not differentiation of a rotating-coordinate position and does not add `Rdot*r`. No GCRF-to-EME2000 frame bias, observed nutation offsets, polar motion, or UT1 conversion is used. This scoped fixture converter is not a general frames/OEM library. [CelesTrak's FAQ](https://celestrak.org/publications/AIAA/2006-6753/faq.php) discusses the limitations of mapping TEME to standard celestial frames.

For October 2026, TT−UTC is 69.184 s: 32.184 s plus TAI−UTC of 37 s. The frozen [IANA leap-second list](https://data.iana.org/time-zones/data/leap-seconds.list) expires 2027-06-28, covering the entire excerpt. The converter intentionally fixes this offset for this specific dataset; it is not a future leap-second prediction. Source and derived hashes are enforced by `tests/data/check.cmake`.

`check-frames` independently checks the conversion against the Vallado numerical example recorded in [Orekit 13.1 TEMEProviderTest](https://github.com/CS-SI/Orekit/blob/13.1/src/test/java/org/orekit/frames/TEMEProviderTest.java). Only its numerical reference values are used. At 2000-06-30T18:50:19.733568Z (TT−UTC 64.184 s), the difference is **0.116737406 m** and **0.000060466 m/s**. That reference includes two complementary equinox terms omitted by our geometric convention; the test budgets are 0.15 m and 0.0001 m/s. This tests orientation, sign, units, time scale, and velocity convention rather than just an invertible round trip. It does not qualify all frame epochs or conventions. The consulted source SHA256 is `06ad09bea16867004e2f6e1c7036884269aecc045bbaed2869d636fdd61aa1ef`.

## Comparison protocol

Fit 6, 12, 24, and 48 hours, each once with BSTAR fixed at zero and once estimating it. Both estimators receive the same converted TEME positions, regular cadence, and element epoch at the middle observation. They initialize independently from Cartesian states. Orbit-fit uses position-only residuals and identity position covariance, giving uniform unit weights matching eph2tle's least-squares stage. Identity covariance here defines an objective; it is not a claim of one-metre independent NASA measurement noise. No robust loss, decimation, or reference-TLE seed is used.

The external implementation is [Bill Gray's Project Pluto eph2tle](https://github.com/Bill-Gray/find_orb/blob/1de55cca12e7a585c9c7384acbb80538ea2afc3c/eph2tle.cpp). `reference.cmake` pins and hash-checks these source archives:

| Repository | Commit | Archive SHA256 |
|---|---|---|
| find_orb | `1de55cca12e7a585c9c7384acbb80538ea2afc3c` | `383418ecd2da8d2bf8e7dd13a667be0ed83318fc8997f2ea525eb0c71af949f0` |
| sat_code | `2814ea074e814d0f69b4ec1aab03425ea7a6d342` | `f6e24d80d2134eac51c2ca2df8e7a73611531c6f2ea6d357439bfcb1588f8d7f` |
| lunar | `110ab53d46e3e5f1952318360e7bb01cb4884674` | `1cee7eed8b4c1d6b717769782c5880e87bc8d246045bbbefed587a47ce946c6a` |

The `.eph` header uses frame code `-1` to disable eph2tle's approximate internal precession: input is already TEME. Dates are TT Julian dates; the upstream reader converts them back to UTC. The distance/time scale header represents metres and metres/second using 149597870700 metres/AU and 86400 seconds/day. Double-precision Julian dates introduce roughly tens of microseconds of timestamp rounding. The script fixes the epoch index, random seed 1, and 80 least-squares iterations (`-z80`), leaving upstream simplex defaults unchanged.

Algorithms differ: orbit-fit uses a normalized mean-element chart, native AD, and Ceres LM/dense QR; eph2tle uses its own initialization, simplex search, finite differences, SGP4 implementation, and damped least squares. Upstream retains the iterate with the smallest maximum absolute Cartesian position component residual. Thus its delivered product is not necessarily the lowest-SSE iterate. Iteration counts and timings are not directly comparable.

All reported products are evaluated through **the same unmodified Vallado kernel** against the frozen TEME observations. This isolates delivered fitting differences; it is not an independent propagator comparison. The fit interval includes both endpoints. Held-out intervals exclude the last fitted observation and are disjoint: `(end,end+6h]`, `(end+6h,end+12h]`, and `(end+12h,end+24h]` with 90, 90, and 180 samples. The final 48-hour case consumes all 72 hours.

## Measured results

Run on macOS arm64, Apple M4, AppleClang 21, Ceres 2.2.0, Release. Values below are position RMS in metres for **reparsed TLEs**. Complete continuous/TLE position and velocity RMS/maxima are in the [96-row CSV linked in the validation record](../docs/validation.md). `continuous_fit_accepted` describes only the training fit's 2000 m gate, not a held-out acceptance decision. `fit_ms` is incidental elapsed time under uncontrolled system load; this campaign is not a timing benchmark.

| Arc | BSTAR | orbit-fit training | eph2tle training | orbit-fit 12–24 h holdout | eph2tle 12–24 h holdout |
|---|---|---:|---:|---:|---:|
| 6 h | fixed 0 | 238.26 | 238.26 | 1482.20 | 1482.20 |
| 6 h | estimated | 237.21 | 237.21 | 2663.43 | 2663.43 |
| 12 h | fixed 0 | 342.52 | 343.90 | 5567.59 | 5578.05 |
| 12 h | estimated | 276.21 | 276.21 | 16706.23 | 16706.23 |
| 24 h | fixed 0 | 513.95 | 515.78 | 7353.73 | 7485.77 |
| 24 h | estimated | 441.13 | 443.28 | 1989.26 | 2070.57 |
| 48 h | fixed 0 | 1358.65 | 1370.52 | 11399.03 | 11052.32 |
| 48 h | estimated | 483.60 | 483.60 | 782.09 | 782.09 |

All eight orbit-fit candidates converge with full numerical rank. Four cases produce equivalent rounded orbital fields and identical scored states to eph2tle; metadata formatting differs. Orbit-fit's training RMS is comparable or lower throughout, while held-out performance is not uniformly better. Most importantly, 12-hour estimated drag reduces training RMS to 276 m but yields **16.7 km** RMS in the final prediction interval. Longer fitting arcs change that outcome; 48-hour estimated drag yields 782 m in the same relative horizon. This is evidence for validating model complexity and prediction horizons separately, not a universal recommendation to fit drag or always use a longer arc.

Eight ordinary offline regression tests consume only frozen CSV/TLE data, without ERFA, network access, or the external executable. They enforce a 2000 m training gate, no material regression from the external baseline (2% + 10 m training, 15% + 50 m held-out), a 20 m maximum training-RMS change due to TLE quantization, and an OMM round-trip check. These are regression budgets chosen for this corpus, not promised supported accuracy. A separate check preserves the short-arc drag prediction failure so training acceptance cannot be mistaken for prediction qualification.

## Reproduce

The normal tests are offline. Rebuilding the converter downloads hash-pinned ERFA 2.0.1 through CPM; running the external comparison script downloads the three pinned Project Pluto archives. Use fresh output directories. Commands below are for a Unix environment with C++23, CMake, Ninja, and make:

```sh
cmake --preset release -DORBITFIT_BUILD_QUALIFICATION=ON
cmake --build --preset release --parallel 4
build/release/qualification/orbitfit_qualification check-frames
build/release/qualification/orbitfit_qualification prepare \
  tests/data/independent/iss-oem-20261021.txt build/qualification/reproduce-input
cmake -DREFERENCE_ROOT="$PWD/build/qualification/reproduce-external" \
  -DINPUT_DIR="$PWD/build/qualification/reproduce-input" \
  -DOUTPUT_DIR="$PWD/build/qualification/reproduce-reference" \
  -P qualification/reference.cmake
build/release/qualification/orbitfit_qualification compare \
  build/qualification/reproduce-input/iss-teme.csv \
  build/qualification/reproduce-reference > build/qualification/reproduce.csv
cmake -P tests/data/check.cmake
```

The reference script and output were rerun locally from the pinned archives. Fresh downloads and hosted platform runs are separate environmental checks. Reference source trees are local build inputs and must remain unmodified; archive hashes do not attest to edits inside a reused extracted directory.

## Distribution and next evidence

NASA owns its source data attribution. The generated CSV names watson's conversion and NASA's original producer. ERFA is BSD licensed and linked only to the optional converter; its source notices remain intact in the CPM cache. Project Pluto code retains its upstream GPL terms and is built as a separate executable under ignored build directories. No Project Pluto source or executable is copied into or linked with the orbit-fit library; frozen numerical output is accompanied by provenance. Original license notices must accompany any separately redistributed reference tools.

The next accuracy evidence should include independently produced MEO, GEO, and HEO ephemerides, observed trajectory uncertainty, maneuvers, broader initialization stress, and more prediction horizons. These results justify a stronger engineering alpha; they do not establish operational qualification.
