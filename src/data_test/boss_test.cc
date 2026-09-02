// Checks the shipped bosses against the mob catalog. A phase naming a mob file
// that does not exist spawns nothing, which would leave the player staring at
// an empty fight until the clock ran out.
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/frontend/screens/boss_fight_panel.h"
#include "src/item/item.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"
#include "src/spawn.h"
#include "src/testing/data_files.h"

namespace ms {
namespace {

std::map<std::string, Boss> LoadBosses() {
  return LoadTestData<Boss>("bosses");
}

std::map<std::string, Mob> LoadMobs() {
  return LoadTestData<Mob>("mobs");
}

std::map<std::string, ItemPrototype> LoadItems() {
  return LoadTestData<ItemPrototype>("items");
}

std::map<std::string, EquipPrototype> LoadEquips() {
  return LoadTestData<EquipPrototype>("equip");
}

// The shipped catalogs, read once per test. Every test here reads at least
// the bosses, and most of them the mobs behind the fights as well.
class BossDataTest : public testing::Test {
 protected:
  std::map<std::string, Boss> bosses_ = LoadBosses();
  std::map<std::string, Mob> mobs_ = LoadMobs();
};

TEST_F(BossDataTest, EveryPhaseSpawnsAKnownMob) {
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
          ASSERT_GT(mobs_.count(spawn.mob()), 0u)
              << entry.first << " spawns \"" << spawn.mob()
              << "\", which no mob file defines";
          EXPECT_GT(SpawnCount(spawn), 0)
              << entry.first << " spawns " << spawn.mob() << " zero times";
          EXPECT_EQ(spawn.count(), 0)
              << entry.first << " counts " << spawn.mob()
              << " twice: a boss spawn is counted by its spots";
          EXPECT_TRUE(mobs_.at(spawn.mob()).boss())
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
TEST_F(BossDataTest, EveryDifficultyIsNamedClockedAndReset) {
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
TEST_F(BossDataTest, EveryBuiltFightPaysFromItsOwnTable) {
  for (const std::pair<const std::string, Mob>& entry : mobs_) {
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
TEST_F(BossDataTest, AComingSoonDifficultyStatesOnlyItsPhases) {
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
  EXPECT_EQ(coming_soon, 3) << "Chaos Zakum, Chaos Pink Bean and Hard Magnus";
}

// The fights the screen advertises but cannot yet run, at the HP GMS gives
// them. A number here is read straight off the detail panel, so a typo in the
// mob files is a wrong promise on screen.
TEST_F(BossDataTest, TheUnbuiltFightsCarryTheirGmsHp) {
  struct Expectation {
    const char* boss;
    const char* difficulty;
    std::vector<int64_t> phase_hp;
  };
  const std::vector<Expectation> kExpected = {
      {"zakum", "Chaos", {84000000000LL, 84000000000LL}},
      {"magnus", "Hard", {120000000000LL}},
      {"pink_bean", "Chaos", {130200000000LL, 69300000000LL}},
  };
  for (const Expectation& want : kExpected) {
    ASSERT_GT(bosses_.count(want.boss), 0u) << want.boss;
    const BossDifficulty* found = nullptr;
    for (const BossDifficulty& difficulty :
         bosses_.at(want.boss).difficulties()) {
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
        hp += SpawnCount(spawn) * mobs_.at(spawn.mob()).max_hp();
      }
      EXPECT_EQ(hp, want.phase_hp[i])
          << want.boss << " " << want.difficulty << " phase " << i + 1;
    }
  }
}

// A drop names one catalog or the other, and a name neither holds is granted
// to nobody -- silently, since the reward path skips what it cannot find.
TEST_F(BossDataTest, EveryDropNamesAnItem) {
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
TEST_F(BossDataTest, EveryBuiltFightDropsItsOwnSoulShard) {
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
      EXPECT_EQ(items.at(shards[0]).kind(), ITEM_KIND_SOUL_SHARD) << where;
    }
  }
  EXPECT_EQ(fights, 9) << "Zakum, Magnus, Pink Bean, Arkarium, Cygnus and "
                          "both difficulties of Hilla and of Horntail";
}

// A boss pays in meso and in gear, and the gear is the reward: selling it back
// would make every clear a second purse and let a player skip the fight the
// piece is for. So nothing a boss drops is worth anything at the counter --
// equips and shards alike -- and a new boss's drop cannot ship priced.
TEST_F(BossDataTest, NothingABossDropsIsWorthMeso) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  std::map<std::string, ItemPrototype> items = LoadItems();
  int seen = 0;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (difficulty.coming_soon()) {
        continue;
      }
      for (const MobDrop& drop : difficulty.drops()) {
        ++seen;
        if (drop.has_equip()) {
          ASSERT_GT(equips.count(drop.equip()), 0u) << drop.equip();
          EXPECT_EQ(SellPrice(equips.at(drop.equip())), 0)
              << drop.equip() << ", off " << entry.first << ", sells for meso";
          continue;
        }
        ASSERT_GT(items.count(drop.item()), 0u) << drop.item();
        EXPECT_EQ(items.at(drop.item()).sell_price(), 0)
            << drop.item() << ", off " << entry.first << ", sells for meso";
      }
    }
  }
  EXPECT_GT(seen, 0) << "no boss drops in the catalog to check";
}

// Zakum is the first boss and the one the screen was built against, so his
// numbers are pinned: the shape of the fight is a design decision, not data
// that should drift.
TEST_F(BossDataTest, NormalZakumIsEightArmsThenTheBody) {
  ASSERT_GT(bosses_.count("zakum"), 0u);
  const Boss& zakum = bosses_.at("zakum");
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
TEST_F(BossDataTest, NormalMagnusIsOneBodyBehindALateGate) {
  ASSERT_GT(bosses_.count("magnus"), 0u);
  const BossDifficulty& normal = bosses_.at("magnus").difficulties(0);
  EXPECT_EQ(normal.name(), "Normal");
  EXPECT_EQ(normal.reset(), RESET_PERIOD_DAILY);
  EXPECT_EQ(normal.time_limit_seconds(), 600);
  EXPECT_EQ(normal.unlock_level(), 160);
  EXPECT_EQ(normal.meso(), 12960000);
  EXPECT_EQ(normal.exp(), 5300000);
  ASSERT_EQ(normal.phases_size(), 1);
  ASSERT_EQ(normal.phases(0).spawns_size(), 1);
  EXPECT_EQ(SpawnCount(normal.phases(0).spawns(0)), 1);
  const Mob& magnus = mobs_.at("magnus");
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
TEST_F(BossDataTest, NormalPinkBeanIsFiveStatuesThenTheBean) {
  ASSERT_GT(bosses_.count("pink_bean"), 0u);
  const BossDifficulty& normal = bosses_.at("pink_bean").difficulties(0);
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
    const Mob& statue = mobs_.at(spawn.mob());
    EXPECT_EQ(SpawnCount(spawn), 1) << spawn.mob();
    EXPECT_EQ(statue.level(), 180) << spawn.mob();
    EXPECT_EQ(statue.pdr(), 60) << spawn.mob();
    statues += statue.max_hp();
  }
  EXPECT_EQ(statues, 5550000000LL);
  const Mob& bean = mobs_.at("pink_bean");
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
TEST_F(BossDataTest, NormalArkariumIsOneBodyBehindNinetyPdr) {
  ASSERT_GT(bosses_.count("arkarium"), 0u);
  const BossDifficulty& normal = bosses_.at("arkarium").difficulties(0);
  EXPECT_EQ(normal.name(), "Normal");
  EXPECT_EQ(normal.reset(), RESET_PERIOD_DAILY);
  EXPECT_EQ(normal.time_limit_seconds(), 600);
  EXPECT_EQ(normal.unlock_level(), 180);
  EXPECT_EQ(normal.meso(), 12602500);
  EXPECT_EQ(normal.exp(), 50000000);
  ASSERT_EQ(normal.phases_size(), 1);
  ASSERT_EQ(normal.phases(0).spawns_size(), 1);
  EXPECT_EQ(SpawnCount(normal.phases(0).spawns(0)), 1);
  const Mob& arkarium = mobs_.at("arkarium");
  EXPECT_EQ(arkarium.level(), 170);
  EXPECT_EQ(arkarium.max_hp(), 12600000000LL);
  EXPECT_EQ(arkarium.pdr(), 90);
  ASSERT_EQ(normal.drops_size(), 2);
  EXPECT_EQ(normal.drops(0).equip(), "dominator_pendant");
  EXPECT_EQ(normal.drops(1).item(), "arkariums_soul_shard");
}

// The first fight with a second difficulty built, and the only body behind
// 100% PDR. Pinned for the reason Zakum's numbers are.
TEST_F(BossDataTest, HardHillaIsTheSameFightBehindALaterGate) {
  ASSERT_GT(bosses_.count("hilla"), 0u);
  ASSERT_EQ(bosses_.at("hilla").difficulties_size(), 2);
  const BossDifficulty& hard = bosses_.at("hilla").difficulties(1);
  EXPECT_EQ(hard.name(), "Hard");
  EXPECT_FALSE(hard.coming_soon());
  EXPECT_EQ(hard.reset(), RESET_PERIOD_DAILY);
  EXPECT_EQ(hard.time_limit_seconds(), 600);
  EXPECT_EQ(hard.unlock_level(), 190);
  EXPECT_EQ(hard.meso(), 8050000);
  EXPECT_EQ(hard.exp(), 20400000);
  const Mob& silver = mobs_.at("hard_hilla");
  EXPECT_EQ(silver.level(), 190);
  EXPECT_EQ(silver.max_hp(), 16800000000LL);
  EXPECT_EQ(silver.pdr(), 100);
  ASSERT_EQ(hard.drops_size(), 2);
  EXPECT_EQ(hard.drops(0).equip(), "will_o_the_wisps");
  // The same shard Normal drops: a soul belongs to the boss, not the rung.
  EXPECT_EQ(hard.drops(1).item(), "hillas_soul_shard");
}

// The biggest fight in the game: eleven parts over three phases adding to
// 26.6B, and the one second difficulty that changes what a phase fights
// rather than only how hard it hits.
TEST_F(BossDataTest, ChaosHorntailIsTheSameShapeAtChaosNumbers) {
  ASSERT_GT(bosses_.count("horntail"), 0u);
  ASSERT_EQ(bosses_.at("horntail").difficulties_size(), 2);
  const BossDifficulty& normal = bosses_.at("horntail").difficulties(0);
  const BossDifficulty& chaos = bosses_.at("horntail").difficulties(1);
  EXPECT_EQ(chaos.name(), "Chaos");
  EXPECT_FALSE(chaos.coming_soon());
  EXPECT_EQ(chaos.reset(), RESET_PERIOD_DAILY);
  EXPECT_EQ(chaos.time_limit_seconds(), 600);
  EXPECT_EQ(chaos.unlock_level(), 190);
  EXPECT_EQ(chaos.meso(), 6760000);
  EXPECT_EQ(chaos.exp(), 8473319);
  // Phase for phase and cell for cell, the fight Normal is: only the mobs
  // differ, and each is the Chaos part of the one it replaces.
  ASSERT_EQ(chaos.phases_size(), normal.phases_size());
  int64_t total = 0;
  for (int i = 0; i < chaos.phases_size(); ++i) {
    const BossPhase& want = normal.phases(i);
    const BossPhase& phase = chaos.phases(i);
    ASSERT_EQ(phase.spawns_size(), want.spawns_size()) << "phase " << i + 1;
    for (int j = 0; j < phase.spawns_size(); ++j) {
      EXPECT_EQ(phase.spawns(j).mob(), "chaos_" + want.spawns(j).mob());
      EXPECT_EQ(phase.spawns(j).spots(0).x(), want.spawns(j).spots(0).x());
      EXPECT_EQ(phase.spawns(j).spots(0).y(), want.spawns(j).spots(0).y());
      const Mob& part = mobs_.at(phase.spawns(j).mob());
      EXPECT_EQ(part.level(), 160) << phase.spawns(j).mob();
      EXPECT_EQ(part.pdr(), 50) << phase.spawns(j).mob();
      total += part.max_hp();
    }
  }
  EXPECT_EQ(total, 26600000000LL);
  // Normal's rewards with the Chaos necklace in the plain one's place, and
  // the same shard: a soul belongs to the boss, not the rung.
  ASSERT_EQ(chaos.drops_size(), 4);
  EXPECT_EQ(chaos.drops(0).item(), "horntails_soul_shard");
  EXPECT_EQ(chaos.drops(1).equip(), "silver_blossom_ring");
  EXPECT_EQ(chaos.drops(2).equip(), "chaos_horntail_necklace");
  EXPECT_EQ(chaos.drops(3).equip(), "dea_sidus_earring");
}

// The last fight the game opens and the biggest body in it: 63B behind 100%
// PDR, standing where Hilla and Arkarium stand. She pays no equip at all --
// the token she drops is what buys one -- so she is also the only fight whose
// whole reward is a stackable. Pinned for the reason Zakum's numbers are.
TEST_F(BossDataTest, NormalCygnusIsOneBodyBehindTheLastGate) {
  ASSERT_GT(bosses_.count("cygnus"), 0u);
  ASSERT_EQ(bosses_.at("cygnus").difficulties_size(), 1);
  const BossDifficulty& normal = bosses_.at("cygnus").difficulties(0);
  EXPECT_EQ(normal.name(), "Normal");
  EXPECT_EQ(normal.reset(), RESET_PERIOD_DAILY);
  EXPECT_EQ(normal.time_limit_seconds(), 600);
  EXPECT_EQ(normal.unlock_level(), 200);
  EXPECT_EQ(normal.meso(), 10300000);
  EXPECT_EQ(normal.exp(), 204000000);
  ASSERT_EQ(normal.phases_size(), 1);
  ASSERT_EQ(normal.phases(0).spawns_size(), 1);
  EXPECT_EQ(SpawnCount(normal.phases(0).spawns(0)), 1);
  const Mob& cygnus = mobs_.at("cygnus");
  EXPECT_EQ(cygnus.level(), 190);
  EXPECT_EQ(cygnus.max_hp(), 63000000000LL);
  EXPECT_EQ(cygnus.attack(), 30400);
  EXPECT_EQ(cygnus.pdr(), 100);
  ASSERT_EQ(normal.drops_size(), 2);
  EXPECT_EQ(normal.drops(0).item(), "cygnus_shoulder_token");
  EXPECT_EQ(normal.drops(1).item(), "cygnuss_soul_shard");
}

// Where the parts stand is data, and two of them in one cell is a bar drawn on
// top of another one.
TEST_F(BossDataTest, EveryPartStandsSomewhereOfItsOwn) {
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
TEST_F(BossDataTest, EveryPhaseStandsThePlayerInsideItsArena) {
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
TEST_F(BossDataTest, EveryFightOffersTheSpotsItWasDesignedWith) {
  std::map<std::string, std::vector<int>> expected = {
      {"zakum", {7, 5}},    {"hilla", {5}},    {"horntail", {6, 6, 6}},
      {"magnus", {5}},      {"arkarium", {5}}, {"cygnus", {5}},
      {"pink_bean", {5, 5}}};
  for (const std::pair<const std::string, std::vector<int>>& want : expected) {
    ASSERT_GT(bosses_.count(want.first), 0u) << want.first;
    for (const BossDifficulty& difficulty :
         bosses_.at(want.first).difficulties()) {
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
TEST_F(BossDataTest, EveryArenaStandsOnTheOneGrid) {
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
