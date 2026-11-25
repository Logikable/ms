// Checks the shipped bosses against the mob catalog. A phase naming a mob file
// that doesn't exist spawns nothing, leaving the player in an empty fight until
// the timer runs out.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "src/character/force/sacred_power.h"
#include "src/character/force/symbol.h"
#include "src/character/max_character.h"
#include "src/combat/boss/arena_spots.h"
#include "src/combat/loot.h"
#include "src/frontend/screens/boss/boss_fight_panel.h"
#include "src/frontend/screens/boss/boss_select_panel.h"
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

// The item or equip a drop names.
std::string DropKey(const MobDrop& drop) {
  return drop.has_equip() ? drop.equip() : drop.item();
}

// The shipped catalogs, loaded once per test. Every test here reads the bosses,
// and most also read the mobs in the fights.
class BossDataTest : public testing::Test {
 protected:
  std::map<std::string, Boss> bosses_ = LoadBosses();
  std::map<std::string, Mob> mobs_ = LoadMobs();
};

// The bosses the Sacred Symbols name that aren't in the game yet. Building one
// takes it off this list, and its file must use the stem named here.
const std::set<std::string> kUnbuiltSymbolBosses = {
    "malefic_star",
    "limbo",
    "baldrix",
    "jupiter",
};

// The lowest level any of `boss`'s built difficulties opens at. It marks a
// Grandis boss where its mobs' level can't: Darknell, of Arcane River, is
// level 265.
int LowestUnlockLevel(const Boss& boss) {
  int lowest = 0;
  for (const BossDifficulty& difficulty : boss.difficulties()) {
    if (!difficulty.coming_soon() &&
        (lowest == 0 || difficulty.unlock_level() < lowest)) {
      lowest = difficulty.unlock_level();
    }
  }
  return lowest;
}

// A symbol's max-level damage finds its boss by file stem, so a boss built
// under another stem would quietly get none. Three checks close that: a key
// that finds a boss agrees with it on the name, the keys that find nothing are
// exactly the unbuilt list, and every Grandis boss is some symbol's key.
TEST_F(BossDataTest, SacredSymbolsNameRealBosses) {
  std::set<std::string> keys;
  std::set<std::string> unbuilt;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    if (!IsSacredSymbol(entry.second)) {
      continue;
    }
    const SacredSymbolInfo& info = entry.second.sacred_symbol();
    // Unset would read as open at every level.
    EXPECT_GE(info.area_level(), kGrandisLevel) << entry.first;
    keys.insert(info.boss());
    std::map<std::string, Boss>::const_iterator boss =
        bosses_.find(info.boss());
    if (boss == bosses_.end()) {
      unbuilt.insert(info.boss());
      continue;
    }
    EXPECT_EQ(boss->second.name(), info.boss_name()) << entry.first;
  }
  EXPECT_EQ(keys.size(), 8u) << "one boss per Sacred Symbol";
  EXPECT_EQ(unbuilt, kUnbuiltSymbolBosses);
  for (const std::pair<const std::string, Boss>& entry : bosses_) {
    if (LowestUnlockLevel(entry.second) >= kGrandisLevel) {
      EXPECT_GT(keys.count(entry.first), 0u)
          << entry.first << " is a Grandis boss no Sacred Symbol names";
    }
  }
}

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

// A fight with no timer couldn't be lost, and one with no reset could be run
// all day; the boss screen is designed around neither being possible. Unbuilt
// fights are exempt, since they only state their HP.
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
      EXPECT_FALSE(difficulty.arcane_force() > 0 &&
                   difficulty.sacred_power() > 0)
          << entry.first << " " << difficulty.name() << " asks for both forces";
      // A phase's own Sacred Power only raises or lowers the difficulty's.
      for (const BossPhase& phase : difficulty.phases()) {
        EXPECT_TRUE(phase.sacred_power() == 0 || difficulty.sacred_power() > 0)
            << entry.first << " " << difficulty.name()
            << " has a phase asking Sacred Power of a fight that asks none";
      }
    }
  }
}

// EXP and meso belong to the fight: the clear pays them once and the reward
// code ignores a boss mob's own. A number left on the mob is a payout nobody
// receives.
TEST_F(BossDataTest, EveryBuiltFightPaysFromItsOwnTable) {
  std::vector<std::string> unpaid;
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
      EXPECT_GT(difficulty.meso(), 0)
          << entry.first << " " << difficulty.name() << " pays no meso";
      if (difficulty.exp() == 0) {
        unpaid.push_back(entry.first);
      }
    }
  }
  // The four Root Abyss bosses, which open at 200 and pay in pieces instead;
  // Chaos Zakum, for whom GMS gives no EXP; and every difficulty of Lotus,
  // the Guardian Angel Slime, Lucid, Will, Gloom, Darknell, Verus Hilla,
  // the Black Mage, Seren, Kalos, the First Adversary and Kaling, which are
  // the same case and are fought for their drops.
  EXPECT_EQ(unpaid, std::vector<std::string>({"black_mage",
                                              "crimson_queen",
                                              "darknell",
                                              "darknell",
                                              "first_adversary",
                                              "first_adversary",
                                              "gloom",
                                              "gloom",
                                              "guardian_angel_slime",
                                              "guardian_angel_slime",
                                              "kaling",
                                              "kaling",
                                              "kalos",
                                              "kalos",
                                              "lotus",
                                              "lotus",
                                              "lotus",
                                              "lucid",
                                              "lucid",
                                              "pierre",
                                              "seren",
                                              "seren",
                                              "vellum",
                                              "verus_hilla",
                                              "verus_hilla",
                                              "von_bon",
                                              "will",
                                              "will",
                                              "zakum"}));
}

