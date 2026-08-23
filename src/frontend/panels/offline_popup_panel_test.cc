#include "src/frontend/panels/offline_popup_panel.h"

#include <gtest/gtest.h>

#include "ftxui/dom/elements.hpp"
#include "src/combat/offline.h"
#include "src/frontend/widgets/panel_test_base.h"

namespace ms {
namespace {

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

  std::string Render(const OfflineReport& report) {
    return RenderElement(OfflinePopupPanel(report, ftxui::text("[Continue]")));
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
