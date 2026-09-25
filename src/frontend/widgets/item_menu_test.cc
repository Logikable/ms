#include "src/frontend/widgets/item_menu.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/colors.h"

namespace ms {
namespace {

class ItemMenuTest : public testing::Test {
 protected:
  // The colour of the first cell of `label` in the rendered menu, or
  // Color::Default when it isn't drawn, which no expected colour equals.
  static ftxui::Color LabelColor(const ItemMenu& menu,
                                 const std::string& label) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                                 ftxui::Dimension::Fixed(10));
    ftxui::Render(screen, menu.Render(0, 0));
    return ColorOf(screen, label);
  }

  // The width the menu's box covers when drawn at the origin, measured along
  // its top border.
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

// The cursor starts on the first entry and the list wraps, so stepping off
// either end comes out the other.
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

// Disabling the entry under the cursor moves the cursor off it, and a step
// never lands on a disabled entry, including a step that wraps.
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

// With nowhere else to go, the cursor stays put rather than landing on
// something the player can't choose. The walk still ends, because it comes back
// round to the enabled entry it started on.
TEST_F(ItemMenuTest, StaysPutWhenEveryOtherEntryIsDisabled) {
  menu_.Down();
  menu_.Disable(0);
  menu_.Disable(2);
  menu_.Up();
  EXPECT_EQ(menu_.selected(), 1);
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 1);
}

// With nothing enabled the walk goes round once and gives up, or the failure
// would be a hung test. No real menu disables Close, but the loop's ending
// shouldn't depend on a rule in another file.
TEST_F(ItemMenuTest, GivesUpWhenNothingAtAllIsEnabled) {
  menu_.Disable(0);
  menu_.Disable(1);
  menu_.Disable(2);
  menu_.Up();
  menu_.Down();
  SUCCEED() << "both returned rather than walking the ring forever";
}

// A menu of one wraps onto itself: both keys do nothing, instead of the entry
// seeming to move.
TEST_F(ItemMenuTest, ASingleEntryGoesNowhere) {
  ItemMenu lone{{"Close"}};
  lone.Up();
  EXPECT_EQ(lone.selected(), 0);
  lone.Down();
  EXPECT_EQ(lone.selected(), 0);
}

// --- Highlight ---

// The end of the trail from the level-up card. Gold stands out against the
// white of every other entry, on a menu the player has opened many times.
TEST_F(ItemMenuTest, HighlightDrawsAnEntryGold) {
  menu_.Highlight(1);
  EXPECT_EQ(LabelColor(menu_, "Two"), kYellow);
  EXPECT_NE(LabelColor(menu_, "Three"), kYellow);
}

// Unlike Disable, a highlight doesn't change how the cursor moves: the entry is
// pointed at, not blocked.
TEST_F(ItemMenuTest, AHighlightedEntryIsStillReachable) {
  menu_.Highlight(1);
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 1);
}

// Gold invites the player to press the row, so a disabled row doesn't get it.
// Star force stays grey until the item's scroll slots are used, and the trail
// should wait rather than point at a dead row.
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

// An entry named after the state it would leave the item in. It is renamed in
// place, and the box grows to fit.
TEST_F(ItemMenuTest, SetLabelRenamesAnEntryAndWidensTheBox) {
  menu_.SetLabel(0, "Disable");
  EXPECT_NE(LabelColor(menu_, "Disable"), ftxui::Color::Default);
  EXPECT_EQ(menu_.Width(), DrawnWidth(menu_));
}

// A caller keeping the menu inside a panel measures against this, so it must
// match the drawn box, and hidden entries don't count.
TEST_F(ItemMenuTest, WidthIsTheBoxThatIsDrawn) {
  EXPECT_EQ(menu_.Width(), DrawnWidth(menu_));
  menu_.Hide(2);
  EXPECT_EQ(menu_.Width(), DrawnWidth(menu_));
}

}  // namespace
}  // namespace ms
