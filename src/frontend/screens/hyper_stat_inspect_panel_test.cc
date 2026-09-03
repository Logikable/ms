#include "src/frontend/screens/hyper_stat_inspect_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/hyper_stats.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

class HyperStatInspectPanelTest : public PanelTest {
 protected:
  std::string RenderAt(HyperStatField field, int level, int max_level = 10) {
    HyperStatInspectPanel panel;
    panel.SetStat(field, level, max_level);
    return RenderElement(panel.Render());
  }

  // The columns the card asks for, read off its requirement -- the test
  // screen is wider than any card, so a rendered string cannot say.
  static int ColumnsOf(HyperStatField field, int level) {
    HyperStatInspectPanel panel;
    panel.SetStat(field, level, 10);
    ftxui::Element card = panel.Render();
    card->ComputeRequirement();
    return card->requirement().min_x;
  }
};

TEST_F(HyperStatInspectPanelTest, ShowsThisLevelAndTheNext) {
  std::string rendered = RenderAt(HYPER_STAT_FIELD_CRIT_DAMAGE, 4);
  EXPECT_NE(rendered.find("Critical Damage"), std::string::npos);
  EXPECT_NE(rendered.find("Max Level: 10"), std::string::npos);
  EXPECT_NE(rendered.find("Level 4"), std::string::npos);
  EXPECT_NE(rendered.find("+4%"), std::string::npos);
  EXPECT_NE(rendered.find("+5%"), std::string::npos);
  // Only the level still to be bought wears its price.
  EXPECT_NE(rendered.find("Level 5 - 10 points"), std::string::npos);
  EXPECT_EQ(rendered.find("Level 4 -"), std::string::npos);
}

// The first point costs exactly one, and says so in the singular.
TEST_F(HyperStatInspectPanelTest, TheFirstLevelIsPricedInOnePoint) {
  EXPECT_NE(RenderAt(HYPER_STAT_FIELD_STR, 0).find("Level 1 - 1 point"),
            std::string::npos);
}

// At the ceiling there is no price on the card at all.
TEST_F(HyperStatInspectPanelTest, AMaxedStatIsPricedNowhere) {
  EXPECT_EQ(RenderAt(HYPER_STAT_FIELD_STR, 10).find("points"),
            std::string::npos);
}

// Nothing spent on it yet: there is no level to state, only the one the first
// point would buy.
TEST_F(HyperStatInspectPanelTest, AnUnspentStatShowsOnlyItsFirstLevel) {
  std::string rendered = RenderAt(HYPER_STAT_FIELD_STR, 0);
  EXPECT_EQ(rendered.find("Level 0"), std::string::npos);
  EXPECT_NE(rendered.find("Level 1"), std::string::npos);
  EXPECT_NE(rendered.find("+30"), std::string::npos);
}

// And at the ceiling there is no next one.
TEST_F(HyperStatInspectPanelTest, AMaxedStatShowsOnlyItsOwnLevel) {
  std::string rendered = RenderAt(HYPER_STAT_FIELD_STR, 10);
  EXPECT_NE(rendered.find("Level 10"), std::string::npos);
  EXPECT_EQ(rendered.find("Level 11"), std::string::npos);
  EXPECT_NE(rendered.find("+300"), std::string::npos);
}

// The ceiling is the character's, not the stat's: a 5th job reaches 15 and
// the card says so.
TEST_F(HyperStatInspectPanelTest, TheCeilingIsTheOneItIsHanded) {
  std::string rendered = RenderAt(HYPER_STAT_FIELD_STR, 10, /*max_level=*/15);
  EXPECT_NE(rendered.find("Max Level: 15"), std::string::npos);
  EXPECT_NE(rendered.find("Level 11"), std::string::npos);
}

// Every card asks for the same columns, so walking the list does not resize
// the window under the cursor -- and it is far narrower than a skill card,
// whose own floor is 58.
TEST_F(HyperStatInspectPanelTest, EveryCardAsksForTheSameNarrowWidth) {
  int shortest = ColumnsOf(HYPER_STAT_FIELD_STR, 5);
  EXPECT_EQ(shortest, ColumnsOf(HYPER_STAT_FIELD_CRIT_DAMAGE, 5));
  EXPECT_EQ(shortest, HyperStatInspectPanel::Columns());
  EXPECT_LT(shortest, 40);
  // And wide enough to seat the longest name in the roster whole.
  EXPECT_NE(RenderAt(HYPER_STAT_FIELD_CRIT_DAMAGE, 5).find("Critical Damage"),
            std::string::npos);
}

// A card that measures its own width has to ask for the margin: without it a
// value is welded to the right border. Every stat at every level, because the
// one that broke this was EXP at a level nobody would have picked to check --
// its widest value is a decimal below level 10, not the whole percent it ends
// on.
TEST_F(HyperStatInspectPanelTest, EveryRowKeepsAColumnClearOfTheRightBorder) {
  for (int i = 0; i < kNumHyperStats; ++i) {
    for (int level = 0; level <= kMaxHyperStatLevel; ++level) {
      HyperStatInspectPanel panel;
      panel.SetStat(kHyperStatOrder[i], level, kMaxHyperStatLevel);
      std::vector<std::string> touching =
          RowsTouchingTheRightBorder(panel.Render());
      EXPECT_TRUE(touching.empty())
          << HyperStatName(kHyperStatOrder[i]) << " level " << level << ": "
          << (touching.empty() ? "" : touching[0]);
    }
  }
}

TEST_F(HyperStatInspectPanelTest, NoStatRendersAPlaceholder) {
  std::string rendered = RenderAt(HYPER_STAT_FIELD_UNSPECIFIED, 0);
  EXPECT_NE(rendered.find("no stat"), std::string::npos);
}

}  // namespace
}  // namespace ms
