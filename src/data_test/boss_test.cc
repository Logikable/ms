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

#include "src/frontend/screens/boss_fight_panel.h"
#include "src/proto_loader.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"
#include "src/spawn.h"
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
          EXPECT_GT(SpawnCount(spawn), 0)
              << entry.first << " spawns " << spawn.mob() << " zero times";
          EXPECT_EQ(spawn.count(), 0)
              << entry.first << " counts " << spawn.mob()
              << " twice: a boss spawn is counted by its spots";
          EXPECT_TRUE(mobs.at(spawn.mob()).boss())
              << spawn.mob() << " is fought as a boss but is not marked one";
        }
      }
    }
  }
  EXPECT_GT(phases, 0) << "no boss in the catalog has a phase";
}

// A fight with no clock could not be lost, and one with no reset could be run
// all day -- both of which the boss screen is built around not being true. A
// fight that is not built yet is exempt: it states its HP and nothing else.
TEST(BossDataTest, EveryDifficultyIsNamedClockedAndReset) {
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    EXPECT_FALSE(entry.second.name().empty()) << entry.first;
    EXPECT_GT(entry.second.difficulties_size(), 0) << entry.first;
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      EXPECT_FALSE(difficulty.name().empty()) << entry.first;
      if (difficulty.coming_soon()) {
        continue;
      }
      EXPECT_GT(difficulty.time_limit_seconds(), 0)
          << entry.first << " " << difficulty.name();
      EXPECT_NE(difficulty.reset(), RESET_PERIOD_UNSPECIFIED)
          << entry.first << " " << difficulty.name();
    }
  }
}

// EXP and meso belong to the fight, not to the body that ends it: the clear
// pays them once, flat, and the reward path ignores whatever a boss mob
// carries. A number left on the mob is a payout nobody ever receives.
TEST(BossDataTest, EveryBuiltFightPaysFromItsOwnTable) {
  std::map<std::string, Mob> mobs = LoadMobs();
  for (const std::pair<const std::string, Mob>& entry : mobs) {
    if (!entry.second.boss()) {
      continue;
    }
    EXPECT_EQ(entry.second.exp(), 0)
        << entry.first << " carries EXP a boss fight never pays out";
  }
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (difficulty.coming_soon()) {
        continue;
      }
      EXPECT_GT(difficulty.exp(), 0)
          << entry.first << " " << difficulty.name() << " pays no EXP";
      EXPECT_GT(difficulty.meso(), 0)
          << entry.first << " " << difficulty.name() << " pays no meso";
    }
  }
}

// A fight that is not built yet must say nothing it has not decided: the
// detail panel shows its HP alone, and a reward or a gate left in the file
// would be a promise the screen never shows and the fight never keeps.
TEST(BossDataTest, AComingSoonDifficultyStatesOnlyItsPhases) {
  int coming_soon = 0;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (!difficulty.coming_soon()) {
        continue;
      }
      ++coming_soon;
      std::string where = entry.first + " " + difficulty.name();
      EXPECT_EQ(difficulty.time_limit_seconds(), 0) << where;
      EXPECT_EQ(difficulty.reset(), RESET_PERIOD_UNSPECIFIED) << where;
      EXPECT_EQ(difficulty.unlock_level(), 0) << where;
      EXPECT_EQ(difficulty.meso(), 0) << where;
      EXPECT_EQ(difficulty.exp(), 0) << where;
      EXPECT_EQ(difficulty.drops_size(), 0) << where;
    }
  }
  EXPECT_EQ(coming_soon, 4)
      << "Chaos Zakum, Chaos Horntail, Chaos Pink Bean and Hard Magnus";
}

