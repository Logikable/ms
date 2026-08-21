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

// A level lands in the band below it, not on an exact match: the table has a
// row every ten levels and equipment sits anywhere between.
TEST(SpellTraceCostTest, ALevelReadsTheBandBelowIt) {
  EXPECT_EQ(SpellTraceCost(80, TraceCategory::kArmor, 100), 8);
  EXPECT_EQ(SpellTraceCost(85, TraceCategory::kArmor, 100), 8);
  EXPECT_EQ(SpellTraceCost(89, TraceCategory::kArmor, 100), 8);
  EXPECT_EQ(SpellTraceCost(90, TraceCategory::kArmor, 100), 10);
  // A level below every band still has a price: the first row starts at 0.
  EXPECT_EQ(SpellTraceCost(1, TraceCategory::kArmor, 100), 1);
}

// The wiki has no rows for 170-199 or 210-249, so those levels hold the band
// below, which is what GMS itself does.
TEST(SpellTraceCostTest, AGapHoldsTheBandBelowIt) {
  EXPECT_EQ(SpellTraceCost(160, TraceCategory::kWeapon, 100),
            SpellTraceCost(199, TraceCategory::kWeapon, 100));
  EXPECT_EQ(SpellTraceCost(200, TraceCategory::kWeapon, 100),
            SpellTraceCost(249, TraceCategory::kWeapon, 100));
}

// The whole point of the change: one scroll, two prices, because the item it
// goes on is what is being paid for.
TEST(SpellTraceCostTest, TheItemLevelSetsThePrice) {
  Scroll scroll = StatScroll(SCROLL_TARGET_ARMOUR, 30);
  EXPECT_EQ(TraceCost(scroll, 70), 8);
  EXPECT_EQ(TraceCost(scroll, 100), 20);
  EXPECT_LT(TraceCost(scroll, 70), TraceCost(scroll, 100));
}

// Longer odds never cost less, and a weapon never costs less than armour.
//
// Only "never less": the bottom of GMS's table prices armour at 1 and 2
// traces, which cannot separate three rates, so a level 10 armour scroll costs
// the same at 70% and at 30%. The game ships no armour below level 61, so
// nothing reaches that tie today. From level 30 the prices climb strictly.
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

// GMS sells no 15% armour scroll below level 200, and the game must never
// offer one -- a price of zero is a scroll given away.
TEST(SpellTraceCostTest, NoFifteenPercentArmourBelowTwoHundred) {
  EXPECT_EQ(SpellTraceCost(100, TraceCategory::kArmor, 15), 0);
  EXPECT_GT(SpellTraceCost(100, TraceCategory::kWeapon, 15), 0);
  EXPECT_GT(SpellTraceCost(200, TraceCategory::kArmor, 15), 0);
}

// A clean slate is not priced by band -- GMS does not sell one for traces at
// all -- so it keeps the cost written in its own file, whatever the item.
TEST(SpellTraceCostTest, ACleanSlateCarriesItsOwnPrice) {
  Scroll slate;
  slate.set_scroll_category(SCROLL_CATEGORY_CLEAN_SLATE);
  slate.set_success_rate(100);
  slate.set_trace_cost(100);
  EXPECT_EQ(TraceCost(slate, 70), 100);
  EXPECT_EQ(TraceCost(slate, 150), 100);
}

// Same rule for anything else GMS does not sell: no band price, so the file's
// own figure stands rather than the scroll going free.
TEST(SpellTraceCostTest, AnUnsoldScrollFallsBackToItsFile) {
  Scroll armour = StatScroll(SCROLL_TARGET_ARMOUR, 15);  // none below Lv200
  armour.set_trace_cost(40);
  EXPECT_EQ(TraceCost(armour, 100), 40);
  // And GMS does sell it at 200, where the band price takes over again.
  EXPECT_EQ(TraceCost(armour, 200),
            SpellTraceCost(200, TraceCategory::kArmor, 15));
}

// The accessories have no column in the band table yet, so a scroll for one
// keeps the price in its own file rather than being given away.
TEST(SpellTraceCostTest, AnAccessoryScrollCarriesItsOwnPrice) {
  Scroll scroll = StatScroll(SCROLL_TARGET_ACCESSORY, 100);
  scroll.set_trace_cost(50);
  EXPECT_EQ(TraceCost(scroll, 100), 50);
  EXPECT_EQ(TraceCost(scroll, 110), 50);
}

// A stat scroll's own trace_cost is dead data. Leaving one in a file must not
// change what the player pays, or the two prices drift apart unnoticed.
TEST(SpellTraceCostTest, AStatScrollIgnoresItsWrittenPrice) {
  Scroll scroll = StatScroll(SCROLL_TARGET_WEAPON, 70);
  scroll.set_trace_cost(9999);
  EXPECT_EQ(TraceCost(scroll, 70), 8);
}

}  // namespace
}  // namespace ms
