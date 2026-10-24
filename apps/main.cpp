/**
 * @file main.cpp
 * @brief Run cancellable fits with application diagnostics and validated delivery.
 * @author watson
 */
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <print>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "cli_input.hpp"
#include "cli_support.hpp"
#include "orbitfit/io.hpp"

namespace {
namespace fs = std::filesystem;
namespace cli = orbitfit::cli;
/** @brief Command syntax, units, delivery policy, and exit-code contract. */
constexpr auto help = R"(orbit-fit: native C++23 SGP4 fitting with Ceres
Usage:
  orbit-fit example --output-dir example
  orbit-fit fit --config fit.yaml --output-dir results
               [--product-policy tle-required|omm-sufficient]
               [--log-level trace|debug|info|warn|error|critical|off]
  orbit-fit example --output request.json
  orbit-fit fit --input request.json --output-dir results
               [--product-policy tle-required|omm-sufficient]
               [--log-level trace|debug|info|warn|error|critical|off]
  orbit-fit --version
  orbit-fit --help

Input: orbit-fit.job.v1 YAML points to a CSV with epoch,x,y,z,vx,vy,vz columns.
       CSV paths are relative to the YAML file; frame, UTC, and units are explicit.
       --input alternatively accepts self-contained orbit-fit.request.v1 JSON in SI units.
Output: elements.omm.json and elements.tle when serializable, report.json,
        and replayable request.json. YAML jobs also retain input.yaml and states.csv.
        The output directory must not exist.
Default policy: continuous fit, OMM, and TLE must all pass their gates.
omm-sufficient requires the continuous fit and OMM; TLE issues remain in the report.
Exit codes: 0 accepted, 1 input/I/O/solver error, 2 rejected, 130 cancelled.
SIGINT and SIGTERM request cooperative cancellation; incomplete output is not published.
Diagnostics: spdlog to stderr, UTC timestamps, default warn; results/help use stdout.
--log-level controls fit diagnostics, never the exit status or output products.
The example is synthetic. Catalog identity must be supplied for real objects.
)";

/** @brief Unwrap a result at the CLI boundary. @tparam T Value type. @param result Library outcome. @return
 * Owned value. @throws cli::Cancelled On cancellation. @throws std::runtime_error On other errors. */
template <class T>
T take(orbitfit::Result<T> result) {
  if (!result) {
    if (result.error().code == orbitfit::ErrorCode::cancelled) throw cli::Cancelled();
    throw std::runtime_error(result.error().message);
  }
  return std::move(*result);
}

/** @brief Parse a fit logging threshold without silently mapping typos to off.
 * @param name Exact lowercase level name. @return spdlog threshold.
 * @throws std::invalid_argument On an unsupported name. */
spdlog::level::level_enum log_level(std::string_view name) {
  if (name == "trace") return spdlog::level::trace;
  if (name == "debug") return spdlog::level::debug;
  if (name == "info") return spdlog::level::info;
  if (name == "warn") return spdlog::level::warn;
  if (name == "error") return spdlog::level::err;
  if (name == "critical") return spdlog::level::critical;
  if (name == "off") return spdlog::level::off;
  throw std::invalid_argument("Unknown log level: " + std::string(name));
}

/** @brief Parsed fit command; no filesystem side effects occur during parsing. */
struct FitArguments {
  fs::path input;                                                         ///< Existing regular request file.
  fs::path config;                                                        ///< YAML job; exclusive with input.
  fs::path output;                                                        ///< New delivery directory.
  orbitfit::ProductPolicy policy{orbitfit::ProductPolicy::tle_required};  ///< Required products.
  spdlog::level::level_enum level{
      spdlog::level::warn};  ///< Minimum diagnostic severity; output is independent.
};

/** @brief Parse unique options in any order. @param argc Argument count. @param argv Argument vector
 * beginning with executable and fit. @return Fit arguments. @throws std::invalid_argument On missing,
 * duplicate, or unknown options. */
FitArguments arguments(int argc, char** argv) {
  FitArguments result;
  std::set<std::string_view> seen;
  for (int i = 2; i < argc; i += 2) {
    const std::string_view key(argv[i]);
    if (i + 1 == argc || !seen.insert(key).second)
      throw std::invalid_argument("Missing value or duplicate option: " + std::string(key));
    const std::string_view value(argv[i + 1]);
    if (value.empty()) throw std::invalid_argument("Empty option value");
    if (key == "--input")
      result.input = argv[i + 1];
    else if (key == "--config")
      result.config = argv[i + 1];
    else if (key == "--output-dir")
      result.output = argv[i + 1];
    else if (key == "--log-level")
      result.level = log_level(value);
    else if (key == "--product-policy") {
      if (value == "tle-required")
        result.policy = orbitfit::ProductPolicy::tle_required;
      else if (value == "omm-sufficient")
        result.policy = orbitfit::ProductPolicy::omm_sufficient;
      else
        throw std::invalid_argument("Unknown product policy: " + std::string(value));
    } else
      throw std::invalid_argument("Unknown option: " + std::string(key));
  }
  if (result.input.empty() == result.config.empty() || result.output.empty())
    throw std::invalid_argument("fit requires exactly one of --config or --input, and --output-dir");
  return result;
}
/** @brief Execute one CLI operation. @param argc Argument count. @param argv Argument vector.
 * @param log Owned by main, used only in normal execution context. @return 0
 * accepted, 1 error, 2 rejected, or 130 cancelled. */
