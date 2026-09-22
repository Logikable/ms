#include "src/audio/track_title.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "src/audio/tracks.h"

namespace ms {
namespace {

TEST(TrackTitleTest, SpellsTheStemOutAndPutsTheClientsSpellingRight) {
  EXPECT_EQ(TrackTitle("AboveTheTreetops"), "Above the Treetops");
  EXPECT_EQ(TrackTitle("destructionTown"), "Destruction Town");
  EXPECT_EQ(TrackTitle("Suu1phase"), "Suu Phase 1");
  // The client's own misspellings.
  EXPECT_EQ(TrackTitle("AcientForest"), "Ancient Forest");
  EXPECT_EQ(TrackTitle("ConteminatedSea"), "Contaminated Sea");
  EXPECT_EQ(TrackTitle("FightingPinkBeen"), "Fighting Pink Bean");
}

// A track added to bgm/ before the table hears about it shows up under its
// filename rather than not at all.
TEST(TrackTitleTest, AnUnknownStemAnswersItself) {
  EXPECT_EQ(TrackTitle("NoSuchTrack"), "NoSuchTrack");
  EXPECT_EQ(TrackTitle(""), "");
}

// What a stem looks like and a title does not: a word running into a capital,
// or an underscore standing in for a space. A track added to bgm/ that nobody
// spelt out fails here rather than reaching the screen as its filename.
TEST(TrackTitleTest, EveryTrackInTheBuildReadsAsWords) {
  std::vector<std::string_view> tracks = BgmTrackNames();
  if (tracks.empty()) {
    GTEST_SKIP() << "built with --define=audio=off";
  }
  for (std::string_view track : tracks) {
    std::string title = TrackTitle(track);
    EXPECT_EQ(title.find('_'), std::string::npos) << track;
    for (std::size_t i = 1; i < title.size(); ++i) {
      bool ran_on = std::islower(static_cast<unsigned char>(title[i - 1])) &&
                    std::isupper(static_cast<unsigned char>(title[i]));
      EXPECT_FALSE(ran_on) << track << " reads as \"" << title << "\"";
    }
  }
}

TEST(TrackTitleTest, TheSongListIsInTitleOrderAndHoldsEveryTrack) {
  std::vector<std::string_view> tracks = TracksByTitle();
  ASSERT_EQ(tracks.size(), BgmTrackNames().size());
  for (std::size_t i = 1; i < tracks.size(); ++i) {
    EXPECT_LE(TrackTitle(tracks[i - 1]), TrackTitle(tracks[i]));
  }
}

// The durations are counted from the MP3 frame headers when the build embeds
// them, so a build with music has one for every track.
TEST(TrackTitleTest, EveryTrackKnowsHowLongItRuns) {
  std::vector<std::string_view> tracks = BgmTrackNames();
  if (tracks.empty()) {
    GTEST_SKIP() << "built with --define=audio=off";
  }
  for (std::string_view track : tracks) {
    auto data = BgmTrack(track);
    ASSERT_TRUE(data.has_value()) << track;
    EXPECT_GT(data->duration_ms, 0) << track;
    // Nothing under bgm/ runs past a quarter of an hour; a longer answer
    // means the walk lost its place rather than that the track is long.
    EXPECT_LT(data->duration_ms, 15 * 60 * 1000) << track;
  }
}

}  // namespace
}  // namespace ms
