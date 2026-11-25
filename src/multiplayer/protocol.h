/* What both sides of the multiplayer connection must agree on: the version, the
 * server address, the heartbeat, and how messages are encoded.
 *
 * The messages themselves are in //src/protos:multiplayer_proto.
 */
#ifndef MS_SRC_MULTIPLAYER_PROTOCOL_H_
#define MS_SRC_MULTIPLAYER_PROTOCOL_H_

#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include "src/build_config.h"
#include "src/net/frame.h"

namespace ms {

// The version both client and server must be built from, covering both the
// messages and the game data behind them. Increase it when either changes; a
// client that doesn't match is rejected.
//
// The data part is easy to forget, and forgetting it fails silently: a release
// once ran for days against a server giving Cygnus a fifteen-minute limit where
// clients gave her ten. A bump is needed for any fight both sides know by name
// but could disagree on: a changed time limit, phase, drop or mob.
//
// Adding a fight doesn't need one, nor does a mob that only a new fight spawns.
// Only the key crosses the network, so a missing one fails loudly by name.
inline constexpr int kMultiplayerVersion = 7;

// The server's address. The client's --server flag overrides both. A build
// without multiplayer includes no address at all: nothing in it would connect,
// and a single-player game shouldn't ship someone's home address.
#ifdef MS_MULTIPLAYER_OFF
inline constexpr char kServerHost[] = "";
#else
inline constexpr char kServerHost[] = "38.81.229.97";
#endif
inline constexpr int kServerPort = 21711;

// "host:port", with an IPv6 host in brackets: "[::1]:21711".
inline std::string JoinServerAddress(const std::string& host, int port) {
  bool v6 = host.find(':') != std::string::npos;
  return (v6 ? "[" + host + "]" : host) + ":" + std::to_string(port);
}

// The reverse of JoinServerAddress. Nothing for an empty host, or a port that
// isn't a positive number.
inline std::optional<std::pair<std::string, int>> SplitServerAddress(
    const std::string& address) {
  size_t colon = address.rfind(':');
  if (colon == std::string::npos || colon == 0) {
    return std::nullopt;
  }
  std::string host = address.substr(0, colon);
  if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
    host = host.substr(1, host.size() - 2);
  }
  int port = std::atoi(address.c_str() + colon + 1);
  if (host.empty() || port <= 0) {
    return std::nullopt;
  }
  return std::make_pair(host, port);
}

// Both, in the --server flag's format. Empty in a build without multiplayer,
// which makes that build single-player.
inline std::string DefaultServerAddress() {
  if (!kMultiplayerEnabled) {
    return "";
  }
  return JoinServerAddress(kServerHost, kServerPort);
}

// The maximum party size. Every boss phase has more spots to stand on than
// this, so a full party always has room.
inline constexpr int kMaxPartySize = 3;

// How often a client with nothing to send sends a heartbeat anyway.
inline constexpr std::chrono::seconds kHeartbeatInterval(5);
// How long the server waits on a silent session. Three heartbeats, so a couple
// can be lost in a bad minute without dropping anyone.
inline constexpr std::chrono::seconds kSessionTimeout(15);

// Frames `message` onto the end of `out`. Returns false and appends nothing if
// the message is too large to frame.
template <typename Message>
bool Encode(const Message& message, std::string& out) {
  std::string payload;
  if (!message.SerializeToString(&payload)) {
    return false;
  }
  return AppendFrame(payload, out);
}

// How Decode ended.
enum class DecodeStatus {
  kOk,
  // Not all of the next message has arrived. `buffer` is unchanged.
  kIncomplete,
  // The bytes aren't a message this build can read. There's no way to find
  // where the next one starts, so the connection must close.
  kBroken,
};

// Takes the next whole message off the front of `buffer`.
template <typename Message>
DecodeStatus Decode(std::string& buffer, Message& message) {
  std::string payload;
  switch (TakeFrame(buffer, payload)) {
    case FrameStatus::kIncomplete:
      return DecodeStatus::kIncomplete;
    case FrameStatus::kTooLarge:
      return DecodeStatus::kBroken;
    case FrameStatus::kOk:
      break;
  }
  return message.ParseFromString(payload) ? DecodeStatus::kOk
                                          : DecodeStatus::kBroken;
}

}  // namespace ms

#endif  // MS_SRC_MULTIPLAYER_PROTOCOL_H_
