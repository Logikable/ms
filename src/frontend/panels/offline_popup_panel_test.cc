#include "src/frontend/panels/offline_popup_panel.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/combat/offline.h"
#include "src/frontend/widgets/panel_test_base.h"

namespace ms {
namespace {

// How many rules the card draws below the row holding `needle`.
int RuledRowsAfter(const std::string& out, const std::string& needle) {
  std::size_t at = out.find(needle);
  if (at == std::string::npos) {
    return -1;
  }
  int rules = 0;
  std::size_t row = out.find('\n', at);
  while (row != std::string::npos) {
    std::size_t end = out.find('\n', row + 1);
    if (out.substr(row, end - row).find("\u2500") != std::string::npos) {
      ++rules;
    }
    row = end;
  }
  return rules;
}

class OfflinePopupPanelTest : public PanelTest {
 protected:
  // An hour of farming with something to show for it.
  OfflineReport Report() {
    OfflineReport report;
    report.farmed = true;
    report.absence = 3600.0;
    report.seconds = 3600.0;
    report.kills = 1234;
    report.start_level = 20;
    report.end_level = 22;
    report.rewards.exp = 45678;
    report.rewards.meso = 91011;
    report.rewards.items.push_back({"Green Snail Shell", 500, 0});
    report.map_name = "Snail Field";
    return report;
  }

  std::string Render(const OfflineReport& report, bool show_honor = true) {
    return RenderElement(
        OfflinePopupPanel(report, ftxui::text("[Continue]"), show_honor));
  }
};

TEST_F(OfflinePopupPanelTest, ShowsWhatTheAbsencePaid) {
  std::string out = Render(Report());

  EXPECT_NE(out.find("Welcome Back"), std::string::npos);
  EXPECT_NE(out.find("Away for 1h 0m"), std::string::npos);
  EXPECT_NE(out.find("1,234 kills on Snail Field"), std::string::npos);
  EXPECT_NE(out.find("45,678 EXP"), std::string::npos);
  EXPECT_NE(out.find("Level 20 -> 22"), std::string::npos);
  EXPECT_NE(out.find("Green Snail Shell x500"), std::string::npos);
  EXPECT_NE(out.find("[Continue]"), std::string::npos);
}

// What the absence earned reads first -- levels, EXP, meso, honor -- and the
// loot is ruled off under it.
TEST_F(OfflinePopupPanelTest, ARuleDividesWhatWasEarnedFromTheLoot) {
  OfflineReport report = Report();
  report.rewards.honor = 250;

  std::string out = Render(report);

  std::size_t level = out.find("Level 20 -> 22");
  std::size_t exp = out.find("45,678 EXP");
  std::size_t meso = out.find("91,011");
  std::size_t honor = out.find("250 Honor");
  std::size_t loot = out.find("Green Snail Shell");
  ASSERT_NE(loot, std::string::npos);
  EXPECT_LT(level, exp);
  EXPECT_LT(exp, meso);
  EXPECT_LT(meso, honor);
  EXPECT_LT(honor, loot);
  EXPECT_NE(out.substr(honor, loot - honor).find("\u2500"), std::string::npos);
}

// Nothing to rule off from: an absence that dropped nothing draws no rule
// under its numbers.
TEST_F(OfflinePopupPanelTest, NoLootDrawsNoRule) {
  OfflineReport bare = Report();
  bare.rewards.items.clear();

  EXPECT_EQ(RuledRowsAfter(Render(bare), "91,011") + 1,
            RuledRowsAfter(Render(Report()), "91,011"));
}

// The farming pays honor whatever the level, but a player who cannot spend it
// yet is not told about a currency.
TEST_F(OfflinePopupPanelTest, TheHonorWaitsForInnerAbility) {
  OfflineReport report = Report();
  report.rewards.honor = 250;

  std::string out = Render(report, /*show_honor=*/false);

  EXPECT_EQ(out.find("Honor"), std::string::npos);
  EXPECT_NE(out.find("45,678 EXP"), std::string::npos);
}

// Counted in units, not stacks, and what the bag could not hold is called out
// rather than quietly missing from the total.
TEST_F(OfflinePopupPanelTest, SaysWhatAFullBagLost) {
  OfflineReport report = Report();
  report.rewards.items = {{"Green Snail Shell", 10000, 2500}};

  std::string out = Render(report);

  EXPECT_NE(out.find("Green Snail Shell x10,000"), std::string::npos);
  EXPECT_NE(out.find("(2,500 lost)"), std::string::npos);
}

// The header is the whole absence; the death line is how far into it the
// farming got. A card that showed only the shorter of the two would be
// telling the player they were away less time than they were.
TEST_F(OfflinePopupPanelTest, SaysHowFarIntoTheAbsenceThePlayerFell) {
  OfflineReport report = Report();
  report.absence = 28800.0;
  report.seconds = 3600.0;
  report.died = true;

  std::string out = Render(report);

  EXPECT_NE(out.find("Away for 8h 0m"), std::string::npos);
  EXPECT_NE(out.find("Defeated after 1h 0m"), std::string::npos);
  EXPECT_NE(out.find("Maple Island"), std::string::npos);
}

// A player who logged off in town is told that, not shown an empty ledger.
TEST_F(OfflinePopupPanelTest, SaysWhenNothingWasFarmed) {
  OfflineReport report;
  report.absence = 3600.0;

  std::string out = Render(report);

  EXPECT_NE(out.find("not fighting anywhere"), std::string::npos);
  EXPECT_EQ(out.find("kills on"), std::string::npos);
}

TEST_F(OfflinePopupPanelTest, ReadsAnAbsenceInItsTwoLargestUnits) {
  EXPECT_EQ(FormatAbsence(38.0), "38s");
  EXPECT_EQ(FormatAbsence(2700.0), "45m");
  EXPECT_EQ(FormatAbsence(25920.0), "7h 12m");
  EXPECT_EQ(FormatAbsence(273600.0), "3d 4h");
}

}  // namespace
}  // namespace ms
