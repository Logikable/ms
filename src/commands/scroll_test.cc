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

TEST(ScrollCommandTest, ReturnsErrorWhenNoWeaponEquipped) {
  CharacterInstance c = CharacterInstance(Character{});
  Scroll scroll;
  std::mt19937 rng(0);
  EXPECT_EQ(ScrollCommand(c, scroll, rng), "No weapon equipped.");
}

TEST(ScrollCommandTest, ReturnsErrorWhenNoSlotsRemaining) {
  CharacterInstance c = CharacterWithSword(/*upgrade_slots=*/0);
  Scroll scroll;
  std::mt19937 rng(0);
  EXPECT_NE(ScrollCommand(c, scroll, rng).find("No upgrade slots remaining"),
            std::string::npos);
}

TEST(ScrollCommandTest, ReturnsSuccessPrefixOnSuccess) {
  CharacterInstance c = CharacterWithSword(/*upgrade_slots=*/3);
  Scroll scroll;
  scroll.set_success_rate(100);
  std::mt19937 rng(0);
  EXPECT_EQ(ScrollCommand(c, scroll, rng).substr(0, 8), "Success!");
}

TEST(ScrollCommandTest, ReturnsFailedPrefixOnFailure) {
  CharacterInstance c = CharacterWithSword(/*upgrade_slots=*/3);
  Scroll scroll;
  scroll.set_success_rate(0);
  std::mt19937 rng(0);
  EXPECT_EQ(ScrollCommand(c, scroll, rng).substr(0, 7), "Failed.");
}

}  // namespace
}  // namespace ms
