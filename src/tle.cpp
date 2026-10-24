#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>

#include "orbitfit/orbitfit.hpp"

namespace orbitfit {
namespace {
constexpr double degree = std::numbers::pi / 180.0;
Error error(std::string message) { return {ErrorCode::serialization, std::move(message)}; }
double degrees(double rad) {
  auto d = std::fmod(rad / degree, 360.0);
  if (d < 0) d += 360.0;
  return std::fmod(std::round(d * 1e4) / 1e4, 360.0);
}
std::string_view trim(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
  return s;
}
template <class T>
Result<T> number(std::string_view s) {
  s = trim(s);
  T value{};
  const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
  if (ec != std::errc{} || end != s.data() + s.size() || !std::isfinite(static_cast<double>(value)))
    return std::unexpected(error("Invalid numeric TLE field"));
  return value;
}
int checksum(std::string_view s) {
  int sum = 0;
  for (char c : s) {
    if (c >= '0' && c <= '9')
      sum += c - '0';
    else if (c == '-')
      ++sum;
  }
  return sum % 10;
}
Result<std::string> exponent(double value) {
  if (value == 0) return " 00000-0";
  int exp = static_cast<int>(std::floor(std::log10(std::abs(value)))) + 1;
  if (exp < -10 || exp > 9) return std::unexpected(error("BSTAR is outside the TLE exponent range"));
  auto mantissa = std::llround(std::abs(value) * std::pow(10.0, 5 - exp));
  if (mantissa == 100000) {
    mantissa = 10000;
    ++exp;
  }
  if (exp < -9 || exp > 9 || mantissa > 99999)
    return std::unexpected(error("BSTAR is outside the TLE exponent range"));
  return std::format("{}{:05}{}{}", value < 0 ? '-' : ' ', mantissa, exp < 0 ? '-' : '+', std::abs(exp));
}
Result<double> parse_exponent(std::string_view s) {
  if (s.size() != 8 || (s[0] != ' ' && s[0] != '-' && s[0] != '+') || (s[6] != '-' && s[6] != '+') ||
      s[7] < '0' || s[7] > '9')
    return std::unexpected(error("Invalid TLE exponent field"));
  const auto mantissa = number<int>(s.substr(1, 5));
  if (!mantissa || *mantissa < 0) return std::unexpected(error("Invalid TLE mantissa"));
  return (s[0] == '-' ? -1.0 : 1.0) * static_cast<double>(*mantissa) * 1e-5 *
         std::pow(10.0, (s[6] == '-' ? -1 : 1) * (s[7] - '0'));
}
}  // namespace

Result<Tle> to_tle(const ElementSet& set) {
  const auto& e = set.elements;
  const auto& m = set.metadata;
  if (auto v = validate(e); !v) return std::unexpected(v.error());
  if (m.catalog_number == 0 || m.catalog_number > 99999 || m.element_set_number > 9999 ||
      m.revolution_number > 99999 ||
      (m.classification != 'U' && m.classification != 'C' && m.classification != 'S'))
    return std::unexpected(
        error("Metadata is not representable as a numeric TLE; use OMM for large catalog IDs"));
  // One TLE epoch tick is exactly 864 microseconds. Round before extracting the year/day.
  const auto count = e.epoch.utc.time_since_epoch().count();
  auto rem = count % 864;
  if (rem < 0) rem += 864;
  const Epoch rounded{std::chrono::sys_time<std::chrono::microseconds>{
      std::chrono::microseconds{count - rem + (rem >= 432 ? 864 : 0)}}};
  const auto date = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(rounded.utc)};
  const int year = int(date.year());
  if (year < 1957 || year > 2056)
    return std::unexpected(error("TLE epoch requires years 1957..2056; use OMM"));
  const auto jan1 = std::chrono::sys_days{date.year() / 1 / 1};
  const auto ticks = (rounded.utc - jan1).count() / 864;
  const auto day = ticks / 100000000 + 1;
  const auto fraction = ticks % 100000000;
  std::string designator;
  if (!m.international_designator.empty()) {
    const auto& id = m.international_designator;
    if (id.size() < 9 || id.size() > 11 || id[4] != '-' ||
        !std::ranges::all_of(id.substr(0, 4) + id.substr(5, 3),
                             [](char c) { return c >= '0' && c <= '9'; }) ||
        !std::ranges::all_of(id.substr(8), [](char c) { return c >= 'A' && c <= 'Z'; }))
      return std::unexpected(error("International designator must be YYYY-NNNPPP"));
    designator = id.substr(2, 2) + id.substr(5);
    const auto launch_year = number<int>(std::string_view(id).substr(0, 4));
    if (!launch_year || *launch_year < 1957 || *launch_year > 2056)
      return std::unexpected(error("TLE launch year requires years 1957..2056; use OMM"));
  }
  const auto bstar = exponent(e.bstar);
  if (!bstar) return std::unexpected(bstar.error());
  const auto ecc = std::llround(e.eccentricity * 1e7);
  const auto motion = std::round(e.mean_motion_rev_per_day * 1e8) / 1e8;
  if (ecc > 9999999 || motion <= 0 || motion >= 100)
    return std::unexpected(error("Mean elements are not representable at TLE precision"));
  Tle tle;
  tle.line1 =
      std::format("1 {:05}{} {:8} {:02}{:03}.{:08}  .00000000  00000-0 {} 0 {:4}", m.catalog_number,
                  m.classification, designator, year % 100, day, fraction, *bstar, m.element_set_number);
  tle.line2 =
      std::format("2 {:05} {:8.4f} {:8.4f} {:07} {:8.4f} {:8.4f} {:11.8f}{:5}", m.catalog_number,
                  e.inclination_rad / degree, degrees(e.raan_rad), ecc, degrees(e.argument_of_perigee_rad),
                  degrees(e.mean_anomaly_rad), motion, m.revolution_number);
  if (tle.line1.size() != 68 || tle.line2.size() != 68) return std::unexpected(error("TLE field overflow"));
  tle.line1 += static_cast<char>('0' + checksum(tle.line1));
  tle.line2 += static_cast<char>('0' + checksum(tle.line2));
  return tle;
}

