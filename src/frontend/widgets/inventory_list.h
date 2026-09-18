/* The bag's lists, as the Inventory panel, the Multi-Sell screen and the trade
 * screen draw them: the tab bar, the Equip and stack rows, and the headers
 * over them. They all show the same items, so a bag is built in one place --
 * which is also where the rule under the tabs and the scrolling that follows
 * the cursor live, so no bag can be missing either.
 *
 * Every row and header takes a `lead` and a `tail`: columns the caller adds on
 * either side, for Multi-Sell's sale mark and price. The bag passes neither.
 *
 * `body_width` holds the row's own cells to that many columns, so a tail lands
 * under the same letters on every list however long the row itself came out.
 * Zero leaves the cells at their natural width.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_INVENTORY_LIST_H_
#define MS_SRC_FRONTEND_WIDGETS_INVENTORY_LIST_H_

#include <chrono>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/account.h"
#include "src/character/character.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/item_columns.h"
#include "src/frontend/widgets/item_row.h"
#include "src/item/item.h"
#include "src/protos/item.pb.h"

namespace ms {

// The bag's tabs, in bar order. Multi-Sell shows every one but the shop.
enum InventoryTab : int {
  kEquipTab = 0,
  // Every currency but meso, read-only: what the player has to spend, rather
  // than anything they can act on. It sits before Etc because a currency is
  // worth more of a glance than a drop is.
  kTokenTab = 1,
  kEtcTab = 2,
  // Not a list of anything the player owns -- it is the door to the shop, and
  // sits last because it is the only tab that leaves the panel.
  kShopTab = 3,
  kNumInventoryTabs = 4,
};

extern const char* const kInventoryTabLabels[kNumInventoryTabs];

// One Equip row: its cells, and the three things that can shut it.
struct InventoryRowState {
  ItemRowText label;
  bool is_trace;
  bool level_ok;
  bool job_ok;
};

// One row per item on the equip tab. `selected` names the row whose name
// slides under its column, and `elapsed` how long it has been selected.
// `columns` is what the panel fitted into its width -- see FitItemColumns.
std::vector<InventoryRowState> BuildEquipRows(
    const CharacterInstance& character, int selected,
    std::chrono::steady_clock::duration elapsed, const ItemColumns& columns);

// The header row over an Equip list drawing `columns`.
ftxui::Element EquipHeader(const ItemColumns& columns,
                           ftxui::Element lead = nullptr,
                           ftxui::Element tail = nullptr, int body_width = 0);

// One Equip row with its cursor caret, drawn dim with the cell that says why
// left bright and red when nothing can be done with the item. `lead` and
// `tail` may be null for a list with no column on that side.
ftxui::Element RenderEquipRow(const InventoryRowState& row, bool on_cursor,
                              ftxui::Element lead = nullptr,
                              ftxui::Element tail = nullptr,
                              int body_width = 0);

// The header over the Token tab, and one row of it. The tab is two lists side
// by side -- the shop's currencies, then the bosses' soul shards -- so a row
// carries one of each, and either may be null where that column has run out.
// Nothing is ever selected there, so neither takes a cursor.
ftxui::Element CurrencyHeader();
ftxui::Element RenderCurrencyRow(const StackableItem* token,
                                 const StackableItem* shard);

// The two balances the bag's tab bar carries down its middle: meso, and the
// spell traces beside it once the shop is open -- the level a trace can first
// be bought, and so the first level a balance in them can exist. Both are
// passed rather than read off the character: a bag being traded from shows
// what is LEFT of each.
ftxui::Element RenderBalances(int64_t meso, int64_t spell_traces,
                              const CharacterInstance& character,
                              const AccountInstance& account);

// The bag's tab row and the rule under it: the chips, `balances` down the
// middle, and `trailing` right-aligned past them -- the Expand door on the
// main screen, nothing in a trade. The balances are centred except where that
// would stand them against the last chip, and are then pushed right to keep a
// gutter clear; MEASURED rather than left to a pair of fillers, so the gutter
// is a promise rather than a ratio.
//
// `active` is -1 for a bar with nothing on it highlighted. `bar_box` is
// reflected, so a menu opening under the row knows where it is.
ftxui::Element RenderBagTabBar(const std::vector<TabSpec>& tabs, int active,
                               ftxui::Element balances, bool row_selected,
                               bool highlighted, ftxui::Element trailing,
                               int width, ftxui::Box& bar_box);

// A Name/Quantity list of `rows` (places in `stacks`), a "> " cursor on the
// `selected`-th, over a rule under the header. An empty tab is "(empty)" with
// no header: column names are there to tell rows apart, and there are none.
//
// The selected row is marked with ftxui::focus WHETHER OR NOT the list holds
// focus -- that is what the frame scrolls to, so the cursor cannot walk out of
// view and the view does not jump on the way back -- and reflected into
// `cursor_box`, so a menu knows the row to open beside.
ftxui::Element RenderStackList(const std::vector<StackableItem>& stacks,
                               const std::vector<int>& rows, int selected,
                               bool focused, ftxui::Box& cursor_box,
                               bool highlighted,
                               std::chrono::steady_clock::duration elapsed);

// The same for the Equip tab, for a caller drawing plain rows rather than an
// ftxui::Menu. `rows` are places in the character's equip tab, so a bag may
// hold some of it back.
ftxui::Element RenderEquipList(const CharacterInstance& character,
                               const std::vector<int>& rows, int selected,
                               bool focused, const ItemColumns& columns,
                               ftxui::Box& cursor_box, bool highlighted,
                               std::chrono::steady_clock::duration elapsed);

// The header over an Etc list, and one row of one.
ftxui::Element StackHeader(ftxui::Element lead = nullptr,
                           ftxui::Element tail = nullptr, int body_width = 0);
ftxui::Element RenderStackRow(const StackableItem& stack, bool on_cursor,
                              std::chrono::steady_clock::duration elapsed,
                              ftxui::Element lead = nullptr,
                              ftxui::Element tail = nullptr,
                              int body_width = 0);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_INVENTORY_LIST_H_