// A shell can't be entered, so a timer, gate or
// reward on one would be a promise the screen never shows.
TEST_F(BossDataTest, TheShellsStateTheirHpAndNothingElse) {
  std::vector<std::string> shells;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (!difficulty.coming_soon()) {
        continue;
      }
      std::string where = entry.first + " " + difficulty.name();
      shells.push_back(where);
      EXPECT_EQ(difficulty.time_limit_seconds(), 0) << where;
      EXPECT_EQ(difficulty.unlock_level(), 0) << where;
      EXPECT_EQ(difficulty.meso(), 0) << where;
      EXPECT_EQ(difficulty.exp(), 0) << where;
      EXPECT_EQ(difficulty.drops_size(), 0) << where;
    }
  }
  EXPECT_EQ(shells, std::vector<std::string>(
                        {"bellona Easy", "bellona Normal", "black_mage Extreme",
                         "first_adversary Hard", "first_adversary Extreme",
                         "kaling Hard", "kaling Extreme", "kalos Chaos",
                         "kalos Extreme", "seren Extreme"}));
}

// Each harder fight is its Normal with GMS's Hard or Chaos numbers (v272's
// HP), and the difficulty is the only difference: the same mobs in the same
// cells, so building a shell means filling in what's missing. Hard
// Lucid adds a third phase of her own after Normal's two, and Extreme Lotus
// takes GMS's 380 PDR.
TEST_F(BossDataTest, EveryHarderFightIsItsNormalShapeAtGmsNumbers) {
  struct Want {
    std::string boss;
    std::string name;
    std::vector<int64_t> hp;
    int pdr = 0;  // 0: Normal's.
  };
  const std::vector<Want> kHarder = {
      {"damien", "Hard", {17085600000000LL, 7322400000000LL}},
      {"lotus", "Hard", {6830082000000LL, 6830082000000LL, 9106776000000LL}},
      {"lotus",
       "Extreme",
       {250521000000000LL, 250521000000000LL, 330999600000000LL},
       380},
      {"guardian_angel_slime", "Chaos", {61650000000000LL}},
      {"lucid", "Hard", {36322000000000LL, 38610000000000LL, 12800000000000LL}},
      {"will", "Hard", {28182000000000LL, 21136500000000LL, 35227500000000LL}},
      {"gloom", "Chaos", {86012850000000LL}},
      {"darknell", "Hard", {108202500000000LL}},
      {"verus_hilla", "Hard", {122451000000000LL}}};
  for (const Want& want : kHarder) {
    ASSERT_GT(bosses_.count(want.boss), 0u) << want.boss;
    const Boss& boss = bosses_.at(want.boss);
    const BossDifficulty& normal = boss.difficulties(0);
    const auto found = std::find_if(
        boss.difficulties().begin(), boss.difficulties().end(),
        [&](const BossDifficulty& d) { return d.name() == want.name; });
    ASSERT_NE(found, boss.difficulties().end())
        << want.boss << " " << want.name;
    const BossDifficulty& harder = *found;
    SCOPED_TRACE(want.boss + " " + want.name);
    EXPECT_EQ(normal.name(), "Normal");
    ASSERT_EQ(harder.phases_size(), static_cast<int>(want.hp.size()));
    ASSERT_EQ(harder.phases_size(),
              normal.phases_size() + (want.boss == "lucid" ? 1 : 0));
    for (int i = 0; i < harder.phases_size(); ++i) {
      if (i >= normal.phases_size()) {
        EXPECT_EQ(mobs_.at(harder.phases(i).spawns(0).mob()).max_hp(),
                  want.hp[i]);
        continue;
      }
      const BossPhase& shape = normal.phases(i);
      const BossPhase& phase = harder.phases(i);
      SCOPED_TRACE(i);
      EXPECT_EQ(phase.bgm(), shape.bgm());
      ASSERT_EQ(phase.spawns_size(), 1);
      ASSERT_EQ(shape.spawns_size(), 1);
      EXPECT_EQ(SpawnCount(phase.spawns(0)), 1);
      EXPECT_EQ(phase.spawns(0).spots(0).x(), shape.spawns(0).spots(0).x());
      EXPECT_EQ(phase.spawns(0).spots(0).y(), shape.spawns(0).spots(0).y());
      EXPECT_EQ(phase.spawns(0).walk().SerializeAsString(),
                shape.spawns(0).walk().SerializeAsString());
      const Mob& mob = mobs_.at(phase.spawns(0).mob());
      const Mob& before = mobs_.at(shape.spawns(0).mob());
      EXPECT_EQ(mob.name(), before.name());
      EXPECT_EQ(mob.attack(), before.attack());
      EXPECT_EQ(mob.pdr(), want.pdr != 0 ? want.pdr : before.pdr());
      EXPECT_EQ(mob.max_hp(), want.hp[i]);
    }
  }
  // GMS fights her at 250, past the level cap, and a shell's card doesn't show
  // a level, so nothing reads it yet.
  EXPECT_EQ(mobs_.at("chaos_guardian_angel_slime").level(), 250);
}

