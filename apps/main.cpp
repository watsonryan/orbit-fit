#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "orbitfit/io.hpp"

namespace {
namespace fs = std::filesystem;
constexpr std::string_view help = R"(orbit-fit — standalone SGP4 factor-graph fitting

Usage:
  orbit-fit example --output request.json
  orbit-fit fit --input request.json --output-dir results
  orbit-fit --help
  orbit-fit --version

Input: orbit-fit.request.v1 JSON, explicit TEME states in m and m/s, UTC epochs.
Output: elements.omm.json, elements.tle (when representable), report.json,
        and the original request.json. The output directory must not exist.
Exit codes: 0 accepted, 1 invalid input/I/O/solver error, 2 candidate rejected.
The example is synthetic. Catalog identity must be supplied for real objects.
)";

std::string read_file(const fs::path& path) {
  if (fs::file_size(path) > 64 * 1024 * 1024) throw std::runtime_error("Input exceeds 64 MiB");
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Cannot open input: " + path.string());
  std::string contents{std::istreambuf_iterator<char>(stream), {}};
  if (stream.bad()) throw std::runtime_error("Cannot read input: " + path.string());
  return contents;
}
void write_file(const fs::path& path, std::string_view contents) {
  std::ofstream stream(path, std::ios::binary | std::ios::out | std::ios::noreplace);
  if (!stream) throw std::runtime_error("Cannot create file (it may already exist): " + path.string());
  stream << contents;
  stream.close();
  if (!stream) throw std::runtime_error("Cannot write file: " + path.string());
}
template <class T>
T take(orbitfit::Result<T> result) {
  if (!result) throw std::runtime_error(result.error().message);
  return std::move(*result);
}
struct StagingDirectory {
  fs::path path;
  bool published{false};
  explicit StagingDirectory(const fs::path& output) : path(output.string() + ".tmp") {
    if (fs::exists(output)) throw std::runtime_error("Output directory already exists: " + output.string());
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    if (!fs::create_directory(path))
      throw std::runtime_error("Staging directory already exists: " + path.string());
  }
  ~StagingDirectory() {
    if (!published) {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  }
  void publish(const fs::path& output) {
    if (fs::exists(output)) throw std::runtime_error("Output directory appeared during fitting");
    fs::rename(path, output);
    published = true;
  }
};
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
      std::cout << "orbit-fit " << ORBITFIT_VERSION << '\n';
      return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
      std::cout << help;
      return 0;
    }
    if (argc == 4 && std::string_view(argv[1]) == "example" && std::string_view(argv[2]) == "--output") {
      write_file(argv[3], take(orbitfit::make_example_request()));
      std::cout << "Wrote synthetic example to " << argv[3] << '\n';
      return 0;
    }
    if (argc != 6 || std::string_view(argv[1]) != "fit" || std::string_view(argv[2]) != "--input" ||
        std::string_view(argv[4]) != "--output-dir") {
      std::cerr << help;
      return 1;
    }
    const fs::path output(argv[5]);
    StagingDirectory staging(output);
    const auto input = read_file(argv[3]);
    const auto request = take(orbitfit::read_request(input));
    const auto result = take(orbitfit::fit(request.samples, request.options));
    const orbitfit::ElementSet set{result.elements, request.metadata};
    const auto omm = take(orbitfit::to_omm_json(set));
    std::vector<orbitfit::Sample> window;
    for (const auto& s : request.samples)
      if (s.epoch >= request.options.start && s.epoch <= request.options.stop) window.push_back(s);
    const auto omm_metrics = take(orbitfit::evaluate(take(orbitfit::from_omm_json(omm)).elements, window));
    bool accepted = result.accepted && omm_metrics.rms_position_m <= request.options.maximum_rms_position_m;
    std::optional<orbitfit::Metrics> tle_metrics;
    std::string tle_error;
    if (auto tle = orbitfit::to_tle(set)) {
      auto parsed = take(orbitfit::from_tle(*tle));
      const auto metrics = orbitfit::evaluate(parsed.elements, window);
      if (metrics) {
        tle_metrics = *metrics;
        accepted = accepted && metrics->rms_position_m <= request.options.maximum_rms_position_m;
      } else {
        tle_error = metrics.error().message;
        accepted = false;
      }
      write_file(staging.path / "elements.tle", tle->line1 + "\n" + tle->line2 + "\n");
    } else
      tle_error = tle.error().message;
    write_file(staging.path / "elements.omm.json", omm);
    write_file(staging.path / "request.json", input);
    write_file(staging.path / "report.json",
               orbitfit::report_json(result, request.metadata, request.options, tle_metrics, tle_error));
    staging.publish(output);
    std::cout << "Termination: " << orbitfit::to_string(result.termination) << "; rank " << result.rank << '/'
              << result.parameter_count << "; continuous RMS " << result.metrics.rms_position_m << " m";
    if (tle_metrics) std::cout << "; exported TLE RMS " << tle_metrics->rms_position_m << " m";
    std::cout << '\n'
              << (accepted ? "Accepted" : "Rejected; diagnostic candidate retained") << " in " << output
              << '\n';
    if (!tle_error.empty()) std::cout << "TLE export: " << tle_error << '\n';
    return accepted ? 0 : 2;
  } catch (const std::exception& ex) {
    std::cerr << "orbit-fit: " << ex.what() << '\n';
    return 1;
  }
}
