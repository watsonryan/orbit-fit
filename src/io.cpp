/**
 * @file io.cpp
 * @brief Validate request and OMM JSON and encode reproducible diagnostic reports.
 * @author watson
 */
#include "orbitfit/io.hpp"

#include <ceres/version.h>

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <numbers>
#include <set>
#include <stdexcept>

namespace orbitfit {
namespace {
using Json = nlohmann::json;  ///< Private JSON representation, never exposed in installed signatures.
constexpr double degree = std::numbers::pi / 180;  ///< Radians per degree in the GP vocabulary.

/** @brief Private unwind marker for cooperative JSON parsing cancellation. */
struct ParsingCancelled {};

/** @brief Parse bounded JSON and reject duplicate object keys. @param text At most 64 MiB and depth 32.
 * @param stop Cooperative cancellation checked before parsing and at each parser event.
 * @return JSON tree. @throws std::invalid_argument On bounds/duplicates. @throws Json::exception On malformed
 * JSON. @throws ParsingCancelled On a requested stop. */
Json parse(std::string_view text, std::stop_token stop = {}) {
  if (stop.stop_requested()) throw ParsingCancelled{};
  if (text.size() > 64 * 1024 * 1024) throw std::invalid_argument("JSON input exceeds 64 MiB");
  std::vector<std::set<std::string>> keys;
  return Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
    if (stop.stop_requested()) throw ParsingCancelled{};
    if (depth > 32) throw std::invalid_argument("JSON nesting exceeds 32 levels");
    if (event == Json::parse_event_t::object_start) keys.emplace_back();
    if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
      throw std::invalid_argument("Duplicate JSON key: " + value.get<std::string>());
    if (event == Json::parse_event_t::object_end) keys.pop_back();
    return true;
  });
}
/** @brief Enforce a closed object vocabulary. @param j JSON object. @param allowed Allowed property names.
 * @throws std::invalid_argument On wrong type or an unknown property. */
void fields(const Json& j, std::initializer_list<std::string_view> allowed) {
  if (!j.is_object()) throw std::invalid_argument("Expected a JSON object");
  for (const auto& [key, value] : j.items()) {
    (void)value;
    if (std::ranges::find(allowed, key) == allowed.end())
      throw std::invalid_argument("Unknown field: " + key);
  }
}
/** @brief Read a finite real value. @param j Numeric JSON value. @return Finite double. @throws
 * std::invalid_argument On wrong type or non-finite value. */
double real(const Json& j) {
  if (!j.is_number()) throw std::invalid_argument("Expected a numeric value");
  const double x = j.get<double>();
  if (!std::isfinite(x)) throw std::invalid_argument("Expected a finite number");
  return x;
}
/** @brief Read a bounded nonnegative integer. @param j Integer JSON value. @param maximum Inclusive upper
 * limit. @return Unsigned integer. @throws std::invalid_argument On wrong type, sign, or range. */
std::uint32_t integer(const Json& j, std::uint32_t maximum) {
  if (!j.is_number_integer() ||
      (j.is_number_integer() && !j.is_number_unsigned() && j.get<std::int64_t>() < 0) ||
      j.get<std::uint64_t>() > maximum)
    throw std::invalid_argument("Integer outside supported range");
  return j.get<std::uint32_t>();
}
/** @brief Decode a timestamp property. @param j String timestamp. @param omm Allow omitted final Z under
 * explicit OMM UTC metadata. @return Parsed epoch. @throws std::invalid_argument On invalid timestamp.
 * @throws Json::exception On wrong type. */
Epoch epoch(const Json& j, bool omm = false) {
  auto s = j.get<std::string>();
  if (omm && !s.empty() && s.back() != 'Z') s += 'Z';
  auto parsed = parse_epoch(s);
  if (!parsed) throw std::invalid_argument(parsed.error().message);
  return *parsed;
}
/** @brief Enforce GP identity ranges and text vocabulary. @param m Catalog metadata. @throws
 * std::invalid_argument On invalid fields or control characters. */
