/* Which track plays next when the music is on shuffle.
 *
 * It picks from every track in the build, refusing anything it has played in
 * the last kHistory picks, so the same handful cannot come round again and
 * again. A build with fewer tracks than that remembers what it can.
 *
 * The picker holds no audio: it answers in names, and the MusicPlayer plays
 * them. A build with no music picks nothing, which reads as silence.
 */
#ifndef MS_SRC_AUDIO_JUKEBOX_H_
#define MS_SRC_AUDIO_JUKEBOX_H_

#include <deque>
#include <random>
#include <string_view>
#include <vector>

namespace ms {

class Jukebox {
 public:
  // How many picks back a track is still too recent to play again.
  static constexpr int kHistory = 30;

  // Picks out of this build's own music. `rng` is the caller's, and has to
  // outlive the jukebox.
  explicit Jukebox(std::mt19937& rng);
  // Picks out of `tracks` instead, which is what lets a test hand it a
  // library small enough to see the window's edges.
  Jukebox(std::vector<std::string_view> tracks, std::mt19937& rng);

  // The next track to play, or empty in a build with no music. Never one of
  // the last `remembered()` picks, unless the build has nothing else.
  std::string_view Next();

  // How many picks back this jukebox actually remembers -- kHistory, or one
  // short of the whole library where that is smaller. A library of one
  // remembers nothing and repeats the single track.
  int remembered() const {
    return remembered_;
  }

 private:
  std::vector<std::string_view> tracks_;
  // The last `remembered_` picks, oldest first.
  std::deque<std::string_view> recent_;
  int remembered_ = 0;
  std::mt19937& rng_;
};

}  // namespace ms

#endif  // MS_SRC_AUDIO_JUKEBOX_H_
