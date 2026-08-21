#include "src/frontend/main_layout.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"

namespace ms {
namespace {

// The width the two left-column panels share in the game. The layout does not
// set it -- the panels bring their own -- but the stand-ins have to agree on
// one or the column will not line up.
constexpr int kLeftWidth = 35;
constexpr int kScreenWidth = 100;
constexpr int kScreenHeight = 30;
// Deliberately narrower than the room left beside the left column, so a right
// column that failed to flex would be visibly short of the screen's edge.
constexpr int kRightWidth = 40;

// A bordered stand-in for a panel: `rows` rows of content, each reading
// `label`, inside a border `width` columns across. Rendering the real panels
// would drag in a whole GameState, and the layout only ever sees their size.
ftxui::Element Panel(const std::string& label, int width, int rows) {
  std::vector<ftxui::Element> body;
  for (int i = 0; i < rows; ++i) {
    std::string row = label;
    row.resize(width - 2, ' ');
    body.push_back(ftxui::text(row));
  }
  return ftxui::border(ftxui::vbox(std::move(body)));
}

class MainLayoutTest : public testing::Test {
 protected:
  // Renders the layout onto a fixed screen and keeps the rows, so a test can
  // ask where each piece ended up. The stand-ins are sized so the left column
  // is 10 rows of character over 5 of combat, and the right column 3 rows of
  // equipped over a bag of whatever height the test asks for.
  void Render(int bag_rows) {
    RenderWith(Panel("EQUIP", kRightWidth, 1),
               Panel("BAG", kRightWidth, bag_rows), nullptr);
  }

  // The same two panels with the equipped one tall enough to argue over the
  // column, which is what the half cap is there for.
  void RenderTall(int equip_rows, int bag_rows) {
    RenderWith(Panel("EQUIP", kRightWidth, equip_rows),
               Panel("BAG", kRightWidth, bag_rows), nullptr);
  }

  // The same layout with whichever right-column panels the test wants, so it
  // can pass null for one a character has not unlocked.
  void RenderWith(ftxui::Element equipped, ftxui::Element inventory,
                  ftxui::Element corner) {
    screen_ = ftxui::Screen::Create(ftxui::Dimension::Fixed(kScreenWidth),
                                    ftxui::Dimension::Fixed(kScreenHeight));
    ftxui::Element layout =
        MainLayout(Panel("CHAR", kLeftWidth, 8), Panel("COMBAT", kLeftWidth, 3),
                   std::move(equipped), std::move(inventory), std::move(corner),
                   ftxui::text("EXPBAR"));
    ftxui::Render(screen_, layout);
  }

  // Cell (x, y) of the last render, with an unpainted cell read as a space.
  std::string Cell(int x, int y) {
    const std::string& cell = screen_.PixelAt(x, y).character;
    if (cell.empty()) {
      return " ";
    }
    return cell;
  }

  std::string Row(int y) {
    std::string row;
    for (int x = 0; x < kScreenWidth; ++x) {
      row += Cell(x, y);
    }
    return row;
  }

  int FirstRowWith(const std::string& needle) {
    for (int y = 0; y < kScreenHeight; ++y) {
      if (Row(y).find(needle) != std::string::npos) {
        return y;
      }
    }
    return -1;
  }

  int LastRowWith(const std::string& needle) {
    for (int y = kScreenHeight - 1; y >= 0; --y) {
      if (Row(y).find(needle) != std::string::npos) {
        return y;
      }
    }
    return -1;
  }

  // The row of the lowest bottom-left corner in column `col` -- the foot of
  // the lowest panel stacked in that column.
  int LowestPanelFoot(int col) {
    for (int y = kScreenHeight - 1; y >= 0; --y) {
      if (Cell(col, y) == "╰") {
        return y;
      }
    }
    return -1;
  }

  // The row of the highest top-left corner below `after` in column `col`.
  int PanelTopBelow(int col, int after) {
    for (int y = after + 1; y < kScreenHeight; ++y) {
      if (Cell(col, y) == "╭") {
        return y;
      }
    }
    return -1;
  }

