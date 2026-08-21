// Checks the shipped bosses against the mob catalog. A phase naming a mob file
// that does not exist spawns nothing, which would leave the player staring at
// an empty fight until the clock ran out.
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "src/proto_loader.h"
#include "src/protos/boss.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

std::unique_ptr<Runfiles> TestRunfiles() {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  EXPECT_NE(runfiles, nullptr) << err;
  return runfiles;
}

std::map<std::string, Boss> LoadBosses() {
  return LoadTextProtoDir<Boss>(TestRunfiles()->Rlocation("ms/data/bosses"));
}

std::map<std::string, Mob> LoadMobs() {
  return LoadTextProtoDir<Mob>(TestRunfiles()->Rlocation("ms/data/mobs"));
}

std::map<std::string, ItemPrototype> LoadItems() {
  return LoadTextProtoDir<ItemPrototype>(
      TestRunfiles()->Rlocation("ms/data/items"));
}

TEST(BossDataTest, EveryPhaseSpawnsAKnownMob) {
  std::map<std::string, Mob> mobs = LoadMobs();
  int phases = 0;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      EXPECT_GT(difficulty.phases_size(), 0)
          << entry.first << " " << difficulty.name() << " has no phases";
      for (const BossPhase& phase : difficulty.phases()) {
        ++phases;
        EXPECT_GT(phase.spawns_size(), 0)
            << entry.first << " " << difficulty.name() << " has an empty phase";
        for (const Spawn& spawn : phase.spawns()) {
          ASSERT_GT(mobs.count(spawn.mob()), 0u)
              << entry.first << " spawns \"" << spawn.mob()
              << "\", which no mob file defines";
          EXPECT_GT(spawn.count(), 0)
              << entry.first << " spawns " << spawn.mob() << " zero times";
          EXPECT_TRUE(mobs.at(spawn.mob()).boss())
              << spawn.mob() << " is fought as a boss but is not marked one";
        }
      }
    }
  }
  EXPECT_GT(phases, 0) << "no boss in the catalog has a phase";
}

// A fight with no clock could not be lost, and one with no reset could be run
// all day -- both of which the boss screen is built around not being true.
TEST(BossDataTest, EveryDifficultyIsNamedClockedAndReset) {
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    EXPECT_FALSE(entry.second.name().empty()) << entry.first;
    EXPECT_GT(entry.second.difficulties_size(), 0) << entry.first;
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      EXPECT_FALSE(difficulty.name().empty()) << entry.first;
      EXPECT_GT(difficulty.time_limit_seconds(), 0)
          << entry.first << " " << difficulty.name();
      EXPECT_NE(difficulty.reset(), RESET_PERIOD_UNSPECIFIED)
          << entry.first << " " << difficulty.name();
    }
  }
}

TEST(BossDataTest, EveryDropNamesAnItem) {
  std::map<std::string, ItemPrototype> items = LoadItems();
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const MobDrop& drop : difficulty.drops()) {
        EXPECT_GT(items.count(drop.item()), 0u)
            << entry.first << " drops \"" << drop.item()
            << "\", which no item file defines";
        EXPECT_GT(drop.per_kill(), 0.0) << entry.first;
      }
    }
  }
}

// Zakum is the first boss and the one the screen was built against, so his
// numbers are pinned: the shape of the fight is a design decision, not data
// that should drift.
TEST(BossDataTest, NormalZakumIsEightArmsThenTheBody) {
  std::map<std::string, Boss> bosses = LoadBosses();
  ASSERT_GT(bosses.count("zakum"), 0u);
  const Boss& zakum = bosses.at("zakum");
  ASSERT_EQ(zakum.difficulties_size(), 1);
  const BossDifficulty& normal = zakum.difficulties(0);
  EXPECT_EQ(normal.name(), "Normal");
  EXPECT_EQ(normal.reset(), RESET_PERIOD_DAILY);
  EXPECT_EQ(normal.time_limit_seconds(), 300);
  ASSERT_EQ(normal.phases_size(), 2);
  ASSERT_EQ(normal.phases(0).spawns_size(), 1);
  EXPECT_EQ(normal.phases(0).spawns(0).mob(), "zakum_arm");
  EXPECT_EQ(normal.phases(0).spawns(0).count(), 8);
  ASSERT_EQ(normal.phases(1).spawns_size(), 1);
  EXPECT_EQ(normal.phases(1).spawns(0).mob(), "zakum");
  EXPECT_EQ(normal.phases(1).spawns(0).count(), 1);
}

}  // namespace
}  // namespace ms
