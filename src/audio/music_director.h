/* What plays, and what plays next.
 *
 * One tick, one question: given where the player is standing and which of the
 * three modes they left the Jukebox in, is the right thing playing? The
 * director answers it against the MusicPlayer and nothing else -- it holds no
 * audio and reads no game state beyond the track name it is handed.
 *
 * THE RULE that makes the three modes one piece of code: a track playing
 * through ONCE is always left to finish. A song picked off the list, and the
 * track a mode change caught halfway, are the same case -- neither is cut
 * short, and the mode takes over at its end.
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
  // `player` and `rng` are the caller's and have to outlive the director.
  MusicDirector(MusicPlayer& player, std::mt19937& rng);

  // Asked every tick. `map_track` is what the map, or the boss being fought,
  // names -- empty where neither has music, which plays nothing.
  void Update(JukeboxMode mode, std::string_view map_track);

  // Plays `track` now, through once. What follows it is the mode's business,
  // so Enter on a song means this song and then the mode again. A paused
  // player is let go: picking a song is asking to hear it.
  void Play(std::string_view track);
  // Holds what is playing where it stands, or lets it go. Nothing new starts
  // while it is held, so walking to another map stays silent.
  void TogglePause();
  // Moves the cursor `seconds` through the live track, forwards or back.
  void Nudge(float seconds);

  // The track the playlist reaches after `track`, coming round to the first
  // at the end. The first track for a name the list does not carry, which is
  // what a build with nothing playing yet answers.
  std::string_view NextInPlaylist(std::string_view track) const;

  // What is playing and where the cursor stands in it, for a screen to draw.
  // `length_seconds` is 0 where the decoder could not tell.
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
