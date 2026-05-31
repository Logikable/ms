#include "src/commands/equip.h"

#include <gtest/gtest.h>
#include "src/character.h"
#include "src/character.pb.h"
#include "src/equip.pb.h"

namespace ms {
namespace {

CharacterInstance FreshCharacter() {
  return CharacterInstance(Character{});
}

EquipPrototype MakeSword() {
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.set_upgrade_slots(7);
  return proto;
}

TEST(EquipCommandTest, ReturnsEquippedOnSuccess) {
  CharacterInstance c = FreshCharacter();
  c.PickUp(MakeSword());
  EXPECT_EQ(EquipCommand(c, 0), "Equipped.");
}

TEST(EquipCommandTest, ReturnsErrorOnOutOfBoundsIndex) {
  CharacterInstance c = FreshCharacter();
  EXPECT_NE(EquipCommand(c, 0).find("Could not equip"), std::string::npos);
}

TEST(EquipCommandTest, ReturnsErrorOnUnspecifiedSlot) {
  CharacterInstance c = FreshCharacter();
  EquipPrototype proto;
  proto.set_name("Unknown");
  c.PickUp(proto);
  EXPECT_NE(EquipCommand(c, 0).find("Could not equip"), std::string::npos);
}

}  // namespace
}  // namespace ms