void validate_metadata(const Metadata& m) {
  if (m.catalog_number == 0 || m.catalog_number > 999999999 || m.element_set_number > 999999999 ||
      m.revolution_number > 999999999 ||
      std::string_view("UCS").find(m.classification) == std::string_view::npos)
    throw std::invalid_argument(
        "Catalog/element/revolution numbers must fit the GP JSON profile, with classification U, C, or S");
  if (m.object_name.size() > 128 ||
      std::ranges::any_of(m.object_name, [](unsigned char c) { return c < 32 || c == 127; }))
    throw std::invalid_argument("Invalid object name");
  const auto& id = m.international_designator;
  if (!id.empty() &&
      (id.size() < 9 || id.size() > 11 || id[4] != '-' ||
       !std::ranges::all_of(id.substr(0, 4) + id.substr(5, 3), [](char c) { return c >= '0' && c <= '9'; }) ||
       !std::ranges::all_of(id.substr(8), [](char c) { return c >= 'A' && c <= 'Z'; })))
    throw std::invalid_argument("International designator must be YYYY-NNNPPP or empty");
}
/** @brief Decode closed request metadata with defaults. @param j Metadata object. @return Validated identity.
 * @throws std::invalid_argument On invalid fields. @throws Json::exception On missing/wrong-type properties.
 */
Metadata metadata(const Json& j) {
  fields(j, {"catalog_number", "object_name", "international_designator", "classification",
             "element_set_number", "revolution_number"});
  const auto classification = j.value("classification", std::string("U"));
  if (classification.size() != 1) throw std::invalid_argument("Invalid classification");
  Metadata m{integer(j.at("catalog_number"), 999999999),
             j.value("object_name", std::string{}),
             j.value("international_designator", std::string{}),
             classification.front(),
             integer(j.value("element_set_number", Json(1)), 999999999),
             integer(j.value("revolution_number", Json(0)), 999999999)};
  validate_metadata(m);
  return m;
}
/** @brief Encode request/report identity. @param m Validated metadata. @return JSON object; no revalidation
 * is performed. */
Json metadata_json(const Metadata& m) {
  return {{"catalog_number", m.catalog_number},
          {"object_name", m.object_name},
          {"international_designator", m.international_designator},
          {"classification", std::string(1, m.classification)},
          {"element_set_number", m.element_set_number},
          {"revolution_number", m.revolution_number}};
}
/** @brief Read three finite components. @param j Three-element array. @return Cartesian vector. @throws
 * std::invalid_argument On invalid size/value. @throws Json::exception On incompatible type. */
Vector3 vector3(const Json& j) {
  if (!j.is_array() || j.size() != 3) throw std::invalid_argument("State vectors must have three components");
  return {real(j[0]), real(j[1]), real(j[2])};
}
/** @brief Encode unweighted physical statistics. @param m Completed evaluation metrics. @return SI metrics
 * and sample count. */
Json metrics_json(const Metrics& m) {
  return {{"sample_count", m.sample_count},
          {"rms_position_m", m.rms_position_m},
          {"max_position_m", m.max_position_m},
          {"rms_velocity_mps", m.rms_velocity_mps},
          {"max_velocity_mps", m.max_velocity_mps}};
}
/** @brief Reduce a finite angle before degree conversion. @param x Radians. @return Degrees in [0,360). */
double degrees(double x) {
  double d = std::fmod(x, 2 * std::numbers::pi) / degree;
  return d < 0 ? d + 360.0 : d;
}
}  // namespace

