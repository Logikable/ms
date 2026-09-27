#include "src/frontend/widgets/item_columns.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/frontend/widgets/text_columns.h"

namespace ms {
namespace {

ItemListOptions AllUnlocked() {
  return {/*bag=*/false, /*scrolling=*/true, /*star_force=*/true,
          /*potential=*/true, /*bonus_potential=*/true};
}

ItemListOptions Bag() {
  ItemListOptions options = AllUnlocked();
  options.bag = true;
  return options;
}

// The columns drawn, in drawing order.
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
            (std::vector<ItemColumn>{ItemColumn::kName, ItemColumn::kSlot,
                                     ItemColumn::kLevel, ItemColumn::kJob,
                                     ItemColumn::kStats, ItemColumn::kScroll,
                                     ItemColumn::kStars, ItemColumn::kPotential,
                                     ItemColumn::kBonusPotential}));
  // The stretchable columns stop at their widest: three potential effects, and
  // the longest name in the game.
  EXPECT_EQ(columns.potential_width, kItemPotentialMax);
  EXPECT_EQ(columns.bonus_potential_width, kItemPotentialMax);
  EXPECT_EQ(columns.name_width, kItemNameMax);
}

// Columns are taken in priority order, so a narrowing panel always loses the
// lowest-ranked column first.
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
    // A column that has gone never comes back.
    for (ItemColumn column : lost) {
      EXPECT_FALSE(columns.Shows(column));
    }
  }
  // The priority in reverse, down to the slot, which goes last because 40
  // columns can't hold a name and a slot together.
  EXPECT_EQ(lost, (std::vector<ItemColumn>{
                      ItemColumn::kStats, ItemColumn::kJob, ItemColumn::kLevel,
                      ItemColumn::kScroll, ItemColumn::kStars,
                      ItemColumn::kBonusPotential, ItemColumn::kPotential,
                      ItemColumn::kSlot}));
}

// A column ranked below one that didn't fit stays out, however narrow it is.
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

// A mechanic the account hasn't unlocked has no column, and the room goes to
// the columns ranked below it.
TEST(ItemColumnsTest, GatesEachColumnOnItsMechanic) {
  ItemColumns none = FitItemColumns(85, ItemListOptions{});
  EXPECT_FALSE(none.Shows(ItemColumn::kScroll));
  EXPECT_FALSE(none.Shows(ItemColumn::kStars));
  EXPECT_FALSE(none.Shows(ItemColumn::kPotential));
  EXPECT_FALSE(none.Shows(ItemColumn::kBonusPotential));
  // The stats column is the first thing a narrow panel drops, so the three
  // unlocks are what push it out.
  EXPECT_TRUE(none.Shows(ItemColumn::kStats));
  EXPECT_FALSE(FitItemColumns(85, AllUnlocked()).Shows(ItemColumn::kStats));

  // Level and job appear only in the bag, since the equipped list is already
  // wearing the item.
  ItemColumns worn = FitItemColumns(200, AllUnlocked());
  EXPECT_FALSE(worn.Shows(ItemColumn::kLevel));
  EXPECT_FALSE(worn.Shows(ItemColumn::kJob));
  EXPECT_TRUE(FitItemColumns(200, Bag()).Shows(ItemColumn::kLevel));
}

// Leftover room widens the potential columns evenly, then the name, each within
// its limits, and the row never outgrows the panel. None keeps growing with the
// width for long: a little wider and the next column takes the room.
TEST(ItemColumnsTest, LeftoverRoomWidensPotentialThenTheName) {
  for (int width = 40; width <= 200; ++width) {
    ItemColumns columns = FitItemColumns(width, Bag());
    EXPECT_GE(columns.potential_width, kItemPotentialWidth);
    EXPECT_LE(columns.potential_width, kItemPotentialMax);
    if (columns.Shows(ItemColumn::kBonusPotential)) {
      EXPECT_GE(columns.potential_width, kItemMainPotentialWidth);
      EXPECT_GE(columns.bonus_potential_width, kItemBonusPotentialWidth);
      EXPECT_LE(columns.bonus_potential_width, kItemPotentialMax);
      // Each takes a column in turn, so neither runs ahead of the other.
      EXPECT_LE(
          std::abs(columns.bonus_potential_width - columns.potential_width),
          kItemBonusPotentialWidth - kItemMainPotentialWidth)
          << "width " << width;
    }
    EXPECT_GE(columns.name_width, kItemNameWidth);
    EXPECT_LE(columns.name_width, kItemNameMax);
    // The gutter is reserved before anything is handed out, so a row always
    // stops one column short of the border.
    EXPECT_LE(columns.TotalWidth(), width);
    // No room is wasted: either the name is at its widest or the next column in
    // priority wouldn't have fit.
    EXPECT_TRUE(columns.name_width == kItemNameMax ||
                columns.TotalWidth() + kItemCellGap > width)
        << "width " << width;
    // The name takes nothing while the potential column can still use the room.
    EXPECT_TRUE(columns.name_width == kItemNameWidth ||
                ((!columns.Shows(ItemColumn::kPotential) ||
                  columns.potential_width == kItemPotentialMax) &&
                 (!columns.Shows(ItemColumn::kBonusPotential) ||
                  columns.bonus_potential_width == kItemPotentialMax)))
        << "width " << width;
  }
}

