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
#include "src/protos/mob.pb.h"

ABSL_FLAG(int, port, ms::kServerPort, "The port to listen on.");
ABSL_FLAG(std::string, log_dir, "",
          "A directory to keep the log in. One file per run, named for the "
          "moment it started, and nothing is ever removed. Unset logs to "
          "stderr alone.");

namespace {

// Set by SIGTERM and SIGINT to ask the server to drain and exit. A signal
// handler can safely do little else, so the main loop checks this flag.
volatile std::sig_atomic_t g_stop = 0;

void OnStopSignal(int /*signal*/) {
  g_stop = 1;
}

// How long each loop pass waits for socket activity. Waking ten times a second
// is cheap and means heartbeats and shutdown never wait on traffic.
constexpr std::chrono::milliseconds kStepTimeout(100);

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  // journald reads the log from stderr. By default absl only prints warnings
  // and above there.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  // Lives as long as the process, since the sink must outlive every log line.
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
  // The same boss data the game compiles in, so the server knows what each
  // boss name a party asks for means.
  std::map<std::string, ms::Boss> bosses =
      ms::LoadTextProtoMap<ms::Boss>(ms::EmbeddedBosses());
  // The server reads only the mobs' HP, to build each fight's shared roster.
  std::map<std::string, ms::Mob> mobs =
      ms::LoadTextProtoMap<ms::Mob>(ms::EmbeddedMobs());
  ms::Server server(std::move(*listener), bosses, mobs);
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
