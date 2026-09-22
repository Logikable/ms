/* What a track is called on screen.
 *
 * The tracks are keyed by their WZ filename stem -- `AboveTheTreetops`,
 * `destructionTown` -- which is what a map or a boss phase names in its data
 * file and no way to write a song's title. The table here is that stem spelt
 * out: GMS's own name for the track, with the client's misspellings put
 * right.
 *
 * A stem the table does not carry answers itself, so a track added to bgm/
 * shows up under its filename rather than not at all.
 */
#ifndef MS_SRC_AUDIO_TRACK_TITLE_H_
#define MS_SRC_AUDIO_TRACK_TITLE_H_

#include <string>
#include <string_view>
#include <vector>

namespace ms {

// The title `track` goes by, or `track` itself where the table has no entry.
std::string TrackTitle(std::string_view track);

// Every track in this build, ordered by the title it shows under. THE order
// of the song list, and so the order "the song below this one" follows.
std::vector<std::string_view> TracksByTitle();

}  // namespace ms

#endif  // MS_SRC_AUDIO_TRACK_TITLE_H_
