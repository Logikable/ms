#include "src/frontend/widgets/inventory_list.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
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

}  // namespace
}  // namespace ms
