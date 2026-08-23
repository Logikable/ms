#include <csignal>
#include <optional>
#include <string>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "server/server.h"
#include "src/multiplayer/protocol.h"
#include "src/net/socket.h"

ABSL_FLAG(int, port, ms::kServerPort, "The port to listen on.");

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
  if (!ms::StartSockets()) {
    return 1;
  }
  int port = absl::GetFlag(FLAGS_port);
  std::optional<ms::Socket> listener = ms::Listen(port);
  if (!listener.has_value()) {
    LOG(ERROR) << "Could not listen on port " << port;
    return 1;
  }
  ms::Server server(std::move(*listener));
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
