/* The track table for a build made with --define=audio=off.
 *
 * It includes no audio, so every lookup misses. Nothing else changes: the music
 * player makes the same calls and gets nothing back.
 */
#include <optional>
#include <string_view>
#include <vector>

#include "src/audio/tracks.h"

namespace ms {

std::optional<TrackData> BgmTrack(std::string_view) {
  return std::nullopt;
}

std::vector<std::string_view> BgmTrackNames() {
  return {};
}

}  // namespace ms
