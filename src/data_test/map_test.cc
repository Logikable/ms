// Checks the shipped maps, mobs and items against each other. The three
// catalogs refer to each other by filename stem, and a stem that names nothing
// fails silently: the loader skips it, the map farms less than it should, and
// nothing reports it.
#include <gtest/gtest.h>

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "src/character/arcane_force.h"
#include "src/character/sacred_power.h"
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

// The level Arcane River opens at. No map below it is in the river, so a map
// below it that requires Arcane Force has the number in the wrong file.
constexpr int kArcaneRiverFloor = 200;

// The loader drops a spawn that names no mob file, so the map quietly farms
// fewer monsters than its data says. This has caused a live bug before, and
// nothing else would catch it.
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

// The same for drops: the item is looked up by stem when the kill happens, and
// one that can't be found is skipped, so the mob drops nothing.
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

// Checks the Frozen set's drop table as a rule: a piece drops from the twenty
// mob levels starting where it can be worn, and rarely from the twenty above. A
// mob added inside a piece's range without its drop means the player can no
// longer count on finding that piece.
//
// One rule for all six, tokens included: 1/4,000 through the wear band,
// 1/10,000 above it. The rate is set for the fastest character, since the band
// is a window and the quickest pass through it gets the fewest chances. At
// 1/4,000 even that character misses about one climb in eighty.
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

// An Etc drop is only worth picking up for its sell price, and a price of zero
// also disables the Sell option, so the drop would be junk.
//
// Deliberately not checked: that the price is twice the mob's level. The wiki's
// template uses the item's level, not the dropping mob's, and the two agree
// often enough to look like a rule when they aren't.
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

// Every monster needs armour. Every monster has at least the standard 10% PDR
// and bosses have more, which keeps IED worth buying against farming mobs and
// not only bosses. A mob file missing the line has no armour and nothing
// reports it; the damage math just pays out 11% more against it.
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

// A mob with no HP dies to nothing, and one with no EXP pays nothing; either
// makes a map that looks farmable but isn't. Bosses are exempt from the EXP
// check: Zakum's arms are worth nothing in GMS either, and the fight pays out
// for the body at the end.
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

// Every mob a map spawns can be inspected, and the inspect screen starts with
// its bestiary description. There are two exceptions, both shown as an empty
// block instead of invented text: Arcane River and Grandis, which the wiki has
// no archive entries for, and Onyx Stonegar, which the wiki also says nothing
// about. Inventing text would put words in the game that no source supports.
//
// The map decides whether a mob is in the river or Grandis, since it is the map
// that requires a force. Nothing about the monster says so: Tenebris drops no
// symbol, and level doesn't tell you either, since Black Heaven goes up to 219
// and requires no force.
TEST(MapDataTest, EveryMapMobIsDescribed) {
  std::map<std::string, Mob> mobs = LoadMobs();
  for (const std::pair<const std::string, MapData>& entry : LoadMaps()) {
    if (entry.second.arcane_force() > 0 || entry.second.sacred_power() > 0) {
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

// Arcane Force and Arcane River go together, checked from both sides since
// neither can be derived from the other. A river map missing its requirement
// would let a character with no symbols farm it at full damage, defeating the
// point of the stat; a requirement on an overworld map would penalize a fight
// GMS doesn't. The second check only reaches down to the level floor: Black
// Heaven goes up to 219 outside the river, so a stray number there looks like
// river and isn't caught.
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

// A map is in the river, in Grandis, or in neither, and Grandis opens at its
// own level. A map requiring both would be treated as river only.
TEST(MapDataTest, SacredPowerGoesWithGrandis) {
  std::map<std::string, Mob> mobs = LoadMobs();
  int checked = 0;
  for (const std::pair<const std::string, MapData>& entry : LoadMaps()) {
    if (entry.second.sacred_power() == 0) {
      continue;
    }
    ++checked;
    EXPECT_EQ(entry.second.arcane_force(), 0)
        << entry.first << " asks for both forces";
    for (const Spawn& spawn : entry.second.spawns()) {
      std::map<std::string, Mob>::const_iterator mob = mobs.find(spawn.mob());
      if (mob != mobs.end()) {
        EXPECT_GE(mob->second.level(), kGrandisLevel)
            << entry.first << " asks for Sacred Power and spawns "
            << spawn.mob() << ", which is below Grandis";
      }
    }
  }
  EXPECT_GT(checked, 0) << "no Grandis maps in the catalog to check";
}

}  // namespace
}  // namespace ms
