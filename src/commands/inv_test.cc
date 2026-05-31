#include "src/commands/inv.h"

#include <gtest/gtest.h>
#include "src/character.h"
#include "src/character.pb.h"
#include "src/equip.pb.h"

namespace ms {
namespace {

TEST(InvCommandTest, ReturnsNothingEquippedWhenEmpty) {
  CharacterInstance c = CharacterInstance(Character{});
  EXPECT_EQ(InvCommand(c), "Nothing equipped.");
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
