#include "src/net/frame.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ms {

bool AppendFrame(std::string_view payload, std::string& out) {
  if (payload.size() > kMaxFrameBytes) {
    return false;
  }
  uint32_t length = static_cast<uint32_t>(payload.size());
  for (size_t i = 0; i < kFrameHeaderBytes; ++i) {
    out.push_back(static_cast<char>((length >> (8 * i)) & 0xff));
  }
  out.append(payload);
  return true;
}

FrameStatus TakeFrame(std::string& buffer, std::string& payload) {
  if (buffer.size() < kFrameHeaderBytes) {
    return FrameStatus::kIncomplete;
  }
  uint32_t length = 0;
  for (size_t i = 0; i < kFrameHeaderBytes; ++i) {
    length |= static_cast<uint32_t>(static_cast<unsigned char>(buffer[i]))
              << (8 * i);
  }
  if (length > kMaxFrameBytes) {
    return FrameStatus::kTooLarge;
  }
  if (buffer.size() < kFrameHeaderBytes + length) {
    return FrameStatus::kIncomplete;
  }
  payload.assign(buffer, kFrameHeaderBytes, length);
  buffer.erase(0, kFrameHeaderBytes + length);
  return FrameStatus::kOk;
}

}  // namespace ms
