/* What both ends of the multiplayer connection have to agree on: the version,
 * where the server is, the heartbeat, and how a message becomes bytes.
 *
 * The messages themselves are in //src/protos:multiplayer_proto.
 */
#ifndef MS_SRC_MULTIPLAYER_PROTOCOL_H_
#define MS_SRC_MULTIPLAYER_PROTOCOL_H_

#include <chrono>
#include <string>

#include "src/net/frame.h"

namespace ms {

// What the client and the server must both be built from. It covers the
// messages and the game data behind them -- a party fights one boss, and the
// two ends disagreeing about what that boss is would be worse than not
// connecting. Bump it when either changes; a client that does not match is
// turned away and told to update.
inline constexpr int kMultiplayerVersion = 1;

// Where the server runs. The client's --server flag overrides both.
inline constexpr char kServerHost[] = "68.42.95.210";
inline constexpr int kServerPort = 21711;

// How often a client that has nothing to say says it anyway.
inline constexpr std::chrono::seconds kHeartbeatInterval(5);
// How long the server waits on a session that has gone quiet. Three
// heartbeats: a couple can be lost to a bad minute without dropping anyone.
inline constexpr std::chrono::seconds kSessionTimeout(15);

// Frames `message` onto the end of `out`. False, having appended nothing, for
// a message too large to frame.
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
  // Not all of the next message has arrived. `buffer` is left alone.
  kIncomplete,
  // The bytes are not a message this build can read. There is no way to find
  // where the next one starts, so the connection has to close.
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
