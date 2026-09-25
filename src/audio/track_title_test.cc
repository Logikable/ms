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

// A track added to bgm/ before the table has an entry shows under its filename
// instead of not at all.
TEST(TrackTitleTest, AnUnknownStemAnswersItself) {
  EXPECT_EQ(TrackTitle("NoSuchTrack"), "NoSuchTrack");
  EXPECT_EQ(TrackTitle(""), "");
}

// Stems have words running into capitals, or underscores for spaces; titles
// don't. A track added to bgm/ without a title fails here instead of showing
// its filename on screen.
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

// Durations are computed from the MP3 frame headers when the build embeds the
// music, so a build with music has one for every track.
TEST(TrackTitleTest, EveryTrackKnowsHowLongItRuns) {
  std::vector<std::string_view> tracks = BgmTrackNames();
  if (tracks.empty()) {
    GTEST_SKIP() << "built with --define=audio=off";
  }
  for (std::string_view track : tracks) {
    auto data = BgmTrack(track);
    ASSERT_TRUE(data.has_value()) << track;
    EXPECT_GT(data->duration_ms, 0) << track;
    // Nothing under bgm/ is longer than fifteen minutes; a longer value means
    // the header scan went wrong, not that the track is long.
    EXPECT_LT(data->duration_ms, 15 * 60 * 1000) << track;
  }
}

}  // namespace
}  // namespace ms
