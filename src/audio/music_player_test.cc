#include "src/audio/music_player.h"

#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "src/account.h"
#include "src/audio/tracks.h"
#include "src/build_config.h"

namespace ms {
namespace {

// A track this build includes. Which one doesn't matter: the player works with
// names, and the test shouldn't depend on a choice the data may change.
std::string_view AnyTrack() {
  std::vector<std::string_view> names = BgmTrackNames();
  return names.empty() ? std::string_view() : names.front();
}

// The null backend runs on miniaudio's own clock, so these pass on a machine
// with no sound card, like this one.
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
  // Requesting what's already playing is what the frame loop does every tick.
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

// A looping track is the map's: it never ends, so it never requests another.
TEST(MusicPlayerTest, ALoopingTrackIsNeverEnding) {
  if (AnyTrack().empty()) {
    GTEST_SKIP() << "built with --define=audio=off";
  }
  MusicPlayer player = MakePlayer();
  EXPECT_TRUE(player.ending()) << "nothing playing";
  player.Play(AnyTrack());
  EXPECT_FALSE(player.ending());
  // The Jukebox option turns off looping on a track already playing. The track
  // keeps playing; it just has an end now.
  player.StopLooping();
  EXPECT_FALSE(player.looping());
  EXPECT_FALSE(player.ending()) << "a track just started is not near its end";
  EXPECT_EQ(player.playing(), AnyTrack());
}

// When shuffle ends on the exact track the map names, Play must set it looping
// again instead of treating it as already playing.
TEST(MusicPlayerTest, PlayLoopsATrackThatWasPlayingThrough) {
  if (AnyTrack().empty()) {
    GTEST_SKIP() << "built with --define=audio=off";
  }
  MusicPlayer player = MakePlayer();
  player.PlayOnce(AnyTrack());
  EXPECT_FALSE(player.looping());
  player.Play(AnyTrack());
  EXPECT_EQ(player.playing(), AnyTrack());
  EXPECT_TRUE(player.looping());
}

TEST(MusicPlayerTest, PlayOnceRestartsTheSameTrack) {
  if (AnyTrack().empty()) {
    GTEST_SKIP() << "built with --define=audio=off";
  }
  MusicPlayer player = MakePlayer();
  player.PlayOnce(AnyTrack());
  EXPECT_EQ(player.playing(), AnyTrack());
  EXPECT_FALSE(player.ending());
  // Play would treat this as the track already playing and do nothing. A
  // library of one hits exactly this case.
  player.PlayOnce(AnyTrack());
  EXPECT_EQ(player.playing(), AnyTrack());
  EXPECT_FALSE(player.ending());
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
