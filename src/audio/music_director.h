/* Decides what plays now and what plays next.
 *
 * Each tick it checks: given where the player is and which of the three Jukebox
 * modes is set, is the right thing playing? It works only through the
 * MusicPlayer; it holds no audio and reads no game state beyond the track name
 * it's given.
 *
 * One rule lets a single piece of code handle all three modes: a track playing
 * through once is always allowed to finish. A song picked from the list and a
 * track caught halfway by a mode change are the same case. Neither is cut
 * short, and the mode takes over when it ends.
 */
#ifndef MS_SRC_AUDIO_MUSIC_DIRECTOR_H_
#define MS_SRC_AUDIO_MUSIC_DIRECTOR_H_

#include <random>
#include <string_view>
#include <vector>

#include "src/audio/jukebox.h"
#include "src/audio/music_player.h"
#include "src/protos/account.pb.h"

namespace ms {

// How far the two skip buttons move the cursor.
inline constexpr float kSkipSeconds = 5.0f;

class MusicDirector {
 public:
  // `player` and `rng` belong to the caller and must outlive the director.
  MusicDirector(MusicPlayer& player, std::mt19937& rng);

  // Called every tick. `map_track` is the track the map or current boss names,
  // or empty if neither has music, in which case nothing plays.
  void Update(JukeboxMode mode, std::string_view map_track);

  // Plays `track` now, once through. The mode decides what follows, so pressing
  // Enter on a song plays that song and then returns to the mode. Unpauses a
  // paused player, since picking a song means wanting to hear it.
  void Play(std::string_view track);
  // Pauses what's playing, or resumes it. Nothing new starts while paused, so
  // walking to another map stays silent.
  void TogglePause();
  // Moves the playback position `seconds` forward or back in the current track.
  void Nudge(float seconds);

  // The track after `track` in the playlist, wrapping to the first at the end.
  // Returns the first track for a name not in the list, which covers the case
  // of nothing playing yet.
  std::string_view NextInPlaylist(std::string_view track) const;

  // What's playing and the playback position, for a screen to draw.
  // `length_seconds` is 0 if the decoder couldn't tell.
  const std::string& playing() const {
    return player_.playing();
  }
  bool paused() const {
    return player_.paused();
  }
  float position_seconds() const {
    return player_.position_seconds();
  }
  float length_seconds() const {
    return player_.length_seconds();
  }

 private:
  MusicPlayer& player_;
  Jukebox jukebox_;
  // The song list, in the order the screen shows it.
  std::vector<std::string_view> tracks_;
};

}  // namespace ms

#endif  // MS_SRC_AUDIO_MUSIC_DIRECTOR_H_
