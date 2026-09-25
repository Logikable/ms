/* Picks the next track when the music is on shuffle.
 *
 * It picks from every track in the build, excluding anything played in the last
 * kHistory picks, so the same few tracks can't keep repeating. A build with
 * fewer tracks than that remembers as many as it can.
 *
 * The picker holds no audio: it returns names, and the MusicPlayer plays them.
 * A build with no music picks nothing, which means silence.
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
  // How many picks back a track counts as too recent to play again.
  static constexpr int kHistory = 30;

  // Picks from this build's music. `rng` belongs to the caller and must outlive
  // the jukebox.
  explicit Jukebox(std::mt19937& rng);
  // Picks from `tracks` instead, so a test can use a library small enough to
  // hit the window's edges.
  Jukebox(std::vector<std::string_view> tracks, std::mt19937& rng);

  // The next track to play, or empty in a build with no music. Never one of the
  // last `remembered()` picks, unless there's nothing else.
  std::string_view Next();

  // How many picks back this jukebox remembers: kHistory, or one less than the
  // library size if that's smaller. A library of one remembers nothing and
  // repeats its only track.
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
