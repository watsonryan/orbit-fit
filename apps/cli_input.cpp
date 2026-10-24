/**
 * @file cli_input.cpp
 * @brief Parse bounded YAML configuration and external CSV states with explicit units and replay snapshots.
 * @author watson
 */
#include "cli_input.hpp"

#include <yaml-cpp/eventhandler.h>
#include <yaml-cpp/mark.h>
#include <yaml-cpp/parser.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "cli_support.hpp"

namespace orbitfit::cli {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;  ///< Private intermediate configuration, not exposed in installed headers.
constexpr std::size_t maximum_job_bytes = 256 * 1024;  ///< Configuration/seed bound, independent of CSV size.
constexpr std::size_t maximum_csv_samples = 100000;    ///< Same sample cap as the numerical request contract.
/** @brief Required CSV header, with units supplied by the job configuration. */
constexpr std::array<std::string_view, 7> columns{"epoch", "x", "y", "z", "vx", "vy", "vz"};

/** @brief Unwrap a library result at the file-input boundary. @tparam T Owned value.
 * @param result Library outcome. @return Value. @throws Cancelled On cancellation.
 * @throws std::invalid_argument On other errors. */
template <class T>
T take(Result<T> result) {
  if (!result) {
    if (result.error().code == ErrorCode::cancelled) throw Cancelled();
    throw std::invalid_argument(result.error().message);
  }
  return std::move(*result);
}

/** @brief Trim CSV field padding and CRLF endings. @param text Borrowed bytes.
 * @return View without leading/trailing spaces, tabs, or carriage returns. */
std::string_view trim(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r");
  if (first == std::string_view::npos) return {};
  return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

/** @brief Decode a decimal scalar without locale, partial parses, or nonfinite values.
 * @param text Numeric bytes. @param where Diagnostic field/row name. @return Finite double.
 * @throws std::invalid_argument On malformed, out-of-range, or nonfinite input. */
double real(std::string_view text, std::string_view where) {
  text = trim(text);
  if (text.starts_with('+')) {
    text.remove_prefix(1);
    if (text.starts_with('-'))
      throw std::invalid_argument(std::format("{}: expected one numeric sign", where));
  }
  if (text.empty()) throw std::invalid_argument(std::format("{}: expected a finite decimal number", where));
  double value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value))
    throw std::invalid_argument(std::format("{}: expected a finite decimal number", where));
  return value;
}

/** @brief Preserve YAML scalar spelling while bounding the event stream before building a tree.
 * @details The job language uses mappings and scalar values. Rejecting aliases, anchors,
 * type/custom tags, and sequences prevents configuration indirection and duplicate/merge ambiguity.
 * Expected field types are applied later; names such as '123' remain strings. */
