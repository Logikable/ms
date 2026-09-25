/* The bag's lists as the Inventory panel, the Multi-Sell screen and the trade
 * screen draw them: the tab bar, the Equip and stack rows, and their headers.
 * All three show the same items, so they share this code, including the rule
 * under the tabs and the scrolling that follows the cursor.
 *
 * Every row and header takes a `lead` and a `tail`: extra columns the caller
 * adds on either side, such as Multi-Sell's sale mark and price. The bag passes
 * neither.
 *
 * `body_width` pads the row's own cells to that many columns, so a tail lines
 * up on every list however long the row is. Zero leaves the cells at their
 * natural width.
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
#include "src/item/currency.h"
#include "src/item/inventory.h"
#include "src/item/item.h"
#include "src/protos/item.pb.h"

namespace ms {

// The bag's tabs, in bar order. Multi-Sell shows all but the shop.
enum InventoryTab : int {
  kEquipTab = 0,
  // Every currency except meso, read-only. It comes before Etc because
  // currencies are worth a glance more than drops are.
  kTokenTab = 1,
  kEtcTab = 2,
  // These two don't list anything the player owns. Both open other screens, so
  // they come last. The shop comes first because a character unlocks it
  // earlier.
  kShopTab = 3,
  kBankTab = 4,
  kNumInventoryTabs = 5,
};

extern const char* const kInventoryTabLabels[kNumInventoryTabs];

// One Equip row: its cells, and the three things that can block it.
struct InventoryRowState {
  ItemRowText label;
  bool is_trace;
  bool level_ok;
  bool job_ok;
};

// One row per item in `items`, which is a bag's Equip tab or the bank's.
// `character` is the viewer: the level and job cells say whether they could
// wear the item, wherever it is stored. `selected` is the row whose name
// scrolls, and `elapsed` how long it has been selected. `columns` is what
// fitted the panel's width (see FitItemColumns).
std::vector<InventoryRowState> BuildEquipRows(
    const CharacterInstance& character, const InventoryInstance& items,
    int selected, std::chrono::steady_clock::duration elapsed,
    const ItemColumns& columns);

// The header row over an Equip list drawing `columns`.
ftxui::Element EquipHeader(const ItemColumns& columns,
                           ftxui::Element lead = nullptr,
                           ftxui::Element tail = nullptr, int body_width = 0);

// One Equip row with its cursor caret. A row the player can't act on is dimmed,
// with the cell that says why kept bright red. `lead` and `tail` may be null.
ftxui::Element RenderEquipRow(const InventoryRowState& row, bool on_cursor,
                              ftxui::Element lead = nullptr,
                              ftxui::Element tail = nullptr,
                              int body_width = 0);

// The Token tab's header and one row. The tab shows two lists side by side, the
// shop's currencies and the bosses' soul shards, so a row holds one of each,
// and either may be null once its list runs out. Nothing is selectable there,
// so neither takes a cursor.
ftxui::Element CurrencyHeader();
ftxui::Element RenderCurrencyRow(const CurrencyAmount* token,
                                 const CurrencyAmount* shard);

// Which balance a bar's cursor is on. Only the bank screen lets the cursor land
// on them. Elsewhere they are read-only.
enum BalanceCell : int {
  // A bar whose balances are read-only. They take their natural width, so the
  // bag's bar doesn't reserve room for digits nobody has yet.
  kBalancesReadOnly = -2,
  // A bar whose balances can be selected, with the cursor on neither.
  kNoBalance = -1,
  kMesoBalance = 0,
  kTraceBalance = 1,
};

// The two balances in the middle of the bag's tab bar: meso, and spell traces
// once the shop opens. That is the level traces can first be bought, so a trace
// balance can't exist before it. Both are passed in rather than read from the
// character, so a bag in a trade can show what is left.
//
// Any `cursor` other than kBalancesReadOnly gives both cells a fixed width and
// bands the one `cursor` names. The band then stays the same size, a growing
// number doesn't move its neighbour, and two bars on one screen line up. It
// uses a band instead of inverting because the cells are in the theme colour,
// and inverting would make that the background.
ftxui::Element RenderBalances(int64_t meso, int64_t spell_traces,
                              const CharacterInstance& character,
                              const AccountInstance& account,
                              int cursor = kBalancesReadOnly);

// The bag's tab row and the rule under it: the chips, `balances` in the middle,
// and `trailing` on the right (the Expand button on the main screen, nothing in
// a trade). The balances are centred unless that would put them against the
// last chip. Then they move right to keep a gutter. The gap is measured rather
// than left to fillers, so the gutter is guaranteed.
//
// `active` is -1 when no chip is highlighted. `bar_box` is reflected so a menu
// opening under the row knows where it is.
ftxui::Element RenderBagTabBar(const std::vector<TabSpec>& tabs, int active,
                               ftxui::Element balances, bool row_selected,
                               bool highlighted, ftxui::Element trailing,
                               int width, ftxui::Box& bar_box);

// Every index in a list of `count`, for a caller that shows all of it.
std::vector<int> AllRows(int count);

// A Name/Quantity list of `rows` (indices into `stacks`), with a "> " cursor on
// the `selected`-th and a rule under the header. An empty tab shows "(empty)"
// with no header.
//
// The selected row gets ftxui::focus even when the list doesn't have focus. The
// frame scrolls to it, so the cursor can't leave the view and the view doesn't
// jump when focus returns. It is also reflected into `cursor_box` so a menu
// knows which row to open beside.
ftxui::Element RenderStackList(const std::vector<StackableItem>& stacks,
                               const std::vector<int>& rows, int selected,
                               bool focused, ftxui::Box& cursor_box,
                               bool highlighted,
                               std::chrono::steady_clock::duration elapsed);

// The same for the Equip tab, for a caller drawing plain rows instead of an
// ftxui::Menu. `rows` are indices into `items`, so a bag can leave some out.
ftxui::Element RenderEquipList(const CharacterInstance& character,
                               const InventoryInstance& items,
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
