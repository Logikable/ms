/* Plays the music.
 *
 * It holds a single looping stream and crossfades when the track changes, so a
 * caller only says what should be playing now. Requesting the track already
 * playing costs nothing, so the frame loop can ask every tick.
 *
 * miniaudio runs its own thread, so nothing here blocks the ftxui event loop. A
 * build with no audio, or a machine with no sound device, leaves `ready()`
 * false and every call below does nothing: silence is never an error.
 */
#ifndef MS_SRC_AUDIO_MUSIC_PLAYER_H_
#define MS_SRC_AUDIO_MUSIC_PLAYER_H_

#include <string>
#include <string_view>

#include "miniaudio/miniaudio.h"

namespace ms {

class MusicPlayer {
 public:
  // Which device to open. Tests use kNull, which runs on miniaudio's own clock
  // and needs no sound card (this machine has no ALSA card at all).
  enum class Backend { kDevice, kNull };

  explicit MusicPlayer(Backend backend = Backend::kDevice);
  ~MusicPlayer();
  MusicPlayer(const MusicPlayer&) = delete;
  MusicPlayer& operator=(const MusicPlayer&) = delete;

  // Whether an engine started. If false, every call below does nothing.
  bool ready() const {
    return ready_;
  }

  // Crossfades to `track` and loops it, or stops if this build has no such
  // track. Requesting the track already looping costs nothing; requesting one
  // playing through once sets it looping again.
  void Play(std::string_view track);
  // Like Play, but plays `track` once instead of looping. Always restarts it,
  // so the track that's ending can be requested again, which is what happens
  // with a library of one.
  void PlayOnce(std::string_view track);
  // Turns off looping for what's playing, so it ends instead of repeating. It
  // isn't interrupted.
  void StopLooping();
  // Pauses what's playing, and resumes it. A pause outlasts the track it was
  // called on: nothing new starts until Resume, so walking to another map while
  // paused stays silent. Starting a track by name resumes, since the player
  // chose a song to hear.
  void Pause();
  void Resume();
  bool paused() const {
    return paused_;
  }
  // Moves the playback position to `seconds`, clamped to the track. Does
  // nothing if the track's length is unknown.
  void Seek(float seconds);
  // Whether the current track is within a crossfade of its end, nothing is
  // playing, or the track's length is unknown. Only ever true of a track that
  // isn't looping. Not const, because miniaudio's position is read from the
  // live sound.
  bool ending();
  // Fades out whatever is playing.
  void Stop();

  // The playback position in the current track, and the track's length. Both
  // are 0 with nothing playing, and `length_seconds` is also 0 if the decoder
  // couldn't tell.
  float position_seconds() const;
  float length_seconds() const {
    return length_seconds_;
  }

  // The volume every track plays at, 0 to 100. Applied immediately, and
  // clamped.
  void SetVolume(int volume);
  int volume() const {
    return volume_;
  }

  // The track Play was last given, or empty.
  const std::string& playing() const {
    return playing_;
  }
  // Whether it repeats at its end instead of stopping.
  bool looping() const {
    return looping_;
  }

 private:
  // How long a track takes to fade out while the next fades in.
  static constexpr int kFadeMs = 400;

  // Crossfades to `track`, looping or not. Shared by Play and PlayOnce.
  void Start(std::string_view track, bool looping);
  // Frees the slot that was fading out, whether or not it has finished.
  void DropFading();
  // Moves the current slot to fading and starts fading it out.
  void FadeOutLive();

  bool ready_ = false;
  ma_context context_;
  ma_engine engine_;
  // Two slots: the one playing, and the one fading out behind it. Each slot
  // owns its decoder, which must outlive the sound reading from it.
  ma_decoder decoders_[2];
  ma_sound sounds_[2];
  bool loaded_[2] = {false, false};
  int live_ = 0;
  std::string playing_;
  bool looping_ = false;
  bool paused_ = false;
  // The current track's length, or 0 if the decoder couldn't tell.
  float length_seconds_ = 0.0f;
  int volume_ = 10;
};

}  // namespace ms

#endif  // MS_SRC_AUDIO_MUSIC_PLAYER_H_
