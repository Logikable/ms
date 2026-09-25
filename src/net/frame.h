/* Framing for the multiplayer socket: one message per frame, preceded by a
 * 4-byte little-endian length.
 *
 * TCP is a byte stream, so one read can return half a message, or three at
 * once. Everything read is appended to a buffer and taken off the front one
 * whole frame at a time.
 *
 * This file doesn't know what a frame contains. The messages are defined by the
 * protocol; see //src/multiplayer:protocol.
 */
#ifndef MS_SRC_NET_FRAME_H_
#define MS_SRC_NET_FRAME_H_

#include <cstddef>
#include <string>
#include <string_view>

namespace ms {

// The length header in front of every frame.
inline constexpr size_t kFrameHeaderBytes = 4;

// The largest payload one frame may carry. It's far above anything the protocol
// sends; it exists so a corrupt length can't make the reader wait for a
// gigabyte that never arrives.
inline constexpr size_t kMaxFrameBytes = 1 << 20;

// Appends `payload` to `out` as one frame. Returns false and appends nothing
// for a payload too large to frame.
bool AppendFrame(std::string_view payload, std::string& out);

// How TakeFrame ended.
enum class FrameStatus {
  kOk,
  // Not all of a frame has arrived. `buffer` is unchanged; try again after more
  // bytes have been read into it.
  kIncomplete,
  // The length exceeds kMaxFrameBytes. There's no way to find where the next
  // frame starts, so the connection must close.
  kTooLarge,
};

// Takes the first whole frame off the front of `buffer` and into `payload`.
FrameStatus TakeFrame(std::string& buffer, std::string& payload);

}  // namespace ms

#endif  // MS_SRC_NET_FRAME_H_
