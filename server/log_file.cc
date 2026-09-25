#include "server/log_file.h"

#include <ctime>
#include <filesystem>
#include <string>

#include "absl/log/log_entry.h"

namespace ms {
namespace {

// Returns this run's file name, such as ms-server-20260823-141500.log.
std::string FileName() {
  std::time_t now = std::time(nullptr);
  std::tm broken = {};
  localtime_r(&now, &broken);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &broken);
  return std::string("ms-server-") + stamp + ".log";
}

}  // namespace

FileLogSink::FileLogSink(const std::string& dir) {
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  if (error) {
    return;
  }
  path_ = (std::filesystem::path(dir) / FileName()).string();
  out_.open(path_, std::ios::app);
}

void FileLogSink::Send(const absl::LogEntry& entry) {
  if (!out_.is_open()) {
    return;
  }
  // Flush every line so a crash does not lose the last thing logged.
  out_ << entry.text_message_with_prefix_and_newline();
  out_.flush();
}

}  // namespace ms
