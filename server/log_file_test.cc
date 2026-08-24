#include "server/log_file.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "absl/log/log.h"
#include "absl/log/log_sink_registry.h"

namespace ms {
namespace {

std::string Contents(const std::string& path) {
  std::ifstream in(path);
  std::stringstream all;
  all << in.rdbuf();
  return all.str();
}

TEST(FileLogSinkTest, WritesEveryLineToItsOwnFile) {
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "ms_log_file_test";
  std::filesystem::remove_all(dir);

  FileLogSink sink(dir.string());
  ASSERT_TRUE(sink.ok());
  EXPECT_EQ(std::filesystem::path(sink.path()).parent_path(), dir);

  absl::AddLogSink(&sink);
  LOG(INFO) << "a party was made";
  LOG(WARNING) << "and then broken";
  absl::RemoveLogSink(&sink);

  std::string written = Contents(sink.path());
  EXPECT_NE(written.find("a party was made"), std::string::npos);
  EXPECT_NE(written.find("and then broken"), std::string::npos);
  // Nothing logged after the sink is gone reaches the file.
  LOG(INFO) << "unheard";
  EXPECT_EQ(Contents(sink.path()).find("unheard"), std::string::npos);

  std::filesystem::remove_all(dir);
}

TEST(FileLogSinkTest, ADirectoryItCannotMakeIsNotOk) {
  FileLogSink sink("/proc/no/such/place");
  EXPECT_FALSE(sink.ok());
  EXPECT_TRUE(sink.path().empty());
}

}  // namespace
}  // namespace ms
