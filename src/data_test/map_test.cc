// Checks the shipped maps, mobs and items against each other rather than any
// one function. The three catalogs reference each other by filename stem, and a
// stem that names nothing fails silently: the loader skips it, the map farms
// less than it looks like it should, and nothing says so.
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
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

std::map<std::string, Mob> LoadMobs() {
  return LoadTextProtoDir<Mob>(TestRunfiles()->Rlocation("ms/data/mobs"));
}

std::map<std::string, MapData> LoadMaps() {
  return LoadTextProtoDir<MapData>(TestRunfiles()->Rlocation("ms/data/maps"));
}

std::map<std::string, ItemPrototype> LoadItems() {
  return LoadTextProtoDir<ItemPrototype>(
      TestRunfiles()->Rlocation("ms/data/items"));
}

std::map<std::string, EquipPrototype> LoadEquips() {
  return LoadTextProtoDir<EquipPrototype>(
      TestRunfiles()->Rlocation("ms/data/equip"));
}

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
      // A drop names one catalog or the other, never both and never neither.
      if (!drop.equip().empty()) {
        EXPECT_TRUE(drop.item().empty())
            << entry.first << " drops \"" << drop.equip()
            << "\" as both an equip and a stackable";
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

// The Frozen set's whole drop table, checked as the rule it is rather than as
// a copy of itself: each piece drops from the ten mob levels below the level
// it can be worn at, and thinly from everything above that up to its own top.
// A mob added inside a piece's reach without its share is a piece the player
// can no longer expect to find. One rule for all four: 1/4,000 through a
// piece's own band, 1/10,000 the rest of the way up.
//
// The tops are staggered rather than shared. The better the piece, the longer
// it stays worth finding, so the cape is still turning up thirty levels past
// where the top has stopped.
TEST(MapDataTest, EveryMobInAPiecesReachDropsIt) {
  struct Piece {
    const char* stem;
    int band_low;   // first mob level that drops it
    int band_high;  // last one
  };
  const Piece kPieces[] = {
      {"frozen_top", 61, 100},
      {"frozen_bottom", 71, 110},
      {"frozen_hat", 81, 120},
      {"frozen_cape", 91, 130},
  };
  constexpr double kInBand = 0.00025;
  constexpr double kTrickle = 0.0001;

  int checked = 0;
  for (const std::pair<const std::string, Mob>& entry : LoadMobs()) {
    if (entry.second.boss()) {
      continue;  // a boss drops its own table, not its band's
    }
    int level = entry.second.level();
    std::map<std::string, double> rates;
    for (const MobDrop& drop : entry.second.drops()) {
      if (!drop.equip().empty()) {
        rates[drop.equip()] = drop.per_kill();
      }
    }
    for (const Piece& piece : kPieces) {
      double expected = 0.0;
      if (level >= piece.band_low && level <= piece.band_high) {
        expected = level <= piece.band_low + 9 ? kInBand : kTrickle;
      }
      if (expected == 0.0) {
        EXPECT_EQ(rates.count(piece.stem), 0u)
            << entry.first << " (Lv" << level << ") drops " << piece.stem
            << ", which belongs to mobs " << piece.band_low << " to "
            << piece.band_high;
        continue;
      }
      ++checked;
      ASSERT_EQ(rates.count(piece.stem), 1u)
          << entry.first << " (Lv" << level << ") does not drop " << piece.stem;
      EXPECT_DOUBLE_EQ(rates[piece.stem], expected)
          << entry.first << " drops " << piece.stem << " at the wrong rate";
    }
  }
  EXPECT_GT(checked, 0) << "no mob in the catalog drops the set";
}

// The Frozen tokens buy the last two pieces of the same set, and drop from the
// band the weapons they buy are worn in -- 1/4,000, one flat rate, with
// nothing above 120. A player past the band who still wants one goes back down
// for it, the way they would for any other drop they walked past.
//
// The rate is set by the fastest character rather than the average one: the
// band is a window, and whoever crosses it quickest buys the fewest chances.
// //analysis:level_sim counts those kills -- 17k for a Dark Knight against
// 120k for a Crusader -- and at 1/4,000 even the shortest crossing comes away
// empty about one climb in eighty.
TEST(MapDataTest, OnlyTheTokenBandDropsBothFrozenTokens) {
  constexpr int kBandLow = 101;
  constexpr int kBandHigh = 120;
  constexpr double kInBand = 0.00025;
  const char* kTokens[] = {"frozen_weapon_token", "frozen_secondary_token"};

  int checked = 0;
  for (const std::pair<const std::string, Mob>& entry : LoadMobs()) {
    if (entry.second.boss()) {
      continue;  // as above: a boss is not part of the band's table
    }
    int level = entry.second.level();
    std::map<std::string, double> rates;
    for (const MobDrop& drop : entry.second.drops()) {
      if (!drop.item().empty()) {
        rates[drop.item()] = drop.per_kill();
      }
    }
    double expected = level >= kBandLow && level <= kBandHigh ? kInBand : 0.0;
    for (const char* token : kTokens) {
      if (expected == 0.0) {
        EXPECT_EQ(rates.count(token), 0u)
            << entry.first << " (Lv" << level << ") drops " << token
            << ", which belongs to mobs " << kBandLow << " to " << kBandHigh;
        continue;
      }
      ++checked;
      ASSERT_EQ(rates.count(token), 1u)
          << entry.first << " (Lv" << level << ") does not drop " << token;
      EXPECT_DOUBLE_EQ(rates[token], expected)
          << entry.first << " drops " << token << " at the wrong rate";
    }
  }
  EXPECT_GT(checked, 0) << "no mob in the catalog drops a token";
}

// An Etc drop is worth picking up only for what it sells for, and a price of
// zero also disables the Sell menu entry -- so the drop would be litter.
//
// Deliberately NOT checked: that the price is twice the mob's level. That is
// the wiki's {{Leftover Price|N}} template, but N is the ITEM's level, not the
// dropping mob's -- Firewood is priced at 21 off Axe Stump and also drops from
// the level-22 Dark Axe Stump. The two agree often enough to look like a rule
// and are not one.
TEST(MapDataTest, EveryEtcDropIsWorthSomething) {
  std::map<std::string, ItemPrototype> items = LoadItems();
  for (const std::pair<const std::string, Mob>& entry : LoadMobs()) {
    for (const MobDrop& drop : entry.second.drops()) {
      std::map<std::string, ItemPrototype>::const_iterator it =
          items.find(drop.item());
      if (it == items.end() || it->second.category() != ITEM_CATEGORY_ETC) {
        continue;  // a missing item is covered above; a Use drop has a use
      }
      if (!it->second.currency_mark().empty()) {
        continue;  // a token buys gear, so it is not litter at any price
      }
      EXPECT_GT(it->second.sell_price(), 0)
          << drop.item() << ", off " << entry.first << ", sells for nothing";
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

}  // namespace
}  // namespace ms
