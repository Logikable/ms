#include "src/frontend/widgets/item_columns.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "src/frontend/widgets/text_columns.h"

namespace ms {
namespace {

ItemListOptions AllUnlocked() {
  return {/*bag=*/false, /*scrolling=*/true, /*star_force=*/true,
          /*potential=*/true};
}

ItemListOptions Bag() {
  ItemListOptions options = AllUnlocked();
  options.bag = true;
  return options;
}

// The columns drawn, in the order they are drawn.
std::vector<ItemColumn> Drawn(const ItemColumns& columns) {
  std::vector<ItemColumn> drawn;
  for (int i = 0; i < kNumItemColumns; ++i) {
    ItemColumn column = static_cast<ItemColumn>(i);
    if (columns.Shows(column)) {
      drawn.push_back(column);
    }
  }
  return drawn;
}

TEST(ItemColumnsTest, WideEnoughForEverything) {
  ItemColumns columns = FitItemColumns(200, Bag());
  EXPECT_EQ(Drawn(columns),
            (std::vector<ItemColumn>{
                ItemColumn::kName, ItemColumn::kSlot, ItemColumn::kLevel,
                ItemColumn::kJob, ItemColumn::kStats, ItemColumn::kScroll,
                ItemColumn::kStars, ItemColumn::kPotential}));
  // The name stops growing at the longest name the game ships.
  EXPECT_EQ(columns.name_width, kItemNameMax);
}

// Columns go in priority order, so what a narrowing panel loses is the last
// of that order and never a column above it.
TEST(ItemColumnsTest, DropsInPriorityOrder) {
  std::vector<ItemColumn> lost;
  ItemColumns wide = FitItemColumns(200, Bag());
  for (int width = 199; width >= 40; --width) {
    ItemColumns columns = FitItemColumns(width, Bag());
    for (ItemColumn column : Drawn(wide)) {
      if (!columns.Shows(column) &&
          std::find(lost.begin(), lost.end(), column) == lost.end()) {
        lost.push_back(column);
      }
    }
    // Nothing comes back once it has gone.
    for (ItemColumn column : lost) {
      EXPECT_FALSE(columns.Shows(column));
    }
  }
  // The priority read backwards, down to the slot, which goes last of all --
  // 40 columns will not hold a name and a slot together.
  EXPECT_EQ(lost, (std::vector<ItemColumn>{
                      ItemColumn::kStats, ItemColumn::kJob, ItemColumn::kLevel,
                      ItemColumn::kScroll, ItemColumn::kStars,
                      ItemColumn::kPotential, ItemColumn::kSlot}));
}

// A column below one that did not fit stays out, however much narrower it is.
TEST(ItemColumnsTest, NarrowerColumnDoesNotSlipPastAWiderOne) {
  for (int width = 40; width <= 200; ++width) {
    ItemColumns columns = FitItemColumns(width, Bag());
    bool dropped = false;
    for (ItemColumn column : kItemColumnPriority) {
      if (!columns.Shows(column)) {
        dropped = true;
      } else {
        EXPECT_FALSE(dropped) << "width " << width;
      }
    }
  }
}

// A mechanic the account has not unlocked has no column, and the room it did
// not take goes to whatever is under it.
TEST(ItemColumnsTest, GatesEachColumnOnItsMechanic) {
  ItemColumns none = FitItemColumns(85, ItemListOptions{});
  EXPECT_FALSE(none.Shows(ItemColumn::kScroll));
  EXPECT_FALSE(none.Shows(ItemColumn::kStars));
  EXPECT_FALSE(none.Shows(ItemColumn::kPotential));
  // The stats column is the first thing a narrow panel drops, so it is what
  // the three unlocks buy back.
  EXPECT_TRUE(none.Shows(ItemColumn::kStats));
  EXPECT_FALSE(FitItemColumns(85, AllUnlocked()).Shows(ItemColumn::kStats));

  // Level and job are the bag's alone: the equipped list is wearing the item.
  ItemColumns worn = FitItemColumns(200, AllUnlocked());
  EXPECT_FALSE(worn.Shows(ItemColumn::kLevel));
  EXPECT_FALSE(worn.Shows(ItemColumn::kJob));
  EXPECT_TRUE(FitItemColumns(200, Bag()).Shows(ItemColumn::kLevel));
}

// Whatever no column claimed goes to the name, between its two bounds, and
// the row never outgrows the panel. The name does not climb with the width:
// one column further along, a new cell takes the room and the name gives it
// all back.
TEST(ItemColumnsTest, LeftoverRoomGoesToTheName) {
  for (int width = 40; width <= 200; ++width) {
    ItemColumns columns = FitItemColumns(width, Bag());
    EXPECT_GE(columns.name_width, kItemNameWidth);
    EXPECT_LE(columns.name_width, kItemNameMax);
    EXPECT_LE(columns.TotalWidth(), width);
    // Nothing is left on the table: either the name is full or the next
    // column in the priority would not have fitted.
    EXPECT_TRUE(columns.name_width == kItemNameMax ||
                columns.TotalWidth() + kItemCellGap > width)
        << "width " << width;
  }
}

// A panel too narrow for anything still lists names: a row with no name is
// not a row.
TEST(ItemColumnsTest, KeepsTheNameAtAnyWidth) {
  ItemColumns columns = FitItemColumns(10, Bag());
  EXPECT_TRUE(columns.Shows(ItemColumn::kName));
  EXPECT_EQ(columns.name_width, kItemNameWidth);
}

// The header stands over the cells: one label per drawn column, each starting
// where its cell starts.
TEST(ItemColumnsTest, HeaderStandsOverTheColumns) {
  ItemColumns columns = FitItemColumns(200, Bag());
  std::string header = ItemListHeader(columns);
  EXPECT_EQ(header.substr(0, kItemListCursor + 4),
            std::string(kItemListCursor, ' ') + "Name");
  int at = kItemListCursor + columns.name_width;
  for (int i = 1; i < kNumItemColumns; ++i) {
    ItemColumn column = static_cast<ItemColumn>(i);
    if (!columns.Shows(column)) {
      continue;
    }
    at += kItemCellGap;
    EXPECT_EQ(header.substr(at, std::string(ItemColumnHeader(column)).size()),
              ItemColumnHeader(column));
    at += columns.Width(column);
  }
  // No blank tail: the last label ends the row.
  EXPECT_EQ(header.back(), 'l');  // "...Potential"
  EXPECT_LE(TextColumns(header) + kItemListGutter, 200);
}

}  // namespace
}  // namespace ms