int run(int argc, char** argv, spdlog::logger& log) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
      std::println("orbit-fit {}", ORBITFIT_VERSION);
      return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
      std::print("{}", help);
      return 0;
    }
    if (argc == 4 && std::string_view(argv[1]) == "example" && std::string_view(argv[2]) == "--output-dir") {
      const auto example = cli::make_example_job();
      cli::StagingDirectory staging(argv[3]);
      cli::write_file(staging.path() / "fit.yaml", example.yaml);
      cli::write_file(staging.path() / "states.csv", example.csv);
      staging.publish();
      std::println("Wrote synthetic YAML/CSV example to {}", argv[3]);
      return 0;
    }
    if (argc == 4 && std::string_view(argv[1]) == "example" && std::string_view(argv[2]) == "--output") {
      cli::write_file(argv[3], take(orbitfit::make_example_request()));
      std::println("Wrote synthetic example to {}", argv[3]);
      return 0;
    }
    if (argc < 2 || std::string_view(argv[1]) != "fit") {
      std::print(stderr, "{}", help);
      return 1;
    }
    const auto args = arguments(argc, argv);
    log.set_level(args.level);
    log.debug("orbit-fit {}; product policy {}", ORBITFIT_VERSION, orbitfit::to_string(args.policy));
    cli::SignalCancellation cancellation;
    const auto stop = cancellation.token();
    cli::StagingDirectory staging(args.output);
    log.trace("Staging output in {}", staging.path().string());
    log.info("Reading {} from {}", args.config.empty() ? "request" : "YAML job",
             (args.config.empty() ? args.input : args.config).string());
    auto loaded =
        args.config.empty() ? cli::load_request(args.input, stop) : cli::load_job(args.config, stop);
    auto& request = loaded.request;
    cancellation.check();
    request.options.stop_token = stop;
    if (loaded.source)
      log.debug("Read {} CSV bytes from {}", loaded.source->csv.size(), loaded.source->data_path.string());
    log.debug("Resolved request: {} bytes; {} TEME samples; fit_bstar={}; use_velocity={}",
              loaded.snapshot_json.size(), request.samples.size(), request.options.fit_bstar,
              request.options.use_velocity);
    log.info("Fitting {} supplied samples", request.samples.size());
    const auto fitted = take(orbitfit::fit(request.samples, request.options));
    if (fitted.termination == orbitfit::Termination::cancelled) throw cli::Cancelled();
    log.debug("Fit: termination={}; iterations={}; rank={}/{}; continuous RMS={:.6f} m",
              orbitfit::to_string(fitted.termination), fitted.iterations, fitted.rank, fitted.parameter_count,
              fitted.metrics.rms_position_m);
    for (const auto& diagnostic : fitted.diagnostics) log.debug("Fit diagnostic: {}", diagnostic);
    log.info("Validating serialized OMM and TLE products");
    const auto delivery = take(
        orbitfit::prepare_products(fitted, request.samples, request.metadata, request.options, args.policy));
    log.debug("Product validation: OMM={}; TLE={}", orbitfit::to_string(delivery.omm.status),
              orbitfit::to_string(delivery.tle.status));
    if (delivery.omm_json) cli::write_file(staging.path() / "elements.omm.json", *delivery.omm_json, stop);
    if (delivery.tle_text)
      cli::write_file(staging.path() / "elements.tle",
                      delivery.tle_text->line1 + "\n" + delivery.tle_text->line2 + "\n", stop);
    cli::write_file(staging.path() / "request.json", loaded.snapshot_json, stop);
    if (loaded.source) {
      cli::write_file(staging.path() / "input.yaml", loaded.source->yaml, stop);
      cli::write_file(staging.path() / "states.csv", loaded.source->csv, stop);
    }
    cli::write_file(staging.path() / "report.json",
                    orbitfit::report_json(fitted, request.metadata, request.options, delivery), stop);
    cancellation.check();
    staging.publish(stop);
    log.info("Published delivery to {}", args.output.string());
    if (!delivery.accepted)
      log.warn("Delivery rejected: {}; diagnostic candidate retained in {}",
               orbitfit::to_string(delivery.reason), args.output.string());
    else if (delivery.tle.status != orbitfit::ProductStatus::accepted)
      log.warn("OMM delivery accepted; optional TLE validation failed: {} ({})",
               orbitfit::to_string(delivery.tle.status), delivery.tle.message);
    std::println("{}: {} in {}", delivery.accepted ? "Accepted" : "Rejected; diagnostic candidate retained",
                 orbitfit::to_string(delivery.reason), args.output.string());
    std::println("Termination: {}; rank {}/{}; continuous RMS {:.6f} m",
                 orbitfit::to_string(fitted.termination), fitted.rank, fitted.parameter_count,
                 fitted.metrics.rms_position_m);
    return delivery.accepted ? 0 : 2;
  } catch (const cli::Cancelled& error) {
    log.warn("{}", error.what());
    return 130;
  } catch (const std::exception& error) {
    log.error("{}", error.what());
    return 1;
  }
}
}  // namespace

/** @brief Own the synchronous CLI logger and run one command. @param argc Argument count.
 * @param argv Argument vector. @return Command exit status, or 1 if logger setup fails.
 * @details The stderr logger is not registered globally. UTC timestamps and automatic
 * terminal-only color keep redirected logs readable. No signal handler calls the logger. */
int main(int argc, char** argv) {
  try {
    spdlog::logger log("orbit-fit", std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
    log.set_pattern("[%Y-%m-%dT%H:%M:%S.%eZ] [%n] [%^%l%$] %v", spdlog::pattern_time_type::utc);
    log.set_level(spdlog::level::warn);
    log.flush_on(spdlog::level::warn);
    return run(argc, argv, log);
  } catch (const std::exception& error) {
    // Last-resort output when a logger cannot be constructed or used.
    std::println(stderr, "orbit-fit: logging failure: {}", error.what());
    return 1;
  }
}
