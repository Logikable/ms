// Checks that every map and boss phase names music the build actually
// carries. A track name is a filename stem, so a typo or a renamed file
// fails silently at run time -- the jukebox finds nothing and plays nothing,
// and the only symptom is a quiet map.
#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <string_view>

#include "src/audio/tracks.h"
#include "src/protos/boss.pb.h"
#include "src/protos/map.pb.h"
#include "src/testing/data_files.h"

namespace ms {
namespace {

std::set<std::string> EmbeddedTracks() {
  std::set<std::string> names;
  for (std::string_view name : BgmTrackNames()) {
    names.emplace(name);
  }
  return names;
}

TEST(BgmTest, EveryMapNamesATrackInTheBuild) {
  std::set<std::string> tracks = EmbeddedTracks();
  if (tracks.empty()) {
    GTEST_SKIP() << "built with --define=audio=off";
  }
  std::map<std::string, MapData> maps = LoadTestData<MapData>("maps");
  // The catalog loading empty would pass every check below, so pin one map
  // that has to be there and the count of the rest.
  ASSERT_GE(maps.size(), 60u);
  ASSERT_EQ(maps.at("right_around_lith_harbor").bgm(), "AboveTheTreetops");
  for (const auto& [stem, map] : maps) {
    EXPECT_FALSE(map.bgm().empty()) << stem << " names no music";
    EXPECT_TRUE(tracks.count(map.bgm()) > 0)
        << stem << " names a track this build has not got: " << map.bgm();
  }
}

TEST(BgmTest, EveryBossOpensOnATrackInTheBuild) {
  std::set<std::string> tracks = EmbeddedTracks();
  if (tracks.empty()) {
    GTEST_SKIP() << "built with --define=audio=off";
  }
  std::map<std::string, Boss> bosses = LoadTestData<Boss>("bosses");
  ASSERT_GE(bosses.size(), 10u);
  ASSERT_EQ(bosses.at("horntail").difficulties(0).phases(2).bgm(), "HonTale");
  for (const auto& [stem, boss] : bosses) {
    for (const BossDifficulty& difficulty : boss.difficulties()) {
      ASSERT_GT(difficulty.phases_size(), 0) << stem;
      // Only the first phase must name one. A later phase saying nothing is
      // how a one-track boss keeps playing what it opened with.
      EXPECT_FALSE(difficulty.phases(0).bgm().empty())
          << stem << " " << difficulty.name() << " opens on no music";
      for (const BossPhase& phase : difficulty.phases()) {
        if (phase.bgm().empty()) {
          continue;
        }
        EXPECT_TRUE(tracks.count(phase.bgm()) > 0)
            << stem << " names a track this build has not got: " << phase.bgm();
      }
    }
  }
}

}  // namespace
}  // namespace ms