// A shell above a built Normal is that Normal cell for cell, its mobs the same
// monsters at GMS's v272 HP, so building it means filling in the difficulty's
// own fields. Bellona has no Normal built and stands on her own floor.
TEST_F(BossDataTest, EveryShellIsItsNormalAtGmsHp) {
  // Each shell's HP over every phase, every spawn counted.
  const std::map<std::string, int64_t> kTotals = {
      {"kalos Chaos", 2284560000000000LL},
      {"kalos Extreme", 9625011600000000LL},
      {"seren Extreme", 2681982100000000LL},
      {"first_adversary Hard", 7177900000000000LL},
      {"first_adversary Extreme", 22753200000000000LL},
      {"kaling Hard", 6873300000000000LL},
      {"kaling Extreme", 31915400000000000LL},
      {"bellona Easy", 779000000000000LL},
      {"bellona Normal", 4122000000000000LL},
      {"black_mage Extreme", 3257047000000000LL}};
  // Drops the shell's own prefix from its monsters, and what only a built
  // difficulty carries, so what's left is the shape.
  auto shape = [](BossPhase phase, const std::string& prefix) {
    phase.clear_sacred_power();
    auto strip = [&prefix](std::string* mob) {
      if (absl::StartsWith(*mob, prefix)) {
        *mob = mob->substr(prefix.size());
      }
    };
    for (Spawn& spawn : *phase.mutable_spawns()) {
      strip(spawn.mutable_mob());
      strip(spawn.mutable_shares_hp());
      strip(spawn.mutable_walk()->mutable_teleport()->mutable_beside());
      for (std::string& shielded : *spawn.mutable_shield()->mutable_mobs()) {
        strip(&shielded);
      }
      // A shield's size is a GMS number, like the HP.
      spawn.mutable_self_shield()->clear_percent();
    }
    return phase.SerializeAsString();
  };
  int shells = 0;
  for (const std::pair<const std::string, Boss>& entry : bosses_) {
    // Normal, or for a boss with none (the Black Mage) its first built one.
    const BossDifficulty* normal = nullptr;
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (!difficulty.coming_soon() &&
          (difficulty.name() == "Normal" || normal == nullptr)) {
        normal = &difficulty;
      }
    }
    std::string base_prefix;
    if (normal != nullptr && normal->name() != "Normal") {
      base_prefix = normal->name() + "_";
      std::transform(base_prefix.begin(), base_prefix.end(),
                     base_prefix.begin(), ::tolower);
    }
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (!difficulty.coming_soon()) {
        continue;
      }
      ++shells;
      const std::string where = entry.first + " " + difficulty.name();
      SCOPED_TRACE(where);
      int64_t total = 0;
      for (const BossPhase& phase : difficulty.phases()) {
        for (const Spawn& spawn : phase.spawns()) {
          total += HpCount(spawn) * mobs_.at(spawn.mob()).max_hp();
        }
      }
      ASSERT_GT(kTotals.count(where), 0u);
      EXPECT_EQ(total, kTotals.at(where));
      if (normal == nullptr) {
        continue;
      }
      std::string prefix = difficulty.name() + "_";
      std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
      ASSERT_EQ(difficulty.phases_size(), normal->phases_size());
      for (int i = 0; i < difficulty.phases_size(); ++i) {
        EXPECT_EQ(shape(difficulty.phases(i), prefix),
                  shape(normal->phases(i), base_prefix))
            << "phase " << i + 1;
        for (int j = 0; j < difficulty.phases(i).spawns_size(); ++j) {
          const Mob& mob = mobs_.at(difficulty.phases(i).spawns(j).mob());
          const Mob& before = mobs_.at(normal->phases(i).spawns(j).mob());
          EXPECT_EQ(mob.name(), before.name());
          EXPECT_EQ(mob.attack(), before.attack());
          EXPECT_EQ(mob.pdr(), before.pdr());
        }
      }
    }
  }
  EXPECT_EQ(shells, static_cast<int>(kTotals.size()));
}

// A drop names an item or an equip, and a name in neither catalog is given to
// nobody. It fails silently, since the reward code skips what it can't find.
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
        EXPECT_GE(drop.rolls(), 0) << entry.first;
      }
    }
  }
  EXPECT_GT(drops, 0) << "no boss in the catalog drops anything";
}

