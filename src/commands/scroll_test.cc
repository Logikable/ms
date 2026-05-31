#include "src/commands/scroll.h"

#include <random>

#include <gtest/gtest.h>
#include "src/character.h"
#include "src/character.pb.h"
#include "src/equip.pb.h"
#include "src/scroll.pb.h"

namespace ms {
namespace {

CharacterInstance CharacterWithSword(int upgrade_slots) {
  CharacterInstance c = CharacterInstance(Character{});
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.set_upgrade_slots(upgrade_slots);
  c.PickUp(proto);
  c.Equip(0);
  return c;
}

class ScrollCommandTest : public testing::Test {
 protected:
  Scroll scroll_;
  std::mt19937 rng_{0};
};

TEST_F(ScrollCommandTest, ReturnsErrorWhenNoWeaponEquipped) {
  CharacterInstance c = CharacterInstance(Character{});
  EXPECT_EQ(ScrollCommand(c, scroll_, rng_), "No weapon equipped.");
}

TEST_F(ScrollCommandTest, ReturnsErrorWhenNoSlotsRemaining) {
  CharacterInstance c = CharacterWithSword(/*upgrade_slots=*/0);
  EXPECT_NE(ScrollCommand(c, scroll_, rng_).find("No upgrade slots remaining"),
            std::string::npos);
}

TEST_F(ScrollCommandTest, ReturnsSuccessPrefixOnSuccess) {
  CharacterInstance c = CharacterWithSword(/*upgrade_slots=*/3);
  scroll_.set_success_rate(100);
  EXPECT_EQ(ScrollCommand(c, scroll_, rng_).substr(0, 8), "Success!");
}

TEST_F(ScrollCommandTest, ReturnsFailedPrefixOnFailure) {
  CharacterInstance c = CharacterWithSword(/*upgrade_slots=*/3);
  scroll_.set_success_rate(0);
  EXPECT_EQ(ScrollCommand(c, scroll_, rng_).substr(0, 7), "Failed.");
}

}  // namespace
}  // namespace ms
