#include "src/frontend/panels/hotkeys_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/progression.h"
#include "src/frontend/testing/screen_text.h"

namespace ms {
namespace {

// The panel's rows, read cell by cell: Screen::ToString puts colour escapes
// between border and text. The screen is fitted to the panel, so a row reaching
// its border is one the panel made room for.
std::vector<std::string> RenderRows() {
  ftxui::Element tip = HotkeysPanel();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(tip));
  ftxui::Render(screen, tip);
  return ScreenRows(screen);
}

// The row containing `needle`, or "" if none does.
std::string RowWith(const std::string& needle) {
  for (const std::string& row : RenderRows()) {
    if (row.find(needle) != std::string::npos) {
      return row;
    }
  }
  return "";
}

TEST(HotkeysPanelTest, NamesEveryKeyTheGameIsPlayedWith) {
  EXPECT_NE(RowWith("Enter: open/confirm"), "");
  EXPECT_NE(RowWith("Escape: exit/cancel"), "");
  EXPECT_NE(RowWith("↑/↓/←/→: move within a panel"), "");
  EXPECT_NE(RowWith("Tab: switch panels"), "");
}

TEST(HotkeysPanelTest, SaysWhenItWillGoAway) {
  // Read from the progression table rather than written out, so retuning the
  // early game can't leave the tip naming the wrong level.
  EXPECT_NE(RowWith("close at level " +
                    std::to_string(HotkeysTipRetireLevel()) + "."),
            "");
}

TEST(HotkeysPanelTest, KeepsItsLongestLineInsideTheBorder) {
  // Each arrow is three bytes but one column. Sizing the panel by byte length
  // would push the longest row past the border or cut a glyph in half. Measured
  // at the panel's own width, so this fails if the arrows are counted wrong.
  std::string row = RowWith("move within a panel");
  ASSERT_NE(row, "");
  EXPECT_EQ(row.substr(0, std::string("│").size()), "│") << "opening border";
  EXPECT_EQ(row.rfind("│"), row.size() - std::string("│").size())
      << "closing border, with the whole line inside it";
  EXPECT_EQ(row.find("�"), std::string::npos) << "no glyph split in two";
}

TEST(HotkeysPanelTest, RetiresTheLevelAfterTheBagArrives) {
  // The tip exists until the panels around it have arrived, and the bag is the
  // last. They are tied together so neither can move without the other.
  EXPECT_EQ(HotkeysTipRetireLevel(), UnlockLevel(Feature::kBag) + 1);
}

TEST(HotkeysPanelTest, TheTipsKeepOffTheRightBorder) {
  EXPECT_TRUE(RowsTouchingTheRightBorder(HotkeysPanel()).empty());
}
}  // namespace
}  // namespace ms