// Every built fight drops its own soul shard, and the boss screen shows its
// name. A fight with a missing or misnamed shard drops nothing the player can
// tell apart from another boss's.
TEST_F(BossDataTest, EveryBuiltFightDropsItsOwnSoulShard) {
  std::map<std::string, ItemPrototype> items = LoadItems();
  int fights = 0;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (difficulty.coming_soon()) {
        continue;
      }
      // GMS made no soul of the Black Mage.
      if (entry.first == "black_mage") {
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
      EXPECT_EQ(items.at(shards[0]).kind(), ITEM_KIND_SOUL_SHARD) << where;
      // The bag's Token tab heads the column "Soul Shard" and shows only the
      // boss name under it, so a shard with no short name would repeat its own
      // column heading.
      EXPECT_EQ(items.at(shards[0]).short_name(), entry.second.name()) << where;
      EXPECT_NE(items.at(shards[0]).soul_tier(), SOUL_TIER_UNSPECIFIED)
          << where << " makes no soul";
    }
  }
  EXPECT_EQ(fights, 43) << "Arkarium, Cygnus, Princess No, Papulatus, the "
                           "four of Root Abyss, both difficulties of "
                           "Zakum, Magnus, Pink Bean, Hilla, Horntail, "
                           "Damien, the Guardian Angel Slime, Lucid, Will, "
                           "Gloom, Darknell, Verus Hilla, Seren, Kalos, the "
                           "First Adversary and Kaling, and all three of "
                           "Lotus";
}

// A spawn that leaves with another monster names one standing beside it, at a
// mark that monster passes before it dies.
TEST_F(BossDataTest, AVanishFollowsAMonsterOfItsOwnPhase) {
  int vanishing = 0;
  for (const std::pair<const std::string, Boss>& entry : bosses_) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const BossPhase& phase : difficulty.phases()) {
        for (const Spawn& spawn : phase.spawns()) {
          if (!spawn.has_vanish()) {
            continue;
          }
          ++vanishing;
          SCOPED_TRACE(entry.first + " " + difficulty.name() + " " +
                       spawn.mob());
          EXPECT_NE(spawn.vanish().mob(), spawn.mob());
          EXPECT_TRUE(std::any_of(phase.spawns().begin(), phase.spawns().end(),
                                  [&](const Spawn& other) {
                                    return other.mob() == spawn.vanish().mob();
                                  }));
          EXPECT_GT(spawn.vanish().hp_pct(), 0);
          EXPECT_LT(spawn.vanish().hp_pct(), 100);
        }
      }
    }
  }
  EXPECT_EQ(vanishing, 2) << "Zakum's arms, Normal and Chaos";
}

// A boss drop that sold would pay every clear twice, so nothing a boss drops
// has a price, and a new boss's drop can't ship with one.
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

// Max mode clears a fight at the level progression_sim's sweep first beat it,
// so a new fight needs a level in max_character.cc's kClears, read off the
// next sweep, and a clear can't come before the fight opens. A fight the sweep
// never beat is named here instead, and max mode never clears it.
TEST_F(BossDataTest, MaxModeKnowsWhenEveryBuiltFightFalls) {
  // No branch of the uncapped sweeps of 2026-11-21 to 11-23 beat these.
  const std::set<std::string> kNeverBeaten = {
      "kaling Normal", "first_adversary Normal", "lotus Extreme"};
  for (const std::pair<const std::string, Boss>& entry : bosses_) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (difficulty.coming_soon()) {
        continue;
      }
      const int cleared = MaxClearLevel(entry.first, difficulty.name());
      if (kNeverBeaten.count(entry.first + " " + difficulty.name()) > 0) {
        EXPECT_EQ(cleared, 0) << difficulty.name() << " " << entry.first;
        continue;
      }
      EXPECT_GE(cleared, difficulty.unlock_level())
          << difficulty.name() << " " << entry.first;
    }
  }
}

