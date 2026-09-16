#include "src/audio/jukebox.h"

#include <random>
#include <set>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "src/audio/tracks.h"

namespace ms {
namespace {

// A library big enough to see the window fill and still leave a choice.
std::vector<std::string_view> Library(int size) {
  static const char* kNames[] = {"a", "b", "c", "d", "e", "f", "g", "h",
                                 "i", "j", "k", "l", "m", "n", "o", "p"};
  return std::vector<std::string_view>(kNames, kNames + size);
}

// Every pick of `count`, in order.
std::vector<std::string_view> Picks(Jukebox& jukebox, int count) {
  std::vector<std::string_view> picks;
  for (int i = 0; i < count; ++i) {
    picks.push_back(jukebox.Next());
  }
  return picks;
}

// Whether any track in `picks` comes round again inside `window` picks.
bool RepeatsWithin(const std::vector<std::string_view>& picks, int window) {
  for (int i = 0; i < static_cast<int>(picks.size()); ++i) {
    for (int j = i + 1; j <= i + window && j < static_cast<int>(picks.size());
         ++j) {
      if (picks[i] == picks[j]) {
        return true;
      }
    }
  }
  return false;
}

TEST(JukeboxTest, HoldsATrackBackForTheWholeWindow) {
  std::mt19937 rng(7);
  Jukebox jukebox(Library(16), rng);
  // 16 tracks, so the window is one short of the library.
  EXPECT_EQ(jukebox.remembered(), 15);
  std::vector<std::string_view> picks = Picks(jukebox, 200);
  EXPECT_FALSE(RepeatsWithin(picks, jukebox.remembered()));
  // Holding fifteen back leaves one choice, so every track gets a turn.
  EXPECT_EQ(std::set(picks.begin(), picks.end()).size(), 16u);
}

TEST(JukeboxTest, WindowStopsAtThirty) {
  std::mt19937 rng(11);
  Jukebox jukebox(BgmTrackNames(), rng);
  if (BgmTrackNames().size() <= Jukebox::kHistory) {
    GTEST_SKIP() << "needs a library past the window";
  }
  EXPECT_EQ(jukebox.remembered(), Jukebox::kHistory);
  std::vector<std::string_view> picks = Picks(jukebox, 300);
  EXPECT_FALSE(RepeatsWithin(picks, Jukebox::kHistory));
}

TEST(JukeboxTest, ASingleTrackRepeats) {
  std::mt19937 rng(3);
  Jukebox jukebox(Library(1), rng);
  EXPECT_EQ(jukebox.remembered(), 0);
  EXPECT_EQ(jukebox.Next(), "a");
  EXPECT_EQ(jukebox.Next(), "a");
}

TEST(JukeboxTest, TwoTracksAlternate) {
  std::mt19937 rng(5);
  Jukebox jukebox(Library(2), rng);
  EXPECT_EQ(jukebox.remembered(), 1);
  std::vector<std::string_view> picks = Picks(jukebox, 6);
  for (int i = 1; i < 6; ++i) {
    EXPECT_NE(picks[i], picks[i - 1]) << "pick " << i;
  }
}

TEST(JukeboxTest, ASilentBuildPicksNothing) {
  std::mt19937 rng(1);
  Jukebox jukebox({}, rng);
  EXPECT_EQ(jukebox.remembered(), 0);
  EXPECT_EQ(jukebox.Next(), "");
}

}  // namespace
}  // namespace ms
