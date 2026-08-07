// Checks the shipped equip catalog rather than any one function. The model
// enforces whatever a prototype declares, which is exactly why the declaration
// itself needs pinning: an item added without it is an item the model happily
// lets the player upgrade.
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "src/frontend/widgets/panel_util.h"
#include "src/item/item.h"
#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

std::map<std::string, EquipPrototype> LoadEquips() {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  EXPECT_NE(runfiles, nullptr) << err;
  return LoadTextProtoDir<EquipPrototype>(runfiles->Rlocation("ms/data/equip"));
}

// Stars are ammunition, not a weapon a player invests in. Asserted over the
// whole catalog because the refusal has to be written on each one: nothing
// derives it from the type, deliberately, since a later star may well differ.
TEST(EquipDataTest, ThrowingStarsTakeNoUpgrades) {
  int seen = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_type() != EQUIP_TYPE_THROWING_STAR) {
      continue;
    }
    ++seen;
    EXPECT_FALSE(Supports(proto, UPGRADE_SCROLL))
        << entry.first << " can be scrolled";
    EXPECT_FALSE(Supports(proto, UPGRADE_STAR_FORCE))
        << entry.first << " can be star forced";
    EXPECT_EQ(proto.upgrade_slots(), 0)
        << entry.first << " carries slots it will never spend";
  }
  EXPECT_GT(seen, 0) << "no throwing stars in the catalog to check";
}

// The refusal is the exception. A catalog where it spread to ordinary weapons
// would pass every check above and leave the player unable to upgrade anything.
TEST(EquipDataTest, OrdinaryWeaponsStillTakeUpgrades) {
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_PRIMARY_WEAPON) {
      continue;
    }
    EXPECT_TRUE(Supports(proto, UPGRADE_SCROLL))
        << entry.first << " cannot be scrolled";
    EXPECT_TRUE(Supports(proto, UPGRADE_STAR_FORCE))
        << entry.first << " cannot be star forced";
    EXPECT_GT(proto.upgrade_slots(), 0)
        << entry.first << " has no slots to scroll";
  }
}

// Every shipped item has to be describable. A slot or a weapon type added
// without a display name shows up as a blank column in the bag, which reads as
// a bug in the item rather than a missing label.
// A weapon type has one attack speed, and every weapon of it swings at that
// speed. GMS's own low-level items disagree among themselves -- the polearms
// range over three stages -- but by the level 150 tier, the one that matters,
// Nexon had settled each type on a single value. That value is what the
// catalog uses, all the way down.
TEST(EquipDataTest, AWeaponTypeHasOneAttackSpeed) {
  std::map<EquipType, std::pair<AttackSpeed, std::string>> speed_of_type;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_type() == EQUIP_TYPE_UNSPECIFIED ||
        proto.attack_speed() == ATTACK_SPEED_UNSPECIFIED) {
      continue;  // not a weapon, or ammunition that is never swung
    }
    std::map<EquipType, std::pair<AttackSpeed, std::string>>::iterator it =
        speed_of_type.find(proto.equip_type());
    if (it == speed_of_type.end()) {
      speed_of_type[proto.equip_type()] = {proto.attack_speed(), entry.first};
      continue;
    }
    EXPECT_EQ(proto.attack_speed(), it->second.first)
        << entry.first << " and " << it->second.second << " are both "
        << FormatEquipType(proto.equip_type())
        << " but swing at different speeds";
  }
}

TEST(EquipDataTest, EveryItemsSlotAndTypeHaveNames) {
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    EXPECT_FALSE(FormatSlot(proto.equip_slot()).empty())
        << entry.first << " wears in an unnamed slot";
    if (proto.equip_type() != EQUIP_TYPE_UNSPECIFIED) {
      EXPECT_FALSE(FormatEquipType(proto.equip_type()).empty())
          << entry.first << " is an unnamed kind of item";
    }
  }
}

}  // namespace
}  // namespace ms
