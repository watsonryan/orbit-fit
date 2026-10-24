#include <charconv>
#include <cmath>
#include <format>
#include <limits>

#include "orbitfit/orbitfit.hpp"

namespace orbitfit {
namespace {
Result<int> digits(std::string_view s) {
  int value{};
  const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
  if (error != std::errc{} || end != s.data() + s.size() || s.empty() || s.front() == '-')
    return std::unexpected(Error{ErrorCode::invalid_input, "Invalid UTC timestamp digits"});
  return value;
}
}  // namespace

Result<Epoch> parse_epoch(std::string_view s) {
  const auto bad = [] {
    return std::unexpected(
        Error{ErrorCode::invalid_input,
              "Expected UTC YYYY-MM-DDTHH:MM:SS[.ffffff]Z; leap-second labels are unsupported"});
  };
  if (s.size() < 20 || s.size() > 27 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' ||
      s[16] != ':' || s.back() != 'Z')
    return bad();
  const auto yy = digits(s.substr(0, 4));
  const auto mo = digits(s.substr(5, 2));
  const auto dd = digits(s.substr(8, 2));
  const auto hh = digits(s.substr(11, 2));
  const auto mi = digits(s.substr(14, 2));
  const auto ss = digits(s.substr(17, 2));
  if (!yy || !mo || !dd || !hh || !mi || !ss || *yy < 1957 || *hh > 23 || *mi > 59 || *ss > 59) return bad();
  const std::chrono::year_month_day date{std::chrono::year{*yy}, std::chrono::month{unsigned(*mo)},
                                         std::chrono::day{unsigned(*dd)}};
  if (!date.ok()) return bad();
  int micros = 0;
  if (s.size() > 20) {
    if (s[19] != '.' || s.size() < 22) return bad();
    const auto frac = s.substr(20, s.size() - 21);
    auto parsed = digits(frac);
    if (!parsed) return bad();
    micros = *parsed;
    for (auto n = frac.size(); n < 6; ++n) micros *= 10;
  }
  return Epoch{std::chrono::sys_days{date} + std::chrono::hours{*hh} + std::chrono::minutes{*mi} +
               std::chrono::seconds{*ss} + std::chrono::microseconds{micros}};
}

std::string format_epoch(Epoch epoch) {
  const auto day = std::chrono::floor<std::chrono::days>(epoch.utc);
  const std::chrono::year_month_day date{day};
  const std::chrono::hh_mm_ss tod{epoch.utc - day};
  return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:06}Z", int(date.year()), unsigned(date.month()),
                     unsigned(date.day()), tod.hours().count(), tod.minutes().count(), tod.seconds().count(),
                     tod.subseconds().count());
}

double seconds_between(Epoch later, Epoch earlier) {
  const auto a = later.utc.time_since_epoch().count();
  const auto b = earlier.utc.time_since_epoch().count();
  // Preserve nearby microseconds, including across a whole-second boundary.
  if (!((b > 0 && a < std::numeric_limits<std::int64_t>::min() + b) ||
        (b < 0 && a > std::numeric_limits<std::int64_t>::max() + b)))
    return static_cast<double>(a - b) / 1e6;
  // The full duration storage range can exceed a signed integer difference.
  return static_cast<double>(a / 1000000 - b / 1000000) +
         static_cast<double>(a % 1000000 - b % 1000000) / 1e6;
}

Result<Epoch> add_seconds(Epoch epoch, double seconds) {
  if (!std::isfinite(seconds) || std::abs(seconds) > 1e12)
    return std::unexpected(Error{ErrorCode::invalid_input, "Invalid epoch offset"});
  const auto offset = std::llround(seconds * 1e6);
  const auto count = epoch.utc.time_since_epoch().count();
  if ((offset > 0 && count > std::numeric_limits<std::int64_t>::max() - offset) ||
      (offset < 0 && count < std::numeric_limits<std::int64_t>::min() - offset))
    return std::unexpected(Error{ErrorCode::invalid_input, "Epoch offset overflows"});
  return Epoch{std::chrono::sys_time<std::chrono::microseconds>{std::chrono::microseconds{count + offset}}};
}

std::string_view to_string(Termination t) {
  switch (t) {
    case Termination::converged:
      return "converged";
    case Termination::iteration_limit:
      return "iteration_limit";
    case Termination::stalled:
      return "stalled";
    case Termination::cancelled:
      return "cancelled";
    case Termination::numerical_failure:
      return "numerical_failure";
  }
  return "unknown";
}
}  // namespace orbitfit
