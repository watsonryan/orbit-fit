/**
 * @file cli_support.cpp
 * @brief Implement bounded I/O, exclusive directory publication, and signal-safe cancellation.
 * @author watson
 */
#include "cli_support.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <random>
#include <system_error>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <stdio.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#else
#error "CLI publication needs a no-replace directory rename implementation for this platform"
#endif

namespace orbitfit::cli {
namespace {
namespace fs = std::filesystem;
static_assert(std::atomic<bool>::is_always_lock_free, "Signal bridge requires lock-free atomic bool");
std::atomic<bool> interrupted{false};   ///< Signal-set flag; stop requests happen in normal thread context.
std::atomic<bool> signal_owner{false};  ///< Enforces one scoped process-signal owner.
/** @brief Record a signal using only a lock-free atomic store. @param signal Signal number; both handled
 * signals request cancellation. */
void signal_handler(int signal) {
  (void)signal;
  interrupted.store(true, std::memory_order_relaxed);
}

// Directory rename must fail if another writer creates even an empty destination
// at commit. An existence-check + ordinary-rename fallback would lose that property.
/** @brief Commit a same-filesystem directory without replacement. @param from Owned staging directory. @param
 * to New final path. @throws fs::filesystem_error On failure, including unsupported no-replace semantics. */
void rename_exclusive(const fs::path& from, const fs::path& to) {
  std::error_code error;
#if defined(_WIN32)
  if (!MoveFileExW(from.c_str(), to.c_str(), 0))
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#elif defined(__APPLE__)
  if (::renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) != 0)
    error = std::error_code(errno, std::generic_category());
#elif defined(__linux__)
  if (::syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) != 0)
    error = std::error_code(errno, std::generic_category());
#endif
  if (error) throw fs::filesystem_error("Cannot publish without replacing the destination", from, to, error);
}
/** @brief Generate a staging candidate suffix. @return 32 hexadecimal digits; exclusive creation resolves
 * collisions. */
std::string suffix() {
  std::random_device random;
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (int word = 0; word < 4; ++word) {
    auto value = random();
    for (int digit = 0; digit < 8; ++digit) {
      result += hex[value & 15];
      value >>= 4;
    }
  }
  return result;
}
}  // namespace

void checkpoint(std::stop_token stop) {
  if (stop.stop_requested()) throw Cancelled();
}
std::string read_bounded(std::istream& stream, std::size_t limit, std::stop_token stop) {
  std::string text;
  std::array<char, 16384> buffer{};
  for (;;) {
    checkpoint(stop);
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    checkpoint(stop);
    if (stream.bad() || (stream.fail() && !stream.eof())) throw std::runtime_error("Input read failed");
    const auto count = static_cast<std::size_t>(stream.gcount());
    if (count > limit - text.size()) throw std::runtime_error("Input exceeds the byte limit");
    text.append(buffer.data(), count);
    if (stream.eof()) break;
  }
  return text;
}
std::string read_file(const std::filesystem::path& path, std::stop_token stop, std::size_t limit) {
  checkpoint(stop);
  if (!fs::is_regular_file(path)) throw std::runtime_error("Input must be a regular file: " + path.string());
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Cannot open input: " + path.string());
  return read_bounded(stream, limit, stop);
}
void write_all(std::ostream& stream, std::string_view contents, std::stop_token stop) {
  checkpoint(stop);
  while (!contents.empty()) {
    const auto count = std::min<std::size_t>(contents.size(), 16384);
    stream.write(contents.data(), static_cast<std::streamsize>(count));
    if (!stream) throw std::runtime_error("Output write failed");
    contents.remove_prefix(count);
    checkpoint(stop);
  }
}
void write_file(const std::filesystem::path& path, std::string_view contents, std::stop_token stop) {
  checkpoint(stop);
  std::ofstream stream(path, std::ios::binary | std::ios::out | std::ios::noreplace);
  if (!stream) throw std::runtime_error("Cannot create file (it may already exist): " + path.string());
  write_all(stream, contents, stop);
  stream.close();
  if (!stream) throw std::runtime_error("Cannot close output: " + path.string());
  checkpoint(stop);
}
StagingDirectory::StagingDirectory(const std::filesystem::path& output) {
  if (output.empty() || output.filename().empty() || output.filename() == "." || output.filename() == "..")
    throw std::invalid_argument("Output must name a new directory");
  destination_ = fs::absolute(output).lexically_normal();
  if (fs::exists(fs::symlink_status(destination_)))
    throw fs::filesystem_error("Output already exists", destination_,
                               std::make_error_code(std::errc::file_exists));
  fs::create_directories(destination_.parent_path());
  for (int attempt = 0; attempt < 64; ++attempt) {
    const auto candidate =
        destination_.parent_path() / (destination_.filename().string() + ".tmp-" + suffix());
    if (fs::create_directory(candidate)) {
      path_ = candidate;
      return;
    }
  }
  throw fs::filesystem_error("Cannot allocate unique staging directory", destination_,
                             std::make_error_code(std::errc::file_exists));
}
StagingDirectory::~StagingDirectory() {
  if (!published_) {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }
}
void StagingDirectory::publish(std::stop_token stop) {
  if (published_) throw std::logic_error("Output was already published");
  checkpoint(stop);
  rename_exclusive(path_, destination_);
  published_ = true;
}
SignalCancellation::SignalCancellation() {
  if (signal_owner.exchange(true)) throw std::runtime_error("Signal cancellation already has an owner");
  interrupted.store(false, std::memory_order_relaxed);
  previous_int_ = std::signal(SIGINT, signal_handler);
  if (previous_int_ == SIG_ERR) {
    signal_owner.store(false);
    throw std::runtime_error("Cannot install SIGINT handler");
  }
  previous_term_ = std::signal(SIGTERM, signal_handler);
  if (previous_term_ == SIG_ERR) {
    std::signal(SIGINT, previous_int_);
    signal_owner.store(false);
    throw std::runtime_error("Cannot install SIGTERM handler");
  }
  try {
    monitor_ = std::jthread([this](std::stop_token stop) {
      while (!stop.stop_requested()) {
        if (interrupted.load(std::memory_order_relaxed)) source_.request_stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    });
  } catch (...) {
    std::signal(SIGINT, previous_int_);
    std::signal(SIGTERM, previous_term_);
    signal_owner.store(false);
    throw;
  }
}
SignalCancellation::~SignalCancellation() {
  monitor_.request_stop();
  if (monitor_.joinable()) monitor_.join();
  std::signal(SIGINT, previous_int_);
  std::signal(SIGTERM, previous_term_);
  signal_owner.store(false);
}
void SignalCancellation::check() {
  if (interrupted.load(std::memory_order_relaxed)) source_.request_stop();
  checkpoint(source_.get_token());
}
}  // namespace orbitfit::cli
