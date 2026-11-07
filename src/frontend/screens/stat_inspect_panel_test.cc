#include "src/frontend/screens/stat_inspect_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/widgets/stat_rows.h"

namespace ms {
namespace {

class StatInspectPanelTest : public PanelTest {
 protected:
  std::string RenderAt(InspectStat stat, const StatBreakdown& breakdown) {
    StatInspectPanel panel;
    panel.SetStat(stat, breakdown);
    return RenderElement(panel.Render());
  }

  static int ColumnsOf(InspectStat stat, const StatBreakdown& breakdown) {
    StatInspectPanel panel;
    panel.SetStat(stat, breakdown);
    ftxui::Element card = panel.Render();
    card->ComputeRequirement();
    return card->requirement().min_x;
  }
};

TEST_F(StatInspectPanelTest, ShowsEachPartAndTheTotal) {
  std::string rendered = RenderAt(
      InspectStat::kStr,
      {.base = 3546, .pct = 4.53, .final_stat = 18000, .total = 37609});
  EXPECT_NE(rendered.find("Stat Inspect"), std::string::npos);
  EXPECT_NE(rendered.find("STR"), std::string::npos);
  EXPECT_NE(rendered.find("Base Stat"), std::string::npos);
  EXPECT_NE(rendered.find("3546"), std::string::npos);
  EXPECT_NE(rendered.find("453%"), std::string::npos);
  EXPECT_NE(rendered.find("18000"), std::string::npos);
  EXPECT_NE(rendered.find("Total"), std::string::npos);
  EXPECT_NE(rendered.find("37609"), std::string::npos);
}

TEST_F(StatInspectPanelTest, AFractionalPercentKeepsOnlyItsDigits) {
  EXPECT_NE(RenderAt(InspectStat::kLuk, {.pct = 0.155}).find("15.5%"),
            std::string::npos);
  EXPECT_NE(RenderAt(InspectStat::kLuk, {.pct = 0.0}).find(" 0%"),
            std::string::npos);
}

// Moving between stats must not resize the card.
TEST_F(StatInspectPanelTest, EveryCardIsTheSameWidth) {
  const StatBreakdown big = {.base = 999999999,
                             .pct = 99.99,
                             .final_stat = 999999999,
                             .total = 999999999};
  for (int i = 0; i < kNumInspectStats; ++i) {
    InspectStat stat = static_cast<InspectStat>(i);
    EXPECT_EQ(ColumnsOf(stat, {}), StatInspectPanel::Columns());
    EXPECT_EQ(ColumnsOf(stat, big), StatInspectPanel::Columns());
  }
}

}  // namespace
}  // namespace ms