Result<FitRequest> read_request(std::string_view text, std::stop_token stop) {
  try {
    const auto j = parse(text, stop);
    fields(j, {"schema", "frame", "time_system", "metadata", "fit", "seed_omm", "samples"});
    if (j.at("schema") != "orbit-fit.request.v1" || j.at("time_system") != "UTC")
      throw std::invalid_argument("Expected orbit-fit.request.v1 with time_system UTC");
    if (j.at("frame") != "TEME")
      return std::unexpected(
          Error{ErrorCode::unsupported_frame, "Only explicitly declared TEME samples are supported"});
    FitRequest request;
    request.metadata = metadata(j.at("metadata"));
    auto& o = request.options;
    o.stop_token = stop;
    const auto& f = j.at("fit");
    fields(f, {"start", "stop", "epoch", "fit_bstar", "fixed_bstar", "use_velocity", "weighting",
               "derivatives", "position_scale_m", "velocity_scale_mps", "huber_threshold", "maximum_samples",
               "maximum_iterations", "relative_cost_tolerance", "absolute_cost_tolerance",
               "gradient_tolerance", "maximum_rms_position_m"});
    o.start = epoch(f.at("start"));
    o.stop = epoch(f.at("stop"));
    o.epoch = epoch(f.at("epoch"));
    o.fit_bstar = f.value("fit_bstar", o.fit_bstar);
    o.use_velocity = f.value("use_velocity", o.use_velocity);
    o.fixed_bstar = real(f.value("fixed_bstar", Json(o.fixed_bstar)));
    o.position_scale_m = real(f.value("position_scale_m", Json(o.position_scale_m)));
    o.velocity_scale_mps = real(f.value("velocity_scale_mps", Json(o.velocity_scale_mps)));
    o.relative_cost_tolerance = real(f.value("relative_cost_tolerance", Json(o.relative_cost_tolerance)));
    o.absolute_cost_tolerance = real(f.value("absolute_cost_tolerance", Json(o.absolute_cost_tolerance)));
    o.gradient_tolerance = real(f.value("gradient_tolerance", Json(o.gradient_tolerance)));
    o.maximum_rms_position_m = real(f.value("maximum_rms_position_m", Json(o.maximum_rms_position_m)));
    o.maximum_samples = integer(f.value("maximum_samples", Json(o.maximum_samples)), 100000);
    o.maximum_iterations = integer(f.value("maximum_iterations", Json(o.maximum_iterations)), 10000);
    if (f.contains("huber_threshold")) o.huber_threshold = real(f.at("huber_threshold"));
    const auto weighting = f.value("weighting", std::string("approximation"));
    if (weighting == "approximation")
      o.weighting = Weighting::approximation;
    else if (weighting == "independent_measurements")
      o.weighting = Weighting::independent_measurements;
    else
      throw std::invalid_argument("Unknown weighting mode");
    const auto derivatives = f.value("derivatives", std::string("automatic"));
    if (derivatives == "automatic")
      o.derivatives = Derivatives::automatic;
    else if (derivatives == "finite_difference")
      o.derivatives = Derivatives::finite_difference;
    else
      throw std::invalid_argument("Unknown derivative method");
    if (j.contains("seed_omm")) {
      auto seed = from_omm_json(j.at("seed_omm").dump());
      if (!seed) return std::unexpected(seed.error());
      if (seed->metadata.catalog_number != request.metadata.catalog_number)
        throw std::invalid_argument("Seed catalog number must match request metadata");
      o.seed = seed->elements;
    }
    const auto& samples = j.at("samples");
    if (!samples.is_array() || samples.size() < 3 || samples.size() > 100000)
      throw std::invalid_argument("Expected 3..100000 state samples");
    request.samples.reserve(samples.size());
    for (const auto& s : samples) {
      if (stop.stop_requested()) throw ParsingCancelled{};
      fields(s, {"epoch", "position_m", "velocity_mps", "covariance"});
      Sample sample{epoch(s.at("epoch")),
                    {vector3(s.at("position_m")), vector3(s.at("velocity_mps"))},
                    Frame::teme,
                    std::nullopt};
      if (s.contains("covariance")) {
        const auto& c = s.at("covariance");
        if (!c.is_array() || c.size() != 36)
          throw std::invalid_argument("Covariance must contain 36 row-major entries");
        sample.covariance.emplace();
        for (std::size_t i = 0; i < 36; ++i) (*sample.covariance)[i] = real(c[i]);
      }
      request.samples.push_back(sample);
    }
    if (stop.stop_requested()) throw ParsingCancelled{};
    return request;
  } catch (const ParsingCancelled&) {
    return std::unexpected(Error{ErrorCode::cancelled, "Request parsing cancelled"});
  } catch (const std::exception& ex) {
    return std::unexpected(Error{ErrorCode::invalid_input, ex.what()});
  }
}