// The fights the screen advertises but cannot yet run, at the HP GMS gives
// them. A number here is read straight off the detail panel, so a typo in the
// mob files is a wrong promise on screen.
TEST(BossDataTest, TheUnbuiltFightsCarryTheirGmsHp) {
  std::map<std::string, Mob> mobs = LoadMobs();
  std::map<std::string, Boss> bosses = LoadBosses();
  struct Expectation {
    const char* boss;
    const char* difficulty;
    std::vector<int64_t> phase_hp;
  };
  const std::vector<Expectation> kExpected = {
      {"zakum", "Chaos", {84000000000LL, 84000000000LL}},
      {"horntail", "Chaos", {3300000000LL, 3300000000LL, 20000000000LL}},
      {"magnus", "Hard", {120000000000LL}},
      {"pink_bean", "Chaos", {130200000000LL, 69300000000LL}},
  };
  for (const Expectation& want : kExpected) {
    ASSERT_GT(bosses.count(want.boss), 0u) << want.boss;
    const BossDifficulty* found = nullptr;
    for (const BossDifficulty& difficulty :
         bosses.at(want.boss).difficulties()) {
      if (difficulty.name() == want.difficulty) {
        found = &difficulty;
      }
    }
    ASSERT_NE(found, nullptr) << want.boss << " " << want.difficulty;
    EXPECT_TRUE(found->coming_soon()) << want.boss;
    ASSERT_EQ(found->phases_size(), static_cast<int>(want.phase_hp.size()));
    for (int i = 0; i < found->phases_size(); ++i) {
      int64_t hp = 0;
      for (const Spawn& spawn : found->phases(i).spawns()) {
        hp += SpawnCount(spawn) * mobs.at(spawn.mob()).max_hp();
      }
      EXPECT_EQ(hp, want.phase_hp[i])
          << want.boss << " " << want.difficulty << " phase " << i + 1;
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
        EXPECT_NE(drop.drop_case(), MobDrop::DROP_NOT_SET)
            << entry.first << " has a drop that names nothing";
        if (!drop.has_equip()) {
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

// Every built fight pays a shard of the soul it took, and the naming is what
// the boss screen prints: a fight whose shard is missing or misnamed drops
// nothing a player can tell apart from another boss's.
TEST(BossDataTest, EveryBuiltFightDropsItsOwnSoulShard) {
  std::map<std::string, ItemPrototype> items = LoadItems();
  int fights = 0;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (difficulty.coming_soon()) {
        continue;
      }
      ++fights;
      std::vector<std::string> shards;
      for (const MobDrop& drop : difficulty.drops()) {
        if (!drop.has_equip() &&
            drop.item().find("soul_shard") != std::string::npos) {
          shards.push_back(drop.item());
        }
      }
      std::string where = entry.first + " " + difficulty.name();
      ASSERT_EQ(shards.size(), 1u)
          << where << " drops no soul shard of its own";
      EXPECT_EQ(items.at(shards[0]).name(),
                entry.second.name() + "'s Soul Shard")
          << where;
      EXPECT_EQ(items.at(shards[0]).category(), ITEM_CATEGORY_ETC) << where;
    }
  }
  EXPECT_EQ(fights, 7) << "Zakum, Horntail, Magnus, Pink Bean, Arkarium and "
                          "both difficulties of Hilla";
}

// Zakum is the first boss and the one the screen was built against, so his
// numbers are pinned: the shape of the fight is a design decision, not data
// that should drift.
TEST(BossDataTest, NormalZakumIsEightArmsThenTheBody) {
  std::map<std::string, Boss> bosses = LoadBosses();
  ASSERT_GT(bosses.count("zakum"), 0u);
  const Boss& zakum = bosses.at("zakum");
  ASSERT_GT(zakum.difficulties_size(), 0);
  const BossDifficulty& normal = zakum.difficulties(0);
  EXPECT_EQ(normal.name(), "Normal");
  EXPECT_EQ(normal.reset(), RESET_PERIOD_DAILY);
  EXPECT_EQ(normal.time_limit_seconds(), 300);
  ASSERT_EQ(normal.phases_size(), 2);
  ASSERT_EQ(normal.phases(0).spawns_size(), 1);
  EXPECT_EQ(normal.phases(0).spawns(0).mob(), "zakum_arm");
  EXPECT_EQ(SpawnCount(normal.phases(0).spawns(0)), 8);
  ASSERT_EQ(normal.phases(1).spawns_size(), 1);
  EXPECT_EQ(normal.phases(1).spawns(0).mob(), "zakum");
  EXPECT_EQ(SpawnCount(normal.phases(1).spawns(0)), 1);
  EXPECT_EQ(normal.meso(), 3062500);
  ASSERT_EQ(normal.drops_size(), 3);
  EXPECT_EQ(normal.drops(0).equip(), "aquatic_letter_eye_accessory");
  EXPECT_EQ(normal.drops(0).per_kill(), 1.0);
  EXPECT_EQ(normal.drops(1).equip(), "condensed_power_crystal");
  EXPECT_EQ(normal.drops(1).per_kill(), 1.0);
  EXPECT_EQ(normal.drops(2).item(), "zakums_soul_shard");
  EXPECT_EQ(normal.drops(2).per_kill(), 1.0);
}

// The last boss the game opens, and the first whose gate is thirty levels
// above the body behind it: he is fought at 160 and is level 130. Pinned for
// the reason Zakum's numbers are -- the shape of a fight is a design decision.
TEST(BossDataTest, NormalMagnusIsOneBodyBehindALateGate) {
  std::map<std::string, Boss> bosses = LoadBosses();
  std::map<std::string, Mob> mobs = LoadMobs();
  ASSERT_GT(bosses.count("magnus"), 0u);
  const BossDifficulty& normal = bosses.at("magnus").difficulties(0);
  EXPECT_EQ(normal.name(), "Normal");
  EXPECT_EQ(normal.reset(), RESET_PERIOD_DAILY);
  EXPECT_EQ(normal.time_limit_seconds(), 600);
  EXPECT_EQ(normal.unlock_level(), 160);
  EXPECT_EQ(normal.meso(), 12960000);
  EXPECT_EQ(normal.exp(), 5300000);
  ASSERT_EQ(normal.phases_size(), 1);
  ASSERT_EQ(normal.phases(0).spawns_size(), 1);
  EXPECT_EQ(SpawnCount(normal.phases(0).spawns(0)), 1);
  const Mob& magnus = mobs.at("magnus");
  EXPECT_EQ(magnus.level(), 130);
  EXPECT_EQ(magnus.max_hp(), 6000000000LL);
  EXPECT_EQ(magnus.pdr(), 50);
  ASSERT_EQ(normal.drops_size(), 3);
  EXPECT_EQ(normal.drops(0).equip(), "crystal_ventus_badge");
  EXPECT_EQ(normal.drops(0).per_kill(), 1.0);
  EXPECT_EQ(normal.drops(1).equip(), "royal_black_metal_shoulder");
  EXPECT_EQ(normal.drops(1).per_kill(), 1.0);
  EXPECT_EQ(normal.drops(2).item(), "magnuss_soul_shard");
  EXPECT_EQ(normal.drops(2).per_kill(), 1.0);
}

// The biggest fight in the game and the only one whose first phase is most of
// it: the five statues are 5.55B of the 7.65B. Pinned for the reason Zakum's
// and Magnus's numbers are -- the shape of a fight is a design decision.
TEST(BossDataTest, NormalPinkBeanIsFiveStatuesThenTheBean) {
  std::map<std::string, Boss> bosses = LoadBosses();
  std::map<std::string, Mob> mobs = LoadMobs();
  ASSERT_GT(bosses.count("pink_bean"), 0u);
  const BossDifficulty& normal = bosses.at("pink_bean").difficulties(0);
  EXPECT_EQ(normal.name(), "Normal");
  EXPECT_EQ(normal.reset(), RESET_PERIOD_DAILY);
  EXPECT_EQ(normal.time_limit_seconds(), 600);
  EXPECT_EQ(normal.unlock_level(), 170);
  EXPECT_EQ(normal.meso(), 7022500);
  // The statues' 3,300,000 and the bean's 6,290,000, paid as one flat number.
  EXPECT_EQ(normal.exp(), 9590000);
  ASSERT_EQ(normal.phases_size(), 2);
  ASSERT_EQ(normal.phases(0).spawns_size(), 5);
  ASSERT_EQ(normal.phases(1).spawns_size(), 1);
  EXPECT_EQ(normal.phases(1).spawns(0).mob(), "pink_bean");

  int64_t statues = 0;
  for (const Spawn& spawn : normal.phases(0).spawns()) {
    const Mob& statue = mobs.at(spawn.mob());
    EXPECT_EQ(SpawnCount(spawn), 1) << spawn.mob();
    EXPECT_EQ(statue.level(), 180) << spawn.mob();
    EXPECT_EQ(statue.pdr(), 60) << spawn.mob();
    statues += statue.max_hp();
  }
  EXPECT_EQ(statues, 5550000000LL);
  const Mob& bean = mobs.at("pink_bean");
  EXPECT_EQ(bean.level(), 180);
  EXPECT_EQ(bean.max_hp(), 2100000000LL);
  EXPECT_EQ(bean.pdr(), 70);

  ASSERT_EQ(normal.drops_size(), 4);
  EXPECT_EQ(normal.drops(0).equip(), "black_bean_mark");
  EXPECT_EQ(normal.drops(1).equip(), "golden_clover_belt");
  EXPECT_EQ(normal.drops(2).equip(), "pink_holy_cup");
  EXPECT_EQ(normal.drops(3).item(), "pink_beans_soul_shard");
}

// The last fight the game opens and the biggest single body in it: 12.6B
// behind 90% PDR, on the ten-minute clock Horntail set. Pinned for the reason
// Zakum's and Magnus's numbers are -- the shape of a fight is a design
// decision, not data that should drift.
TEST(BossDataTest, NormalArkariumIsOneBodyBehindNinetyPdr) {
  std::map<std::string, Boss> bosses = LoadBosses();
  std::map<std::string, Mob> mobs = LoadMobs();
  ASSERT_GT(bosses.count("arkarium"), 0u);
  const BossDifficulty& normal = bosses.at("arkarium").difficulties(0);
  EXPECT_EQ(normal.name(), "Normal");
  EXPECT_EQ(normal.reset(), RESET_PERIOD_DAILY);
  EXPECT_EQ(normal.time_limit_seconds(), 600);
  EXPECT_EQ(normal.unlock_level(), 180);
  EXPECT_EQ(normal.meso(), 12602500);
  EXPECT_EQ(normal.exp(), 50000000);
  ASSERT_EQ(normal.phases_size(), 1);
  ASSERT_EQ(normal.phases(0).spawns_size(), 1);
  EXPECT_EQ(SpawnCount(normal.phases(0).spawns(0)), 1);
  const Mob& arkarium = mobs.at("arkarium");
  EXPECT_EQ(arkarium.level(), 170);
  EXPECT_EQ(arkarium.max_hp(), 12600000000LL);
  EXPECT_EQ(arkarium.pdr(), 90);
  ASSERT_EQ(normal.drops_size(), 2);
  EXPECT_EQ(normal.drops(0).equip(), "dominator_pendant");
  EXPECT_EQ(normal.drops(1).item(), "arkariums_soul_shard");
}

// The only fight in the game with a second difficulty built, and the only body
// behind 100% PDR. Pinned for the reason Zakum's numbers are.
TEST(BossDataTest, HardHillaIsTheSameFightBehindALaterGate) {
  std::map<std::string, Boss> bosses = LoadBosses();
  std::map<std::string, Mob> mobs = LoadMobs();
  ASSERT_GT(bosses.count("hilla"), 0u);
  ASSERT_EQ(bosses.at("hilla").difficulties_size(), 2);
  const BossDifficulty& hard = bosses.at("hilla").difficulties(1);
  EXPECT_EQ(hard.name(), "Hard");
  EXPECT_FALSE(hard.coming_soon());
  EXPECT_EQ(hard.reset(), RESET_PERIOD_DAILY);
  EXPECT_EQ(hard.time_limit_seconds(), 600);
  EXPECT_EQ(hard.unlock_level(), 190);
  EXPECT_EQ(hard.meso(), 56250000);
  EXPECT_EQ(hard.exp(), 20400000);
  const Mob& silver = mobs.at("hard_hilla");
  EXPECT_EQ(silver.level(), 190);
  EXPECT_EQ(silver.max_hp(), 16800000000LL);
  EXPECT_EQ(silver.pdr(), 100);
  ASSERT_EQ(hard.drops_size(), 2);
  EXPECT_EQ(hard.drops(0).equip(), "will_o_the_wisps");
  // The same shard Normal drops: a soul belongs to the boss, not the rung.
  EXPECT_EQ(hard.drops(1).item(), "hillas_soul_shard");
}

// Where the parts stand is data, and two of them in one cell is a bar drawn on
// top of another one.
TEST(BossDataTest, EveryPartStandsSomewhereOfItsOwn) {
  int placed = 0;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const BossPhase& phase : difficulty.phases()) {
        std::string where = entry.first + " " + difficulty.name();
        std::map<int, std::vector<int>> rows;
        for (const Spawn& spawn : phase.spawns()) {
          EXPECT_GT(spawn.spots_size(), 0)
              << where << " does not stand " << spawn.mob() << " anywhere";
          for (const ArenaSpot& spot : spawn.spots()) {
            ++placed;
            EXPECT_LT(spot.x(), phase.arena_width())
                << where << " reaches past the right of its arena";
            EXPECT_LT(spot.y(), phase.arena_height())
                << where << " reaches past the bottom of its arena";
            rows[spot.y()].push_back(spot.x());
          }
        }
        for (const ArenaSpot& spot : phase.player_spots()) {
          rows[spot.y()].push_back(spot.x());
        }
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

// A phase with nowhere to stand starts the player at the origin, on top of
// whatever is drawn there.
TEST(BossDataTest, EveryPhaseStandsThePlayerInsideItsArena) {
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const BossPhase& phase : difficulty.phases()) {
        std::string where = entry.first + " " + difficulty.name();
        EXPECT_GT(phase.player_spots_size(), 0)
            << where << " gives the player nowhere to stand";
        for (const ArenaSpot& spot : phase.player_spots()) {
          EXPECT_LT(spot.x(), phase.arena_width())
              << where << " stands the player past the right of its arena";
          EXPECT_LT(spot.y(), phase.arena_height())
              << where << " stands the player past the bottom of its arena";
        }
      }
    }
  }
}