// The hard difficulties of built fights open at 190 on the same ten-minute
// timer. Each is its Normal fight at GMS's Chaos or Hard numbers and drops the
// same gear: harder gets it faster, not different.
TEST_F(BossDataTest, TheHardRungsAreTheirNormalShapeAtGmsNumbers) {
  struct Want {
    std::string boss;
    std::string name;
    int64_t meso;
    int64_t exp;
    int64_t hp;
  };
  const std::vector<Want> kRungs = {
      {"zakum", "Chaos", 11550000, 0, 113400000000LL},
      {"magnus", "Hard", 13600000, 22000000, 81960000000LL},
      {"pink_bean", "Chaos", 12800000, 9590000, 199500000000LL}};
  for (const Want& want : kRungs) {
    ASSERT_GT(bosses_.count(want.boss), 0u) << want.boss;
    ASSERT_EQ(bosses_.at(want.boss).difficulties_size(), 2) << want.boss;
    const BossDifficulty& normal = bosses_.at(want.boss).difficulties(0);
    const BossDifficulty& hard = bosses_.at(want.boss).difficulties(1);
    SCOPED_TRACE(want.boss);
    EXPECT_EQ(hard.name(), want.name);
    EXPECT_FALSE(hard.coming_soon());
    EXPECT_EQ(hard.time_limit_seconds(), 600);
    EXPECT_EQ(hard.unlock_level(), 190);
    EXPECT_EQ(hard.meso(), want.meso);
    EXPECT_EQ(hard.exp(), want.exp);
    // Phase for phase and cell for cell, the same fight as Normal.
    ASSERT_EQ(hard.phases_size(), normal.phases_size());
    int64_t total = 0;
    for (int i = 0; i < hard.phases_size(); ++i) {
      const BossPhase& shape = normal.phases(i);
      const BossPhase& phase = hard.phases(i);
      ASSERT_EQ(phase.spawns_size(), shape.spawns_size()) << "phase " << i + 1;
      for (int j = 0; j < phase.spawns_size(); ++j) {
        EXPECT_EQ(SpawnCount(phase.spawns(j)), SpawnCount(shape.spawns(j)));
        EXPECT_EQ(phase.spawns(j).spots(0).x(), shape.spawns(j).spots(0).x());
        EXPECT_EQ(phase.spawns(j).spots(0).y(), shape.spawns(j).spots(0).y());
        total += SpawnCount(phase.spawns(j)) *
                 mobs_.at(phase.spawns(j).mob()).max_hp();
      }
    }
    EXPECT_EQ(total, want.hp);
    // What Normal drops, in the same order; the rates are the tiers' to set.
    ASSERT_EQ(hard.drops_size(), normal.drops_size());
    for (int i = 0; i < hard.drops_size(); ++i) {
      EXPECT_EQ(DropKey(hard.drops(i)), DropKey(normal.drops(i)))
          << "drop " << i + 1;
    }
  }
}

// Every boss drop's rate and pity: the user's table (2026-11-24, memory
// boss_pity.md), the same at every difficulty of a boss. A new boss's drop
// fails here until it is in the table.
struct TableDrop {
  double rate = 0.0;
  int pity = 0;
  int pity_copies = 0;
};

const std::map<std::string, TableDrop>& DropTable() {
  static const std::map<std::string, TableDrop>* const kTable = [] {
    auto* table = new std::map<std::string, TableDrop>;
    auto put = [table](std::initializer_list<const char*> keys,
                       TableDrop drop) {
      for (const char* key : keys) {
        (*table)[key] = drop;
      }
    };
    // 1% for what a player needs one of, 3% for what they star-force
    // through several of, 5% for currency, the Jade boxes aside: they drop
    // from many bosses.
    put({"aquatic_letter_eye_accessory", "condensed_power_crystal",
         "stone_of_eternal_life", "dea_sidus_earring", "horntail_necklace",
         "silver_blossom_ring", "crystal_ventus_badge",
         "royal_black_metal_shoulder", "black_bean_mark", "golden_clover_belt",
         "pink_holy_cup", "chaos_horntail_necklace"},
        {0.01, 1});
    put({"cygnus_shoulder_token", "piece_of_mockery", "piece_of_time",
         "piece_of_anguish", "piece_of_destruction"},
        {0.05, 1, 5});
    put({"dominator_pendant"}, {0.03, 1});
    put({"papulatus_mark"}, {0.01, 3});
    put({"kannas_treasure", "guardian_angel_ring"}, {0.03, 3});
    put({"captivating_fragment"}, {0.01, 1, 5});
    put({"absolab_coin", "kalos_residual_determination",
         "echo_of_ancient_resolve", "ferocious_beast_entanglement_ring"},
        {0.05, 1, 10});
    put({"phantasma_coin"}, {0.05, 1, 6});
    put({"arachno_coin"}, {0.05, 1, 4});
    put({"twilight_mark", "magic_eyepatch", "berserked", "estella_earrings",
         "daybreak_pendant", "source_of_suffering", "endless_terror",
         "commanding_force_earring", "dreamy_belt"},
        {0.03, 10});
    put({"black_heart", "wills_cursed_spellbook_selection_box"}, {0.01, 10});
    put({"genesis_badge", "mitras_rage_selection_box"}, {0.01, 15});
    put({"total_control"}, {0.03, 15});
    put({"black_jade_boss_ring_box", "white_jade_boss_ring_box"}, {0.01, 15});
    // Boxes the table doesn't list: 0.5%, no pity (the user's).
    put({"absolab_armor_box", "absolab_weapon_box", "arcane_umbra_armor_box",
         "arcane_umbra_weapon_box"},
        {0.005});
    // The scroll rates are the user's (2026-10-04).
    put({"premium_scroll_for_accessory", "premium_scroll_for_pet",
         "scroll_for_accessory"},
        {0.5});
    put({"magical_scroll_for_weapon"}, {0.1});
    return table;
  }();
  return *kTable;
}

TEST_F(BossDataTest, EveryDropFollowsTheDropTable) {
  for (const std::pair<const std::string, Boss>& entry : bosses_) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const MobDrop& drop : difficulty.drops()) {
        const std::string key = DropKey(drop);
        SCOPED_TRACE(entry.first + " " + difficulty.name() + " " + key);
        EXPECT_EQ(drop.rolls(), 0) << "one roll, whatever the difficulty";
        if (absl::EndsWith(key, "_soul_shard")) {
          EXPECT_DOUBLE_EQ(drop.per_kill(), 1.0);
          EXPECT_EQ(drop.pity(), 0);
          continue;
        }
        std::map<std::string, TableDrop>::const_iterator it =
            DropTable().find(key);
        ASSERT_NE(it, DropTable().end()) << "not in the table";
        EXPECT_DOUBLE_EQ(drop.per_kill(), it->second.rate);
        EXPECT_EQ(drop.pity(), it->second.pity);
        EXPECT_EQ(drop.pity_copies(), it->second.pity_copies);
      }
    }
  }
}

