#include "src/frontend/screens/upgrade/symbol_combine_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"

namespace ms {
namespace {

// amount_selector_test covers the selector's mechanics. These tests cover what
// this dialog says and what amount it starts at.
class SymbolCombinePanelTest : public testing::Test {
 protected:
  static std::string Render(const SymbolCombinePanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(48),
                                                 ftxui::Dimension::Fixed(14));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  }
};

// Starting at every spare held is the point: the last level needs 372
// duplicates, and one keypress each would be too much to ask.
TEST_F(SymbolCombinePanelTest, OpensAtEverySpareHeld) {
  SymbolCombinePanel panel;
  panel.Reset("Arcane Symbol: Vanishing Journey", 1, 0, 12, 2679,
              std::vector<int>(30, 1));
  EXPECT_EQ(panel.quantity(), 30);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Vanishing Journey"), std::string::npos);
  EXPECT_NE(rendered.find("Level 1"), std::string::npos);
}

// The EXP row goes past the level's requirement instead of stopping at it,
// since the overflow isn't lost; it goes toward the next level.
TEST_F(SymbolCombinePanelTest, TheExpRowRunsPastTheRung) {
  SymbolCombinePanel panel;
  panel.Reset("Arcane Symbol: Arcana", 1, 5, 12, 2679, std::vector<int>(3, 1));
  EXPECT_NE(Render(panel).find("EXP 8 / 12"), std::string::npos)
      << Render(panel);

  SymbolCombinePanel over;
  over.Reset("Arcane Symbol: Arcana", 1, 5, 12, 2679, std::vector<int>(40, 1));
  EXPECT_NE(Render(over).find("EXP 45 / 12"), std::string::npos)
      << Render(over);
}

// A spare isn't worth one each: a claimed stack carries twenty, and the EXP row
// counts what would actually be fed in.
TEST_F(SymbolCombinePanelTest, APackedSpareCountsForWhatItCarries) {
  SymbolCombinePanel panel;
  panel.Reset("Arcane Symbol: Lachelein", 1, 0, 12, 2679, {20, 20, 1});
  ASSERT_EQ(panel.quantity(), 3);
  EXPECT_NE(Render(panel).find("EXP 41 / 12"), std::string::npos)
      << Render(panel);
}

// At level 19 the symbol holds only the last level's 372, so 350 plus a 75
// stack reads 372 / 372, and spares past the one that fills it aren't offered.
TEST_F(SymbolCombinePanelTest, TheExpRowStopsAtWhatTheSymbolHolds) {
  SymbolCombinePanel panel;
  panel.Reset("Arcane Symbol: Esfera", 19, 350, 372, 372, {75, 75});
  EXPECT_EQ(panel.quantity(), 1);
  EXPECT_NE(Render(panel).find("EXP 372 / 372"), std::string::npos)
      << Render(panel);
}

TEST_F(SymbolCombinePanelTest, PassesTheAnswerThrough) {
  SymbolCombinePanel panel;
  panel.Reset("Arcane Symbol: Morass", 2, 0, 15, 2667, std::vector<int>(4, 1));
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);

  panel.Reset("Arcane Symbol: Morass", 2, 0, 15, 2667, std::vector<int>(4, 1));
  panel.OnEvent(ftxui::Event::ArrowDown);  // textbox to [Confirm]
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

TEST_F(SymbolCombinePanelTest, TheExpRowKeepsOffTheRightBorder) {
  SymbolCombinePanel panel;
  panel.Reset("Arcane Symbol: Vanishing Journey", 1, 0, 12, 2679,
              std::vector<int>(30, 1));
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
}
}  // namespace
}  // namespace ms
