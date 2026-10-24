/**
 * @file parsers.cpp
 * @brief Fuzz bounded public parsers and successful serialization round trips.
 * @author watson
 */
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "orbitfit/io.hpp"

namespace {
void require(bool condition) {
  if (!condition) std::abort();
}
void round_trip(const orbitfit::ElementSet& set) {
  const auto omm = orbitfit::to_omm_json(set);
  require(omm.has_value());
  require(orbitfit::from_omm_json(*omm).has_value());
  // Some valid OMM elements cannot fit legacy TLE widths or year limits.
  if (const auto tle = orbitfit::to_tle(set)) require(orbitfit::from_tle(*tle).has_value());
}
}  // namespace

/**
 * @brief libFuzzer entry point; does not run the unbounded-cost optimizer.
 * @param data Arbitrary bytes owned by libFuzzer for this call.
 * @param size Byte length, capped here at 64 KiB for throughput and memory bounds.
 * @return Zero; violated round-trip invariants abort for corpus minimization.
 */
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > 65536) return 0;
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  if (const auto epoch = orbitfit::parse_epoch(input)) {
    const auto reparsed = orbitfit::parse_epoch(orbitfit::format_epoch(*epoch));
    require(reparsed && *reparsed == *epoch);
  }
  if (const auto set = orbitfit::from_omm_json(input)) round_trip(*set);
  static_cast<void>(orbitfit::read_request(input));
  if (const auto newline = input.find('\n'); newline != std::string_view::npos) {
    auto second = input.substr(newline + 1);
    if (second.ends_with('\n')) second.remove_suffix(1);
    const orbitfit::Tle tle{std::string(input.substr(0, newline)), std::string(second)};
    if (const auto set = orbitfit::from_tle(tle)) round_trip(*set);
  }
  return 0;
}
