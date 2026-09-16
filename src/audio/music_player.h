/* The one thing that plays music.
 *
 * It holds a single looping stream and crossfades when the track changes, so
 * a caller only ever says what should be playing now -- asking for the track
 * already playing costs nothing, which is what lets the frame loop ask every
 * tick.
 *
 * miniaudio runs its own thread, so nothing here blocks the ftxui event loop.
 * A build with no audio, or a machine with no sound device, leaves `ready()`
 * false and every call below a no-op: silence is never an error.
 */
#ifndef MS_SRC_AUDIO_MUSIC_PLAYER_H_
#define MS_SRC_AUDIO_MUSIC_PLAYER_H_

#include <string>
#include <string_view>

#include "third_party/miniaudio/miniaudio.h"

namespace ms {

class MusicPlayer {
 public:
  // Which device to open. Tests take kNull, which runs miniaudio's own clock
  // and needs no sound card -- this box has no ALSA card at all.
  enum class Backend { kDevice, kNull };

  explicit MusicPlayer(Backend backend = Backend::kDevice);
  ~MusicPlayer();
  MusicPlayer(const MusicPlayer&) = delete;
  MusicPlayer& operator=(const MusicPlayer&) = delete;

  // Whether an engine came up. False leaves every call below doing nothing.
  bool ready() const {
    return ready_;
  }

  // Crossfades to `track`, looping it. Does nothing if it is already playing,
  // and stops if this build has no such track.
  void Play(std::string_view track);
  // The same, but plays `track` through once rather than looping it. Always
  // starts it over, so the track that is ending can be asked for again --
  // which is what a library of one comes to.
  void PlayOnce(std::string_view track);
  // Takes the loop off whatever is playing, so it ends where it is rather
  // than coming round again. What is playing is not interrupted.
  void StopLooping();
  // Whether what is playing is within a crossfade of its end, nothing is
  // playing at all, or the track's length cannot be told. Only ever true of a
  // track that is not looping. Not const: miniaudio's cursor is a query on
  // the live sound.
  bool ending();
  // Fades out whatever is playing.
  void Stop();

  // The volume every track plays at, 0 to 100. Applied live, and clamped.
  void SetVolume(int volume);
  int volume() const {
    return volume_;
  }

  // The track Play was last given, or empty.
  const std::string& playing() const {
    return playing_;
  }

 private:
  // How long a track takes to fade out, and the next to come up under it.
  static constexpr int kFadeMs = 400;

  // Crossfades to `track`, looping it or not. The shared half of Play and
  // PlayOnce.
  void Start(std::string_view track, bool looping);
  // Frees the slot that was fading out, whether or not it has finished.
  void DropFading();
  // Moves the live slot to fading and starts it on its way out.
  void FadeOutLive();

  bool ready_ = false;
  ma_context context_;
  ma_engine engine_;
  // Two slots: the one playing, and the one fading out behind it. A slot
  // owns its decoder, which has to outlive the sound reading from it.
  ma_decoder decoders_[2];
  ma_sound sounds_[2];
  bool loaded_[2] = {false, false};
  int live_ = 0;
  std::string playing_;
  // How long the live track runs, or 0 where the decoder could not say.
  float length_seconds_ = 0.0f;
  int volume_ = 10;
};

}  // namespace ms

#endif  // MS_SRC_AUDIO_MUSIC_PLAYER_H_
