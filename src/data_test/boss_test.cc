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
#include "src/character/max_character.h"
#include "src/character/sacred_power.h"
#include "src/character/symbol.h"
#include "src/combat/arena_spots.h"
#include "src/combat/loot.h"
#include "src/frontend/screens/boss_fight_panel.h"
#include "src/frontend/screens/boss_select_panel.h"
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
    "seren",        "kalos", "first_adversary", "kaling",
    "malefic_star", "limbo", "baldrix",         "jupiter",
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
      EXPECT_NE(difficulty.reset(), RESET_PERIOD_UNSPECIFIED)
          << entry.first << " " << difficulty.name();
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
  // Chaos Zakum, for whom GMS gives no EXP; and both difficulties of Lotus,
  // the Guardian Angel Slime, Lucid, Will, Gloom, Darknell and Verus Hilla,
  // which are the same case and are fought for their drops.
  EXPECT_EQ(unpaid,
            std::vector<std::string>(
                {"crimson_queen", "darknell", "darknell", "gloom", "gloom",
                 "guardian_angel_slime", "guardian_angel_slime", "lotus",
                 "lotus", "lucid", "lucid", "pierre", "vellum", "verus_hilla",
                 "verus_hilla", "von_bon", "will", "will", "zakum"}));
}

// A shell (none today) can't be entered, so a timer, gate or
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
      EXPECT_EQ(difficulty.reset(), RESET_PERIOD_UNSPECIFIED) << where;
      EXPECT_EQ(difficulty.unlock_level(), 0) << where;
      EXPECT_EQ(difficulty.meso(), 0) << where;
      EXPECT_EQ(difficulty.exp(), 0) << where;
      EXPECT_EQ(difficulty.drops_size(), 0) << where;
    }
  }
  EXPECT_EQ(shells, std::vector<std::string>());
}

