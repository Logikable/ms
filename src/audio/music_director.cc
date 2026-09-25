#include "src/audio/music_director.h"

#include <algorithm>
#include <random>
#include <string_view>

#include "src/audio/track_title.h"

namespace ms {

MusicDirector::MusicDirector(MusicPlayer& player, std::mt19937& rng)
    : player_(player), jukebox_(rng), tracks_(TracksByTitle()) {
}

std::string_view MusicDirector::NextInPlaylist(std::string_view track) const {
  if (tracks_.empty()) {
    return {};
  }
  auto it = std::find(tracks_.begin(), tracks_.end(), track);
  if (it == tracks_.end() || ++it == tracks_.end()) {
    return tracks_.front();
  }
  return *it;
}

void MusicDirector::Update(JukeboxMode mode, std::string_view map_track) {
  if (!player_.ready() || player_.paused()) {
    return;
  }
  // A track playing through once is left to finish; see the header.
  if (!player_.looping() && !player_.ending()) {
    return;
  }
  if (mode == JUKEBOX_MODE_FOLLOW_MAP) {
    // Requesting the track already looping costs nothing, so this is all it
    // takes to follow the map.
    player_.Play(map_track);
    return;
  }
  // Turning off the loop on the map's track lets it end, so the first track of
  // the list can start after it.
  player_.StopLooping();
  if (!player_.ending()) {
    return;
  }
  player_.PlayOnce(mode == JUKEBOX_MODE_SHUFFLE
                       ? jukebox_.Next()
                       : NextInPlaylist(player_.playing()));
}

void MusicDirector::Play(std::string_view track) {
  player_.Resume();
  player_.PlayOnce(track);
}

void MusicDirector::TogglePause() {
  if (player_.paused()) {
    player_.Resume();
  } else {
    player_.Pause();
  }
}

void MusicDirector::Nudge(float seconds) {
  player_.Seek(player_.position_seconds() + seconds);
}

}  // namespace ms
