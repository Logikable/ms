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

// A spawn naming no mob file is dropped by the loader, so the map quietly
// farms fewer monsters than its data says -- which has cost us a live bug
// before, and which nothing else would catch.
TEST(MapDataTest, EverySpawnNamesAMob) {
  std::map<std::string, Mob> mobs = LoadMobs();
  for (const std::pair<const std::string, MapData>& entry : LoadMaps()) {
    for (const MapData::Spawn& spawn : entry.second.spawns()) {
      EXPECT_GT(mobs.count(spawn.mob()), 0u)
          << entry.first << " spawns \"" << spawn.mob() << "\", which no mob "
          << "file defines";
      EXPECT_GT(spawn.count(), 0)
          << entry.first << " spawns " << spawn.mob() << " zero times";
    }
  }
}

// Likewise a drop: the item is looked up by stem when the kill lands, and an
// unresolvable one is skipped, so the mob simply drops nothing.
TEST(MapDataTest, EveryDropNamesAnItem) {
  std::map<std::string, ItemPrototype> items = LoadItems();
  for (const std::pair<const std::string, Mob>& entry : LoadMobs()) {
    for (const MobDrop& drop : entry.second.drops()) {
      EXPECT_GT(items.count(drop.item()), 0u)
          << entry.first << " drops \"" << drop.item() << "\", which no item "
          << "file defines";
      EXPECT_GT(drop.per_kill(), 0.0)
          << entry.first << " drops " << drop.item() << " never";
    }
  }
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
      EXPECT_GT(it->second.sell_price(), 0)
          << drop.item() << ", off " << entry.first << ", sells for nothing";
    }
  }
}

// A mob with no HP dies to nothing and one with no EXP pays for nothing;
// either would make a map that looks farmable and is not.
TEST(MapDataTest, EveryMobCanBeFoughtAndIsWorthFighting) {
  for (const std::pair<const std::string, Mob>& entry : LoadMobs()) {
    EXPECT_GT(entry.second.level(), 0) << entry.first;
    EXPECT_GT(entry.second.max_hp(), 0) << entry.first;
    EXPECT_GT(entry.second.exp(), 0) << entry.first;
    EXPECT_FALSE(entry.second.name().empty()) << entry.first;
  }
}

}  // namespace
}  // namespace ms
