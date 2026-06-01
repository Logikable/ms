#include "src/commands/equip.h"

#include <gtest/gtest.h>

#include "src/character.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

class EquipCommandTest : public testing::Test {
 protected:
  void SetUp() override {
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_upgrade_slots(7);
  }
  CharacterInstance c_{Character{}};
  EquipPrototype sword_;
};

TEST_F(EquipCommandTest, ReturnsEquippedOnSuccess) {
  c_.PickUp(sword_);
  EXPECT_EQ(EquipCommand(c_, 0), "Equipped.");
}

TEST_F(EquipCommandTest, ReturnsErrorOnOutOfBoundsIndex) {
  EXPECT_NE(EquipCommand(c_, 0).find("Could not equip"), std::string::npos);
}

TEST_F(EquipCommandTest, ReturnsErrorOnUnspecifiedSlot) {
  EquipPrototype proto;
  proto.set_name("Unknown");
  c_.PickUp(proto);
  EXPECT_NE(EquipCommand(c_, 0).find("Could not equip"), std::string::npos);
}

}  // namespace
}  // namespace ms