Result<ElementSet> from_tle(const Tle& tle) {
  const auto& a = tle.line1;
  const auto& b = tle.line2;
  if (a.size() != 69 || b.size() != 69 || a.substr(0, 2) != "1 " || b.substr(0, 2) != "2 " ||
      a[68] != '0' + checksum(std::string_view(a).substr(0, 68)) ||
      b[68] != '0' + checksum(std::string_view(b).substr(0, 68)))
    return std::unexpected(error("TLE lines must have 69 characters and valid checksums"));
  if (a[62] != '0' && a[62] != ' ')
    return std::unexpected(error("Unsupported TLE ephemeris type; expected standard SGP4 type 0"));
  auto sat = number<std::uint32_t>(std::string_view(a).substr(2, 5));
  auto sat2 = number<std::uint32_t>(std::string_view(b).substr(2, 5));
  auto yy = number<int>(std::string_view(a).substr(18, 2));
  auto doy = number<int>(std::string_view(a).substr(20, 3));
  auto fraction = number<std::int64_t>(std::string_view(a).substr(24, 8));
  auto i = number<double>(std::string_view(b).substr(8, 8));
  auto raan = number<double>(std::string_view(b).substr(17, 8));
  auto ecc = number<int>(std::string_view(b).substr(26, 7));
  auto argp = number<double>(std::string_view(b).substr(34, 8));
  auto anomaly = number<double>(std::string_view(b).substr(43, 8));
  auto motion = number<double>(std::string_view(b).substr(52, 11));
  auto bs = parse_exponent(std::string_view(a).substr(53, 8));
  auto elnum = number<std::uint32_t>(std::string_view(a).substr(64, 4));
  auto rev = number<std::uint32_t>(std::string_view(b).substr(63, 5));
  if (!sat || !sat2 || *sat == 0 || *sat != *sat2 || !yy || !doy || !fraction || !i || !raan || !ecc ||
      !argp || !anomaly || !motion || !bs || !elnum || !rev || a[23] != '.' || *fraction < 0 || *yy < 0 ||
      *yy > 99 || *raan < 0 || *raan >= 360 || *argp < 0 || *argp >= 360 || *anomaly < 0 || *anomaly >= 360 ||
      (a[7] != 'U' && a[7] != 'C' && a[7] != 'S'))
    return std::unexpected(error("Invalid or unsupported TLE fields (numeric catalog IDs only)"));
  const std::chrono::year year{*yy >= 57 ? 1900 + *yy : 2000 + *yy};
  if (*doy < 1 || *doy > (year.is_leap() ? 366 : 365))
    return std::unexpected(error("TLE day of year outside calendar"));
  ElementSet set;
  set.elements = {Epoch{std::chrono::sys_days{year / 1 / 1} + std::chrono::days{*doy - 1} +
                        std::chrono::microseconds{*fraction * 864}},
                  *i * degree,
                  *raan * degree,
                  static_cast<double>(*ecc) * 1e-7,
                  *argp * degree,
                  *anomaly * degree,
                  *motion,
                  *bs};
  set.metadata = {*sat, "", "", a[7], *elnum, *rev};
  const auto intl = trim(std::string_view(a).substr(9, 8));
  if (!intl.empty()) {
    if (intl.size() < 5) return std::unexpected(error("Invalid international designator"));
    auto launch_year = number<int>(intl.substr(0, 2));
    if (!launch_year) return std::unexpected(launch_year.error());
    set.metadata.international_designator = std::format(
        "{:04}-{}", *launch_year >= 57 ? 1900 + *launch_year : 2000 + *launch_year, intl.substr(2));
  }
  if (auto v = validate(set.elements); !v) return std::unexpected(v.error());
  return set;
}
}  // namespace orbitfit
