/* The track table of a build made with --define=audio=off.
 *
 * It carries no audio, so every lookup misses. Nothing else changes: the
 * jukebox asks the same questions and gets nothing back.
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