class JobEvents final : public YAML::EventHandler {
 public:
  /** @brief Bind cooperative cancellation. @param stop Operation token. */
  explicit JobEvents(std::stop_token stop) : stop_(stop) {}
  /** @brief Obtain the completed mapping. @return Owned string-valued JSON tree. */
  Json take_root() { return std::move(root_); }
  /** @brief Require one document. @param mark YAML source location. */
  void OnDocumentStart(const YAML::Mark& mark) override {
    tick(mark);
    if (started_) fail(mark, "Expected exactly one YAML document");
    started_ = true;
  }
  /** @brief Finish one complete mapping. */
  void OnDocumentEnd() override { checkpoint(stop_); }
  /** @brief Reject missing/null fields. @param mark Source location. @param anchor Unused anchor ID. */
  void OnNull(const YAML::Mark& mark, YAML::anchor_t anchor) override {
    (void)anchor;
    fail(mark, "Null or empty values are not supported");
  }
  /** @brief Reject alias references. @param mark Source location. @param anchor Unused anchor ID. */
  void OnAlias(const YAML::Mark& mark, YAML::anchor_t anchor) override {
    (void)anchor;
    fail(mark, "YAML aliases are not supported");
  }
  /** @brief Reject anchor definitions. @param mark Source location. @param name Unused anchor name. */
  void OnAnchor(const YAML::Mark& mark, const std::string& name) override {
    (void)name;
    fail(mark, "YAML anchors are not supported");
  }
  /** @brief Append a unique scalar key or value. @param mark Source location.
   * @param tag YAML tag. @param anchor Anchor ID. @param value Exact scalar spelling. */
  void OnScalar(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t anchor,
                const std::string& value) override {
    node(mark, tag, anchor);
    if (stack_.empty()) fail(mark, "Expected a YAML mapping");
    if (value.size() > 4096) fail(mark, "YAML scalar exceeds 4096 bytes");
    auto& frame = stack_.back();
    if (!frame.key) {
      if (value.empty() || value == "<<") fail(mark, "Empty or merge keys are not supported");
      if (frame.object->contains(value)) fail(mark, "Duplicate YAML key: " + value);
      frame.key = value;
    } else {
      (*frame.object)[*frame.key] = value;
      frame.key.reset();
    }
  }
  /** @brief Reject sequences in the job schema. @param mark Source location.
   * @param tag Unused tag. @param anchor Unused anchor ID. @param style Unused YAML style. */
  void OnSequenceStart(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t anchor,
                       YAML::EmitterStyle::value style) override {
    (void)tag;
    (void)anchor;
    (void)style;
    fail(mark, "YAML job fields must be mappings or scalars; samples belong in the CSV file");
  }
  /** @brief Unreachable because sequence starts are rejected. */
  void OnSequenceEnd() override { checkpoint(stop_); }
  /** @brief Start a bounded mapping. @param mark Source location. @param tag YAML tag.
   * @param anchor Anchor ID. @param style Unused YAML style. */
  void OnMapStart(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t anchor,
                  YAML::EmitterStyle::value style) override {
    (void)style;
    node(mark, tag, anchor);
    if (stack_.size() >= 8) fail(mark, "YAML nesting exceeds 8 levels");
    Json* object = &root_;
    if (!stack_.empty()) {
      auto& parent = stack_.back();
      if (!parent.key) fail(mark, "YAML keys must be scalars");
      object = &(*parent.object)[*parent.key];
      parent.key.reset();
    }
    *object = Json::object();
    stack_.push_back({object, std::nullopt});
  }
  /** @brief Close a completed mapping. */
  void OnMapEnd() override {
    checkpoint(stop_);
    stack_.pop_back();
  }

 private:
  /** @brief One open mapping; std::map-backed JSON retains child addresses during insertion. */
  struct Frame {
    Json* object;                    ///< Mapping owned by root_.
    std::optional<std::string> key;  ///< Key awaiting a scalar or child mapping value.
  };
  Json root_;                 ///< Owner of all parsed fields.
  std::vector<Frame> stack_;  ///< At most eight open mappings.
  std::stop_token stop_;      ///< Cancellation shared with file I/O and the solver.
  std::size_t events_{};      ///< Bounded scalar/map/document event count.
  bool started_{};            ///< Rejects additional YAML documents.
  /** @brief Report a location-aware configuration error. @param mark Source location.
   * @param message Diagnostic text. @throws std::invalid_argument Always. */
  [[noreturn]] void fail(const YAML::Mark& mark, std::string_view message) const {
    checkpoint(stop_);
    throw std::invalid_argument(
        std::format("YAML line {}, column {}: {}", mark.line + 1, mark.column + 1, message));
  }
  /** @brief Check cancellation and event count. @param mark Source location. */
  void tick(const YAML::Mark& mark) {
    checkpoint(stop_);
    if (++events_ > 512) fail(mark, "YAML configuration exceeds 512 events");
  }
  /** @brief Check non-specific tags and reject anchors. @param mark Source location.
   * @param tag YAML tag. @param anchor Anchor ID. */
  void node(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t anchor) {
    tick(mark);
    if (anchor != 0) fail(mark, "YAML anchors are not supported");
    if (tag != "?" && tag != "!") fail(mark, "YAML type/custom tags are not supported");
  }
};

/** @brief Parse one bounded job mapping. @param text YAML bytes already size-bounded.
 * @param stop Cooperative token. @return String-valued configuration.
 * @throws std::exception On syntax, schema-shape, or cancellation errors. */