// How much room each fight gives the player is a design decision, so the
// count per phase is pinned: five on the floor of every fight, plus the two
// ledges over the ends of Zakum's first phase, and six around each of
// Horntail's heads and six around the dragon. Every difficulty of a boss is
// laid out alike, and every phase holds more than a full party, so a party of
// three always has somewhere left to walk.
TEST(BossDataTest, EveryFightOffersTheSpotsItWasDesignedWith) {
  std::map<std::string, std::vector<int>> expected = {
      {"zakum", {7, 5}}, {"hilla", {5}},        {"horntail", {6, 6, 6}},
      {"magnus", {5}},   {"pink_bean", {5, 5}}, {"arkarium", {5}}};
  std::map<std::string, Boss> bosses = LoadBosses();
  for (const std::pair<const std::string, std::vector<int>>& want : expected) {
    ASSERT_GT(bosses.count(want.first), 0u) << want.first;
    for (const BossDifficulty& difficulty :
         bosses.at(want.first).difficulties()) {
      std::string where = want.first + " " + difficulty.name();
      ASSERT_EQ(difficulty.phases_size(), static_cast<int>(want.second.size()))
          << where;
      for (int i = 0; i < difficulty.phases_size(); ++i) {
        EXPECT_EQ(difficulty.phases(i).player_spots_size(), want.second[i])
            << where << " phase " << i + 1;
      }
    }
  }
}

// One grid for every fight in the game, so a room is the same shape whichever
// boss is standing in it -- and, more to the point, so no arena outgrows the
// smallest terminal the game is laid out for. See kArenaColumns.
TEST(BossDataTest, EveryArenaStandsOnTheOneGrid) {
  int phases = 0;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const BossPhase& phase : difficulty.phases()) {
        ++phases;
        std::string where = entry.first + " " + difficulty.name();
        EXPECT_EQ(phase.arena_width(), kArenaColumns) << where;
        EXPECT_EQ(phase.arena_height(), kArenaRows) << where;
      }
    }
  }
  EXPECT_GT(phases, 0);
}

}  // namespace
}  // namespace ms
