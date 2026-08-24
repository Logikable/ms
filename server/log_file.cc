#include "server/log_file.h"

#include <ctime>
#include <filesystem>
#include <string>

#include "absl/log/log_entry.h"

namespace ms {
namespace {

// The name this run's file takes: ms-server-20260823-141500.log.
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
  // Flushed every line: a server killed mid-write must not lose what it was
  // saying about the moment it died.
  out_ << entry.text_message_with_prefix_and_newline();
  out_.flush();
}

}  // namespace ms
