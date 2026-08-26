#include "src/frontend/widgets/inventory_list.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_widths.h"
#include "src/frontend/widgets/colors.h"
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
};

TEST_F(InventoryListTest, TabCategoryNamesOnlyTheStackTabs) {
  EXPECT_EQ(TabCategory(kEquipTab), ITEM_CATEGORY_UNSPECIFIED);
  EXPECT_EQ(TabCategory(kUseTab), ITEM_CATEGORY_USE);
  EXPECT_EQ(TabCategory(kEtcTab), ITEM_CATEGORY_ETC);
  EXPECT_EQ(TabCategory(kShopTab), ITEM_CATEGORY_UNSPECIFIED);
}

TEST_F(InventoryListTest, EquipRowsCarryTheItemAndWhatShutsIt) {
  c_.PickUp(std::make_unique<EquipInstance>(
      sword_));  // required level 10, Warrior only
  std::vector<InventoryRowState> rows =
      BuildEquipRows(c_, 0, std::chrono::steady_clock::duration::zero());
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_NE(rows[0].label.find("Sword"), std::string::npos);
  EXPECT_FALSE(rows[0].is_trace);
  EXPECT_FALSE(rows[0].level_ok);
  EXPECT_FALSE(rows[0].job_ok);
}

TEST_F(InventoryListTest, TheCursorShowsOnTheRowItIsOn) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  std::vector<InventoryRowState> rows =
      BuildEquipRows(c_, 0, std::chrono::steady_clock::duration::zero());
  EXPECT_NE(RowText(RenderEquipRow(rows[0], /*on_cursor=*/true)).find("> "),
            std::string::npos);
  EXPECT_EQ(RowText(RenderEquipRow(rows[0], /*on_cursor=*/false)).find("> "),
            std::string::npos);
}

TEST_F(InventoryListTest, AffixColumnsRideEitherSideOfARow) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  std::vector<InventoryRowState> rows =
      BuildEquipRows(c_, 0, std::chrono::steady_clock::duration::zero());
  std::string text = RowText(RenderEquipRow(
      rows[0], /*on_cursor=*/false, ftxui::text("**"), ftxui::text("!!")));
  EXPECT_EQ(text.find("**"), 0u);
  EXPECT_NE(text.find("!!"), std::string::npos);
  // The headers make the same room, so the columns line up over the rows.
  EXPECT_EQ(RenderElement(EquipHeader(ftxui::text("**"), ftxui::text("!!")))
                .find("**"),
            0u);
  EXPECT_EQ(RenderElement(StackHeader(ftxui::text("**"), ftxui::text("!!")))
                .find("**"),
            0u);
}

TEST_F(InventoryListTest, StackRowsNameTheirCount) {
  ItemPrototype potion;
  potion.set_name("Red Potion");
  potion.set_category(ITEM_CATEGORY_USE);
  StackableItem stack(potion, 42);
  std::string text = RowText(RenderStackRow(
      stack, /*on_cursor=*/true, std::chrono::steady_clock::duration::zero()));
  EXPECT_NE(text.find("> Red Potion"), std::string::npos);
  EXPECT_NE(text.find("42"), std::string::npos);
}

// A wide terminal buys the name column room, and the columns after it move
// over rather than staying where a narrow panel put them.
TEST_F(InventoryListTest, AWideNameColumnMovesTheColumnsAfterIt) {
  EquipPrototype wordy = sword_;
  wordy.set_name("Metallic Blue Book (Antistrophe)");
  c_.PickUp(std::make_unique<EquipInstance>(wordy));
  std::chrono::steady_clock::duration still =
      std::chrono::steady_clock::duration::zero();

  std::vector<InventoryRowState> narrow = BuildEquipRows(c_, -1, still);
  std::vector<InventoryRowState> wide =
      BuildEquipRows(c_, -1, still, kItemNameMax);
  ASSERT_EQ(wide.size(), 1u);
  EXPECT_EQ(narrow[0].label.find("Metallic Blue Book (Antistrophe)"),
            std::string::npos)
      << "the narrow column cuts it, which is what this compares against";
  EXPECT_NE(wide[0].label.find("Metallic Blue Book (Antistrophe)"),
            std::string::npos);
  EXPECT_EQ(wide[0].name_bytes - narrow[0].name_bytes,
            kItemNameMax - kItemNameWidth);
  // The row still reads as columns: the header over it makes the same room.
  ftxui::Element header =
      EquipHeader(nullptr, nullptr, /*body_width=*/0, kItemNameMax);
  // The row carries no cursor of its own, so its slot cell sits two columns
  // left of the header's -- the width of the caret the render adds.
  EXPECT_EQ(wide[0].label.find("Weapon") + 2,
            RenderElement(header).find("Equip Slot"));
}

// The cell that says why a row is shut is found from the name's own length,
// so widening the name column does not paint the wrong cell red.
TEST_F(InventoryListTest, AWideRowStillReddensTheCellThatShutsIt) {
  c_.PickUp(std::make_unique<EquipInstance>(sword_));
  std::vector<InventoryRowState> rows = BuildEquipRows(
      c_, -1, std::chrono::steady_clock::duration::zero(), kItemNameMax);
  ftxui::Element row = RenderEquipRow(rows[0], /*on_cursor=*/false);
  ftxui::Screen screen = ftxui::Screen::Create(
      ftxui::Dimension::Fixed(kTestScreenWidth), ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, row);
  // Where the level cell lands: the caret, the name, and the slot cell after
  // it. Read as columns rather than searched for, since that arithmetic is
  // what the row itself does.
  int column = 2 + rows[0].name_bytes + 14;
  EXPECT_EQ(screen.PixelAt(column, 0).character, "L");
  EXPECT_EQ(screen.PixelAt(column, 0).foreground_color, kRed)
      << "the cell that says why the row is shut";
}

// The bag's lists carry the same columns as the equipped panel's, so they
// share its minimum width -- see panel_widths.h.
TEST_F(InventoryListTest, TheHeadersFitTheRightColumnMinimum) {
  ftxui::Element equips = EquipHeader();
  ftxui::Element stacks = StackHeader();
  EXPECT_LE(ftxui::Dimension::Fit(equips).dimx + 2, kRightColumnMin);
  EXPECT_LE(ftxui::Dimension::Fit(stacks).dimx + 2, kRightColumnMin);
}

}  // namespace
}  // namespace ms
