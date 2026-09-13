#include "src/frontend/widgets/inventory_list.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_widths.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/item_columns.h"
#include "src/frontend/widgets/item_row.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/item/equip_instance.h"

namespace ms {
namespace {

class InventoryListTest : public PanelTest {
 protected:
  // One row's text, with the columns a caller adds on either side.
  std::string RowText(ftxui::Element row) {
    return RenderElement(ftxui::vbox({std::move(row)}));
  }

  // The bag's columns at `width`, with every mechanic open.
  ItemColumns Columns(int width) {
    return FitItemColumns(width, {/*bag=*/true, /*scrolling=*/true,
                                  /*star_force=*/true, /*potential=*/true});
  }

  std::vector<InventoryRowState> Rows(int width = kRightColumnMin - 2) {
    return BuildEquipRows(c_, 0, std::chrono::steady_clock::duration::zero(),
                          Columns(width));
  }

  StackableItem Token(const std::string& name, const std::string& mark,
                      int count) {
    ItemPrototype proto;
    proto.set_name(name);
    proto.set_kind(ITEM_KIND_TOKEN);
    proto.set_currency_mark(mark);
    return StackableItem(proto, count);
  }

  // Named the short way, as the shards in the catalog are.
  StackableItem Shard(const std::string& short_name, int count) {
    ItemPrototype proto;
    proto.set_name(short_name + " Soul Shard");
    proto.set_short_name(short_name);
    proto.set_kind(ITEM_KIND_SOUL_SHARD);
    return StackableItem(proto, count);
  }
};

TEST_F(InventoryListTest, EquipRowsCarryTheItemAndWhatShutsIt) {
  c_.PickUp(std::make_unique<EquipInstance>(
      sword_));  // required level 10, Warrior only
  std::vector<InventoryRowState> rows = Rows();
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_NE(rows[0].label.text.find("Sword"), std::string::npos);
  EXPECT_FALSE(rows[0].is_trace);
  EXPECT_FALSE(rows[0].level_ok);
  EXPECT_FALSE(rows[0].job_ok);
}

TEST_F(InventoryListTest, TheCursorShowsOnTheRowItIsOn) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  std::vector<InventoryRowState> rows = Rows();
  EXPECT_NE(RowText(RenderEquipRow(rows[0], /*on_cursor=*/true)).find("> "),
            std::string::npos);
  EXPECT_EQ(RowText(RenderEquipRow(rows[0], /*on_cursor=*/false)).find("> "),
            std::string::npos);
}

TEST_F(InventoryListTest, AffixColumnsRideEitherSideOfARow) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  std::vector<InventoryRowState> rows = Rows();
  std::string text = RowText(RenderEquipRow(
      rows[0], /*on_cursor=*/false, ftxui::text("**"), ftxui::text("!!")));
  EXPECT_EQ(text.find("**"), 0u);
  EXPECT_NE(text.find("!!"), std::string::npos);
  // The headers make the same room, so the columns line up over the rows.
  EXPECT_EQ(RenderElement(EquipHeader(Columns(kRightColumnMin - 2),
                                      ftxui::text("**"), ftxui::text("!!")))
                .find("**"),
            0u);
  EXPECT_EQ(RenderElement(StackHeader(ftxui::text("**"), ftxui::text("!!")))
                .find("**"),
            0u);
}

TEST_F(InventoryListTest, StackRowsNameTheirCount) {
  ItemPrototype shell;
  shell.set_name("Green Snail Shell");
  shell.set_category(ITEM_CATEGORY_ETC);
  StackableItem stack(shell, 42);
  std::string text = RowText(RenderStackRow(
      stack, /*on_cursor=*/true, std::chrono::steady_clock::duration::zero()));
  EXPECT_NE(text.find("> Green Snail Shell"), std::string::npos);
  EXPECT_NE(text.find("42"), std::string::npos);
}

// A wide terminal buys the name column room, and the columns after it move
// over rather than staying where a narrow panel put them.
TEST_F(InventoryListTest, AWideNameColumnMovesTheColumnsAfterIt) {
  EquipPrototype wordy = sword_;
  wordy.set_name("Metallic Blue Book (Antistrophe) Trace");
  c_.PickUp(std::make_unique<EquipInstance>(wordy));

  std::vector<InventoryRowState> narrow = Rows();
  std::vector<InventoryRowState> wide = Rows(200);
  ASSERT_EQ(wide.size(), 1u);
  EXPECT_EQ(narrow[0].label.text.find("Metallic Blue Book (Antistrophe) Trace"),
            std::string::npos)
      << "the narrow column cuts it, which is what this compares against";
  EXPECT_NE(wide[0].label.text.find("Metallic Blue Book (Antistrophe) Trace"),
            std::string::npos);
  // The row still reads as columns: the header over it makes the same room.
  // The row carries no cursor of its own, so its slot cell sits two columns
  // left of the header's -- the width of the caret the render adds.
  EXPECT_EQ(wide[0].label.text.find("Weapon") + kItemListCursor,
            RenderElement(EquipHeader(Columns(200))).find("Equip Slot"));
}

