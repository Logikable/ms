/* The music this build includes, looked up by track name.
 *
 * Every .mp3 under bgm/ is compiled into the binary, keyed by its filename
 * stem: the WZ track name it was extracted as, which is what a map or boss
 * phase names in its data file.
 *
 * A build made with --define=audio=off returns nothing here and includes no
 * audio. Callers must handle a missing track, since a silent build gives that
 * answer for every name.
 */
#ifndef MS_SRC_AUDIO_TRACKS_H_
#define MS_SRC_AUDIO_TRACKS_H_

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace ms {

// One track's bytes, pointing into the executable's own image. They last for
// the whole program, so nothing here owns or frees them.
struct TrackData {
  const unsigned char* data;
  std::size_t size;
  // How long the track plays. Computed from the MP3's frame headers when the
  // build embedded it, so reading it is free and decodes nothing.
  int duration_ms;
};

// The MP3 for `track`, or nullopt where this build has no such track.
std::optional<TrackData> BgmTrack(std::string_view track);

// Every track in this build, in name order. Empty in a silent build.
std::vector<std::string_view> BgmTrackNames();

}  // namespace ms

#endif  // MS_SRC_AUDIO_TRACKS_H_