Json parse_job(const std::string& text, std::stop_token stop) {
  checkpoint(stop);
  std::istringstream stream(text);
  YAML::Parser parser(stream);
  JobEvents events(stop);
  if (!parser.HandleNextDocument(events)) throw std::invalid_argument("YAML job is empty");
  (void)parser.HandleNextDocument(events);  // A second document fails in OnDocumentStart.
  return events.take_root();
}

/** @brief Enforce a closed configuration mapping. @param object Parsed mapping.
 * @param allowed Supported keys. @param where Field name used in errors.
 * @throws std::invalid_argument On a nonmapping or unknown field. */
void fields(const Json& object, std::initializer_list<std::string_view> allowed, std::string_view where) {
  if (!object.is_object()) throw std::invalid_argument(std::format("{} must be a mapping", where));
  for (const auto& [key, value] : object.items()) {
    (void)value;
    if (std::ranges::find(allowed, key) == allowed.end())
      throw std::invalid_argument(std::format("Unknown {} field: {}", where, key));
  }
}

/** @brief Read a required YAML scalar. @param object Parent mapping. @param key Required field.
 * @return Scalar spelling. @throws std::invalid_argument On missing/nonscalar fields. */
std::string scalar(const Json& object, std::string_view key) {
  if (!object.contains(key) || !object.at(key).is_string())
    throw std::invalid_argument(std::format("{} must be a scalar", key));
  return object.at(key).get<std::string>();
}

/** @brief Apply explicit integer types to known configuration fields. @param object Mutable mapping.
 * @param keys Integer field names; absent optional fields are unchanged.
 * @throws std::invalid_argument On anything other than a bounded unsigned decimal integer. */
void integers(Json& object, std::initializer_list<std::string_view> keys) {
  for (const auto key : keys) {
    if (!object.contains(key)) continue;
    const auto text = scalar(object, key);
    std::uint32_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size())
      throw std::invalid_argument(std::format("{} must be an unsigned decimal integer", key));
    object[key] = value;
  }
}

/** @brief Apply field types without YAML implicit typing of names/paths/dates. @param job Mutable job.
 * @throws std::invalid_argument On malformed booleans, integers, or real numbers. */
void type_configuration(Json& job) {
  auto& metadata = job.at("metadata");
  if (!metadata.is_object()) throw std::invalid_argument("metadata must be a mapping");
  integers(metadata, {"catalog_number", "element_set_number", "revolution_number"});
  auto& fit = job.at("fit");
  if (!fit.is_object()) throw std::invalid_argument("fit must be a mapping");
  integers(fit, {"maximum_samples", "maximum_iterations"});
  for (const auto key : {"fit_bstar", "use_velocity"}) {
    if (!fit.contains(key)) continue;
    const auto value = scalar(fit, key);
    if (value != "true" && value != "false")
      throw std::invalid_argument(std::format("{} must be true or false", key));
    fit[key] = value == "true";
  }
  for (const auto key :
       {"fixed_bstar", "position_scale_m", "velocity_scale_mps", "huber_threshold", "relative_cost_tolerance",
        "absolute_cost_tolerance", "gradient_tolerance", "maximum_rms_position_m"}) {
    if (fit.contains(key)) fit[key] = real(scalar(fit, key), key);
  }
  if (fit.contains("weighting") && scalar(fit, "weighting") == "independent_measurements")
    throw std::invalid_argument(
        "CSV states have no measurement covariance; use approximation weighting or "
        "an inline JSON request with covariance");
}

/** @brief Resolve a literal UTF-8 path against the configuration directory. @param config YAML file path.
 * @param value Scalar path, with no environment or tilde expansion. @return Absolute normalized path.
 * @throws std::invalid_argument On an empty or NUL-containing path. */
fs::path relative_file(const fs::path& config, const std::string& value) {
  if (value.empty() || value.find('\0') != std::string::npos)
    throw std::invalid_argument("Input file path must be nonempty and contain no NUL bytes");
  const fs::path path(std::u8string(value.begin(), value.end()));
  return fs::absolute(path.is_absolute() ? path : config.parent_path() / path).lexically_normal();
}

