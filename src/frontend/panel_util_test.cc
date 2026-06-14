#include "src/frontend/panel_util.h"

#include <gtest/gtest.h>

#include <string>

namespace ms {
namespace {

// --- PadRight ---

TEST(PadRightTest, PadsShortStringWithSpaces) {
  EXPECT_EQ(PadRight("hi", 5), "hi   ");
}

TEST(PadRightTest, ExactWidthUnchanged) {
  EXPECT_EQ(PadRight("hello", 5), "hello");
}

TEST(PadRightTest, TruncatesLongString) {
  EXPECT_EQ(PadRight("toolong", 4), "tool");
}

TEST(PadRightTest, EmptyStringProducesAllSpaces) {
  EXPECT_EQ(PadRight("", 3), "   ");
}

// --- AppendStat ---

TEST(AppendStatTest, ZeroValueIsNoOp) {
  std::string out;
  AppendStat(out, 0, "ATT");
  EXPECT_TRUE(out.empty());
}

TEST(AppendStatTest, NegativeValueIsNoOp) {
  std::string out;
  AppendStat(out, -1, "ATT");
  EXPECT_TRUE(out.empty());
}

TEST(AppendStatTest, AppendsLabelAndValue) {
  std::string out;
  AppendStat(out, 5, "ATT");
  EXPECT_EQ(out, "+5 ATT");
}

TEST(AppendStatTest, AddsSeparatorBetweenStats) {
  std::string out;
  AppendStat(out, 3, "STR");
  AppendStat(out, 7, "DEX");
  EXPECT_EQ(out, "+3 STR  +7 DEX");
}

TEST(AppendStatTest, SkipsZeroInMiddle) {
  std::string out;
  AppendStat(out, 3, "STR");
  AppendStat(out, 0, "DEX");
  AppendStat(out, 2, "LUK");
  EXPECT_EQ(out, "+3 STR  +2 LUK");
}

// --- FormatItemEntry ---

TEST(FormatItemEntryTest, ContainsNameSlotInfoAndScrollCounts) {
  std::string entry =
      FormatItemEntry("Sword", EQUIP_SLOT_PRIMARY_WEAPON, "+7 ATT", 3, 2, 4);
  EXPECT_NE(entry.find("Sword"), std::string::npos);
  EXPECT_NE(entry.find("Weapon"), std::string::npos);
  EXPECT_NE(entry.find("+7 ATT"), std::string::npos);
  EXPECT_NE(entry.find("3/2/4"), std::string::npos);
}

TEST(FormatItemEntryTest, InfoColumnPaddedForAlignment) {
  // Short and long info strings should position scroll counts at the same
  // offset.
  std::string short_entry =
      FormatItemEntry("Sword", EQUIP_SLOT_PRIMARY_WEAPON, "A", 3, 2, 4);
  std::string long_entry = FormatItemEntry("Sword", EQUIP_SLOT_PRIMARY_WEAPON,
                                           "A longer info", 3, 2, 4);
  EXPECT_EQ(short_entry.find("3/2/4"), long_entry.find("3/2/4"));
}

TEST(FormatItemEntryTest, NonUpgradeableItemShowsDash) {
  std::string entry =
      FormatItemEntry("Sword", EQUIP_SLOT_PRIMARY_WEAPON, "info", -1, -1, -1);
  EXPECT_NE(entry.find("-"), std::string::npos);
  EXPECT_EQ(entry.find("/"), std::string::npos);
}

// --- FormatJobCategories ---

TEST(FormatJobCategoriesTest, EmptyCategoriesReturnsAll) {
  EquipPrototype proto;
  EXPECT_EQ(FormatJobCategories(proto), "All");
}

TEST(FormatJobCategoriesTest, UniversalReturnsAll) {
  EquipPrototype proto;
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  EXPECT_EQ(FormatJobCategories(proto), "All");
}

TEST(FormatJobCategoriesTest, SingleCategory) {
  EquipPrototype proto;
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_EQ(FormatJobCategories(proto), "Warrior");
}

TEST(FormatJobCategoriesTest, MultipleCategories) {
  EquipPrototype proto;
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_THIEF);
  EXPECT_EQ(FormatJobCategories(proto), "Warrior/Thief");
}

}  // namespace
}  // namespace ms