// A locked potential column takes no room, so the leftover goes to the name, as
// it did before the mechanic unlocked.
TEST(ItemColumnsTest, NameTakesTheRoomWithNoPotentialColumn) {
  ItemColumns columns = FitItemColumns(200, ItemListOptions{/*bag=*/true});
  EXPECT_FALSE(columns.Shows(ItemColumn::kPotential));
  EXPECT_EQ(columns.name_width, kItemNameMax);
}

// A panel too narrow for anything else still lists names, since a row without a
// name is useless.
TEST(ItemColumnsTest, KeepsTheNameAtAnyWidth) {
  ItemColumns columns = FitItemColumns(10, Bag());
  EXPECT_TRUE(columns.Shows(ItemColumn::kName));
  EXPECT_EQ(columns.name_width, kItemNameWidth);
}

// The potential column is the one that grows, so its header moves with it and
// the row still ends inside the panel.
TEST(ItemColumnsTest, HeaderFollowsTheWidenedPotentialColumn) {
  // At 106 columns every cell the bag can show but the stats fits with nothing
  // left over.
  ItemColumns narrow = FitItemColumns(106, Bag());
  ItemColumns wide = FitItemColumns(200, Bag());
  EXPECT_TRUE(narrow.Shows(ItemColumn::kJob));
  EXPECT_EQ(narrow.potential_width, kItemMainPotentialWidth);
  EXPECT_EQ(narrow.bonus_potential_width, kItemBonusPotentialWidth);
  EXPECT_GT(wide.potential_width, narrow.potential_width);
  EXPECT_LE(TextColumns(ItemListHeader(wide)) + kItemListGutter, 200);
}

// Each drawn column gets one label, starting where its cell starts.
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
    EXPECT_EQ(header.substr(at, std::string(columns.Header(column)).size()),
              columns.Header(column));
    at += columns.Width(column);
  }
  // No trailing blanks: the last label ends the row.
  EXPECT_EQ(header.back(), 'l');  // "...Potential"
  EXPECT_LE(TextColumns(header) + kItemListGutter, 200);
}

// The bonus column renames the main one and widens it to fit the new name. A
// list with no room for the bonus column keeps the plain name.
TEST(ItemColumnsTest, BonusPotentialRenamesTheMainColumn) {
  // A 120-column terminal's equipped list: both potentials push Scroll off.
  ItemColumns both = FitItemColumns(83, AllUnlocked());
  EXPECT_TRUE(both.Shows(ItemColumn::kBonusPotential));
  EXPECT_TRUE(both.Shows(ItemColumn::kStars));
  EXPECT_FALSE(both.Shows(ItemColumn::kScroll));
  EXPECT_STREQ(both.Header(ItemColumn::kPotential), "Main Potential");
  EXPECT_STREQ(both.Header(ItemColumn::kBonusPotential), "Bonus Potential");
  EXPECT_EQ(both.potential_width, kItemMainPotentialWidth + 1);
  EXPECT_EQ(both.bonus_potential_width, kItemBonusPotentialWidth + 1);

  ItemColumns main_only = FitItemColumns(60, AllUnlocked());
  EXPECT_TRUE(main_only.Shows(ItemColumn::kPotential));
  EXPECT_FALSE(main_only.Shows(ItemColumn::kBonusPotential));
  EXPECT_STREQ(main_only.Header(ItemColumn::kPotential), "Potential");

  // Without the main column the bonus one stands alone under its own name.
  ItemListOptions bonus_only;
  bonus_only.bonus_potential = true;
  ItemColumns alone = FitItemColumns(83, bonus_only);
  EXPECT_FALSE(alone.Shows(ItemColumn::kPotential));
  EXPECT_TRUE(alone.Shows(ItemColumn::kBonusPotential));
  EXPECT_NE(ItemListHeader(alone).find("Bonus Potential"), std::string::npos);
}

}  // namespace
}  // namespace ms