/** @brief Split the seven scalar CSV columns, accepting optional surrounding quotes and padding.
 * @param line One noncomment physical line. @param number One-based line number.
 * @return Borrowed field views without outer quotes. @throws std::invalid_argument On wrong field count,
 * embedded quotes, or quoting that requires a multiline/comma-containing scalar. */
std::array<std::string_view, 7> csv_fields(std::string_view line, std::size_t number) {
  std::array<std::string_view, 7> result{};
  for (std::size_t column = 0; column < result.size(); ++column) {
    const auto comma = line.find(',');
    if ((column < result.size() - 1) != (comma != std::string_view::npos))
      throw std::invalid_argument(std::format("CSV line {}: expected exactly seven columns", number));
    auto value = trim(line.substr(0, comma));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value.remove_prefix(1);
      value.remove_suffix(1);
      value = trim(value);
    }
    if (value.find('"') != std::string_view::npos)
      throw std::invalid_argument(
          std::format("CSV line {}, column {}: invalid scalar quoting", number, columns[column]));
    result[column] = value;
    if (comma != std::string_view::npos) line.remove_prefix(comma + 1);
  }
  return result;
}

/** @brief Parse ordered states and normalize declared units to SI. @param text Bounded CSV bytes.
 * @param position_scale Metres per input position unit. @param velocity_scale Metres/second per input unit.
 * @param stop Cooperative token. @return JSON sample array for the shared request reader.
 * @throws std::invalid_argument On row/header/time/number/bound violations. @throws Cancelled On stop. */
Json csv_samples(std::string_view text, double position_scale, double velocity_scale, std::stop_token stop) {
  Json samples = Json::array();
  std::optional<Epoch> previous;
  std::size_t number{};
  bool header{};
  if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
  while (!text.empty()) {
    checkpoint(stop);
    const auto newline = text.find('\n');
    auto line = text.substr(0, newline);
    text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
    ++number;
    if (line.size() > 4096)
      throw std::invalid_argument(std::format("CSV line {} exceeds 4096 bytes", number));
    line = trim(line);
    if (line.empty() || line.starts_with('#')) continue;
    const auto values = csv_fields(line, number);
    if (!header) {
      if (values != columns)
        throw std::invalid_argument(std::format("CSV line {}: header must be epoch,x,y,z,vx,vy,vz", number));
      header = true;
      continue;
    }
    if (samples.size() == maximum_csv_samples) throw std::invalid_argument("CSV exceeds 100000 samples");
    const auto epoch = parse_epoch(values[0]);
    if (!epoch)
      throw std::invalid_argument(
          std::format("CSV line {}, column epoch: {}", number, epoch.error().message));
    if (previous && *epoch <= *previous)
      throw std::invalid_argument(std::format("CSV line {}: epochs must be strictly increasing", number));
    previous = *epoch;
    Vector3 position{}, velocity{};
    for (std::size_t i = 0; i < 6; ++i) {
      const auto where = std::format("CSV line {}, column {}", number, columns[i + 1]);
      const double value = real(values[i + 1], where) * (i < 3 ? position_scale : velocity_scale);
      if (!std::isfinite(value)) throw std::invalid_argument(where + ": SI conversion overflow");
      (i < 3 ? position[i] : velocity[i - 3]) = value;
    }
    samples.push_back(
        {{"epoch", std::string(values[0])}, {"position_m", position}, {"velocity_mps", velocity}});
  }
  if (!header || samples.size() < 3)
    throw std::invalid_argument("CSV requires a header and at least three states");
  checkpoint(stop);
  return samples;
}
}  // namespace

LoadedRequest load_request(const std::filesystem::path& path, std::stop_token stop) {
  auto input = read_file(path, stop);
  auto request = take(read_request(input, stop));
  return {std::move(request), std::move(input), std::nullopt};
}

