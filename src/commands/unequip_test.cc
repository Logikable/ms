#include "src/commands/unequip.h"

#include <gtest/gtest.h>
#include "src/character.h"
#include "src/character.pb.h"
#include "src/equip.pb.h"

namespace ms {
namespace {

class UnequipCommandTest : public testing::Test {
 protected:
  void SetUp() override {
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  }
  CharacterInstance c_{Character{}};
  EquipPrototype sword_;
};

TEST_F(UnequipCommandTest, ReturnsUnequippedOnSuccess) {
  c_.PickUp(sword_);
  c_.Equip(0);
  EXPECT_EQ(UnequipCommand(c_, EQUIP_SLOT_PRIMARY_WEAPON), "Unequipped.");
}

TEST_F(UnequipCommandTest, ReturnsErrorOnUnspecifiedSlot) {
  EXPECT_NE(UnequipCommand(c_, EQUIP_SLOT_UNSPECIFIED).find("Nothing equipped"),
            std::string::npos);
}

TEST_F(UnequipCommandTest, ReturnsErrorOnUnoccupiedSlot) {
  EXPECT_NE(
      UnequipCommand(c_, EQUIP_SLOT_PRIMARY_WEAPON).find("Nothing equipped"),
      std::string::npos);
}

}  // namespace
}  // namespace ms