// Hard Magnus and the Chaos Pink Bean statues have more than 100% PDR, so the
// fight needs Ignore DEF before it does any damage at all. The number is the
// design, so it is fixed here instead of clamped.
TEST_F(BossDataTest, TheHardRungsGateOnIgnoreDefense) {
  EXPECT_EQ(mobs_.at("hard_magnus").pdr(), 120);
  EXPECT_EQ(mobs_.at("chaos_zakum").pdr(), 100);
  EXPECT_EQ(mobs_.at("chaos_pink_bean").pdr(), 100);
  for (const std::string& statue :
       {"chaos_solomon_the_wise", "chaos_rex_the_wise", "chaos_ariel",
        "chaos_hugin", "chaos_munin"}) {
    EXPECT_EQ(mobs_.at(statue).pdr(), 160) << statue;
  }
}

// A special must have a move to make and a wait to draw it in, and a fall
// only happens as one: an unscheduled fall would never be made.
TEST_F(BossDataTest, EverySpecialHasAMoveAndAWait) {
  int specials = 0;
  for (const std::pair<const std::string, Boss>& entry : bosses_) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const BossPhase& phase : difficulty.phases()) {
        for (const Spawn& spawn : phase.spawns()) {
          const ArenaWalk& walk = spawn.walk();
          std::string where = entry.first + " " + spawn.mob();
          if (!walk.has_specials()) {
            continue;
          }
          ++specials;
          EXPECT_TRUE(walk.has_dash() || walk.specials().has_fall()) << where;
          EXPECT_GT(walk.specials().min_interval_ms(), 0) << where;
          EXPECT_GE(walk.specials().max_interval_ms(),
                    walk.specials().min_interval_ms())
              << where;
          EXPECT_EQ(walk.dash().interval_ms(), 0)
              << where << " dashes on two clocks";
          if (walk.specials().has_fall()) {
            EXPECT_GT(walk.specials().fall().step_ms(), 0) << where;
          }
        }
      }
    }
  }
  EXPECT_EQ(specials, 2) << "only Darknell, at both difficulties, takes turns "
                            "between moves";
}

// A jump must come back down before the next one is due, and the row it jumps
// to must be inside the arena and not a player spot, since a jump onto a player
// spot would draw a bar over the player.
TEST_F(BossDataTest, EveryJumpLandsInsideItsArenaBeforeTheNextIsDue) {
  int jumps = 0;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const BossPhase& phase : difficulty.phases()) {
        for (const Spawn& spawn : phase.spawns()) {
          if (!spawn.walk().has_jump()) {
            continue;
          }
          ++jumps;
          const ArenaJump& jump = spawn.walk().jump();
          std::string where = entry.first + " " + spawn.mob();
          EXPECT_GT(jump.hang_ms(), 0) << where << " jumps for no time at all";
          EXPECT_GT(jump.interval_ms(), jump.hang_ms())
              << where << " jumps again before it has landed";
          EXPECT_GE(jump.y(), 0) << where;
          EXPECT_LT(jump.y(), phase.arena_height())
              << where << " jumps past the bottom of its arena";
          for (const ArenaSpot& spot : phase.player_spots()) {
            EXPECT_NE(spot.y(), jump.y())
                << where << " jumps onto a row the player stands on";
          }
        }
      }
    }
  }
  EXPECT_EQ(jumps, 2) << "the Guardian Angel Slime, at both her rungs, is the "
                         "only one that jumps";
}

// Every boss names a page, and every page fits the grid. Page 2 keeps its last
// row for the Black Mage, who isn't built yet.
TEST_F(BossDataTest, EveryPageFitsTheGrid) {
  std::map<int, int> fights;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    EXPECT_GE(entry.second.page(), 1) << entry.first << " names no page";
    EXPECT_LE(entry.second.page(), kBossPages) << entry.first;
    ++fights[entry.second.page()];
  }
  for (const std::pair<const int, int>& page : fights) {
    EXPECT_LE(page.second, kBossListCapacity) << "page " << page.first;
  }
  EXPECT_LE(fights[2], kBossListCapacity - 1)
      << "the Black Mage's row is taken";
}

// The fewest rows the fight card's details may be left: enough for the level,
// the unlock level and two phases' HP before the reader has to scroll.
constexpr int kMinDetailRows = 4;

