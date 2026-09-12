#include "src/audio/jukebox.h"

#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "src/audio/tracks.h"

namespace ms {
namespace {

// A track this build actually carries. Which one does not matter: the
// jukebox is asked about names, and the test should not pin a pick that the
// data is free to change.
std::string_view AnyTrack() {
  std::vector<std::string_view> names = BgmTrackNames();
  return names.empty() ? std::string_view() : names.front();
}

// The null backend runs miniaudio's own clock, so these pass on a box with
// no sound card -- which is what this one is.
Jukebox MakeJukebox() {
  return Jukebox(Jukebox::Backend::kNull);
}

TEST(JukeboxTest, StartsSilentAtTen) {
  Jukebox jukebox = MakeJukebox();
  EXPECT_TRUE(jukebox.ready());
  EXPECT_EQ(jukebox.volume(), 10);
  EXPECT_EQ(jukebox.playing(), "");
}

TEST(JukeboxTest, PlaysAndStops) {
  if (AnyTrack().empty()) {
    GTEST_SKIP() << "built with --define=audio=off";
  }
  Jukebox jukebox = MakeJukebox();
  jukebox.Play(AnyTrack());
  EXPECT_EQ(jukebox.playing(), AnyTrack());
  // Asking again for what is already on is the frame loop's every tick.
  jukebox.Play(AnyTrack());
  EXPECT_EQ(jukebox.playing(), AnyTrack());
  jukebox.Stop();
  EXPECT_EQ(jukebox.playing(), "");
}

TEST(JukeboxTest, SwitchesTracks) {
  std::vector<std::string_view> names = BgmTrackNames();
  if (names.size() < 2) {
    GTEST_SKIP() << "needs two tracks";
  }
  Jukebox jukebox = MakeJukebox();
  jukebox.Play(names[0]);
  jukebox.Play(names[1]);
  EXPECT_EQ(jukebox.playing(), names[1]);
}

TEST(JukeboxTest, UnknownTrackPlaysNothing) {
  Jukebox jukebox = MakeJukebox();
  jukebox.Play("no such track");
  EXPECT_EQ(jukebox.playing(), "");
}

TEST(JukeboxTest, VolumeClamps) {
  Jukebox jukebox = MakeJukebox();
  jukebox.SetVolume(55);
  EXPECT_EQ(jukebox.volume(), 55);
  jukebox.SetVolume(-20);
  EXPECT_EQ(jukebox.volume(), 0);
  jukebox.SetVolume(400);
  EXPECT_EQ(jukebox.volume(), 100);
}

}  // namespace
}  // namespace ms
