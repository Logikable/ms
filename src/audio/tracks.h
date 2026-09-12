/* The music this build carries, looked up by track name.
 *
 * Every .mp3 under bgm/ is compiled into the binary, keyed by its filename
 * stem -- the WZ track name it was extracted under, which is what a map or a
 * boss phase names in its data file.
 *
 * A build made with --define=audio=off answers nothing here and carries no
 * audio at all. Callers must handle a track that is not in the build; that is
 * the same answer a silent build gives for every name.
 */
#ifndef MS_SRC_AUDIO_TRACKS_H_
#define MS_SRC_AUDIO_TRACKS_H_

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace ms {

// One track's bytes, pointing into the executable's own image. They outlive
// everything, so nothing here owns or frees them.
struct TrackData {
  const unsigned char* data;
  std::size_t size;
};

// The MP3 for `track`, or nullopt where this build has no such track.
std::optional<TrackData> BgmTrack(std::string_view track);

// Every track in this build, in name order. Empty in a silent build.
std::vector<std::string_view> BgmTrackNames();

}  // namespace ms

#endif  // MS_SRC_AUDIO_TRACKS_H_