// A name too wide for its column scrolls, and a grid where nothing moves reads
// at a glance, so the shipped names must fit their columns.
TEST_F(BossDataTest, EveryNameFitsWhereTheBossScreenDrawsIt) {
  std::map<std::string, Boss> bosses = LoadBosses();
  for (const std::pair<const std::string, Boss>& entry : bosses) {
    EXPECT_LT(static_cast<int>(entry.second.name().size()), kBossNameWidth)
        << entry.second.name() << " slides under the Name column";
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      // The card's title is "<difficulty> <boss>", with one column of margin on
      // each side like every row below it.
      std::string title = difficulty.name() + " " + entry.second.name();
      EXPECT_LE(static_cast<int>(title.size()), kDetailWidth - 2)
          << title << " fills the card it heads";
      // The rewards are fixed under the details, which scroll in what is left:
      // the title and its rule, then at most meso, EXP, Honor, the drops and a
      // rule between them, under a rule and a heading of their own.
      int rewards = 2 + 3 + difficulty.drops_size() + 1;
      EXPECT_GE(kBossPanelHeight - 2 - 2 - rewards, kMinDetailRows)
          << title << " leaves its details too few rows";
    }
  }
}

// Where parts stand is data, and two in one cell means one bar drawn over
// another.
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
        for (const ArenaSpot& spot : AllPlayerSpots(phase)) {
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

// A phase with no player spots starts the player at the origin, on top of
// whatever is drawn there.
TEST_F(BossDataTest, EveryPhaseStandsThePlayerInsideItsArena) {
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const BossPhase& phase : difficulty.phases()) {
        std::string where = entry.first + " " + difficulty.name();
        EXPECT_GT(phase.player_spots_size(), 0)
            << where << " gives the player nowhere to stand";
        for (const ArenaSpot& spot : AllPlayerSpots(phase)) {
          EXPECT_LT(spot.x(), phase.arena_width())
              << where << " stands the player past the right of its arena";
          EXPECT_LT(spot.y(), phase.arena_height())
              << where << " stands the player past the bottom of its arena";
        }
      }
    }
  }
}

// A giant's bar reaches into the cells beside it and the rows above and below,
// so a spot there would be drawn under it. Timed spots must close before they
// next open, or they would never close at all.
TEST_F(BossDataTest, GiantsAndTimedSpotsLeaveTheirArenasDrawable) {
  int giants = 0;
  int timed = 0;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const BossPhase& phase : difficulty.phases()) {
        std::string where = entry.first + " " + difficulty.name();
        if (!phase.timed_spots().spots().empty()) {
          ++timed;
          EXPECT_GT(phase.timed_spots().open_ms(), 0) << where;
          int interval = phase.timed_spots().interval_ms();
          if (phase.timed_spots().follow_guard()) {
            EXPECT_EQ(interval, 0) << where << " has two clocks";
            for (const Spawn& spawn : phase.spawns()) {
              if (spawn.has_guard()) {
                interval = spawn.guard().interval_ms();
                break;
              }
            }
          }
          EXPECT_LT(phase.timed_spots().open_ms(), interval)
              << where << " opens its timed spots for good";
        }
        for (const Spawn& spawn : phase.spawns()) {
          if (!spawn.has_self_shield()) {
            continue;
          }
          const SelfShield& shield = spawn.self_shield();
          EXPECT_GT(shield.percent(), 0.0) << where;
          EXPECT_GE(shield.percent_per_second(), 0.0) << where;
          EXPECT_LT(shield.damage_pct(), 100) << where;
          if (!shield.form().empty()) {
            EXPECT_TRUE(std::any_of(
                phase.forms().begin(), phase.forms().end(),
                [&](const PhaseForm& f) { return f.name() == shield.form(); }))
                << where << " raises its shield with a form it never takes";
            EXPECT_EQ(shield.interval_ms(), 0) << where << " has two clocks";
          } else {
            // No duration holds it until broken, the next topping it up.
            EXPECT_GT(shield.interval_ms(), 0) << where;
            EXPECT_GE(shield.duration_ms(), 0) << where;
            EXPECT_LT(shield.duration_ms(), shield.interval_ms())
                << where << " outlasts its own clock";
          }
          // Its end heals whatever shield is left, so another's would heal too.
          for (const Spawn& giver : phase.spawns()) {
            EXPECT_FALSE(std::find(giver.shield().mobs().begin(),
                                   giver.shield().mobs().end(),
                                   spawn.mob()) != giver.shield().mobs().end())
                << where << " shields " << spawn.mob() << " twice over";
          }
        }
        for (const Spawn& spawn : phase.spawns()) {
          if (!spawn.has_guard()) {
            continue;
          }
          const SpawnGuard& guard = spawn.guard();
          EXPECT_GT(guard.damage_pct(), 0) << where;
          EXPECT_LT(guard.damage_pct(), 100) << where;
          EXPECT_GT(guard.open_ms(), 0) << where;
          EXPECT_LT(guard.open_ms(), guard.interval_ms())
              << where << " opens its guard for good";
          for (int i = 0; i < guard.hp_opens_size(); ++i) {
            EXPECT_GT(guard.hp_opens(i), 0) << where;
            EXPECT_LT(guard.hp_opens(i), i == 0 ? 100 : guard.hp_opens(i - 1))
                << where << " lists its HP openings out of order";
          }
        }
        for (const Spawn& spawn : phase.spawns()) {
          if (!spawn.giant()) {
            continue;
          }
          for (const ArenaSpot& at : spawn.spots()) {
            ++giants;
            for (const ArenaSpot& spot : AllPlayerSpots(phase)) {
              EXPECT_FALSE(std::abs(spot.x() - at.x()) <= 1 &&
                           std::abs(spot.y() - at.y()) <= 1)
                  << where << " stands the player under its giant at ("
                  << spot.x() << ", " << spot.y() << ")";
            }
          }
        }
      }
    }
  }
  EXPECT_EQ(giants, 2) << "Gloom, at both difficulties, is the only giant";
  EXPECT_EQ(timed, 2) << "Gloom's tentacles are the only timed spots";
}

