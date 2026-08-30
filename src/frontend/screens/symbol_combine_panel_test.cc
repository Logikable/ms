#include "src/frontend/screens/symbol_combine_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"

namespace ms {
namespace {

// The selector's own mechanics belong to amount_selector_test; these cover
// what this dialog says and what it opens at.
class SymbolCombinePanelTest : public testing::Test {
 protected:
  static std::string Render(const SymbolCombinePanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(48),
                                                 ftxui::Dimension::Fixed(14));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  }
};

// Opening at every spare held is the whole point: the last rung asks for 372
// duplicates, and one keypress each is not a thing to ask of anybody.
TEST_F(SymbolCombinePanelTest, OpensAtEverySpareHeld) {
  SymbolCombinePanel panel;
  panel.Reset("Arcane Symbol: Vanishing Journey", 1, 0, 12, 30);
  EXPECT_EQ(panel.quantity(), 30);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Vanishing Journey"), std::string::npos);
  EXPECT_NE(rendered.find("Level 1"), std::string::npos);
}

// The EXP row reads as the bar filling, so it stops at the rung rather than
// showing a total that overshoots it. What spills over is not lost.
TEST_F(SymbolCombinePanelTest, TheExpRowStopsAtTheRung) {
  SymbolCombinePanel panel;
  panel.Reset("Arcane Symbol: Arcana", 1, 5, 12, 3);
  EXPECT_NE(Render(panel).find("EXP 8 / 12"), std::string::npos)
      << Render(panel);

  SymbolCombinePanel over;
  over.Reset("Arcane Symbol: Arcana", 1, 5, 12, 40);
  EXPECT_NE(Render(over).find("EXP 12 / 12"), std::string::npos)
      << Render(over);
}

TEST_F(SymbolCombinePanelTest, PassesTheAnswerThrough) {
  SymbolCombinePanel panel;
  panel.Reset("Arcane Symbol: Morass", 2, 0, 15, 4);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);

  panel.Reset("Arcane Symbol: Morass", 2, 0, 15, 4);
  panel.OnEvent(ftxui::Event::ArrowDown);  // textbox -> [Confirm]
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

}  // namespace
}  // namespace ms
