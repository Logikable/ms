/* The bag's lists, as both the Inventory panel and the Multi-Sell screen draw
 * them: the tab names, the Equip and stack rows, and the headers over them.
 * Both screens show the same items, so the rows are built in one place.
 *
 * Every row and header takes a `lead` and a `tail`: columns the caller adds on
 * either side, for Multi-Sell's sale mark and price. The bag passes neither.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_INVENTORY_LIST_H_
#define MS_SRC_FRONTEND_WIDGETS_INVENTORY_LIST_H_

#include <chrono>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/item/item.h"
#include "src/protos/item.pb.h"

namespace ms {

// The bag's tabs, in bar order. Multi-Sell shows every one but the shop.
enum InventoryTab : int {
  kEquipTab = 0,
  kUseTab = 1,
  kEtcTab = 2,
  // Not a list of anything the player owns -- it is the door to the shop, and
  // sits last because it is the only tab that leaves the panel.
  kShopTab = 3,
  kNumInventoryTabs = 4,
};

extern const char* const kInventoryTabLabels[kNumInventoryTabs];

// The stackable category `tab` lists, or ITEM_CATEGORY_UNSPECIFIED for the
// Equip and Shop tabs, which list no stack at all.
ItemCategory TabCategory(int tab);

// One Equip row: its text, and the three things that can shut it.
struct InventoryRowState {
  std::string label;
  bool is_trace;
  bool level_ok;
  bool job_ok;
};

// One row per item on the equip tab. `selected` names the row whose name
// slides under its column, and `elapsed` how long it has been selected.
std::vector<InventoryRowState> BuildEquipRows(
    const CharacterInstance& character, int selected,
    std::chrono::steady_clock::duration elapsed);

// The two header rows over an Equip list. The second carries the scroll
// column's "Pass/Left/Restore" and nothing else.
ftxui::Element EquipHeader(const std::string& lead, const std::string& tail);
ftxui::Element EquipSubHeader(const std::string& lead);

// One Equip row with its cursor caret, drawn dim with the cell that says why
// left bright and red when nothing can be done with the item. `lead` and
// `tail` may be null for a list with no column on that side.
ftxui::Element RenderEquipRow(const InventoryRowState& row, bool on_cursor,
                              ftxui::Element lead = nullptr,
                              ftxui::Element tail = nullptr);

// The header over a Use or Etc list, and one row of one.
ftxui::Element StackHeader(const std::string& lead, const std::string& tail);
ftxui::Element RenderStackRow(const StackableItem& stack, bool on_cursor,
                              std::chrono::steady_clock::duration elapsed,
                              ftxui::Element lead = nullptr,
                              ftxui::Element tail = nullptr);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_INVENTORY_LIST_H_
