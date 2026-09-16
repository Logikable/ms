#include "src/audio/music_player.h"

#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "src/account.h"
#include "src/audio/tracks.h"
#include "src/build_config.h"

namespace ms {
namespace {

// A track this build actually carries. Which one does not matter: the
// player is asked about names, and the test should not pin a pick that the
// data is free to change.
std::string_view AnyTrack() {
  std::vector<std::string_view> names = BgmTrackNames();
  return names.empty() ? std::string_view() : names.front();
}

// The null backend runs miniaudio's own clock, so these pass on a box with
// no sound card -- which is what this one is.
MusicPlayer MakePlayer() {
  return MusicPlayer(MusicPlayer::Backend::kNull);
}

TEST(MusicPlayerTest, StartsSilentAtTen) {
  MusicPlayer player = MakePlayer();
  EXPECT_EQ(player.ready(), kAudioEnabled);
  EXPECT_EQ(player.volume(), kDefaultBgmVolume);
  EXPECT_EQ(player.playing(), "");
}

TEST(MusicPlayerTest, PlaysAndStops) {
  if (AnyTrack().empty()) {
    GTEST_SKIP() << "built with --define=audio=off";
  }
  MusicPlayer player = MakePlayer();
  player.Play(AnyTrack());
  EXPECT_EQ(player.playing(), AnyTrack());
  // Asking again for what is already on is the frame loop's every tick.
  player.Play(AnyTrack());
  EXPECT_EQ(player.playing(), AnyTrack());
  player.Stop();
  EXPECT_EQ(player.playing(), "");
}

TEST(MusicPlayerTest, SwitchesTracks) {
  std::vector<std::string_view> names = BgmTrackNames();
  if (names.size() < 2) {
    GTEST_SKIP() << "needs two tracks";
  }
  MusicPlayer player = MakePlayer();
  player.Play(names[0]);
  player.Play(names[1]);
  EXPECT_EQ(player.playing(), names[1]);
}

TEST(MusicPlayerTest, UnknownTrackPlaysNothing) {
  MusicPlayer player = MakePlayer();
  player.Play("no such track");
  EXPECT_EQ(player.playing(), "");
}

TEST(MusicPlayerTest, VolumeClamps) {
  MusicPlayer player = MakePlayer();
  player.SetVolume(55);
  EXPECT_EQ(player.volume(), 55);
  player.SetVolume(-20);
  EXPECT_EQ(player.volume(), 0);
  player.SetVolume(400);
  EXPECT_EQ(player.volume(), 100);
}

}  // namespace
}  // namespace ms
