#include "src/item/spell_trace_cost.h"

#include <gtest/gtest.h>

#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

Scroll StatScroll(ScrollTarget target, int rate) {
  Scroll scroll;
  scroll.set_target(target);
  scroll.set_success_rate(rate);
  return scroll;
}

// A level uses the band below it, not an exact match: the table has a row every
// ten levels and equipment can be any level in between.
TEST(SpellTraceCostTest, ALevelReadsTheBandBelowIt) {
  EXPECT_EQ(SpellTraceCost(80, TraceCategory::kArmor, 100), 8);
  EXPECT_EQ(SpellTraceCost(85, TraceCategory::kArmor, 100), 8);
  EXPECT_EQ(SpellTraceCost(89, TraceCategory::kArmor, 100), 8);
  EXPECT_EQ(SpellTraceCost(90, TraceCategory::kArmor, 100), 10);
  // A level below every band still has a price, since the first row starts at
  // 0.
  EXPECT_EQ(SpellTraceCost(1, TraceCategory::kArmor, 100), 1);
}

// The wiki has no rows for 170-199 or 210-249, so those levels use the band
// below, as GMS does.
TEST(SpellTraceCostTest, AGapHoldsTheBandBelowIt) {
  EXPECT_EQ(SpellTraceCost(160, TraceCategory::kWeapon, 100),
            SpellTraceCost(199, TraceCategory::kWeapon, 100));
  EXPECT_EQ(SpellTraceCost(200, TraceCategory::kWeapon, 100),
            SpellTraceCost(249, TraceCategory::kWeapon, 100));
}

// The key behaviour: one scroll, two prices, because the price depends on the
// item.
TEST(SpellTraceCostTest, TheItemLevelSetsThePrice) {
  Scroll scroll = StatScroll(SCROLL_TARGET_ARMOUR, 30);
  EXPECT_EQ(TraceCost(scroll, 70), 8);
  EXPECT_EQ(TraceCost(scroll, 100), 20);
  EXPECT_LT(TraceCost(scroll, 70), TraceCost(scroll, 100));
}

// Only "never less": GMS's table prices low armour at 1 and 2 traces, which
// can't separate three rates.
TEST(SpellTraceCostTest, RiskAndWeaponsNeverCostLess) {
  const int kLevels[] = {1, 10, 30, 60, 70, 100, 150};
  for (int level : kLevels) {
    for (TraceCategory category :
         {TraceCategory::kArmor, TraceCategory::kWeapon}) {
      EXPECT_LE(SpellTraceCost(level, category, 100),
                SpellTraceCost(level, category, 70))
          << "at level " << level;
      EXPECT_LE(SpellTraceCost(level, category, 70),
                SpellTraceCost(level, category, 30))
          << "at level " << level;
      if (level >= 30) {
        EXPECT_LT(SpellTraceCost(level, category, 100),
                  SpellTraceCost(level, category, 30))
            << "at level " << level;
      }
    }
    for (int rate : {100, 70, 30}) {
      EXPECT_LE(SpellTraceCost(level, TraceCategory::kArmor, rate),
                SpellTraceCost(level, TraceCategory::kWeapon, rate))
          << "at level " << level << ", " << rate << "%";
    }
  }
}

// GMS sells no 15% armour scroll below level 200, and the game must never offer
// one, since a price of zero would make it free.
TEST(SpellTraceCostTest, NoFifteenPercentArmourBelowTwoHundred) {
  EXPECT_EQ(SpellTraceCost(100, TraceCategory::kArmor, 15), 0);
  EXPECT_GT(SpellTraceCost(100, TraceCategory::kWeapon, 15), 0);
  EXPECT_GT(SpellTraceCost(200, TraceCategory::kArmor, 15), 0);
}

// A Clean Slate isn't priced by band, since GMS doesn't sell one for traces, so
// it keeps the cost in its own file whatever the item.
TEST(SpellTraceCostTest, ACleanSlateCarriesItsOwnPrice) {
  Scroll slate;
  slate.set_scroll_category(SCROLL_CATEGORY_CLEAN_SLATE);
  slate.set_success_rate(100);
  slate.set_trace_cost(100);
  EXPECT_EQ(TraceCost(slate, 70), 100);
  EXPECT_EQ(TraceCost(slate, 150), 100);
}

// The same rule for anything else GMS doesn't sell: there's no band price, so
// the file's own price is used instead of the scroll being free.
TEST(SpellTraceCostTest, AnUnsoldScrollFallsBackToItsFile) {
  Scroll armour = StatScroll(SCROLL_TARGET_ARMOUR, 15);  // none below Lv200
  armour.set_trace_cost(40);
  EXPECT_EQ(TraceCost(armour, 100), 40);
  // GMS does sell it at 200, where the band price applies again.
  EXPECT_EQ(TraceCost(armour, 200),
            SpellTraceCost(200, TraceCategory::kArmor, 15));
}

// Accessories have their own GMS column, between armour and weapons at every
// level the game reaches.
TEST(SpellTraceCostTest, AnAccessoryReadsItsOwnColumn) {
  Scroll scroll = StatScroll(SCROLL_TARGET_ACCESSORY, 100);
  scroll.set_trace_cost(9999);
  EXPECT_EQ(TraceCost(scroll, 100), 18);
  EXPECT_EQ(TraceCost(scroll, 110), 22);
  // Between the two at every level the game reaches. Not above: GMS's table
  // puts the 150 band's accessories below its armour, and these are the wiki's
  // numbers, not our own rule.
  for (int level : {70, 100, 110, 140}) {
    EXPECT_GE(SpellTraceCost(level, TraceCategory::kAccessory, 100),
              SpellTraceCost(level, TraceCategory::kArmor, 100))
        << "at level " << level;
    EXPECT_LE(SpellTraceCost(level, TraceCategory::kAccessory, 100),
              SpellTraceCost(level, TraceCategory::kWeapon, 100))
        << "at level " << level;
  }
}

// A stat scroll's own trace_cost is unused. Leaving one in a file mustn't
// change what the player pays, or the two prices could drift apart unnoticed.
TEST(SpellTraceCostTest, AStatScrollIgnoresItsWrittenPrice) {
  Scroll scroll = StatScroll(SCROLL_TARGET_WEAPON, 70);
  scroll.set_trace_cost(9999);
  EXPECT_EQ(TraceCost(scroll, 70), 8);
}

}  // namespace
}  // namespace ms
