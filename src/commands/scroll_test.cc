#include "src/commands/scroll.h"

#include <map>
#include <random>

#include <gtest/gtest.h>
#include "src/character.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

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
  std::map<std::string, Scroll> scrolls_;
  std::mt19937 rng_{0};
};

TEST_F(ScrollCommandTest, ReturnsErrorOnUnknownScroll) {
  CharacterInstance c = CharacterInstance(Character{});
  EXPECT_EQ(ScrollCommand(c, scrolls_, "unknown", rng_),
            "Unknown scroll 'unknown'.");
}

TEST_F(ScrollCommandTest, ReturnsErrorWhenNoWeaponEquipped) {
  scrolls_["test"] = Scroll{};
  CharacterInstance c = CharacterInstance(Character{});
  EXPECT_EQ(ScrollCommand(c, scrolls_, "test", rng_), "No weapon equipped.");
}

TEST_F(ScrollCommandTest, ReturnsErrorWhenNoSlotsRemaining) {
  scrolls_["test"] = Scroll{};
  CharacterInstance c = CharacterWithSword(/*upgrade_slots=*/0);
  EXPECT_NE(ScrollCommand(c, scrolls_, "test", rng_).find("No upgrade slots remaining"),
            std::string::npos);
}

TEST_F(ScrollCommandTest, ReturnsSuccessPrefixOnSuccess) {
  Scroll scroll;
  scroll.set_success_rate(100);
  scrolls_["test"] = scroll;
  CharacterInstance c = CharacterWithSword(/*upgrade_slots=*/3);
  EXPECT_EQ(ScrollCommand(c, scrolls_, "test", rng_).substr(0, 8), "Success!");
}

TEST_F(ScrollCommandTest, ReturnsFailedPrefixOnFailure) {
  Scroll scroll;
  scroll.set_success_rate(0);
  scrolls_["test"] = scroll;
  CharacterInstance c = CharacterWithSword(/*upgrade_slots=*/3);
  EXPECT_EQ(ScrollCommand(c, scrolls_, "test", rng_).substr(0, 7), "Failed.");
}

}  // namespace
}  // namespace ms
