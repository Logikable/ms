#include "src/audio/music_director.h"

#include <random>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "src/audio/track_title.h"
#include "src/audio/tracks.h"
#include "src/build_config.h"

namespace ms {
namespace {

class MusicDirectorTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!kAudioEnabled || TracksByTitle().empty()) {
      GTEST_SKIP() << "built with --define=audio=off";
    }
  }

  // The song list, which is what the playlist walks.
  std::vector<std::string_view> Songs() const {
    return TracksByTitle();
  }

  // The null backend runs miniaudio's own clock, so nothing here needs a
  // sound card.
  MusicPlayer player_{MusicPlayer::Backend::kNull};
  std::mt19937 rng_{7};
  MusicDirector director_{player_, rng_};
};

TEST_F(MusicDirectorTest, FollowMapLoopsTheMapsOwnTrack) {
  std::vector<std::string_view> songs = Songs();
  director_.Update(JUKEBOX_MODE_FOLLOW_MAP, songs[0]);
  EXPECT_EQ(player_.playing(), songs[0]);
  EXPECT_TRUE(player_.looping());

  // Walking somewhere else moves the music with the player.
  director_.Update(JUKEBOX_MODE_FOLLOW_MAP, songs[1]);
  EXPECT_EQ(player_.playing(), songs[1]);
  EXPECT_TRUE(player_.looping());
}

// The rule the three modes are built on: a track playing through once is
// never cut short, whatever the mode does afterwards.
TEST_F(MusicDirectorTest, APickedTrackPlaysOutAndTheModeTakesOverAtItsEnd) {
  std::vector<std::string_view> songs = Songs();
  director_.Update(JUKEBOX_MODE_FOLLOW_MAP, songs[0]);
  director_.Play(songs[3]);
  EXPECT_EQ(player_.playing(), songs[3]);
  EXPECT_FALSE(player_.looping());

  // Ticking while it plays leaves it alone; the map's track is not put back.
  director_.Update(JUKEBOX_MODE_FOLLOW_MAP, songs[0]);
  EXPECT_EQ(player_.playing(), songs[3]);
}

TEST_F(MusicDirectorTest, LeavingTheMapsTrackForAListTakesTheLoopOffIt) {
  std::vector<std::string_view> songs = Songs();
  director_.Update(JUKEBOX_MODE_FOLLOW_MAP, songs[0]);
  ASSERT_TRUE(player_.looping());
  director_.Update(JUKEBOX_MODE_PLAYLIST, songs[0]);
  // Still the same track, but on its way to an end the list can follow.
  EXPECT_EQ(player_.playing(), songs[0]);
  EXPECT_FALSE(player_.looping());
}

TEST_F(MusicDirectorTest, ThePlaylistIsTheSongListAndComesRound) {
  std::vector<std::string_view> songs = Songs();
  EXPECT_EQ(director_.NextInPlaylist(songs[0]), songs[1]);
  EXPECT_EQ(director_.NextInPlaylist(songs.back()), songs.front());
  // Nothing playing yet, and a name the list does not carry, both start it.
  EXPECT_EQ(director_.NextInPlaylist(""), songs.front());
  EXPECT_EQ(director_.NextInPlaylist("NoSuchTrack"), songs.front());
}

TEST_F(MusicDirectorTest, PauseHoldsTheMusicAndStopsAnythingNewStarting) {
  std::vector<std::string_view> songs = Songs();
  director_.Update(JUKEBOX_MODE_FOLLOW_MAP, songs[0]);
  director_.TogglePause();
  EXPECT_TRUE(director_.paused());

  // Walking to another map while paused stays silent.
  director_.Update(JUKEBOX_MODE_FOLLOW_MAP, songs[1]);
  EXPECT_EQ(player_.playing(), songs[0]);

  director_.TogglePause();
  EXPECT_FALSE(director_.paused());
  director_.Update(JUKEBOX_MODE_FOLLOW_MAP, songs[1]);
  EXPECT_EQ(player_.playing(), songs[1]);
}

// Picking a song is asking to hear it, so it lets a paused player go.
TEST_F(MusicDirectorTest, PickingASongUnpauses) {
  std::vector<std::string_view> songs = Songs();
  director_.Update(JUKEBOX_MODE_FOLLOW_MAP, songs[0]);
  director_.TogglePause();
  director_.Play(songs[2]);
  EXPECT_FALSE(director_.paused());
  EXPECT_EQ(player_.playing(), songs[2]);
}

TEST_F(MusicDirectorTest, NudgeMovesTheCursorAndStopsAtTheEnds) {
  std::vector<std::string_view> songs = Songs();
  director_.Play(songs[0]);
  ASSERT_GT(director_.length_seconds(), kSkipSeconds);
  // Back from the head of a track is the head of it, not a negative cursor.
  director_.Nudge(-kSkipSeconds);
  EXPECT_GE(director_.position_seconds(), 0.0f);
  director_.Nudge(kSkipSeconds);
  EXPECT_GE(director_.position_seconds(), kSkipSeconds - 1.0f);
  // Past the end clamps to the track's length rather than running off it.
  director_.Nudge(director_.length_seconds() * 2.0f);
  EXPECT_LE(director_.position_seconds(), director_.length_seconds());
}

}  // namespace
}  // namespace ms
