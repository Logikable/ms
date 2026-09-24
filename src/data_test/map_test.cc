// Checks the shipped maps, mobs and items against each other rather than any
// one function. The three catalogs reference each other by filename stem, and a
// stem that names nothing fails silently: the loader skips it, the map farms
// less than it looks like it should, and nothing says so.
#include <gtest/gtest.h>

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "src/character/arcane_force.h"
#include "src/frontend/screens/mob_inspect_panel.h"
#include "src/frontend/widgets/format.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/testing/data_files.h"

namespace ms {
namespace {

std::map<std::string, Mob> LoadMobs() {
  return LoadTestData<Mob>("mobs");
}

std::map<std::string, MapData> LoadMaps() {
  return LoadTestData<MapData>("maps");
}

std::map<std::string, ItemPrototype> LoadItems() {
  return LoadTestData<ItemPrototype>("items");
}

std::map<std::string, EquipPrototype> LoadEquips() {
  return LoadTestData<EquipPrototype>("equip");
}

// Whether `mob` drops one of the six Arcane Symbols.
bool DropsASymbol(const Mob& mob,
                  const std::map<std::string, EquipPrototype>& equips) {
  for (const MobDrop& drop : mob.drops()) {
    std::map<std::string, EquipPrototype>::const_iterator it =
        equips.find(drop.equip());
    if (it != equips.end() && IsArcaneSymbol(it->second)) {
      return true;
    }
  }
  return false;
}

// The level Arcane River opens at. Nothing below it stands in the river, so a
// map asking for Arcane Force under it has the number on the wrong file.
constexpr int kArcaneRiverFloor = 200;

// A spawn naming no mob file is dropped by the loader, so the map quietly
// farms fewer monsters than its data says -- which has cost us a live bug
// before, and which nothing else would catch.
TEST(MapDataTest, EverySpawnNamesAMob) {
  std::map<std::string, Mob> mobs = LoadMobs();
  for (const std::pair<const std::string, MapData>& entry : LoadMaps()) {
    for (const Spawn& spawn : entry.second.spawns()) {
      EXPECT_GT(mobs.count(spawn.mob()), 0u)
          << entry.first << " spawns \"" << spawn.mob() << "\", which no mob "
          << "file defines";
      EXPECT_GT(spawn.count(), 0)
          << entry.first << " spawns " << spawn.mob() << " zero times";
      EXPECT_EQ(spawn.spots_size(), 0)
          << entry.first << " stands " << spawn.mob()
          << " on a spot, which only a boss arena has";
    }
  }
}

// Likewise a drop: the item is looked up by stem when the kill lands, and an
// unresolvable one is skipped, so the mob simply drops nothing.
TEST(MapDataTest, EveryDropNamesAnItem) {
  std::map<std::string, ItemPrototype> items = LoadItems();
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  for (const std::pair<const std::string, Mob>& entry : LoadMobs()) {
    for (const MobDrop& drop : entry.second.drops()) {
      EXPECT_NE(drop.drop_case(), MobDrop::DROP_NOT_SET)
          << entry.first << " has a drop that names nothing";
      if (drop.has_equip()) {
        EXPECT_GT(equips.count(drop.equip()), 0u)
            << entry.first << " drops \"" << drop.equip()
            << "\", which no equip file defines";
      } else {
        EXPECT_GT(items.count(drop.item()), 0u)
            << entry.first << " drops \"" << drop.item()
            << "\", which no item file defines";
      }
      EXPECT_GT(drop.per_kill(), 0.0)
          << entry.first << " drops something never";
    }
  }
}

// The Frozen set's drop table, checked as the RULE it is: a piece drops from
// the twenty mob levels that open where it can be worn, and thinly from the
// twenty above. A mob added inside a piece's reach without its share is a
// piece the player can no longer expect to find.
//
// One rule for all six, tokens included: 1/4,000 through the wear band,
// 1/10,000 the rest of the way. The rate is set by the FASTEST character, a
// band being a window and the quickest crossing buying the fewest chances --
// at 1/4,000 even that comes away empty about one climb in eighty.
TEST(MapDataTest, EveryMobInAPiecesReachDropsIt) {
  struct Piece {
    const char* stem;
    int wear;  // the level it is worn at, which is its band's first mob
  };
  const Piece kPieces[] = {
      {"frozen_top", 70},           {"frozen_bottom", 80},
      {"frozen_hat", 90},           {"frozen_cape", 100},
      {"frozen_weapon_token", 120}, {"frozen_secondary_token", 120},
      {"frozen_gloves", 140},       {"frozen_boots", 140},
  };
  constexpr int kBand = 20;
  constexpr double kInBand = 0.00025;
  constexpr double kTrickle = 0.0001;

  int in_band = 0;
  int trickle = 0;
  for (const std::pair<const std::string, Mob>& entry : LoadMobs()) {
    if (entry.second.boss()) {
      continue;  // a boss drops its own table, not its band's
    }
    int level = entry.second.level();
    std::map<std::string, double> rates;
    for (const MobDrop& drop : entry.second.drops()) {
      rates[drop.has_equip() ? drop.equip() : drop.item()] = drop.per_kill();
    }
    for (const Piece& piece : kPieces) {
      double expected = 0.0;
      if (level >= piece.wear && level <= piece.wear + kBand) {
        expected = kInBand;
      } else if (level > piece.wear + kBand &&
                 level <= piece.wear + 2 * kBand) {
        expected = kTrickle;
      }
      if (expected == 0.0) {
        EXPECT_EQ(rates.count(piece.stem), 0u)
            << entry.first << " (Lv" << level << ") drops " << piece.stem
            << ", which belongs to mobs " << piece.wear << " to "
            << piece.wear + 2 * kBand;
        continue;
      }
      expected == kInBand ? ++in_band : ++trickle;
      ASSERT_EQ(rates.count(piece.stem), 1u)
          << entry.first << " (Lv" << level << ") does not drop " << piece.stem;
      EXPECT_DOUBLE_EQ(rates[piece.stem], expected)
          << entry.first << " drops " << piece.stem << " at the wrong rate";
    }
  }
  EXPECT_GT(in_band, 0) << "no mob drops the set inside its own band";
  EXPECT_GT(trickle, 0) << "no mob drops the set past its band";
}

// An Etc drop is worth picking up only for what it sells for, and a price of
// zero disables the Sell entry as well -- so the drop would be litter.
//
// NOT checked, deliberately: that the price is twice the mob's level. The
// wiki's template is keyed on the ITEM's level, not the dropping mob's, and
// the two agree often enough to look like a rule they are not.
TEST(MapDataTest, EveryEtcDropIsWorthSomething) {
  std::map<std::string, ItemPrototype> items = LoadItems();
  for (const std::pair<const std::string, Mob>& entry : LoadMobs()) {
    for (const MobDrop& drop : entry.second.drops()) {
      std::map<std::string, ItemPrototype>::const_iterator it =
          items.find(drop.item());
      if (it == items.end()) {
        continue;  // covered above
      }
      if (!it->second.currency_mark().empty()) {
        continue;  // a token buys gear, so it is not litter at any price
      }
      EXPECT_GT(it->second.sell_price(), 0)
          << drop.item() << ", off " << entry.first << ", sells for nothing";
    }
  }
}

// Armour is not optional. Every monster carries at least the regular 10% PDR
// and the bosses carry more, which is what keeps IED worth buying against the
// things the player actually farms rather than only on a boss night. A mob
// file that forgets the line reads as armourless and nothing else says so --
// the damage math just quietly pays out 11% more against it.
TEST(MapDataTest, EveryMobWearsAtLeastTheRegularArmour) {
  constexpr int kRegularPdr = 10;
  for (const std::pair<const std::string, Mob>& entry : LoadMobs()) {
    EXPECT_GE(entry.second.pdr(), kRegularPdr)
        << entry.first << " has no armour on it";
    if (!entry.second.boss()) {
      EXPECT_EQ(entry.second.pdr(), kRegularPdr)
          << entry.first << " is not a boss, so it wears the regular armour "
          << "and nothing else -- a monster the player cannot see the defense "
          << "of should not have its own";
    }
  }
}

// A mob with no HP dies to nothing and one with no EXP pays for nothing;
// either would make a map that looks farmable and is not. A boss is exempt
// from the EXP half: Zakum's arms are worth nothing in GMS either, and what
// the fight pays is the body at the end of it.
TEST(MapDataTest, EveryMobCanBeFoughtAndIsWorthFighting) {
  for (const std::pair<const std::string, Mob>& entry : LoadMobs()) {
    EXPECT_GT(entry.second.level(), 0) << entry.first;
    EXPECT_GT(entry.second.max_hp(), 0) << entry.first;
    EXPECT_FALSE(entry.second.name().empty()) << entry.first;
    if (!entry.second.boss()) {
      EXPECT_GT(entry.second.exp(), 0) << entry.first;
    }
  }
}

// Every mob a map spawns is inspectable, and the screen leads with its
// bestiary blurb. Two exemptions, both drawn as an empty block rather than a
// made-up one: Arcane River, which the wiki writes no archive entry for at
// all, and Onyx Stonegar, the one straggler it also says nothing about.
// Inventing text would put words in the game's mouth no source stands
// behind.
//
// The map is what says a mob is in the river: it is the map that asks for
// Arcane Force. Nothing about the monster itself does -- Tenebris drops no
// symbol, and the level does not say so either, Black Heaven running to 219
// and asking for no force at all.
TEST(MapDataTest, EveryMapMobIsDescribed) {
  std::map<std::string, Mob> mobs = LoadMobs();
  for (const std::pair<const std::string, MapData>& entry : LoadMaps()) {
    if (entry.second.arcane_force() > 0) {
      continue;
    }
    for (const Spawn& spawn : entry.second.spawns()) {
      std::map<std::string, Mob>::const_iterator it = mobs.find(spawn.mob());
      if (it == mobs.end() || spawn.mob() == "onyx_stonegar") {
        continue;
      }
      EXPECT_FALSE(it->second.description().empty())
          << spawn.mob() << ", spawned by " << entry.first
          << ", has nothing to read on the inspect screen";
      EXPECT_LE(WrapBalanced(it->second.description(), kFlavourWidth).size(),
                static_cast<size_t>(kFlavourLines))
          << spawn.mob() << "'s blurb overruns the block the inspect screen "
          << "keeps for it, which would push its stats down the panel";
    }
  }
}

// Arcane Force and Arcane River go together, checked from both ends since
// neither end can be derived from the other. A map that forgot its
// requirement would let a character with no symbols farm the river at full
// damage, which is the whole of what the stat is for; a number on an
// overworld map would tax a fight that GMS asks nothing of. The second half
// reaches as far as the level floor: Black Heaven runs to 219 outside the
// river, so a stray number there reads as river and is not caught.
TEST(MapDataTest, ArcaneForceGoesWithArcaneRiver) {
  std::map<std::string, Mob> mobs = LoadMobs();
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  int checked = 0;
  for (const std::pair<const std::string, MapData>& entry : LoadMaps()) {
    for (const Spawn& spawn : entry.second.spawns()) {
      std::map<std::string, Mob>::const_iterator mob = mobs.find(spawn.mob());
      if (mob == mobs.end()) {
        continue;  // covered above
      }
      if (DropsASymbol(mob->second, equips)) {
        ++checked;
        EXPECT_GT(entry.second.arcane_force(), 0)
            << entry.first << " drops an Arcane Symbol and asks for no force";
      }
      if (entry.second.arcane_force() > 0) {
        EXPECT_GE(mob->second.level(), kArcaneRiverFloor)
            << entry.first << " asks for Arcane Force and spawns "
            << spawn.mob() << ", which is below the river";
      }
    }
  }
  EXPECT_GT(checked, 0) << "no Arcane River maps in the catalog to check";
}

}  // namespace
}  // namespace ms