Result<ElementSet> from_omm_json(std::string_view text) {
  try {
    const auto j = parse(text);
    if (j.at("CENTER_NAME") != "EARTH" || j.at("REF_FRAME") != "TEME" || j.at("TIME_SYSTEM") != "UTC" ||
        j.at("MEAN_ELEMENT_THEORY") != "SGP4" || integer(j.value("EPHEMERIS_TYPE", Json(0)), 0) != 0)
      throw std::invalid_argument("OMM requires EARTH, TEME, UTC, SGP4, and ephemeris type 0");
    const auto classification = j.value("CLASSIFICATION_TYPE", std::string("U"));
    if (classification.size() != 1) throw std::invalid_argument("Invalid classification");
    ElementSet set;
    set.metadata = {integer(j.at("NORAD_CAT_ID"), 999999999),
                    j.value("OBJECT_NAME", std::string{}),
                    j.value("OBJECT_ID", std::string{}),
                    classification.front(),
                    integer(j.value("ELEMENT_SET_NO", Json(1)), 999999999),
                    integer(j.value("REV_AT_EPOCH", Json(0)), 999999999)};
    validate_metadata(set.metadata);
    set.elements = {epoch(j.at("EPOCH"), true),
                    real(j.at("INCLINATION")) * degree,
                    real(j.at("RA_OF_ASC_NODE")) * degree,
                    real(j.at("ECCENTRICITY")),
                    real(j.at("ARG_OF_PERICENTER")) * degree,
                    real(j.at("MEAN_ANOMALY")) * degree,
                    real(j.at("MEAN_MOTION")),
                    real(j.at("BSTAR"))};
    for (auto key : {"MEAN_MOTION_DOT", "MEAN_MOTION_DDOT"})
      if (j.contains(key)) (void)real(j.at(key));  // Not used by SGP4; exports explicitly write zero.
    if (auto v = validate(set.elements); !v) return std::unexpected(v.error());
    return set;
  } catch (const std::exception& ex) {
    return std::unexpected(Error{ErrorCode::invalid_input, ex.what()});
  }
}

Result<std::string> to_omm_json(const ElementSet& set) {
  if (auto v = validate(set.elements); !v) return std::unexpected(v.error());
  try {
    const auto& e = set.elements;
    const auto& m = set.metadata;
    validate_metadata(m);
    auto stamp = format_epoch(e.epoch);
    stamp.pop_back();  // TIME_SYSTEM declares UTC in the GP JSON profile.
    return Json{{"OBJECT_NAME", m.object_name},
                {"OBJECT_ID", m.international_designator},
                {"CENTER_NAME", "EARTH"},
                {"REF_FRAME", "TEME"},
                {"TIME_SYSTEM", "UTC"},
                {"MEAN_ELEMENT_THEORY", "SGP4"},
                {"EPOCH", stamp},
                {"MEAN_MOTION", e.mean_motion_rev_per_day},
                {"ECCENTRICITY", e.eccentricity},
                {"INCLINATION", e.inclination_rad / degree},
                {"RA_OF_ASC_NODE", degrees(e.raan_rad)},
                {"ARG_OF_PERICENTER", degrees(e.argument_of_perigee_rad)},
                {"MEAN_ANOMALY", degrees(e.mean_anomaly_rad)},
                {"EPHEMERIS_TYPE", 0},
                {"CLASSIFICATION_TYPE", std::string(1, m.classification)},
                {"NORAD_CAT_ID", m.catalog_number},
                {"ELEMENT_SET_NO", m.element_set_number},
                {"REV_AT_EPOCH", m.revolution_number},
                {"BSTAR", e.bstar},
                {"MEAN_MOTION_DOT", 0.0},
                {"MEAN_MOTION_DDOT", 0.0}}
               .dump(2) +
           "\n";
  } catch (const std::exception& ex) {
    return std::unexpected(Error{ErrorCode::serialization, ex.what()});
  }
}