  ftxui::Screen screen_ = ftxui::Screen::Create(ftxui::Dimension::Fixed(1),
                                                ftxui::Dimension::Fixed(1));
};

TEST_F(MainLayoutTest, TheExpBarIsTheLastRow) {
  Render(/*bag_rows=*/40);
  EXPECT_EQ(FirstRowWith("EXPBAR"), kScreenHeight - 1);
}

// The bug that started this: the bag stopped partway down and the rest of its
// width sat blank. A bag with more in it than fits runs to the exp bar.
TEST_F(MainLayoutTest, AFullBagReachesTheExpBar) {
  Render(/*bag_rows=*/40);
  EXPECT_EQ(LowestPanelFoot(kLeftWidth), kScreenHeight - 2);
}

// ...and gives way rather than running past it, which would push the exp bar
// off the screen.
TEST_F(MainLayoutTest, AFullBagDoesNotPushPastTheExpBar) {
  Render(/*bag_rows=*/40);
  EXPECT_EQ(LastRowWith("BAG"), kScreenHeight - 3);
}

// The other half of the ask: a tab with little in it is a few rows, not a
// screen of blank held open down to the exp bar.
TEST_F(MainLayoutTest, AnEmptyBagKeepsItsOwnHeight) {
  Render(/*bag_rows=*/1);
  // Equipped is three rows (0..2), so a one-row bag ends on row 5.
  EXPECT_EQ(LowestPanelFoot(kLeftWidth), 5);
}

TEST_F(MainLayoutTest, AShortBagKeepsItsOwnHeight) {
  Render(/*bag_rows=*/6);
  EXPECT_EQ(LowestPanelFoot(kLeftWidth), 10);
}

// Combat is pinned to the bottom-left corner, so its foot lands on the row
// above the exp bar rather than following the character panel down from the
// top of the column.
TEST_F(MainLayoutTest, CombatSitsInTheBottomLeftCorner) {
  Render(/*bag_rows=*/40);
  EXPECT_EQ(LowestPanelFoot(0), kScreenHeight - 2);
  EXPECT_NE(FirstRowWith("COMBAT"), -1);
}

// ...and it is pinned rather than stretched: the gap opens above it, and it
// keeps the height of its own contents.
TEST_F(MainLayoutTest, CombatKeepsItsOwnHeightAtTheBottom) {
  Render(/*bag_rows=*/40);
  int combat_top = FirstRowWith("COMBAT") - 1;
  EXPECT_EQ(Cell(0, combat_top), "╭");
  EXPECT_EQ(LowestPanelFoot(0), combat_top + 4);
}

// The character panel stays at the top of the column, keeping its own height.
TEST_F(MainLayoutTest, TheCharacterPanelKeepsItsHeight) {
  Render(/*bag_rows=*/40);
  EXPECT_EQ(FirstRowWith("CHAR"), 1);
  EXPECT_EQ(Cell(0, 9), "╰");
}

// The gap the pinning opens is empty, not a stretched panel.
TEST_F(MainLayoutTest, TheGapBetweenPanelsStaysBlank) {
  Render(/*bag_rows=*/40);
  int combat_top = FirstRowWith("COMBAT") - 1;
  for (int y = 10; y < combat_top; ++y) {
    EXPECT_EQ(Cell(0, y), " ") << "the left column is painted on row " << y;
  }
}

// What the combat panel used to take up the whole width of. Beside it is the
// bag, not blank screen.
TEST_F(MainLayoutTest, TheBagRunsDownBesideTheCombatPanel) {
  Render(/*bag_rows=*/40);
  int combat_row = FirstRowWith("COMBAT");
  ASSERT_GE(combat_row, 0);
  // The bag's left border, in the first column past the left column.
  EXPECT_EQ(Cell(kLeftWidth, combat_row), "│");
  EXPECT_NE(Row(combat_row).find("BAG"), std::string::npos);
}

// Equipped keeps its own height while it fits under the cap, however little
// the bag beside it wants.
TEST_F(MainLayoutTest, TheEquippedPanelKeepsItsOwnHeight) {
  Render(/*bag_rows=*/1);
  EXPECT_EQ(FirstRowWith("EQUIP"), 1);
  EXPECT_EQ(Cell(kLeftWidth, 2), "╰");
}

// And keeps it when the bag is overflowing: what the bag cannot have is the
// half of the column the cap holds for it, not a row off a panel that is
// already well short of that.
TEST_F(MainLayoutTest, AFullBagDoesNotSquashTheEquippedPanel) {
  Render(/*bag_rows=*/40);
  EXPECT_EQ(FirstRowWith("EQUIP"), 1);
  EXPECT_EQ(Cell(kLeftWidth, 2), "╰")
      << "the equipped panel gave up a row to the bag below it";
}

// --- the half cap ---

// There are enough gear slots now to fill a column, so the equipped panel is
// held to half of what the two panels have to share -- and the odd row of an
// odd column falls to the bag, which is the panel the player works out of.
TEST_F(MainLayoutTest, ATallEquippedPanelStopsAtHalfTheColumn) {
  RenderTall(/*equip_rows=*/30, /*bag_rows=*/40);
  EXPECT_EQ(FirstRowWith("EQUIP"), 1) << "still at the top of the column";
  EXPECT_EQ(Cell(kLeftWidth, 13), "╰") << "14 rows of a 29-row column";
  EXPECT_EQ(PanelTopBelow(kLeftWidth, 13), 14) << "the bag takes over here";
  EXPECT_EQ(LowestPanelFoot(kLeftWidth), kScreenHeight - 2)
      << "and the bag runs to the exp bar, 15 rows of it";
}

// The cap is on the room the pair has, not on the room the bag asks for: a
// nearly empty bag does not hand its half back.
TEST_F(MainLayoutTest, AnEmptyBagDoesNotLiftTheCap) {
  RenderTall(/*equip_rows=*/30, /*bag_rows=*/3);
  EXPECT_EQ(Cell(kLeftWidth, 13), "╰") << "the same 14 rows";
  EXPECT_EQ(PanelTopBelow(kLeftWidth, 13), 14);
  EXPECT_EQ(LowestPanelFoot(kLeftWidth), 18) << "a 5-row bag under it";
}

// What the short bag leaves over is blank, not a stretched panel.
TEST_F(MainLayoutTest, TheRoomUnderAShortBagStaysBlank) {
  RenderTall(/*equip_rows=*/30, /*bag_rows=*/3);
  for (int y = 19; y < kScreenHeight - 1; ++y) {
    EXPECT_EQ(Cell(kLeftWidth, y), " ")
        << "the right column is painted on row " << y;
  }
}

// The corner panel is not part of the pair, so the half is measured on the
// column above it -- and the corner stays pinned to the foot.
TEST_F(MainLayoutTest, TheCornerPanelIsNotHalvedWithThem) {
  RenderWith(Panel("EQUIP", kRightWidth, 30), Panel("BAG", kRightWidth, 40),
             Panel("KEYS", kRightWidth, 5));
  EXPECT_EQ(Cell(kLeftWidth, 10), "╰") << "11 rows of the 22 left over";
  EXPECT_EQ(PanelTopBelow(kLeftWidth, 10), 11);
  EXPECT_EQ(LastRowWith("KEYS"), kScreenHeight - 3)
      << "still pinned above the exp bar";
}

// The right column takes the width left over rather than its own, so the bag
// reaches the edge of the terminal.
TEST_F(MainLayoutTest, TheRightColumnFillsTheRemainingWidth) {
  Render(/*bag_rows=*/40);
  int bag_row = LastRowWith("BAG");
  ASSERT_GE(bag_row, 0);
  EXPECT_EQ(Cell(kScreenWidth - 1, bag_row), "│");
}

// --- panels a character has not unlocked ---

// Nothing to the right of the character panel at level 1, and the two panels
// that do exist keep the places they will have for the rest of the game.
TEST_F(MainLayoutTest, NoRightPanelsLeavesOneColumn) {
  RenderWith(nullptr, nullptr, nullptr);
  EXPECT_EQ(FirstRowWith("EQUIP"), -1);
  EXPECT_EQ(FirstRowWith("BAG"), -1);
  EXPECT_NE(FirstRowWith("CHAR"), -1);
  EXPECT_EQ(FirstRowWith("CHAR"), 1) << "character still at the top";
  EXPECT_NE(FirstRowWith("EXPBAR"), -1);
  EXPECT_EQ(LastRowWith("COMBAT"), kScreenHeight - 3)
      << "combat still pinned above the exp bar";
}

// The bag arrives a level after the equipped panel, so for one level the
// right column is the equipped panel alone -- and it must sit at the top of
// the column rather than floating where the bag would have put it.
TEST_F(MainLayoutTest, TheEquippedPanelStandsAloneWithoutTheBag) {
  RenderWith(Panel("EQUIP", kRightWidth, 1), nullptr, nullptr);
  EXPECT_EQ(FirstRowWith("BAG"), -1);
  EXPECT_EQ(FirstRowWith("EQUIP"), 1);
  EXPECT_EQ(FirstRowWith("CHAR"), 1) << "both columns start at the top";
}

// The right column is what flexes to fill the width. With nothing in it the
// left column must not stretch to take its place.
TEST_F(MainLayoutTest, TheLeftColumnKeepsItsWidthAlone) {
  RenderWith(nullptr, nullptr, nullptr);
  int row = FirstRowWith("CHAR");
  ASSERT_NE(row, -1);
  EXPECT_EQ(Cell(kLeftWidth - 1, row), "\u2502") << "right border of the panel";
  EXPECT_EQ(Cell(kLeftWidth, row), " ") << "and nothing past it";
}

// --- the hotkeys tip ---

// It mirrors combat: pinned to the foot of its column so it lands in the
// bottom-right corner however tall the terminal is.
TEST_F(MainLayoutTest, TheHotkeysTipSitsInTheBottomRightCorner) {
  RenderWith(Panel("EQUIP", kRightWidth, 1), Panel("BAG", kRightWidth, 3),
             Panel("KEYS", kRightWidth, 5));
  EXPECT_EQ(LastRowWith("KEYS"), kScreenHeight - 3)
      << "pinned just above the exp bar";
  EXPECT_LT(FirstRowWith("BAG"), FirstRowWith("KEYS")) << "and below the bag";
}

// For the first two levels there is no equipped panel and no bag, so the tip
// is the whole right column -- and must still be at the bottom of it rather
// than at the top where the only other child would have put it.
TEST_F(MainLayoutTest, TheHotkeysTipAloneStillSitsAtTheBottom) {
  RenderWith(nullptr, nullptr, Panel("KEYS", kRightWidth, 5));
  EXPECT_EQ(FirstRowWith("EQUIP"), -1);
  EXPECT_EQ(FirstRowWith("BAG"), -1);
  EXPECT_EQ(LastRowWith("KEYS"), kScreenHeight - 3);
  EXPECT_EQ(FirstRowWith("CHAR"), 1) << "the left column is unaffected";
}

// A bag long enough to fill the column must not squeeze the tip: the bag is
// the one panel marked shrinkable, and everything else keeps its own height.
TEST_F(MainLayoutTest, AFullBagDoesNotSquashTheHotkeysTip) {
  RenderWith(Panel("EQUIP", kRightWidth, 1), Panel("BAG", kRightWidth, 40),
             Panel("KEYS", kRightWidth, 5));
  int keys_top = FirstRowWith("KEYS") - 1;
  EXPECT_EQ(LastRowWith("KEYS"), kScreenHeight - 3);
  EXPECT_EQ(LastRowWith("KEYS") + 1, keys_top + 6)
      << "five content rows and two borders";
}

// Once the tip retires the right column goes back to exactly what it was, so
// nothing below level 5 leaves a gap behind it.
TEST_F(MainLayoutTest, TheRetiredTipLeavesNoGap) {
  RenderWith(Panel("EQUIP", kRightWidth, 1), Panel("BAG", kRightWidth, 3),
             nullptr);
  EXPECT_EQ(FirstRowWith("KEYS"), -1);
  EXPECT_EQ(FirstRowWith("EQUIP"), 1) << "equipped still at the top";
  EXPECT_EQ(FirstRowWith("BAG"), 4) << "and the bag directly under it";
}

}  // namespace
}  // namespace ms
