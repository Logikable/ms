#include "src/frontend/widgets/item_menu.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/screen_text.h"

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
    return ColorOf(screen, label);
  }

  // The columns the menu's box actually covers when drawn at the origin,
  // counted off its top border.
  static int DrawnWidth(const ItemMenu& menu) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                                 ftxui::Dimension::Fixed(10));
    ftxui::Render(screen, menu.Render(0, 0));
    int width = 0;
    for (int x = 0; x < screen.dimx(); ++x) {
      const std::string& cell = screen.PixelAt(x, 0).character;
      if (cell.empty() || cell == " ") {
        break;
      }
      ++width;
    }
    return width;
  }

  ItemMenu menu_{{"One", "Two", "Three"}};
};

// The cursor starts on the first entry and the ends of the list are joined, so
// stepping off either one comes out the other.
TEST_F(ItemMenuTest, TheListIsARing) {
  EXPECT_EQ(menu_.selected(), 0);
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 1);
  menu_.Up();
  menu_.Up();
  EXPECT_EQ(menu_.selected(), 2) << "up off the first reaches the last";
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 0);
}

TEST_F(ItemMenuTest, ResetClearsTheCursorAndTheDisabled) {
  menu_.Down();
  menu_.Disable(0);
  menu_.Reset();
  EXPECT_EQ(menu_.selected(), 0);
}

// Disabling the entry under the cursor moves it off, and a step never lands on
// one -- including a step that wraps, or the one entry the player cannot pick
// becomes the one the wrap lands on.
TEST_F(ItemMenuTest, DisabledEntriesAreSteppedOver) {
  menu_.Disable(0);
  EXPECT_EQ(menu_.selected(), 1) << "moved off the entry it disabled";
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 2);
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 1) << "wrapped past the disabled first entry";
  menu_.Reset();
  menu_.Disable(1);
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 2) << "stepped over the middle";
  menu_.Up();
  EXPECT_EQ(menu_.selected(), 0);
  menu_.Reset();
  menu_.Disable(2);
  menu_.Up();
  EXPECT_EQ(menu_.selected(), 1) << "wrapped past the disabled last entry";
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

// Gold is an invitation to press the row, so the one row that cannot be
// pressed refuses it. Star force is why: it greys until the item's slots are
// spent, and the trail should wait rather than point at a dead row.
TEST_F(ItemMenuTest, ADisabledEntryRefusesTheGold) {
  menu_.Disable(1);
  menu_.Highlight(1);
  EXPECT_NE(LabelColor(menu_, "Two"), kYellow);
}

TEST_F(ItemMenuTest, ResetClearsTheHighlight) {
  menu_.Highlight(1);
  menu_.Reset();
  EXPECT_NE(LabelColor(menu_, "Two"), kYellow);
}

// An entry whose name is the state it would leave the item in: renamed in
// place, and the box grows to hold it.
TEST_F(ItemMenuTest, SetLabelRenamesAnEntryAndWidensTheBox) {
  menu_.SetLabel(0, "Disable");
  EXPECT_NE(LabelColor(menu_, "Disable"), ftxui::Color::Default);
  EXPECT_EQ(menu_.Width(), DrawnWidth(menu_));
}

// What an anchor holding the menu inside a panel measures against, so it has
// to be the box that is drawn -- and a hidden entry is not one it makes room
// for.
TEST_F(ItemMenuTest, WidthIsTheBoxThatIsDrawn) {
  EXPECT_EQ(menu_.Width(), DrawnWidth(menu_));
  menu_.Hide(2);
  EXPECT_EQ(menu_.Width(), DrawnWidth(menu_));
}

}  // namespace
}  // namespace ms
