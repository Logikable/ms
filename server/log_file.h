/* Writes the server's log to disk.
 *
 * Each run gets its own file, named for the time the process started. Files
 * are never rotated or deleted, so the full history stays on the box.
 */
#ifndef MS_SERVER_LOG_FILE_H_
#define MS_SERVER_LOG_FILE_H_

#include <fstream>
#include <string>

#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"

namespace ms {

class FileLogSink : public absl::LogSink {
 public:
  // Creates `dir` if needed and opens this run's file in it. Check ok()
  // before registering the sink; if the file did not open, it writes nothing.
  explicit FileLogSink(const std::string& dir);

  bool ok() const {
    return out_.is_open();
  }
  // The log file's path, so the process can report where it is logging.
  const std::string& path() const {
    return path_;
  }

  void Send(const absl::LogEntry& entry) override;

 private:
  std::string path_;
  std::ofstream out_;
};

}  // namespace ms

#endif  // MS_SERVER_LOG_FILE_H_
