#include "src/commands/inv.h"

#include <gtest/gtest.h>
#include "src/character.h"
#include "src/character.pb.h"
#include "src/equip.pb.h"

namespace ms {
namespace {

TEST(InvCommandTest, ReturnsNothingToShowWhenEmpty) {
  CharacterInstance c = CharacterInstance(Character{});
  EXPECT_EQ(InvCommand(c), "Nothing to show.");
}

TEST(InvCommandTest, ShowsBagItemsWithIndices) {
  CharacterInstance c = CharacterInstance(Character{});
  EquipPrototype proto;
  proto.set_name("Sword");
  c.PickUp(proto);
  c.PickUp(proto);
  std::string out = InvCommand(c);
  EXPECT_NE(out.find("[0]"), std::string::npos);
  EXPECT_NE(out.find("[1]"), std::string::npos);
}

TEST(InvCommandTest, ShowsBothSectionsWhenPopulated) {
  CharacterInstance c = CharacterInstance(Character{});
  EquipPrototype equipped_proto;
  equipped_proto.set_name("Sword");
  equipped_proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  EquipPrototype bag_proto;
  bag_proto.set_name("Axe");
  c.PickUp(equipped_proto);
  c.Equip(0);
  c.PickUp(bag_proto);
  std::string out = InvCommand(c);
  EXPECT_NE(out.find("Equipped"), std::string::npos);
  EXPECT_NE(out.find("Bag"), std::string::npos);
  EXPECT_NE(out.find("Sword"), std::string::npos);
  EXPECT_NE(out.find("Axe"), std::string::npos);
}

TEST(InvCommandTest, ShowsSlotNameAndItemName) {
  CharacterInstance c = CharacterInstance(Character{});
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  c.PickUp(proto);
  c.Equip(0);
  std::string out = InvCommand(c);
  EXPECT_NE(out.find("Primary Weapon"), std::string::npos);
  EXPECT_NE(out.find("Sword"), std::string::npos);
}

}  // namespace
}  // namespace ms