LoadedRequest load_job(const std::filesystem::path& path, std::stop_token stop) {
  try {
    JobSource source;
    source.yaml = read_file(path, stop, maximum_job_bytes);
    auto job = parse_job(source.yaml, stop);
    fields(job, {"schema", "input", "metadata", "fit", "seed_omm"}, "job");
    if (scalar(job, "schema") != "orbit-fit.job.v1")
      throw std::invalid_argument("Expected schema orbit-fit.job.v1");
    const auto& input = job.at("input");
    fields(input, {"path", "format", "frame", "time_system", "position_unit", "velocity_unit"}, "input");
    if (scalar(input, "format") != "csv") throw std::invalid_argument("input.format must be csv");
    if (scalar(input, "frame") != "TEME" || scalar(input, "time_system") != "UTC")
      throw std::invalid_argument("YAML jobs require explicit TEME and UTC input");
    const auto position_unit = scalar(input, "position_unit");
    const auto velocity_unit = scalar(input, "velocity_unit");
    if (position_unit != "m" && position_unit != "km")
      throw std::invalid_argument("input.position_unit must be m or km");
    if (velocity_unit != "m/s" && velocity_unit != "km/s")
      throw std::invalid_argument("input.velocity_unit must be m/s or km/s");
    type_configuration(job);
    source.data_path = relative_file(path, scalar(input, "path"));
    source.csv = read_file(source.data_path, stop);
    Json samples;
    try {
      samples = csv_samples(source.csv, position_unit == "km" ? 1000.0 : 1.0,
                            velocity_unit == "km/s" ? 1000.0 : 1.0, stop);
    } catch (const Cancelled&) {
      throw;
    } catch (const std::exception& error) {
      throw std::invalid_argument(source.data_path.string() + ": " + error.what());
    }
    Json request{{"schema", "orbit-fit.request.v1"},
                 {"frame", "TEME"},
                 {"time_system", "UTC"},
                 {"metadata", std::move(job["metadata"])},
                 {"fit", std::move(job["fit"])},
                 {"samples", std::move(samples)}};
    if (job.contains("seed_omm")) {
      const auto seed = take(
          from_omm_json(read_file(relative_file(path, scalar(job, "seed_omm")), stop, maximum_job_bytes)));
      checkpoint(stop);
      request["seed_omm"] = Json::parse(take(to_omm_json(seed)));
    }
    auto snapshot = request.dump() + "\n";
    auto parsed = take(read_request(snapshot, stop));
    return {std::move(parsed), std::move(snapshot), std::move(source)};
  } catch (const Cancelled&) {
    throw;
  } catch (const std::exception& error) {
    throw std::invalid_argument(path.string() + ": " + error.what());
  }
}

ExampleJob make_example_job() {
  const auto request = Json::parse(take(make_example_request()));
  ExampleJob result;
  result.yaml = R"(# @file fit.yaml
# @brief Fit the synthetic TEME states in states.csv and validate TLE/OMM delivery.
# @author watson
schema: orbit-fit.job.v1

input:
  path: states.csv  # Resolved relative to this YAML file.
  format: csv
  frame: TEME
  time_system: UTC
  position_unit: m
  velocity_unit: m/s

metadata:
  catalog_number: 99999
  object_name: SYNTHETIC EXAMPLE

fit:
  start: 2026-01-01T00:00:00Z
  stop: 2026-01-01T02:00:00Z
  epoch: 2026-01-01T00:00:00Z
  fit_bstar: false
  use_velocity: true
  weighting: approximation
  position_scale_m: 100
  velocity_scale_mps: 0.1
  maximum_rms_position_m: 10
)";
  result.csv =
      "# @file states.csv\n"
      "# @brief Synthetic TEME SI states generated by the native Vallado SGP4 example.\n"
      "# @author watson\n"
      "epoch,x,y,z,vx,vy,vz\n";
  for (const auto& sample : request.at("samples")) {
    result.csv += sample.at("epoch").get<std::string>();
    for (const auto key : {"position_m", "velocity_mps"})
      for (const auto& value : sample.at(key)) result.csv += std::format(",{:.17g}", value.get<double>());
    result.csv += '\n';
  }
  return result;
}
}  // namespace orbitfit::cli
