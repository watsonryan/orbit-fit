/**
 * @file cli_support.hpp
 * @brief Bound CLI I/O, own staged output, and bridge process signals to cooperative cancellation.
 * @author watson
 */
#pragma once

#include <csignal>
#include <filesystem>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

/** @brief Private command-line infrastructure; not installed as a library API. */
namespace orbitfit::cli {
/** @brief Maximum request text accepted by both the CLI and JSON parser, in bytes. */
inline constexpr std::size_t maximum_input_bytes = 64 * 1024 * 1024;

/** @brief Cancellation before directory publication; mapped to CLI exit code 130. */
class Cancelled : public std::runtime_error {
 public:
  /** @brief Construct a diagnostic cancellation exception. */
  Cancelled() : std::runtime_error("Cancelled before publishing results") {}
};

/** @brief Observe cancellation. @param stop Cooperative token. @throws Cancelled If requested. */
void checkpoint(std::stop_token stop);
/**
 * @brief Read a stream while enforcing the byte limit during each read.
 * @param stream Input stream, including streams without a known length.
 * @param limit Maximum number of bytes retained; exact-limit input is permitted.
 * @param stop Cooperative token checked before and after each bounded read.
 * @return Complete input text.
 * @throws std::runtime_error On I/O failure or excess bytes.
 * @throws Cancelled On a stop request.
 */
std::string read_bounded(std::istream& stream, std::size_t limit, std::stop_token stop = {});
/**
 * @brief Read a regular input file with a streaming byte limit.
 * @param path Existing regular file; devices and pipes are rejected.
 * @param stop Cooperative token.
 * @param limit Maximum file bytes, default 64 MiB; checked during each read.
 * @return Complete file bytes.
 * @throws std::runtime_error On a filesystem/read/size error.
 * @throws Cancelled On a stop request.
 */
std::string read_file(const std::filesystem::path& path, std::stop_token stop = {},
                      std::size_t limit = maximum_input_bytes);
/**
 * @brief Write text in bounded chunks, checking failure and cancellation.
 * @param stream Destination stream; the owner must also check close/flush errors.
 * @param contents Bytes to write unchanged.
 * @param stop Cooperative token.
 * @throws std::runtime_error On a failed write.
 * @throws Cancelled On a stop request; partial data can remain in the stream.
 */
void write_all(std::ostream& stream, std::string_view contents, std::stop_token stop = {});
/**
 * @brief Exclusively create a file, write all bytes, and check close errors.
 * @param path New file; existing files and symlinks are protected.
 * @param contents Bytes to write.
 * @param stop Cooperative token.
 * @throws std::runtime_error On creation/write/close failure.
 * @throws Cancelled On a stop request.
 * @note The staging owner removes partial files on failure. Use inside StagingDirectory.
 */
void write_file(const std::filesystem::path& path, std::string_view contents, std::stop_token stop = {});

/**
 * @brief Own a unique sibling staging directory until atomic no-replace publication.
 * @details Concurrent writers can stage independently; exactly one can publish to a
 * new destination. Existing destinations, including empty directories, are preserved.
 * Destruction removes only this instance's unpublished staging directory. Other stale
 * directories are left untouched. This guarantees visibility, not power-loss durability.
 */
class StagingDirectory {
 public:
  /**
   * @brief Allocate an owned staging directory beside a new destination.
   * @param output Destination with a nonempty final filename; parents are created as needed.
   * @throws std::filesystem::filesystem_error On path/permission/allocation errors.
   * @throws std::invalid_argument On an empty or root-like destination.
   */
  explicit StagingDirectory(const std::filesystem::path& output);
  /** @brief Best-effort cleanup of unpublished files; never removes the final destination. */
  ~StagingDirectory();
  StagingDirectory(const StagingDirectory&) = delete;
  StagingDirectory& operator=(const StagingDirectory&) = delete;
  /** @brief Locate owned staging storage. @return Absolute path, valid until publication/destruction. */
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }
  /**
   * @brief Publish using the platform's no-replace directory rename.
   * @param stop Stop request checked immediately before the commit operation.
   * @throws std::filesystem::filesystem_error If destination exists or filesystem lacks the operation.
   * @throws std::logic_error If already published.
   * @throws Cancelled If cancellation is observed before commit.
   * @note No check-then-rename fallback is used. A signal after commit cannot undo publication.
   */
  void publish(std::stop_token stop = {});

 private:
  std::filesystem::path destination_;  ///< Absolute normalized destination on the staging filesystem.
  std::filesystem::path path_;         ///< Unique directory successfully created and owned by this object.
  bool published_{};  ///< True after successful rename; prevents cleanup of committed output.
};

/**
 * @brief Scoped SIGINT/SIGTERM handlers with a normal-thread stop-token bridge.
 * @details Only one instance may exist per process. The handler only stores into a
 * lock-free atomic; the monitor requests stop outside signal context. Checks cannot
 * interrupt an individual SGP4 call, linear algebra operation, or blocking I/O call.
 */
class SignalCancellation {
 public:
  /** @brief Install handlers and start the monitor. @throws std::runtime_error On installation/conflict. */
  SignalCancellation();
  /** @brief Join the monitor and restore both previous handlers. */
  ~SignalCancellation();
  SignalCancellation(const SignalCancellation&) = delete;
  SignalCancellation& operator=(const SignalCancellation&) = delete;
  /** @brief Get the operation token. @return Token cancelled by either handled signal. */
  [[nodiscard]] std::stop_token token() const { return source_.get_token(); }
  /** @brief Transfer a pending signal and observe stop. @throws Cancelled On cancellation. */
  void check();

 private:
  using Handler = void (*)(int);  ///< Handler type used by std::signal.
  std::stop_source source_;       ///< Stop state shared with library calls and CLI I/O.
  Handler previous_int_{};        ///< SIGINT handler restored at scope exit.
  Handler previous_term_{};       ///< SIGTERM handler restored at scope exit.
  std::jthread monitor_;          ///< Requests stop from normal thread context when scheduled.
};
}  // namespace orbitfit::cli
