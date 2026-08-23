/* Framing for the multiplayer socket: one message per frame, behind a 4-byte
 * little-endian length.
 *
 * TCP is a stream of bytes, so one read can hand back half a message, or three
 * of them at once. Everything read goes on the end of a buffer and comes back
 * off the front one whole frame at a time.
 *
 * Nothing here knows what a frame carries. The messages are the protocol's
 * business -- see //src/multiplayer:protocol.
 */
#ifndef MS_SRC_NET_FRAME_H_
#define MS_SRC_NET_FRAME_H_

#include <cstddef>
#include <string>
#include <string_view>

namespace ms {

// The length that rides ahead of every frame.
inline constexpr size_t kFrameHeaderBytes = 4;

// The most one frame may carry. Far above anything the protocol sends: it is
// here so that a wrong length on the wire cannot make the reader wait for a
// gigabyte that is never coming.
inline constexpr size_t kMaxFrameBytes = 1 << 20;

// Appends `payload` to `out` as one frame. Returns false and appends nothing
// for a payload too large to frame.
bool AppendFrame(std::string_view payload, std::string& out);

// How TakeFrame ended.
enum class FrameStatus {
  kOk,
  // Not all of a frame has arrived. `buffer` is left alone; ask again once
  // more bytes have been read into it.
  kIncomplete,
  // The length says more than kMaxFrameBytes. There is no way to tell where
  // the next frame would start, so the connection has to close.
  kTooLarge,
};

// Takes the first whole frame off the front of `buffer` and into `payload`.
FrameStatus TakeFrame(std::string& buffer, std::string& payload);

}  // namespace ms

#endif  // MS_SRC_NET_FRAME_H_
