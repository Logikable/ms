#include "src/frontend/panels/hotkeys_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/progression.h"
#include "src/frontend/widgets/screen_text.h"

namespace ms {
namespace {

// The panel's rows, read off the screen cell by cell. Not Screen::ToString --
// that threads colour escapes between the border and the text, so a row does
// not read as the line the player sees.
//
// The screen is fitted to the panel, so these are its natural dimensions: a
// row reaching its border here is a row the panel actually made room for.
std::vector<std::string> RenderRows() {
  ftxui::Element tip = HotkeysPanel();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(tip));
  ftxui::Render(screen, tip);
  return ScreenRows(screen);
}

// The row holding `needle`, or "" if no row does.
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
  // Read off progression rather than written out, so retuning the early game
  // cannot leave the tip promising a level it no longer retires at.
  EXPECT_NE(RowWith("close at level " +
                    std::to_string(HotkeysTipRetireLevel()) + "."),
            "");
}

TEST(HotkeysPanelTest, KeepsItsLongestLineInsideTheBorder) {
  // The arrows are three bytes apiece against one column each. Sizing this
  // panel by byte length would leave its longest row hanging past the border,
  // or cut a glyph in half on the way. Measured at the panel's own width, so
  // this fails if the arrows are ever counted wrong.
  std::string row = RowWith("move within a panel");
  ASSERT_NE(row, "");
  EXPECT_EQ(row.substr(0, std::string("│").size()), "│") << "opening border";
  EXPECT_EQ(row.rfind("│"), row.size() - std::string("│").size())
      << "closing border, with the whole line inside it";
  EXPECT_EQ(row.find("�"), std::string::npos) << "no glyph split in two";
}

TEST(HotkeysPanelTest, RetiresTheLevelAfterTheBagArrives) {
  // The tip's whole job is the panels arriving around it, and the bag is the
  // last of them. Tied together so neither can move without the other.
  EXPECT_EQ(HotkeysTipRetireLevel(), UnlockLevel(Feature::kBag) + 1);
}

}  // namespace
}  // namespace ms
