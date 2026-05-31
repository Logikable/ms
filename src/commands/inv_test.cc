#include "src/commands/inv.h"

#include <gtest/gtest.h>
#include "src/character.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

class InvCommandTest : public testing::Test {
 protected:
  CharacterInstance c_{Character{}};
};

TEST_F(InvCommandTest, ReturnsNothingToShowWhenEmpty) {
  EXPECT_EQ(InvCommand(c_), "Nothing to show.");
}

TEST_F(InvCommandTest, ShowsBagItemsWithIndices) {
  EquipPrototype proto;
  proto.set_name("Sword");
  c_.PickUp(proto);
  c_.PickUp(proto);
  std::string out = InvCommand(c_);
  EXPECT_NE(out.find("[0]"), std::string::npos);
  EXPECT_NE(out.find("[1]"), std::string::npos);
}

TEST_F(InvCommandTest, ShowsBothSectionsWhenPopulated) {
  EquipPrototype equipped_proto;
  equipped_proto.set_name("Sword");
  equipped_proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  EquipPrototype bag_proto;
  bag_proto.set_name("Axe");
  c_.PickUp(equipped_proto);
  c_.Equip(0);
  c_.PickUp(bag_proto);
  std::string out = InvCommand(c_);
  EXPECT_NE(out.find("Equipped"), std::string::npos);
  EXPECT_NE(out.find("Bag"), std::string::npos);
  EXPECT_NE(out.find("Sword"), std::string::npos);
  EXPECT_NE(out.find("Axe"), std::string::npos);
}

TEST_F(InvCommandTest, ShowsSlotNameAndItemName) {
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  c_.PickUp(proto);
  c_.Equip(0);
  EXPECT_NE(InvCommand(c_).find("Primary Weapon:  Sword"), std::string::npos);
}

}  // namespace
}  // namespace ms
