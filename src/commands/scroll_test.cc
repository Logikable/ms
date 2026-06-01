#include "src/commands/scroll.h"

#include <gtest/gtest.h>

#include <random>

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
  std::vector<Scroll> scrolls_;
};

TEST_F(ScrollListTest, NoWeaponEquipped) {
  CharacterInstance c = CharacterInstance(Character{});
  EXPECT_EQ(ScrollListCommand(c, scrolls_), "No weapon equipped.");
}

TEST_F(ScrollListTest, NoApplicableScrolls) {
  scrolls_.push_back(MakeScroll(100, EQUIP_JOB_CATEGORY_MAGICIAN));
  CharacterInstance c = CharacterWithSword(7);
  EXPECT_NE(ScrollListCommand(c, scrolls_).find("No applicable scrolls"),
            std::string::npos);
}

TEST_F(ScrollListTest, ListsApplicableScrollsByIndex) {
  scrolls_.push_back(MakeScroll(100, EQUIP_JOB_CATEGORY_WARRIOR));
  scrolls_.push_back(MakeScroll(70, EQUIP_JOB_CATEGORY_WARRIOR));
  CharacterInstance c = CharacterWithSword(7);
  std::string out = ScrollListCommand(c, scrolls_);
  EXPECT_NE(out.find("[0]"), std::string::npos);
  EXPECT_NE(out.find("[1]"), std::string::npos);
}

TEST_F(ScrollListTest, FiltersOutInapplicableScrolls) {
  scrolls_.push_back(MakeScroll(100, EQUIP_JOB_CATEGORY_WARRIOR));
  scrolls_.push_back(MakeScroll(100, EQUIP_JOB_CATEGORY_MAGICIAN));
  CharacterInstance c = CharacterWithSword(7);
  std::string out = ScrollListCommand(c, scrolls_);
  EXPECT_NE(out.find("[0]"), std::string::npos);
  EXPECT_EQ(out.find("[1]"), std::string::npos);
}

TEST_F(ScrollListTest, ShowsItemNameAndRemainingSlots) {
  scrolls_.push_back(MakeScroll(100, EQUIP_JOB_CATEGORY_WARRIOR));
  CharacterInstance c = CharacterWithSword(5);
  std::string out = ScrollListCommand(c, scrolls_);
  EXPECT_NE(out.find("Sword"), std::string::npos);
  EXPECT_NE(out.find("5 upgrade slots"), std::string::npos);
}

class ScrollApplyTest : public testing::Test {
 protected:
  std::vector<Scroll> scrolls_;
  std::mt19937 rng_{0};
};

TEST_F(ScrollApplyTest, NoWeaponEquipped) {
  CharacterInstance c = CharacterInstance(Character{});
  EXPECT_EQ(ScrollApplyCommand(c, scrolls_, 0, rng_), "No weapon equipped.");
}

TEST_F(ScrollApplyTest, NoSlotsRemaining) {
  CharacterInstance c = CharacterWithSword(0);
  scrolls_.push_back(MakeScroll(100));
  EXPECT_NE(ScrollApplyCommand(c, scrolls_, 0, rng_).find("No upgrade slots"),
            std::string::npos);
}

TEST_F(ScrollApplyTest, InvalidIndex) {
  CharacterInstance c = CharacterWithSword(7);
  EXPECT_EQ(ScrollApplyCommand(c, scrolls_, 0, rng_), "Invalid scroll index.");
  scrolls_.push_back(MakeScroll(100));
  EXPECT_EQ(ScrollApplyCommand(c, scrolls_, 1, rng_), "Invalid scroll index.");
  EXPECT_EQ(ScrollApplyCommand(c, scrolls_, -1, rng_), "Invalid scroll index.");
}

TEST_F(ScrollApplyTest, SuccessPrefixOnSuccess) {
  scrolls_.push_back(MakeScroll(100));
  CharacterInstance c = CharacterWithSword(3);
  EXPECT_EQ(ScrollApplyCommand(c, scrolls_, 0, rng_).substr(0, 8), "Success!");
}

TEST_F(ScrollApplyTest, FailedPrefixOnFailure) {
  scrolls_.push_back(MakeScroll(0));
  CharacterInstance c = CharacterWithSword(3);
  EXPECT_EQ(ScrollApplyCommand(c, scrolls_, 0, rng_).substr(0, 7), "Failed.");
}

TEST(SortScrollsTest, SortsStatGroupsInOrder) {
  std::vector<Scroll> scrolls;
  Scroll luk, str, att;
  luk.mutable_stats()->set_luk(2);
  str.mutable_stats()->set_str(2);
  att.mutable_stats()->set_attack(1);
  scrolls.push_back(luk);
  scrolls.push_back(str);
  scrolls.push_back(att);
  SortScrolls(scrolls);
  EXPECT_EQ(scrolls[0].stats().attack(), 1);  // ATT first
  EXPECT_EQ(scrolls[1].stats().str(), 2);     // STR second
  EXPECT_EQ(scrolls[2].stats().luk(), 2);     // LUK last
}

TEST(SortScrollsTest, SortsDescendingSuccessRateWithinGroup) {
  std::vector<Scroll> scrolls;
  Scroll s30, s100, s70;
  s30.mutable_stats()->set_attack(3);
  s30.set_success_rate(30);
  s100.mutable_stats()->set_attack(1);
  s100.set_success_rate(100);
  s70.mutable_stats()->set_attack(2);
  s70.set_success_rate(70);
  scrolls.push_back(s30);
  scrolls.push_back(s100);
  scrolls.push_back(s70);
  SortScrolls(scrolls);
  EXPECT_EQ(scrolls[0].success_rate(), 100);
  EXPECT_EQ(scrolls[1].success_rate(), 70);
  EXPECT_EQ(scrolls[2].success_rate(), 30);
}

TEST(SortScrollsTest, StatGroupBeforeRateAcrossGroups) {
  std::vector<Scroll> scrolls;
  Scroll str30, att70;
  str30.mutable_stats()->set_str(2);
  str30.set_success_rate(30);
  att70.mutable_stats()->set_attack(2);
  att70.set_success_rate(70);
  scrolls.push_back(str30);
  scrolls.push_back(att70);
  SortScrolls(scrolls);
  EXPECT_EQ(scrolls[0].stats().attack(), 2);  // ATT group before STR group
  EXPECT_EQ(scrolls[1].stats().str(), 2);
}

}  // namespace
}  // namespace ms
