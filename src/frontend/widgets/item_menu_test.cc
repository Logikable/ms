#include "src/frontend/widgets/item_menu.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/colors.h"

namespace ms {
namespace {

class ItemMenuTest : public testing::Test {
 protected:
  // The colour of the first cell of `label` in the rendered menu.
  // Color::Default when it is not drawn, which no expected colour equals.
  static ftxui::Color LabelColor(const ItemMenu& menu,
                                 const std::string& label) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                                 ftxui::Dimension::Fixed(10));
    ftxui::Render(screen, menu.Render(0, 0));
    for (int y = 0; y < screen.dimy(); ++y) {
      std::string row;
      for (int x = 0; x < screen.dimx(); ++x) {
        const std::string& cell = screen.PixelAt(x, y).character;
        row += cell.empty() ? " " : cell;
      }
      size_t at = row.find(label);
      if (at != std::string::npos) {
        return screen.PixelAt(static_cast<int>(at), y).foreground_color;
      }
    }
    return ftxui::Color::Default;
  }

  ItemMenu menu_{{"One", "Two", "Three"}};
};

TEST_F(ItemMenuTest, DefaultSelectionIsZero) {
  EXPECT_EQ(menu_.selected(), 0);
}

TEST_F(ItemMenuTest, DownMovesSelection) {
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 1);
}

TEST_F(ItemMenuTest, UpFromTheFirstEntryWrapsToTheLast) {
  menu_.Up();
  EXPECT_EQ(menu_.selected(), 2);
}

TEST_F(ItemMenuTest, DownFromTheLastEntryWrapsToTheFirst) {
  menu_.Down();
  menu_.Down();
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 0);
}

TEST_F(ItemMenuTest, ResetReturnsToZero) {
  menu_.Down();
  menu_.Down();
  menu_.Reset();
  EXPECT_EQ(menu_.selected(), 0);
}

TEST_F(ItemMenuTest, DisableSkipsEntryOnDown) {
  menu_.Disable(1);
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 2);
}

TEST_F(ItemMenuTest, DisableSkipsEntryOnUp) {
  menu_.Down();
  menu_.Down();
  menu_.Disable(1);
  menu_.Up();
  EXPECT_EQ(menu_.selected(), 0);
}

TEST_F(ItemMenuTest, DisableFirstEntryAdvancesSelectionToNext) {
  menu_.Disable(0);
  EXPECT_EQ(menu_.selected(), 1);
}

TEST_F(ItemMenuTest, ResetClearsDisabled) {
  menu_.Disable(0);
  menu_.Reset();
  EXPECT_EQ(menu_.selected(), 0);
}

// Wrapping has to honour disabled the same way a step through the middle does,
// or the one entry the player cannot pick becomes the one the wrap lands on.
TEST_F(ItemMenuTest, WrappingSkipsADisabledEntryAtTheFarEnd) {
  menu_.Disable(2);
  menu_.Up();
  EXPECT_EQ(menu_.selected(), 1);
}

TEST_F(ItemMenuTest, WrappingSkipsADisabledEntryAtTheNearEnd) {
  menu_.Down();
  menu_.Down();
  menu_.Disable(0);
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 1);
}

// Nowhere else to go, so the cursor stays where it is rather than landing on
// something the player cannot choose. The walk still ends, because coming back
// round to an entry that is itself enabled is somewhere to land.
TEST_F(ItemMenuTest, StaysPutWhenEveryOtherEntryIsDisabled) {
  menu_.Down();
  menu_.Disable(0);
  menu_.Disable(2);
  menu_.Up();
  EXPECT_EQ(menu_.selected(), 1);
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 1);
}

// Nothing enabled anywhere, including where the cursor already is: the walk
// rounds the ring once and gives up. Without the bound it would look for
// somewhere to land forever, and the failure would be a hung test rather than
// a failing one.
//
// Close is neither hidden nor disabled in any real menu, so this is the guard
// rather than a state the game reaches. A loop that only terminates because of
// a rule kept in another file is not one to leave lying around.
TEST_F(ItemMenuTest, GivesUpWhenNothingAtAllIsEnabled) {
  menu_.Disable(0);
  menu_.Disable(1);
  menu_.Disable(2);
  menu_.Up();
  menu_.Down();
  SUCCEED() << "both returned rather than walking the ring forever";
}

// A menu of one is a ring of one: both keys are no-ops rather than an entry
// that appears to move.
TEST_F(ItemMenuTest, ASingleEntryGoesNowhere) {
  ItemMenu lone{{"Close"}};
  lone.Up();
  EXPECT_EQ(lone.selected(), 0);
  lone.Down();
  EXPECT_EQ(lone.selected(), 0);
}

// --- Highlight ---

// The far end of the trail from the level-up card. Gold over the white the
// menu paints everything else, so the entry stands out on a menu the player
// has opened a hundred times.
TEST_F(ItemMenuTest, HighlightDrawsAnEntryGold) {
  menu_.Highlight(1);
  EXPECT_EQ(LabelColor(menu_, "Two"), kYellow);
  EXPECT_NE(LabelColor(menu_, "Three"), kYellow);
}

// Unlike Disable, it changes nothing about how the menu is walked: the entry
// is being pointed at, not held back.
TEST_F(ItemMenuTest, AHighlightedEntryIsStillReachable) {
  menu_.Highlight(1);
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 1);
}

TEST_F(ItemMenuTest, ResetClearsTheHighlight) {
  menu_.Highlight(1);
  menu_.Reset();
  EXPECT_NE(LabelColor(menu_, "Two"), kYellow);
}

}  // namespace
}  // namespace ms
