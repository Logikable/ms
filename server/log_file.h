/* The server's log on disk.
 *
 * Every line the server logs is appended to a file of its own, one per start,
 * named for the moment the process began. Nothing is rotated or deleted: the
 * whole history of what the server did stays on the box.
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
  // Makes `dir` if it is not there and opens this run's file inside it. Check
  // ok() before registering: a sink that could not open its file writes
  // nothing.
  explicit FileLogSink(const std::string& dir);

  bool ok() const {
    return out_.is_open();
  }
  // The file being written, for the process to say where its log went.
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