// Each harder fight is its Normal with GMS's Hard or Chaos numbers (v272's
// HP), and the difficulty is the only difference: the same mobs in the same
// cells, so building a shell means filling in what's missing. Hard
// Lucid adds a third phase of her own after Normal's two.
TEST_F(BossDataTest, EveryHarderFightIsItsNormalShapeAtGmsNumbers) {
  struct Want {
    std::string boss;
    std::string name;
    std::vector<int64_t> hp;
  };
  const std::vector<Want> kHarder = {
      {"damien", "Hard", {17085600000000LL, 7322400000000LL}},
      {"lotus", "Hard", {6830082000000LL, 6830082000000LL, 9106776000000LL}},
      {"guardian_angel_slime", "Chaos", {61650000000000LL}},
      {"lucid", "Hard", {34544000000000LL, 36720000000000LL, 12800000000000LL}},
      {"will", "Hard", {28182000000000LL, 21136500000000LL, 35227500000000LL}},
      {"gloom", "Chaos", {86012850000000LL}},
      {"darknell", "Hard", {108202500000000LL}},
      {"verus_hilla", "Hard", {122451000000000LL}}};
  for (const Want& want : kHarder) {
    ASSERT_GT(bosses_.count(want.boss), 0u) << want.boss;
    ASSERT_EQ(bosses_.at(want.boss).difficulties_size(), 2) << want.boss;
    const BossDifficulty& normal = bosses_.at(want.boss).difficulties(0);
    const BossDifficulty& harder = bosses_.at(want.boss).difficulties(1);
    SCOPED_TRACE(want.boss);
    EXPECT_EQ(harder.name(), want.name);
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
      EXPECT_EQ(mob.pdr(), before.pdr());
      EXPECT_EQ(mob.max_hp(), want.hp[i]);
    }
  }
  // GMS fights her at 250, past the level cap, and a shell's card doesn't show
  // a level, so nothing reads it yet.
  EXPECT_EQ(mobs_.at("chaos_guardian_angel_slime").level(), 250);
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
  EXPECT_EQ(fights, 34) << "Arkarium, Cygnus, Princess No, Papulatus, the "
                           "four of Root Abyss, and both difficulties of "
                           "Zakum, Magnus, Pink Bean, Hilla, Horntail, Lotus, "
                           "Damien, the Guardian Angel Slime, Lucid, Will, "
                           "Gloom, Darknell and Verus Hilla";
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
// next sweep, and a clear can't come before the fight opens.
TEST_F(BossDataTest, MaxModeKnowsWhenEveryBuiltFightFalls) {
  for (const std::pair<const std::string, Boss>& entry : bosses_) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (difficulty.coming_soon()) {
        continue;
      }
      const int cleared = MaxClearLevel(entry.first, difficulty.name());
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
    EXPECT_EQ(hard.reset(), RESET_PERIOD_DAILY);
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

// Where a drop sits in the boss drop tiers (memory boss_drop_tiers.md). A
// currency's Normal rate is its tier's; an accessory's is 1 / days to a full
// set x the accessory multiplier, split between the bosses that drop it; a box
// pays one piece for every three its currency buys. A fixed rate is the user's,
// outside the tiers.
enum class DropKind { kCurrency, kAccessory, kBox, kFixed };
struct TierDrop {
  int tier = 1;
  DropKind kind = DropKind::kCurrency;
  // A box only: what its piece costs, and the fights dropping the currency
  // against the fights dropping the box, each counted at the box's
  // difficulty (a Normal fight is half of one).
  int price = 0;
  double currency_fights = 1;
  double box_fights = 1;
  // kFixed only.
  double rate = 0.0;
};

// The two levers, and each tier's currency rate at Normal.
constexpr double kAccessoryMultiplier = 2.0;
constexpr double kDaysPerSet[] = {5, 10, 20, 40};
constexpr double kCurrencyRate[] = {1.0, 0.5, 0.25, 0.1};

const std::map<std::string, TierDrop>& TierDrops() {
  static const std::map<std::string, TierDrop>* const kDrops = [] {
    auto* drops = new std::map<std::string, TierDrop>;
    for (const char* key :
         {"piece_of_anguish", "piece_of_mockery", "piece_of_destruction",
          "piece_of_time", "cygnus_shoulder_token"}) {
      (*drops)[key] = {1, DropKind::kCurrency};
    }
    for (const char* key :
         {"aquatic_letter_eye_accessory", "condensed_power_crystal",
          "silver_blossom_ring", "horntail_necklace", "chaos_horntail_necklace",
          "dea_sidus_earring", "black_bean_mark", "golden_clover_belt",
          "pink_holy_cup", "crystal_ventus_badge", "royal_black_metal_shoulder",
          "stone_of_eternal_life", "will_o_the_wisps", "dominator_pendant",
          "papulatus_mark", "kannas_treasure", "ayames_treasure",
          "hayatos_treasure"}) {
      (*drops)[key] = {1, DropKind::kAccessory};
    }
    for (const char* key : {"absolab_coin", "captivating_fragment"}) {
      (*drops)[key] = {2, DropKind::kCurrency};
    }
    (*drops)["absolab_armor_box"] = {2, DropKind::kBox, 1};
    (*drops)["absolab_weapon_box"] = {2, DropKind::kBox, 3};
    for (const char* key : {"guardian_angel_ring", "twilight_mark",
                            "estella_earrings", "daybreak_pendant"}) {
      (*drops)[key] = {2, DropKind::kAccessory};
    }
    for (const char* key : {"phantasma_coin", "arachno_coin"}) {
      (*drops)[key] = {3, DropKind::kCurrency};
    }
    for (const char* key :
         {"magic_eyepatch", "berserked", "black_heart", "dreamy_belt",
          "wills_cursed_spellbook_selection_box", "endless_terror",
          "commanding_force_earring", "source_of_suffering"}) {
      (*drops)[key] = {3, DropKind::kAccessory};
    }
    // Two coin fights (Hard Lucid, Hard Will) against six box fights, Normal
    // Verus Hilla and Hard Black Mage (unbuilt) among them at half.
    (*drops)["arcane_umbra_armor_box"] = {3, DropKind::kBox, 1, 2, 5};
    (*drops)["arcane_umbra_weapon_box"] = {3, DropKind::kBox, 3, 2, 5};
    // The user's rates (2026-10-04, memory boss_ring_boxes.md).
    (*drops)["black_jade_boss_ring_box"] = {3, DropKind::kFixed, 0, 1, 1, 0.02};
    for (const char* key : {"premium_scroll_for_accessory",
                            "premium_scroll_for_pet", "scroll_for_accessory"}) {
      (*drops)[key] = {3, DropKind::kFixed, 0, 1, 1, 0.5};
    }
    (*drops)["magical_scroll_for_weapon"] = {3, DropKind::kFixed, 0, 1, 1, 0.1};
    return drops;
  }();
  return *kDrops;
}

// Steps above Normal: Easy -1, Hard and Chaos +1, Extreme +2.
int DifficultyRank(const std::string& name) {
  if (name == "Easy") {
    return 0;
  }
  if (name == "Normal") {
    return 1;
  }
  if (name == "Extreme") {
    return 3;
  }
  return 2;
}

// Every drop but a soul shard has the rate its tier gives it. A new boss's
// drop fails here until it is placed in a tier, which is the point: rates come
// from the design, never by hand.
TEST_F(BossDataTest, EveryDropFollowsTheBossDropTiers) {
  std::map<std::string, int> sources;
  for (const std::pair<const std::string, Boss>& entry : bosses_) {
    std::set<std::string> keys;
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const MobDrop& drop : difficulty.drops()) {
        keys.insert(DropKey(drop));
      }
    }
    for (const std::string& key : keys) {
      ++sources[key];
    }
  }
  for (const std::pair<const std::string, Boss>& entry : bosses_) {
    // The easiest difficulty a boss has counts as Normal.
    const int base = DifficultyRank(entry.second.difficulties(0).name()) - 1;
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      const double steps =
          std::pow(2.0, DifficultyRank(difficulty.name()) - 1 - base);
      for (const MobDrop& drop : difficulty.drops()) {
        const std::string key = DropKey(drop);
        SCOPED_TRACE(entry.first + " " + difficulty.name() + " " + key);
        if (absl::EndsWith(key, "_soul_shard")) {
          EXPECT_DOUBLE_EQ(drop.per_kill(), 1.0);
          continue;
        }
        std::map<std::string, TierDrop>::const_iterator it =
            TierDrops().find(key);
        ASSERT_NE(it, TierDrops().end()) << "in no tier";
        const TierDrop& tier = it->second;
        double want = 0.0;
        switch (tier.kind) {
          case DropKind::kCurrency:
            want = kCurrencyRate[tier.tier - 1] * steps;
            break;
          case DropKind::kAccessory:
            want = steps * kAccessoryMultiplier / kDaysPerSet[tier.tier - 1] /
                   sources[key];
            break;
          case DropKind::kBox:
            want = kCurrencyRate[tier.tier - 1] * steps * tier.currency_fights /
                   (3.0 * tier.price * tier.box_fights);
            break;
          case DropKind::kFixed:
            want = tier.rate;
            break;
        }
        EXPECT_NEAR(drop.per_kill() * DropRolls(drop), want, 0.005);
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

// The fewest rows the fight card's details may be left: enough for the level,
// the unlock level and two phases' HP before the reader has to scroll.
constexpr int kMinDetailRows = 4;

// A name too wide for its column scrolls, and a grid where nothing moves reads
// at a glance, so the shipped names must fit their columns.
TEST_F(BossDataTest, EveryNameFitsWhereTheBossScreenDrawsIt) {
  std::map<std::string, Boss> bosses = LoadBosses();
  EXPECT_LE(static_cast<int>(bosses.size()), kBossListCapacity)
      << "the grid has no room for the whole catalog";
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
          EXPECT_LT(phase.timed_spots().open_ms(),
                    phase.timed_spots().interval_ms())
              << where << " opens its timed spots for good";
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

// Five spots on the floor of every fight, plus any ledges, at every difficulty,
// so a full party always has somewhere to move.
TEST_F(BossDataTest, EveryFightOffersTheSpotsItWasDesignedWith) {
  std::map<std::string, std::vector<int>> expected = {
      {"zakum", {7, 5}},       {"hilla", {5}},
      {"horntail", {6, 6, 6}}, {"magnus", {5}},
      {"arkarium", {5}},       {"cygnus", {5}},
      {"pink_bean", {5, 5}},   {"pierre", {5}},
      {"von_bon", {5}},        {"crimson_queen", {5}},
      {"vellum", {5}},         {"princess_no", {9}},
      {"papulatus", {7, 5}},   {"lotus", {5, 8, 8}},
      {"damien", {5, 5}},      {"guardian_angel_slime", {5}},
      {"lucid", {5, 5}},       {"will", {5, 5, 5}},
      {"gloom", {5}},          {"darknell", {5}},
      {"verus_hilla", {5}}};
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
        EXPECT_EQ(phase.arena_width(), kArenaColumns) << where;
        EXPECT_EQ(phase.arena_height(), kArenaRows) << where;
      }
    }
  }
  EXPECT_GT(phases, 0);
}

}  // namespace
}  // namespace ms
