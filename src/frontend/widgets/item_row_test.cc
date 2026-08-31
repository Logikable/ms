#include "src/frontend/widgets/item_row.h"

#include <gtest/gtest.h>

#include <string>

#include "src/frontend/widgets/marquee.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// --- FormatItemEntry ---

TEST(FormatItemEntryTest, ContainsNameSlotInfoAndUpgrades) {
  std::string entry = FormatItemEntry("Sword", "Weapon", "+7 ATT", 3, 7, 12);
  EXPECT_NE(entry.find("Sword"), std::string::npos);
  EXPECT_NE(entry.find("Weapon"), std::string::npos);
  EXPECT_NE(entry.find("+7 ATT"), std::string::npos);
  EXPECT_NE(entry.find("+3/7"), std::string::npos);
  EXPECT_NE(entry.find("12\u2605"), std::string::npos);
}

// Short and long info strings put the upgrade columns at the same offset.
TEST(FormatItemEntryTest, InfoColumnPaddedForAlignment) {
  std::string short_entry = FormatItemEntry("Sword", "Weapon", "A", 3, 7, 12);
  std::string long_entry =
      FormatItemEntry("Sword", "Weapon", "A longer info", 3, 7, 12);
  EXPECT_EQ(short_entry.find("12\u2605"), long_entry.find("12\u2605"));
}

// An item name wider than its column is cut, and slides under the column once
// its row has been selected long enough -- the same treatment a skill name
// gets, through the same ScrollingWindow. Nothing shipped is this long yet;
// the wiring is what is being pinned.
TEST(FormatItemEntryTest, ALongNameIsCutAndThenSlides) {
  const char* kWordy = "Fafnir Mistilteinn Trace Of Old";  // 31 columns
  std::string still = FormatItemEntry(kWordy, "Weapon", "+7 ATT", 3, 7, 12);
  EXPECT_EQ(still.substr(0, kItemNameWidth), "Fafnir Mistilteinn Trace O");

  std::string slid = FormatItemEntry(kWordy, "Weapon", "+7 ATT", 3, 7, 12,
                                     kMarqueePause + kMarqueeStep);
  EXPECT_EQ(slid.substr(0, kItemNameWidth), "fnir Mistilteinn Trace Of ");
  // The columns after the name do not move with it.
  EXPECT_EQ(still.substr(kItemNameWidth), slid.substr(kItemNameWidth));
}

// A wide panel hands the name column the room its other cells do not want,
// and a wider name column is all a wider row is.
TEST(FormatItemEntryTest, AWiderColumnHoldsMoreOfTheName) {
  const char* kWordy = "Fafnir Mistilteinn Trace Of Old";  // 31 columns
  std::string narrow = FormatItemEntry(kWordy, "Weapon", "+7 ATT", 3, 7, 12);
  std::string wide = FormatItemEntry(
      kWordy, "Weapon", "+7 ATT", 3, 7, 12,
      std::chrono::steady_clock::duration::zero(), kItemNameMax);
  EXPECT_EQ(wide.substr(0, kItemNameMax),
            "Fafnir Mistilteinn Trace Of Old       ");
  // Everything after the name is the same row, moved over.
  EXPECT_EQ(narrow.substr(kItemNameWidth), wide.substr(kItemNameMax));
}

// The column follows the panel's width between the two ends of its range,
// and stops at both.
TEST(ItemNameWidthForTest, GrowsWithThePanelAndStopsAtBothEnds) {
  int narrowest = kItemListFixedWidth + kItemNameWidth + kItemListGutter;
  EXPECT_EQ(ItemNameWidthFor(narrowest), kItemNameWidth);
  EXPECT_EQ(ItemNameWidthFor(10), kItemNameWidth) << "never below its own";
  EXPECT_EQ(ItemNameWidthFor(narrowest + 5), kItemNameWidth + 5);
  EXPECT_EQ(ItemNameWidthFor(500), kItemNameMax)
      << "and never past the longest name there is";
}

// A name that fits is padded to the column and never moves, so a list of them
// stays a list rather than shuffling under the cursor.
TEST(FormatItemEntryTest, AShortNameNeverMoves) {
  std::string still = FormatItemEntry("Sword", "Weapon", "+7 ATT", 3, 7, 12);
  std::string later = FormatItemEntry("Sword", "Weapon", "+7 ATT", 3, 7, 12,
                                      kMarqueePause * 10);
  EXPECT_EQ(still, later);
}

TEST(FormatItemEntryTest, AnUpgradeTheItemRefusesShowsDash) {
  std::string entry = FormatItemEntry("Sword", "Weapon", "info", -1, 0, -1);
  EXPECT_NE(entry.find("-"), std::string::npos);
  EXPECT_EQ(entry.find("+"), std::string::npos);
  EXPECT_EQ(entry.find("\u2605"), std::string::npos);
}

// The overload every list actually calls: both counts come off the item, so
// the refused cases cannot be got right in one panel and wrong in another.
TEST(FormatItemEntryTest, ReadsBothUpgradesOffTheItem) {
  EquipPrototype proto;
  proto.set_upgrade_slots(7);
  Equip state;
  state.set_scroll_successes(3);
  state.set_remaining_upgrade_slots(2);
  state.set_stars(12);
  std::string entry = FormatItemEntry("Sword", "Weapon", "info", proto, state);
  EXPECT_NE(entry.find("+3/7"), std::string::npos);
  EXPECT_NE(entry.find("12\u2605"), std::string::npos);

  proto.set_upgrade_slots(0);
  proto.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  entry = FormatItemEntry("Sword", "Weapon", "info", proto, state);
  EXPECT_EQ(entry.find("+"), std::string::npos);
  EXPECT_EQ(entry.find("\u2605"), std::string::npos);
}

}  // namespace
}  // namespace ms