// The arrows reach every spot from where the player starts. A press with two
// spots equally near goes nowhere, so a layout can strand a ledge without
// anything else noticing.
TEST_F(BossDataTest, TheArrowsReachEverySpot) {
  const int kSteps[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (const std::pair<const std::string, Boss>& entry : bosses_) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (int i = 0; i < difficulty.phases_size(); ++i) {
        const BossPhase& phase = difficulty.phases(i);
        std::set<int> seen = {0};
        std::vector<int> frontier = {0};
        while (!frontier.empty()) {
          int from = frontier.back();
          frontier.pop_back();
          for (const auto& step : kSteps) {
            int to = NextPlayerSpot(phase, from, step[0], step[1]);
            if (seen.insert(to).second) {
              frontier.push_back(to);
            }
          }
        }
        EXPECT_EQ(seen.size(), AllPlayerSpots(phase).size())
            << entry.first << " " << difficulty.name() << " phase " << i + 1;
      }
    }
  }
}

// Five spots on the floor of every fight, plus any ledges, at every difficulty,
// so a full party always has somewhere to move.
TEST_F(BossDataTest, EveryFightOffersTheSpotsItWasDesignedWith) {
  std::map<std::string, std::vector<int>> expected = {
      {"zakum", {5}},
      {"hilla", {5}},
      {"horntail", {6, 6, 6}},
      {"magnus", {5}},
      {"arkarium", {5}},
      {"cygnus", {5}},
      {"pink_bean", {5, 5}},
      {"pierre", {5}},
      {"von_bon", {5}},
      {"crimson_queen", {5}},
      {"vellum", {5}},
      {"princess_no", {9}},
      {"papulatus", {7, 5}},
      {"lotus", {5, 8, 8}},
      {"damien", {5, 5}},
      {"guardian_angel_slime", {5}},
      {"lucid", {5, 5}},
      {"will", {5, 5, 5}},
      {"gloom", {5}},
      {"darknell", {5}},
      {"verus_hilla", {5}},
      {"seren", {5, 5}},
      {"kalos", {9, 10}},
      {"kaling", {25, 12, 6}},
      {"first_adversary", {5, 9, 5}}};
  // A difficulty with a phase its Normal lacks.
  const std::map<std::string, std::vector<int>> kOwnShape = {
      {"lucid Hard", {5, 5, 5}}};
  for (const std::pair<const std::string, std::vector<int>>& want : expected) {
    ASSERT_GT(bosses_.count(want.first), 0u) << want.first;
    for (const BossDifficulty& difficulty :
         bosses_.at(want.first).difficulties()) {
      std::string where = want.first + " " + difficulty.name();
      std::map<std::string, std::vector<int>>::const_iterator own =
          kOwnShape.find(where);
      const std::vector<int>& spots =
          own == kOwnShape.end() ? want.second : own->second;
      ASSERT_EQ(difficulty.phases_size(), static_cast<int>(spots.size()))
          << where;
      for (int i = 0; i < difficulty.phases_size(); ++i) {
        EXPECT_EQ(difficulty.phases(i).player_spots_size(), spots[i])
            << where << " phase " << i + 1;
      }
    }
  }
}

// One grid for every fight, so a room has the same shape whichever boss is in
// it and, more importantly, no arena outgrows the smallest terminal the game
// supports. See kArenaColumns.
TEST_F(BossDataTest, EveryArenaStandsOnTheOneGrid) {
  int phases = 0;
  for (const std::pair<const std::string, Boss>& entry : LoadBosses()) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const BossPhase& phase : difficulty.phases()) {
        ++phases;
        std::string where = entry.first + " " + difficulty.name();
        // Rooms side by side are each a grid; see ArenaRoom.
        int rooms = 0;
        for (const PhaseZone& zone : phase.zones()) {
          if (zone.room().width() > 0) {
            EXPECT_EQ(zone.room().width(), kArenaColumns) << where;
            EXPECT_EQ(zone.room().x(), rooms * kArenaColumns) << where;
            ++rooms;
          }
        }
        EXPECT_EQ(phase.arena_width(), std::max(1, rooms) * kArenaColumns)
            << where;
        EXPECT_EQ(phase.arena_height(), kArenaRows) << where;
      }
    }
  }
  EXPECT_GT(phases, 0);
}

}  // namespace
}  // namespace ms
