/* The name a track shows on screen.
 *
 * Tracks are keyed by their WZ filename stem, such as `AboveTheTreetops` or
 * `destructionTown`, which is what a map or boss phase names in its data file
 * but isn't a readable title. The table here maps each stem to GMS's own name
 * for the track, with the client's misspellings corrected.
 *
 * A stem missing from the table is shown as is, so a track added to bgm/ shows
 * under its filename instead of not at all.
 */
#ifndef MS_SRC_AUDIO_TRACK_TITLE_H_
#define MS_SRC_AUDIO_TRACK_TITLE_H_

#include <string>
#include <string_view>
#include <vector>

namespace ms {

// The title `track` is shown as, or `track` itself if the table has no entry.
std::string TrackTitle(std::string_view track);

// Every track in this build, sorted by title. This is the song list's order,
// and so the order "the next song" follows.
std::vector<std::string_view> TracksByTitle();

}  // namespace ms

#endif  // MS_SRC_AUDIO_TRACK_TITLE_H_