// The cell that says why a row is shut is found from its own span, so
// widening the name column does not paint the wrong cell red.
TEST_F(InventoryListTest, AWideRowStillReddensTheCellThatShutsIt) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  // As wide as the screen it is drawn on: a row wider than that is squeezed
  // by the render, and the cells no longer land where the row put them.
  std::vector<InventoryRowState> rows = Rows(kTestScreenWidth);
  ftxui::Element row = RenderEquipRow(rows[0], /*on_cursor=*/false);
  ftxui::Screen screen = ftxui::Screen::Create(
      ftxui::Dimension::Fixed(kTestScreenWidth), ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, row);
  // Where the level cell lands: the caret, then the cell's own span, whose
  // two leading blanks the "L" follows.
  int column = kItemListCursor + rows[0].label.Span(ItemColumn::kLevel).offset +
               kItemCellGap;
  EXPECT_EQ(screen.PixelAt(column, 0).character, "L");
  EXPECT_EQ(screen.PixelAt(column, 0).foreground_color, kRed)
      << "the cell that says why the row is shut";
}

// The bag's lists carry the same columns as the equipped panel's, so they
// share its minimum width -- see panel_widths.h.
TEST_F(InventoryListTest, TheHeadersFitTheRightColumnMinimum) {
  ftxui::Element equips = EquipHeader(Columns(kRightColumnMin - 2));
  ftxui::Element stacks = StackHeader();
  EXPECT_LE(ftxui::Dimension::Fit(equips).dimx + kItemListGutter + 2,
            kRightColumnMin);
  EXPECT_LE(ftxui::Dimension::Fit(stacks).dimx + kItemListGutter + 2,
            kRightColumnMin);
  // Four columns on one row, which is the widest the bag asks a stack tab for,
  // at the longest name each of them ships.
  StackableItem token = Token("Frozen Secondary Token", "●", 999);
  StackableItem shard = Shard("Crimson Queen's", 99);
  ftxui::Element currency = CurrencyHeader();
  ftxui::Element currency_row = RenderCurrencyRow(&token, &shard);
  EXPECT_LE(ftxui::Dimension::Fit(currency).dimx + kItemListGutter + 2,
            kRightColumnMin);
  EXPECT_LE(ftxui::Dimension::Fit(currency_row).dimx + kItemListGutter + 2,
            kRightColumnMin);
}

// The Token tab writes the mark, the name and the count of each column, and
// the short name is what a shard goes by under its own heading.
TEST_F(InventoryListTest, ACurrencyRowCarriesBothColumns) {
  StackableItem token = Token("Frozen Weapon Token", "●", 3);
  StackableItem shard = Shard("Zakum's", 47);
  std::string text = RowText(RenderCurrencyRow(&token, &shard));
  EXPECT_NE(text.find("●"), std::string::npos);
  EXPECT_NE(text.find("Frozen Weapon Token"), std::string::npos);
  EXPECT_NE(text.find("3"), std::string::npos);
  EXPECT_NE(text.find("Zakum's"), std::string::npos);
  EXPECT_EQ(text.find("Soul Shard"), std::string::npos)
      << "the column above it already says so";
  EXPECT_NE(text.find("47"), std::string::npos);
}

// The columns run out at different heights, and the half with nothing left in
// it holds its width so the other stays under its heading.
TEST_F(InventoryListTest, AHalfEmptyCurrencyRowKeepsItsColumns) {
  StackableItem token = Token("Frozen Weapon Token", "●", 3);
  StackableItem shard = Shard("Zakum's", 47);
  ftxui::Element both = RenderCurrencyRow(&token, &shard);
  ftxui::Element shard_only = RenderCurrencyRow(nullptr, &shard);
  EXPECT_EQ(ftxui::Dimension::Fit(shard_only).dimx,
            ftxui::Dimension::Fit(both).dimx);
  EXPECT_NE(RowText(std::move(shard_only)).find("Zakum's"), std::string::npos);
}

}  // namespace
}  // namespace ms
