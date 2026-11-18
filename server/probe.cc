// A headless client for driving the server by hand:
//
//   bazelisk run //server:probe -- --action=create --boss=zakum
//
// It connects, performs one action, and prints the lobby whenever it changes
// until --seconds runs out.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "src/multiplayer/client.h"
#include "src/multiplayer/protocol.h"
#include "src/protos/multiplayer.pb.h"

ABSL_FLAG(std::string, host, ms::kServerHost, "The server to connect to.");
ABSL_FLAG(int, port, ms::kServerPort, "The port it listens on.");
ABSL_FLAG(std::string, name, "Probe", "The character name to arrive under.");
ABSL_FLAG(int, level, 140, "The level to arrive at.");
ABSL_FLAG(std::string, account, "", "An account id to come back as.");
ABSL_FLAG(std::string, token, "", "The token that proves that account.");
ABSL_FLAG(std::string, action, "watch",
          "What to do once connected: check, sheets, watch, create, join, "
          "leave, ready, unready, kick, promote or start.");
ABSL_FLAG(std::string, boss, "zakum", "Which boss --action=start names.");
ABSL_FLAG(int, difficulty, 0, "Which difficulty of it.");
ABSL_FLAG(std::string, mode, "shared",
          "How --action=start shares the drops: shared or solo.");
ABSL_FLAG(std::string, party, "", "Which party --action=join names.");
ABSL_FLAG(std::string, target, "",
          "Whose account --action=kick or --action=promote names.");
ABSL_FLAG(int, seconds, 10, "How long to watch for. 0 watches forever.");
ABSL_FLAG(std::string, out, "",
          "Where --action=sheets writes each online player's PlayerInfo, one "
          "file per player named for them.");

