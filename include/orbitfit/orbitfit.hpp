#pragma once

#include <span>
#include <string_view>

#include "orbitfit/types.hpp"

namespace orbitfit {
[[nodiscard]] Result<Epoch> parse_epoch(std::string_view iso8601);
[[nodiscard]] std::string format_epoch(Epoch epoch);
[[nodiscard]] double seconds_between(Epoch later, Epoch earlier);
[[nodiscard]] Result<Epoch> add_seconds(Epoch epoch, double seconds);
[[nodiscard]] Result<void> validate(const Elements& elements);
[[nodiscard]] Result<State> propagate(const Elements& elements, Epoch epoch);
[[nodiscard]] Result<Tle> to_tle(const ElementSet& set);
[[nodiscard]] Result<ElementSet> from_tle(const Tle& tle);
[[nodiscard]] Result<FitResult> fit(std::span<const Sample> samples, const FitOptions& options);
[[nodiscard]] Result<Metrics> evaluate(const Elements& elements, std::span<const Sample> samples);
[[nodiscard]] std::string_view to_string(Termination termination);
}  // namespace orbitfit
