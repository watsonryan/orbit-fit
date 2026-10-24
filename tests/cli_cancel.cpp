/**
 * @file cli_cancel.cpp
 * @brief Send real process signals to the CLI and verify cancelled runs publish no directory.
 * @author watson
 */
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <print>

#include "cli_support.hpp"
#include "orbitfit/io.hpp"

namespace {
namespace fs = std::filesystem;
namespace cli = orbitfit::cli;

class Child {
 public:
  Child(const std::string& executable, const std::string& input, const std::string& output) {
    pid_ = fork();
    if (pid_ < 0) throw std::runtime_error("fork failed");
    if (pid_ == 0) {
      execl(executable.c_str(), executable.c_str(), "fit", "--input", input.c_str(), "--output-dir",
            output.c_str(), static_cast<char*>(nullptr));
      _exit(127);
    }
  }
  ~Child() {
    if (pid_ > 0) {
      kill(pid_, SIGKILL);
      while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
      }
    }
  }
  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;
  std::optional<int> poll() {
    int status{};
    const auto result = waitpid(pid_, &status, WNOHANG);
    if (result == 0 || (result < 0 && errno == EINTR)) return {};
    if (result < 0) throw std::runtime_error("waitpid failed");
    pid_ = -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }
  void signal(int number) {
    if (kill(pid_, number) != 0) throw std::runtime_error("kill failed");
  }

 private:
  pid_t pid_{};
};

void run(const char* executable, int signal) {
  cli::StagingDirectory root(fs::temp_directory_path() / "orbitfit-cli-signal-test");
  const auto input = root.path() / "request.json";
  const auto output = root.path() / "result";
  const auto example = orbitfit::make_example_request();
  if (!example) throw std::runtime_error(example.error().message);
  auto json = nlohmann::json::parse(*example);
  // A sizeable valid request keeps the child occupied after it creates staging.
  // Detection of staging, not a fixed sleep, synchronizes with installed handlers.
  const auto request = orbitfit::read_request(*example);
  if (!request) throw std::runtime_error(request.error().message);
  const auto start = request->options.start;
  const auto sample = json["samples"][0];
  json["samples"] = nlohmann::json::array();
  for (int i = 0; i < 20000; ++i) {
    auto row = sample;
    row["epoch"] = orbitfit::format_epoch(*orbitfit::add_seconds(start, i * 0.4));
    json["samples"].push_back(std::move(row));
  }
  cli::write_file(input, json.dump());
  Child child(executable, input.string(), output.string());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  bool sent = false;
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto status = child.poll()) {
      if (!sent || *status != 130) throw std::runtime_error("CLI did not return cancellation exit code 130");
      if (fs::exists(output)) throw std::runtime_error("Cancelled CLI published a delivery");
      for (const auto& item : fs::directory_iterator(root.path()))
        if (item.path().filename().string().starts_with("result.tmp-"))
          throw std::runtime_error("Cancelled CLI left owned staging");
      return;
    }
    if (!sent) {
      for (const auto& item : fs::directory_iterator(root.path())) {
        if (item.path().filename().string().starts_with("result.tmp-")) {
          child.signal(signal);
          sent = true;
          break;
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  throw std::runtime_error("Timed out waiting for CLI cancellation");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Expected CLI path");
    run(argv[1], SIGINT);
    run(argv[1], SIGTERM);
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "{}", error.what());
    return 1;
  }
}