std::string report_json(const FitResult& r, const Metadata& m, const FitOptions& o,
                        const Delivery& delivery) {
  Json residuals = Json::array();
  for (const auto& v : r.residuals)
    residuals.push_back(
        {{"epoch", format_epoch(v.epoch)}, {"position_m", v.position_m}, {"velocity_mps", v.velocity_mps}});
  Json report{
      {"schema", "orbit-fit.report.v2"},
      {"version", ORBITFIT_VERSION},
      {"metadata", metadata_json(m)},
      {"termination", to_string(r.termination)},
      {"accepted", delivery.accepted},
      {"reason", to_string(delivery.reason)},
      {"product_policy", to_string(delivery.policy)},
      {"continuous_accepted", delivery.continuous_accepted},
      {"iterations", r.iterations},
      {"fitted_samples", r.fitted_samples},
      {"parameter_count", r.parameter_count},
      {"rank", r.rank},
      {"condition_number", std::isfinite(r.condition_number) ? Json(r.condition_number) : Json(nullptr)},
      {"initial_cost", r.initial_cost},
      {"final_cost", r.final_cost},
      {"continuous_metrics", metrics_json(delivery.continuous_metrics)},
      {"residuals", std::move(residuals)},
      {"diagnostics", r.diagnostics},
      {"fit",
       {{"start", format_epoch(o.start)},
        {"stop", format_epoch(o.stop)},
        {"epoch", format_epoch(o.epoch)},
        {"weighting", o.weighting == Weighting::approximation ? "approximation" : "independent_measurements"},
        {"derivatives", o.derivatives == Derivatives::automatic ? "automatic" : "finite_difference"},
        {"fit_bstar", o.fit_bstar},
        {"fixed_bstar", o.fixed_bstar},
        {"use_velocity", o.use_velocity},
        {"position_scale_m", o.position_scale_m},
        {"velocity_scale_mps", o.velocity_scale_mps},
        {"huber_threshold", o.huber_threshold ? Json(*o.huber_threshold) : Json(nullptr)},
        {"maximum_samples", o.maximum_samples},
        {"maximum_iterations", o.maximum_iterations},
        {"relative_cost_tolerance", o.relative_cost_tolerance},
        {"absolute_cost_tolerance", o.absolute_cost_tolerance},
        {"gradient_tolerance", o.gradient_tolerance},
        {"maximum_rms_position_m", o.maximum_rms_position_m}}},
      {"model",
       {{"frame", "TEME"},
        {"time_system", "UTC; 86400 model seconds per civil day"},
        {"gravity", "WGS72"},
        {"operation_mode", "i"},
        {"propagator", "Vallado SGP4 2025-12-30"},
        {"propagator_commit", "49df0479c950917c0a2c5ac31f2db6477bba57f6"},
        {"optimizer", "Ceres LM / DENSE_QR"},
        {"ceres_version", CERES_VERSION_STRING},
        {"compiler", ORBITFIT_COMPILER},
        {"jacobian", o.derivatives == Derivatives::automatic
                         ? "forward automatic differentiation / Ceres Jet " CERES_VERSION_STRING
                         : "scaled central finite difference"},
        {"gradient_scaling", "whitened Jacobian column norms"},
        {"rank_relative_threshold", 1e-8}}}};
  const auto product = [](const ProductValidation& validation, bool available) {
    return Json{{"available", available},
                {"accepted", validation.status == ProductStatus::accepted},
                {"status", to_string(validation.status)},
                {"metrics", validation.metrics ? metrics_json(*validation.metrics) : Json(nullptr)},
                {"error", validation.message}};
  };
  report["serialized_omm"] = product(delivery.omm, delivery.omm_json.has_value());
  report["serialized_tle"] = product(delivery.tle, delivery.tle_text.has_value());
  return report.dump(2) + "\n";
}

Result<std::string> make_example_request() {
  const auto start = parse_epoch("2026-01-01T00:00:00Z");
  const Elements truth{*start, 0.9, 1.2, 0.005, 0.4, 2.1, 14.8, 0.0};
  Json samples = Json::array();
  for (int i = 0; i <= 60; ++i) {
    const auto t = *add_seconds(*start, i * 120.0);
    const auto state = propagate(truth, t);
    if (!state) return std::unexpected(state.error());
    samples.push_back({{"epoch", format_epoch(t)},
                       {"position_m", state->position_m},
                       {"velocity_mps", state->velocity_mps}});
  }
  return Json{{"schema", "orbit-fit.request.v1"},
              {"frame", "TEME"},
              {"time_system", "UTC"},
              {"metadata", {{"catalog_number", 99999}, {"object_name", "SYNTHETIC EXAMPLE"}}},
              {"fit",
               {{"start", format_epoch(*start)},
                {"stop", format_epoch(*add_seconds(*start, 7200))},
                {"epoch", format_epoch(*start)},
                {"maximum_rms_position_m", 10.0}}},
              {"samples", std::move(samples)}}
             .dump(2) +
         "\n";
}
}  // namespace orbitfit
