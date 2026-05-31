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
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  c.PickUp(proto);
  c.Equip(0);
  return c;
}

Scroll MakeScroll(int success_rate,
                  EquipJobCategory category = EQUIP_JOB_CATEGORY_WARRIOR) {
  Scroll s;
  s.set_name("Test Scroll");
  s.set_success_rate(success_rate);
  s.add_applicable_job_categories(category);
  return s;
}

class ScrollListTest : public testing::Test {
 protected:
  std::map<std::string, Scroll> scrolls_;
};

TEST_F(ScrollListTest, NoWeaponEquipped) {
  CharacterInstance c = CharacterInstance(Character{});
  EXPECT_EQ(ScrollListCommand(c, scrolls_), "No weapon equipped.");
}

TEST_F(ScrollListTest, NoApplicableScrolls) {
  scrolls_["mage_scroll"] = MakeScroll(100, EQUIP_JOB_CATEGORY_MAGICIAN);
  CharacterInstance c = CharacterWithSword(7);
  EXPECT_NE(ScrollListCommand(c, scrolls_).find("No applicable scrolls"),
            std::string::npos);
}

TEST_F(ScrollListTest, ListsApplicableScrollsByIndex) {
  scrolls_["a"] = MakeScroll(100, EQUIP_JOB_CATEGORY_WARRIOR);
  scrolls_["b"] = MakeScroll(70, EQUIP_JOB_CATEGORY_WARRIOR);
  CharacterInstance c = CharacterWithSword(7);
  std::string out = ScrollListCommand(c, scrolls_);
  EXPECT_NE(out.find("[0]"), std::string::npos);
  EXPECT_NE(out.find("[1]"), std::string::npos);
}

TEST_F(ScrollListTest, FiltersOutInapplicableScrolls) {
  scrolls_["warrior"] = MakeScroll(100, EQUIP_JOB_CATEGORY_WARRIOR);
  scrolls_["mage"] = MakeScroll(100, EQUIP_JOB_CATEGORY_MAGICIAN);
  CharacterInstance c = CharacterWithSword(7);
  std::string out = ScrollListCommand(c, scrolls_);
  EXPECT_NE(out.find("[0]"), std::string::npos);
  EXPECT_EQ(out.find("[1]"), std::string::npos);
}

TEST_F(ScrollListTest, ShowsItemNameAndRemainingSlots) {
  scrolls_["s"] = MakeScroll(100, EQUIP_JOB_CATEGORY_WARRIOR);
  CharacterInstance c = CharacterWithSword(5);
  std::string out = ScrollListCommand(c, scrolls_);
  EXPECT_NE(out.find("Sword"), std::string::npos);
  EXPECT_NE(out.find("5 upgrade slots"), std::string::npos);
}

class ScrollApplyTest : public testing::Test {
 protected:
  std::map<std::string, Scroll> scrolls_;
  std::mt19937 rng_{0};
};

TEST_F(ScrollApplyTest, NoWeaponEquipped) {
  CharacterInstance c = CharacterInstance(Character{});
  EXPECT_EQ(ScrollApplyCommand(c, scrolls_, 0, rng_), "No weapon equipped.");
}

TEST_F(ScrollApplyTest, NoSlotsRemaining) {
  CharacterInstance c = CharacterWithSword(0);
  scrolls_["s"] = MakeScroll(100);
  EXPECT_NE(ScrollApplyCommand(c, scrolls_, 0, rng_).find("No upgrade slots"),
            std::string::npos);
}

TEST_F(ScrollApplyTest, InvalidIndex) {
  CharacterInstance c = CharacterWithSword(7);
  EXPECT_EQ(ScrollApplyCommand(c, scrolls_, 0, rng_), "Invalid scroll index.");
  scrolls_["s"] = MakeScroll(100);
  EXPECT_EQ(ScrollApplyCommand(c, scrolls_, 1, rng_), "Invalid scroll index.");
  EXPECT_EQ(ScrollApplyCommand(c, scrolls_, -1, rng_), "Invalid scroll index.");
}

TEST_F(ScrollApplyTest, SuccessPrefixOnSuccess) {
  scrolls_["s"] = MakeScroll(100);
  CharacterInstance c = CharacterWithSword(3);
  EXPECT_EQ(ScrollApplyCommand(c, scrolls_, 0, rng_).substr(0, 8), "Success!");
}

TEST_F(ScrollApplyTest, FailedPrefixOnFailure) {
  scrolls_["s"] = MakeScroll(0);
  CharacterInstance c = CharacterWithSword(3);
  EXPECT_EQ(ScrollApplyCommand(c, scrolls_, 0, rng_).substr(0, 7), "Failed.");
}

}  // namespace
}  // namespace ms
