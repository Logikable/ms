// Checks the shipped bosses against the mob catalog. A phase naming a mob file
// that does not exist spawns nothing, which would leave the player staring at
// an empty fight until the clock ran out.
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/proto_loader.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
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

std::map<std::string, EquipPrototype> LoadEquips() {
  return LoadTextProtoDir<EquipPrototype>(
      TestRunfiles()->Rlocation("ms/data/equip"));
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

// A drop names one catalog or the other, and a name neither holds is granted
// to nobody -- silently, since the reward path skips what it cannot find.
TEST(BossDataTest, EveryDropNamesAnItem) {
  std::map<std::string, ItemPrototype> items = LoadItems();
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  int drops = 0;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const MobDrop& drop : difficulty.drops()) {
        ++drops;
        EXPECT_NE(drop.item().empty(), drop.equip().empty())
            << entry.first << " has a drop that is not one item or one equip";
        if (drop.equip().empty()) {
          EXPECT_GT(items.count(drop.item()), 0u)
              << entry.first << " drops \"" << drop.item()
              << "\", which no item file defines";
        } else {
          EXPECT_GT(equips.count(drop.equip()), 0u)
              << entry.first << " drops \"" << drop.equip()
              << "\", which no equip file defines";
        }
        EXPECT_GT(drop.per_kill(), 0.0) << entry.first;
      }
    }
  }
  EXPECT_GT(drops, 0) << "no boss in the catalog drops anything";
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
  ASSERT_EQ(normal.phases(0).spawns_size(), 8);
  for (const Spawn& arm : normal.phases(0).spawns()) {
    EXPECT_EQ(arm.mob(), "zakum_arm");
    EXPECT_EQ(arm.count(), 1);
  }
  ASSERT_EQ(normal.phases(1).spawns_size(), 1);
  EXPECT_EQ(normal.phases(1).spawns(0).mob(), "zakum");
  EXPECT_EQ(normal.phases(1).spawns(0).count(), 1);
  EXPECT_EQ(normal.meso(), 3062500);
  ASSERT_EQ(normal.drops_size(), 3);
  EXPECT_EQ(normal.drops(0).equip(), "aquatic_letter_eye_accessory");
  EXPECT_EQ(normal.drops(0).per_kill(), 0.5);
  EXPECT_EQ(normal.drops(1).equip(), "condensed_power_crystal");
  EXPECT_EQ(normal.drops(1).per_kill(), 0.5);
  EXPECT_EQ(normal.drops(2).item(), "zakums_soul_shard");
  EXPECT_EQ(normal.drops(2).per_kill(), 1.0);
}

// Where the parts stand is data, and two of them in one cell is a bar drawn on
// top of another one.
TEST(BossDataTest, EveryPartStandsSomewhereOfItsOwn) {
  int placed = 0;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const BossPhase& phase : difficulty.phases()) {
        std::string where = entry.first + " " + difficulty.name();
        ASSERT_EQ(phase.spots_size(), phase.spawns_size())
            << where << " does not stand every spawn somewhere";
        std::map<int, std::vector<int>> rows;
        for (int i = 0; i < phase.spots_size(); ++i) {
          ++placed;
          EXPECT_EQ(phase.spawns(i).count(), 1)
              << where << " stands more than one monster in one spot";
          EXPECT_LT(phase.spots(i).x(), phase.arena_width())
              << where << " reaches past the right of its arena";
          EXPECT_LT(phase.spots(i).y(), phase.arena_height())
              << where << " reaches past the bottom of its arena";
          rows[phase.spots(i).y()].push_back(phase.spots(i).x());
        }
        rows[phase.player().y()].push_back(phase.player().x());
        for (std::pair<const int, std::vector<int>>& row : rows) {
          std::sort(row.second.begin(), row.second.end());
          for (std::size_t i = 1; i < row.second.size(); ++i) {
            EXPECT_GT(row.second[i], row.second[i - 1])
                << entry.first << " " << difficulty.name()
                << " overlaps two bars on row " << row.first;
          }
        }
      }
    }
  }
  EXPECT_GT(placed, 0);
}

}  // namespace
}  // namespace ms
