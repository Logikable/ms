#include "src/commands/unequip.h"

#include <gtest/gtest.h>
#include "src/character.h"
#include "src/character.pb.h"
#include "src/equip.pb.h"

namespace ms {
namespace {

TEST(UnequipCommandTest, ReturnsUnequippedOnSuccess) {
  CharacterInstance c = CharacterInstance(Character{});
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  c.PickUp(proto);
  c.Equip(0);
  EXPECT_EQ(UnequipCommand(c, EQUIP_SLOT_PRIMARY_WEAPON), "Unequipped.");
}

TEST(UnequipCommandTest, ReturnsErrorOnUnspecifiedSlot) {
  CharacterInstance c = CharacterInstance(Character{});
  EXPECT_NE(UnequipCommand(c, EQUIP_SLOT_UNSPECIFIED).find("Nothing equipped"),
            std::string::npos);
}

TEST(UnequipCommandTest, ReturnsErrorOnUnoccupiedSlot) {
  CharacterInstance c = CharacterInstance(Character{});
  EXPECT_NE(
      UnequipCommand(c, EQUIP_SLOT_PRIMARY_WEAPON).find("Nothing equipped"),
      std::string::npos);
}

}  // namespace
}  // namespace ms
