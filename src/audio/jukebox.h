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
#ifndef MS_SRC_AUDIO_JUKEBOX_H_
#define MS_SRC_AUDIO_JUKEBOX_H_

#include <string>
#include <string_view>

#include "third_party/miniaudio/miniaudio.h"

namespace ms {

class Jukebox {
 public:
  // Which device to open. Tests take kNull, which runs miniaudio's own clock
  // and needs no sound card -- this box has no ALSA card at all.
  enum class Backend { kDevice, kNull };

  explicit Jukebox(Backend backend = Backend::kDevice);
  ~Jukebox();
  Jukebox(const Jukebox&) = delete;
  Jukebox& operator=(const Jukebox&) = delete;

  // Whether an engine came up. False leaves every call below doing nothing.
  bool ready() const {
    return ready_;
  }

  // Crossfades to `track`, looping it. Does nothing if it is already playing,
  // and stops if this build has no such track.
  void Play(std::string_view track);
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
  int volume_ = 10;
};

}  // namespace ms

#endif  // MS_SRC_AUDIO_JUKEBOX_H_
