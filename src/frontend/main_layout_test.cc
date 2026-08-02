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
    screen_ = ftxui::Screen::Create(ftxui::Dimension::Fixed(kScreenWidth),
                                    ftxui::Dimension::Fixed(kScreenHeight));
    ftxui::Element layout =
        MainLayout(Panel("CHAR", kLeftWidth, 8), Panel("COMBAT", kLeftWidth, 3),
                   Panel("EQUIP", kRightWidth, 1),
                   Panel("BAG", kRightWidth, bag_rows), ftxui::text("EXPBAR"));
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

// Combat stacks directly under the character panel. As a row of its own
// beneath both columns it capped the bag at its own top edge.
TEST_F(MainLayoutTest, CombatSitsDirectlyUnderTheCharacterPanel) {
  Render(/*bag_rows=*/40);
  int character_foot = FirstRowWith("CHAR") + 8;
  EXPECT_EQ(Cell(0, character_foot), "╰");
  EXPECT_EQ(PanelTopBelow(0, character_foot), character_foot + 1);
  EXPECT_NE(FirstRowWith("COMBAT"), -1);
}

// Neither left-column panel stretches to the height of the row: their bottom
// borders stay against their contents.
TEST_F(MainLayoutTest, TheLeftColumnPanelsKeepTheirOwnHeight) {
  Render(/*bag_rows=*/40);
  EXPECT_EQ(LowestPanelFoot(0), FirstRowWith("COMBAT") + 3);
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

// Equipped is a fixed handful of slots and never takes the slack, however
// little the bag beside it wants.
TEST_F(MainLayoutTest, TheEquippedPanelKeepsItsOwnHeight) {
  Render(/*bag_rows=*/1);
  EXPECT_EQ(FirstRowWith("EQUIP"), 1);
  EXPECT_EQ(Cell(kLeftWidth, 2), "╰");
}

// And keeps it when the bag is overflowing, which is the case that costs it a
// row if the bag is not the one thing marked shrinkable: ftxui then takes the
// path that shares the overflow out across every panel in the column.
TEST_F(MainLayoutTest, AFullBagDoesNotSquashTheEquippedPanel) {
  Render(/*bag_rows=*/40);
  EXPECT_EQ(FirstRowWith("EQUIP"), 1);
  EXPECT_EQ(Cell(kLeftWidth, 2), "╰")
      << "the equipped panel gave up a row to the bag below it";
}

// The right column takes the width left over rather than its own, so the bag
// reaches the edge of the terminal.
TEST_F(MainLayoutTest, TheRightColumnFillsTheRemainingWidth) {
  Render(/*bag_rows=*/40);
  int bag_row = LastRowWith("BAG");
  ASSERT_GE(bag_row, 0);
  EXPECT_EQ(Cell(kScreenWidth - 1, bag_row), "│");
}

}  // namespace
}  // namespace ms
