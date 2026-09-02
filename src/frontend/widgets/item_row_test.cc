#include "src/frontend/widgets/item_row.h"

#include <gtest/gtest.h>

#include <string>

#include "src/frontend/widgets/item_columns.h"
#include "src/frontend/widgets/marquee.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

ItemColumns EveryColumn() {
  return FitItemColumns(200,
                        {/*bag=*/true, /*scrolling=*/true, /*star_force=*/true,
                         /*potential=*/true});
}

ItemCells SwordCells() {
  ItemCells cells;
  cells.name = "Sword";
  cells.slot = "Weapon";
  cells.level = "Lv120";
  cells.job = "Warrior";
  cells.stats = "+7 ATT";
  cells.scroll = "+3/7";
  cells.stars = "12★";
  cells.potential = "+12% ATT";
  return cells;
}

TEST(FormatItemRowTest, WritesEveryCellItIsGiven) {
  ItemRowText row = FormatItemRow(EveryColumn(), SwordCells());
  for (const char* cell : {"Sword", "Weapon", "Lv120", "Warrior", "+7 ATT",
                           "+3/7", "12★", "+12% ATT"}) {
    EXPECT_NE(row.text.find(cell), std::string::npos) << cell;
  }
}

// A column the list is not drawing takes no room and leaves no span, and the
// cells after it close up behind it.
TEST(FormatItemRowTest, LeavesOutAColumnTheListDoesNotDraw) {
  ItemColumns worn = FitItemColumns(
      200, {/*bag=*/false, /*scrolling=*/true, /*star_force=*/true,
            /*potential=*/true});
  ItemRowText row = FormatItemRow(worn, SwordCells());
  EXPECT_EQ(row.text.find("Lv120"), std::string::npos);
  EXPECT_EQ(row.text.find("Warrior"), std::string::npos);
  EXPECT_EQ(row.Span(ItemColumn::kLevel).bytes, 0);
  EXPECT_EQ(row.Span(ItemColumn::kJob).bytes, 0);
  EXPECT_NE(row.text.find("+7 ATT"), std::string::npos);
}

// The spans butt up against one another and cover the whole row, which is
// what lets a caller write it out cell by cell without losing a column.
TEST(FormatItemRowTest, SpansCoverTheRowEndToEnd) {
  ItemColumns columns = EveryColumn();
  ItemRowText row = FormatItemRow(columns, SwordCells());
  std::string rebuilt;
  for (int i = 0; i < kNumItemColumns; ++i) {
    CellSpan span = row.Span(static_cast<ItemColumn>(i));
    if (span.bytes == 0) {
      continue;
    }
    EXPECT_EQ(span.offset, static_cast<int>(rebuilt.size()));
    rebuilt += row.text.substr(span.offset, span.bytes);
  }
  EXPECT_EQ(rebuilt, row.text);
  // The row is exactly the width the columns were fitted to, less the cursor
  // and the gutter the panel keeps around it.
  EXPECT_EQ(TextColumns(row.text) + kItemListCursor + kItemListGutter,
            columns.TotalWidth());
}

// An item name wider than its column is cut, and slides under the column once
// its row has been selected long enough -- the same treatment a skill name
// gets, through the same ScrollingWindow.
TEST(FormatItemRowTest, ALongNameIsCutAndThenSlides) {
  ItemColumns columns = EveryColumn();
  ItemCells cells = SwordCells();
  cells.name = "Fafnir Mistilteinn Trace Of Old Iron Age";  // 39 columns
  ItemRowText still = FormatItemRow(columns, cells);
  EXPECT_EQ(still.text.substr(0, kItemNameMax),
            "Fafnir Mistilteinn Trace Of Old Iron A");

  ItemRowText slid =
      FormatItemRow(columns, cells, kMarqueePause + kMarqueeStep);
  EXPECT_EQ(slid.text.substr(0, kItemNameMax),
            "fnir Mistilteinn Trace Of Old Iron Age");
  // The columns after the name do not move with it.
  EXPECT_EQ(still.text.substr(kItemNameMax), slid.text.substr(kItemNameMax));
}

// A name that fits is padded to the column and never moves, so a list of them
// stays a list rather than shuffling under the cursor.
TEST(FormatItemRowTest, AShortNameNeverMoves) {
  ItemColumns columns = EveryColumn();
  EXPECT_EQ(FormatItemRow(columns, SwordCells()).text,
            FormatItemRow(columns, SwordCells(), kMarqueePause * 10).text);
}

// The upgrade cells come off the item, so no two lists can disagree about
// what an item that takes neither shows.
TEST(EquipUpgradeCellsTest, ReadsBothUpgradesAndThePotentialOffTheItem) {
  EquipPrototype proto;
  proto.set_upgrade_slots(7);
  proto.set_required_level(150);
  Equip state;
  state.set_scroll_successes(3);
  state.set_remaining_upgrade_slots(2);
  state.set_stars(12);
  PotentialLine* line = state.mutable_main_potential()->add_lines();
  line->set_type(POTENTIAL_LINE_TYPE_ATTACK_PCT);
  line->set_rank(POTENTIAL_RANK_LEGENDARY);
  // A second line, which the column never shows: only the first carries the
  // potential's own rank.
  state.mutable_main_potential()->add_lines()->set_type(
      POTENTIAL_LINE_TYPE_MESO_RATE);

  ItemCells cells = EquipUpgradeCells(proto, state);
  EXPECT_EQ(cells.scroll, "+3/7");
  EXPECT_EQ(cells.stars, "12★");
  EXPECT_EQ(cells.potential, "+12% ATT     ");

  // An upgrade the item refuses, and an item carrying no potential, each read
  // "-" rather than blank.
  proto.set_upgrade_slots(0);
  proto.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  state.clear_main_potential();
  cells = EquipUpgradeCells(proto, state);
  EXPECT_EQ(cells.scroll, "-");
  EXPECT_EQ(cells.stars, "-");
  EXPECT_EQ(cells.potential, "-");
}

}  // namespace
}  // namespace ms
