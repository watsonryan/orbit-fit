/**
 * @file orbitfit.hpp
 * @brief Public time, SGP4, TLE, fitting, and physical-error evaluation functions.
 * @author watson
 */
#pragma once

#include <span>
#include <string_view>

#include "orbitfit/types.hpp"

namespace orbitfit {
/**
 * @brief Parse a strict civil-UTC timestamp.
 * @param iso8601 YYYY-MM-DDTHH:MM:SS[.ffffff]Z, with 1..6 fractional digits when present.
 * @return Microsecond epoch, or invalid_input for bad syntax/calendar, year outside
 * 1957..9999, offsets other than Z, or a leap-second label.
 */
[[nodiscard]] Result<Epoch> parse_epoch(std::string_view iso8601);

/**
 * @brief Format UTC with exactly six fractional digits and a trailing Z.
 * @param epoch Timestamp in calendar years 1957..9999.
 * @return YYYY-MM-DDTHH:MM:SS.ffffffZ.
 * @throws std::invalid_argument If epoch is outside the supported calendar range.
 */
[[nodiscard]] std::string format_epoch(Epoch epoch);

/**
 * @brief Subtract timestamps using SGP4 civil-day seconds.
 * @param later Minuend, not necessarily later in time.
 * @param earlier Subtrahend.
 * @return Signed difference in seconds; nearby microseconds are preserved.
 * @note This does not include elapsed leap seconds and is safe across the storage range.
 */
[[nodiscard]] double seconds_between(Epoch later, Epoch earlier);

/**
 * @brief Add a finite offset, rounded to the nearest microsecond.
 * @param epoch Starting timestamp.
 * @param seconds Signed civil-second offset with magnitude at most 1e12.
 * @return Shifted timestamp, or invalid_input for non-finite/excessive offset or storage overflow.
 * @note The result may lie outside the calendar range accepted by format_epoch.
 */
[[nodiscard]] Result<Epoch> add_seconds(Epoch epoch, double seconds);

/**
 * @brief Check finite mean elements, scalar domains, and element-epoch range.
 * @param elements Candidate SGP4 mean elements.
 * @return Success or invalid_input.
 * @note This does not prove SGP4 validity at any propagation time or AD chart regularity.
 */
[[nodiscard]] Result<void> validate(const Elements& elements);

/**
 * @brief Propagate with the unmodified Vallado kernel, WGS-72 and improved mode.
 * @param elements SGP4 mean elements.
 * @param epoch Requested epoch, within 5,259,600 model minutes of the element epoch.
 * @return TEME state in SI units, or invalid_input/propagation.
 * @details Each call initializes a fresh record, including deep-space state.
 * Calls share no mutable propagation record and may run concurrently.
 */
[[nodiscard]] Result<State> propagate(const Elements& elements, Epoch epoch);

/**
 * @brief Encode a strict numeric TLE and its checksums.
 * @param set Mean elements and representable identity metadata.
 * @return Two 69-character lines or invalid_input/serialization.
 * @details Supports numeric IDs 1..99999 and epoch/launch years 1957..2056.
 * Epoch rounds to 864-microsecond ticks; angular and exponent carries are checked.
 * Mean-motion derivatives are emitted as zero. Epoch rounding does not refit the
 * elements: evaluate the reparsed output to assess the delivered accuracy.
 */
[[nodiscard]] Result<Tle> to_tle(const ElementSet& set);

/**
 * @brief Parse numeric TLE lines with fixed-width layout and valid checksums.
 * @param tle Exactly two 69-character lines without CR/LF or a name line.
 * @return Validated elements and metadata, or serialization/invalid_input.
 * @details Alpha-5 identifiers and nonstandard ephemeris types are unsupported.
 * Mean-motion derivative fields are validated but are not used by SGP4.
 */
[[nodiscard]] Result<ElementSet> from_tle(const Tle& tle);

/**
 * @brief Fit one SGP4 mean-element block to a covered TEME observation window.
 * @param samples 3..100000 finite samples, strictly increasing and unique.
 * Both position and velocity are required, including for position-only fitting.
 * @param options Window, initialization, weighting, numerical controls, and stop token.
 * @return A candidate with acceptance and diagnostics, or an explicit input,
 * frame, propagation, solver, or early-cancellation error.
 * @details Uses Ceres LM with dense QR. Missing seeds are initialized from a
 * Cartesian state (Hermite interpolation between bracketing samples), then fit
 * as mean elements. No globally reliable initialization is promised. Invalid AD
 * trials are rejected; exactly circular/equatorial chart origins are unsupported.
 * Errors return no candidate. Nonconverged or inaccurate candidates can return
 * successfully with accepted=false. See FitResult for partial cancellation results.
 * @note Concurrent calls are independent; input storage must remain valid and
 * unmodified for the duration of this synchronous call.
 */
[[nodiscard]] Result<FitResult> fit(std::span<const Sample> samples, const FitOptions& options);

/**
 * @brief Evaluate unweighted physical errors at all supplied epochs.
 * @param elements Candidate SGP4 mean elements.
 * @param samples Nonempty TEME samples; input order is arbitrary.
 * @return SI position/velocity RMS and maxima, or input/frame/propagation error.
 * @details Uses both position and velocity, ignores covariance, performs no
 * decimation, and applies no optimization acceptance gate.
 */
[[nodiscard]] Result<Metrics> evaluate(const Elements& elements, std::span<const Sample> samples);

/**
 * @brief Obtain a stable spelling for a stopping reason.
 * @param termination Numerical stopping reason.
 * @return One of the enum spellings, or "unknown" for an invalid enum value.
 */
[[nodiscard]] std::string_view to_string(Termination termination);
}  // namespace orbitfit
