#include <csignal>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/log/log_sink_registry.h"
#include "server/log_file.h"
#include "server/server.h"
#include "src/embedded_data.h"
#include "src/multiplayer/protocol.h"
#include "src/net/socket.h"
#include "src/proto_loader.h"
#include "src/protos/boss.pb.h"

ABSL_FLAG(int, port, ms::kServerPort, "The port to listen on.");
ABSL_FLAG(std::string, log_dir, "",
          "A directory to keep the log in. One file per run, named for the "
          "moment it started, and nothing is ever removed. Unset logs to "
          "stderr alone.");

namespace {

// Set by SIGTERM and SIGINT: the ask to drain and exit. A signal handler can
// touch nothing else, so the loop reads this and does the work.
volatile std::sig_atomic_t g_stop = 0;

void OnStopSignal(int /*signal*/) {
  g_stop = 1;
}

// How long a pass of the loop waits for a socket before going round again. An
// idle server wakes ten times a second, which costs nothing and keeps the
// heartbeat and the shutdown signal from waiting on traffic.
constexpr std::chrono::milliseconds kStepTimeout(100);

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  // The log is the only thing this process says, and journald reads it off
  // stderr. Without this, absl keeps everything below a warning to itself.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  // Kept for the life of the process: the sink has to outlive every line
  // logged through it.
  std::unique_ptr<ms::FileLogSink> log_file;
  std::string log_dir = absl::GetFlag(FLAGS_log_dir);
  if (!log_dir.empty()) {
    log_file = std::make_unique<ms::FileLogSink>(log_dir);
    if (log_file->ok()) {
      absl::AddLogSink(log_file.get());
      LOG(INFO) << "Logging to " << log_file->path();
    } else {
      LOG(ERROR) << "Could not open a log file in " << log_dir;
    }
  }
  if (!ms::StartSockets()) {
    return 1;
  }
  int port = absl::GetFlag(FLAGS_port);
  std::optional<ms::Socket> listener = ms::Listen(port);
  if (!listener.has_value()) {
    LOG(ERROR) << "Could not listen on port " << port;
    return 1;
  }
  // The fight catalog, compiled in exactly as the game's is: the server has
  // to know what a party means by "Normal Zakum" to let them at it.
  std::map<std::string, ms::Boss> bosses =
      ms::LoadTextProtoMap<ms::Boss>(ms::EmbeddedBosses());
  ms::Server server(std::move(*listener), bosses);
  LOG(INFO) << "Listening on port " << port << ", protocol version "
            << ms::kMultiplayerVersion;

  std::signal(SIGTERM, OnStopSignal);
  std::signal(SIGINT, OnStopSignal);
  while (true) {
    server.Step(std::chrono::steady_clock::now(), kStepTimeout);
    if (g_stop != 0) {
      server.Drain();
    }
    if (server.drained()) {
      break;
    }
  }
  LOG(INFO) << "Stopped";
  return 0;
}