namespace {

const char* StateName(ms::ConnectionState state) {
  switch (state) {
    case ms::ConnectionState::kOffline:
      return "offline";
    case ms::ConnectionState::kConnecting:
      return "connecting";
    case ms::ConnectionState::kConnected:
      return "connected";
    case ms::ConnectionState::kUnavailable:
      return "unavailable";
    case ms::ConnectionState::kRefused:
      return "refused";
  }
  return "?";
}

std::string PartyLine(const ms::Party& party) {
  std::string line = party.id();
  for (const ms::PartyMember& member : party.members()) {
    const ms::PlayerInfo& player = member.player();
    line += "\n    " + player.name() + " (Lv" + std::to_string(player.level()) +
            ")";
    if (player.account_id() == party.leader_account_id()) {
      line += " [leader]";
    } else if (member.ready()) {
      line += " [ready]";
    }
  }
  return line;
}

// Formats a snapshot as a block of text. The caller prints it only when it
// changes.
std::string Describe(const ms::MultiplayerSnapshot& snapshot) {
  std::string text = std::string(StateName(snapshot.state));
  if (!snapshot.message.empty()) {
    text += ": " + snapshot.message;
  }
  text += "\n  account: " + snapshot.account_id;
  text += "\n  token: " + snapshot.token;
  if (!snapshot.notice.empty()) {
    text += "\n  notice: " + snapshot.notice;
  }
  text += "\n  in party: ";
  text += snapshot.party.id().empty() ? "(none)" : PartyLine(snapshot.party);
  text += "\n  open parties: ";
  if (snapshot.parties.parties_size() == 0) {
    text += "(none)";
  }
  for (const ms::Party& party : snapshot.parties.parties()) {
    text += "\n    " + PartyLine(party);
  }
  return text;
}

ms::PartyMode ParseMode(const std::string& mode) {
  return mode == "solo" ? ms::PARTY_MODE_SOLO_TOGETHER : ms::PARTY_MODE_SHARED;
}

// Performs --action. Returns false for an unknown action.
bool Act(ms::MultiplayerClient& client, const std::string& action) {
  if (action == "watch") {
    return true;
  }
  if (action == "create") {
    client.CreateParty();
    return true;
  }
  if (action == "join") {
    client.JoinParty(absl::GetFlag(FLAGS_party));
    return true;
  }
  if (action == "leave") {
    client.LeaveParty();
    return true;
  }
  if (action == "ready" || action == "unready") {
    client.SetReady(action == "ready");
    return true;
  }
  if (action == "kick") {
    client.Kick(absl::GetFlag(FLAGS_target));
    return true;
  }
  if (action == "promote") {
    client.Promote(absl::GetFlag(FLAGS_target));
    return true;
  }
  if (action == "start") {
    client.StartFight(absl::GetFlag(FLAGS_boss),
                      absl::GetFlag(FLAGS_difficulty),
                      ParseMode(absl::GetFlag(FLAGS_mode)));
    return true;
  }
  return false;
}

// Waits for the connection to settle and reports whether the server accepts
// this build. Use it to spot a version mismatch after a deploy, since the
// server refuses every client then. Returns 0 if accepted, 1 otherwise.
int CheckServer(ms::MultiplayerClient& client, int seconds) {
  std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (std::chrono::steady_clock::now() < deadline) {
    ms::MultiplayerSnapshot snapshot = client.Snapshot();
    if (snapshot.state == ms::ConnectionState::kConnected) {
      std::printf("ok: the server took a client built from version %d\n",
                  ms::kMultiplayerVersion);
      return 0;
    }
    if (snapshot.state == ms::ConnectionState::kRefused) {
      std::printf("REFUSED: %s\n", snapshot.message.c_str());
      std::printf("  this build speaks version %d", ms::kMultiplayerVersion);
      if (snapshot.server_protocol_version != 0) {
        std::printf(", the server speaks %d", snapshot.server_protocol_version);
      }
      std::printf("\n");
      return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::printf("UNREACHABLE: no answer in %ds (state %s)\n", seconds,
              StateName(client.Snapshot().state));
  return 1;
}

// Waits up to `seconds` for `done` to hold on a snapshot. Returns the last one.
template <typename Done>
ms::MultiplayerSnapshot WaitFor(ms::MultiplayerClient& client, int seconds,
                                Done done) {
  std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  ms::MultiplayerSnapshot snapshot = client.Snapshot();
  while (!done(snapshot) && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    snapshot = client.Snapshot();
  }
  return snapshot;
}

// Writes every other online player's full sheet to `dir`, as the Inspect
// screen would fetch it, for sims to read. Returns 0 if all were written.
int SaveSheets(ms::MultiplayerClient& client, int seconds,
               const std::string& dir) {
  ms::MultiplayerSnapshot snapshot =
      WaitFor(client, seconds, [](const ms::MultiplayerSnapshot& s) {
        return s.state == ms::ConnectionState::kConnected &&
               s.online.players_size() > 0;
      });
  if (snapshot.state != ms::ConnectionState::kConnected) {
    std::printf("not connected (state %s)\n", StateName(snapshot.state));
    return 1;
  }
  std::filesystem::create_directories(dir);
  int failed = 0;
  for (const ms::PlayerInfo& listed : snapshot.online.players()) {
    if (listed.account_id() == snapshot.account_id) {
      continue;
    }
    client.WatchPlayer(listed.account_id());
    ms::MultiplayerSnapshot seen =
        WaitFor(client, seconds, [&](const ms::MultiplayerSnapshot& s) {
          return s.watched.account_id() == listed.account_id() &&
                 s.watched.has_sheet();
        });
    if (seen.watched.account_id() != listed.account_id()) {
      std::printf("no sheet for %s\n", listed.name().c_str());
      ++failed;
      continue;
    }
    std::string path = dir + "/" + listed.name() + ".pb";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << seen.watched.SerializeAsString();
    std::printf("%s (Lv%d) -> %s\n", listed.name().c_str(), listed.level(),
                path.c_str());
  }
  client.WatchPlayer("");
  return failed == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  ms::PlayerInfo player;
  player.set_name(absl::GetFlag(FLAGS_name));
  player.set_level(absl::GetFlag(FLAGS_level));
  player.set_account_id(absl::GetFlag(FLAGS_account));

  ms::MultiplayerClient client(absl::GetFlag(FLAGS_host),
                               absl::GetFlag(FLAGS_port));
  client.Start(player, absl::GetFlag(FLAGS_token));

  std::string action = absl::GetFlag(FLAGS_action);
  if (action == "check") {
    return CheckServer(client, absl::GetFlag(FLAGS_seconds));
  }
  if (action == "sheets") {
    return SaveSheets(client, absl::GetFlag(FLAGS_seconds),
                      absl::GetFlag(FLAGS_out));
  }

  // The request is queued now and sent once the server accepts the client.
  if (!Act(client, action)) {
    LOG(ERROR) << "Unknown --action '" << action << "'";
    return 1;
  }

  int seconds = absl::GetFlag(FLAGS_seconds);
  std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  std::string last;
  while (seconds == 0 || std::chrono::steady_clock::now() < deadline) {
    std::string now = Describe(client.Snapshot());
    if (now != last) {
      std::printf("%s\n\n", now.c_str());
      std::fflush(stdout);
      last = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return 0;
}
