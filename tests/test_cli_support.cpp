/**
 * @file test_cli_support.cpp
 * @brief Exercise bounded streams, failed writes, signals, and competing directory publishers.
 * @author watson
 */
#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <fstream>
#include <sstream>

#include "cli_support.hpp"
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;
namespace cli = orbitfit::cli;

class CliIo : public testing::Test {
 protected:
  cli::StagingDirectory root{fs::temp_directory_path() / "orbitfit-test-root"};
  fs::path destination = root.path() / "result";
};

TEST(CliStreams, EnforcesLimitDuringReadIncludingUnknownLengthAndExactBoundary) {
  std::istringstream exact("abcd");
  EXPECT_EQ(cli::read_bounded(exact, 4), "abcd");
  std::istringstream too_large("abcde");
  EXPECT_THROW(cli::read_bounded(too_large, 4), std::runtime_error);
  std::istringstream empty;
  EXPECT_TRUE(cli::read_bounded(empty, 0).empty());
  std::istringstream bad("abcd");
  bad.setstate(std::ios::badbit);
  EXPECT_THROW(cli::read_bounded(bad, 4), std::runtime_error);
}

TEST(CliStreams, DetectsShortWriteRatherThanPublishingPartialData) {
  struct ShortWrite : std::streambuf {
    std::streamsize xsputn(const char*, std::streamsize n) override { return n / 2; }
  } buffer;
  std::ostream stream(&buffer);
  EXPECT_THROW(cli::write_all(stream, std::string(32768, 'x')), std::runtime_error);
}

TEST(CliStreams, StopsBetweenWriteChunks) {
  std::stop_source source;
  struct StopOnWrite : std::stringbuf {
    std::stop_source& source;
    explicit StopOnWrite(std::stop_source& value) : source(value) {}
    std::streamsize xsputn(const char* data, std::streamsize n) override {
      const auto written = std::stringbuf::xsputn(data, n);
      source.request_stop();
      return written;
    }
  } buffer(source);
  std::ostream stream(&buffer);
  EXPECT_THROW(cli::write_all(stream, std::string(32768, 'x'), source.get_token()), cli::Cancelled);
  EXPECT_EQ(buffer.str().size(), 16384);
  std::istringstream input("abcd");
  EXPECT_THROW(cli::read_bounded(input, 4, source.get_token()), cli::Cancelled);
}

TEST_F(CliIo, ExistingAndLateCreatedDestinationsArePreserved) {
  cli::StagingDirectory stage(destination);
  cli::write_file(stage.path() / "new.txt", "new");
  fs::create_directory(destination);  // Even an empty directory must not be replaced.
  EXPECT_THROW(stage.publish(), fs::filesystem_error);
  EXPECT_TRUE(fs::is_empty(destination));
  EXPECT_THROW(cli::StagingDirectory second(destination), fs::filesystem_error);
  EXPECT_TRUE(fs::exists(stage.path() / "new.txt"));
}

TEST_F(CliIo, CompetingPublishersHaveExactlyOneWinner) {
  std::atomic<int> successes{};
  std::atomic<int> conflicts{};
  std::barrier ready(3);
  auto writer = [&](const std::string& content) {
    cli::StagingDirectory stage(destination);
    cli::write_file(stage.path() / "winner.txt", content);
    ready.arrive_and_wait();
    try {
      stage.publish();
      ++successes;
    } catch (const fs::filesystem_error&) {
      ++conflicts;
    }
  };
  std::jthread first(writer, "first"), second(writer, "second");
  ready.arrive_and_wait();
  first.join();
  second.join();
  EXPECT_EQ(successes, 1);
  EXPECT_EQ(conflicts, 1);
  const auto contents = cli::read_file(destination / "winner.txt");
  EXPECT_TRUE(contents == "first" || contents == "second");
  EXPECT_EQ(std::distance(fs::directory_iterator(root.path()), fs::directory_iterator()), 1);
}

TEST_F(CliIo, OwnsOnlyItsStagingAndCleansUpOnFailureOrCancellation) {
  const auto stale = root.path() / "result.tmp-stale";
  fs::create_directory(stale);
  cli::write_file(stale / "keep.txt", "keep");
  fs::path temporary;
  {
    cli::StagingDirectory stage(destination);
    temporary = stage.path();
    cli::write_file(temporary / "partial.txt", "partial");
    std::stop_source source;
    source.request_stop();
    EXPECT_THROW(stage.publish(source.get_token()), cli::Cancelled);
  }
  EXPECT_FALSE(fs::exists(temporary));
  EXPECT_FALSE(fs::exists(destination));
  EXPECT_EQ(cli::read_file(stale / "keep.txt"), "keep");
}

TEST_F(CliIo, FailedFileCreationLeavesNoPublishedDirectory) {
  fs::path temporary;
  {
    cli::StagingDirectory stage(destination);
    temporary = stage.path();
    cli::write_file(temporary / "existing.txt", "original");
    EXPECT_THROW(cli::write_file(temporary / "existing.txt", "replacement"), std::runtime_error);
    EXPECT_EQ(cli::read_file(temporary / "existing.txt"), "original");
    fs::create_directory(temporary / "directory");
    EXPECT_THROW(cli::write_file(temporary / "directory", "cannot write"), std::runtime_error);
  }
  EXPECT_FALSE(fs::exists(temporary));
  EXPECT_FALSE(fs::exists(destination));
}

#if !defined(_WIN32)
TEST_F(CliIo, PermissionFailurePreservesDestinationAndOwnedFiles) {
  if (geteuid() == 0) GTEST_SKIP() << "Root bypasses permission checks";
  cli::StagingDirectory stage(destination);
  cli::write_file(stage.path() / "file", "data");
  fs::permissions(root.path(), fs::perms::owner_read | fs::perms::owner_exec);
  EXPECT_THROW(stage.publish(), fs::filesystem_error);
  fs::permissions(root.path(), fs::perms::owner_all);
  EXPECT_FALSE(fs::exists(destination));
  EXPECT_EQ(cli::read_file(stage.path() / "file"), "data");
}
#endif

TEST(CliSignals, BothSignalsCancelThroughNormalThreadAndHandlersAreRestored) {
  for (const auto signal : {SIGINT, SIGTERM}) {
    cli::SignalCancellation cancellation;
    EXPECT_EQ(std::raise(signal), 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!cancellation.token().stop_requested() && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_TRUE(cancellation.token().stop_requested());
    EXPECT_THROW(cancellation.check(), cli::Cancelled);
  }
  cli::SignalCancellation fresh;
  EXPECT_FALSE(fresh.token().stop_requested());
  EXPECT_THROW(cli::SignalCancellation duplicate, std::runtime_error);
}
}  // namespace
