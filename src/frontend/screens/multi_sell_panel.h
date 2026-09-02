/* MultiSellPanel is the screen for selling many items at once. It shows the
 * bag -- Equip, Use and Etc, the shop tab left out -- with a sale mark down
 * the left of every row and what that row pays down the right. The running
 * total sits in the tab header beside the player's meso, and [Confirm] at the
 * foot opens an "Are you sure?" dialog over the list.
 *
 * The panel marks and totals; it never sells. Reset() opens it on the row the
 * player chose Multi-Sell on, with that row already marked, OnEvent answers
 * with the ConfirmChoice every dialog in the game answers with, and the caller
 * hands basket() to SellBasket().
 */
#ifndef MS_SRC_FRONTEND_SCREENS_MULTI_SELL_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_MULTI_SELL_PANEL_H_

#include <cstdint>
#include <set>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/account.h"
#include "src/character/character.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/inventory_list.h"
#include "src/frontend/widgets/marquee.h"

namespace ms {

// What the player has marked for sale. Equip rows are inventory indices, Use
// and Etc rows indices into that category's stacks. The screen is modal and
// nothing moves under it, so a row index is identity enough.
struct SaleBasket {
  std::set<int> equips;
  std::set<int> use;
  std::set<int> etc;

  const std::set<int>& For(int tab) const;
  std::set<int>& For(int tab);
  bool empty() const;
};

// What one row pays if it is sold: the whole stack for a stackable, and
// nothing at all for a spell trace, which is a record of an item rather than
// one.
int64_t RowSellValue(const CharacterInstance& character, int tab, int row);

// What everything in `basket` pays, without selling any of it.
int64_t BasketTotal(const CharacterInstance& character,
                    const SaleBasket& basket);

// Sells everything in `basket` and returns the meso it paid.
//
// The rows go out back to front -- Etc, then Use, then Equip, each descending
// -- for two reasons. Removing a row shifts every row after it, so a sale that
// walked forwards would sell the wrong items; and the buy-back shelf lists the
// newest sale first, so selling in reverse leaves the shelf reading Equip,
// Use, Etc, each in bag order, from the top.
int64_t SellBasket(CharacterInstance& character, const SaleBasket& basket);

class MultiSellPanel {
 public:
  MultiSellPanel(const CharacterInstance& character,
                 const AccountInstance& account);

  // Opens the screen on `tab` with `row` marked: the item the player chose
  // Multi-Sell on is the one thing in the basket to begin with.
  void Reset(int tab, int row);
  ftxui::Element Render();
  // The "Are you sure?" dialog, drawn by the caller over the list.
  ftxui::Element RenderConfirm() const;
  ConfirmChoice OnEvent(ftxui::Event event);

  const SaleBasket& basket() const {
    return basket_;
  }
  int64_t Total() const;
  bool confirming() const {
    return confirm_.open();
  }
  // Each returns true once: the player went through with the sale, or left
  // without one.

 private:
  // The vertical focus zones, as one ring: the tab bar on top, the rows below
  // it, the buttons at the foot.
  enum Zone { kZoneTabs, kZoneList, kZoneButtons };

  // Rows on the active tab.
  int ListCount() const;
  // Where the cursor stands in the ring: the tab bar is stop 0, the rows are
  // the stops after it, and the button row is the last.
  int CursorStop() const;
  void MoveCursor(int delta);
  // Steps one tab along the bar. The ends are walls, as in the bag.
  void StepTab(int direction);
  // Marks or unmarks the row under the cursor.
  void ToggleMark();
  // Whether `row` is a row at all. Everything the bag holds can be sold, so
  // nothing else stands between a row and the basket.
  bool Markable(int row) const;

  // The tab bar, with the player's meso and the running total beside it.
  ftxui::Element RenderHeader() const;
  ftxui::Element RenderList();
  ftxui::Element RenderEquipTab();
  ftxui::Element RenderStackTab();
  // The mark column of `row`, and the price column beside it.
  ftxui::Element MarkCell(int row) const;
  ftxui::Element PriceCell(int row) const;

  const CharacterInstance& character_;
  // Which upgrade columns the list draws is the account's answer, not the
  // item's: a mechanic the player has not met is not shown a column here
  // any more than it is in the bag.
  const AccountInstance& account_;
  SaleBasket basket_;
  Zone zone_ = kZoneList;
  int active_tab_ = kEquipTab;
  int selected_ = 0;
  bool cancel_focused_ = false;
  ConfirmPrompt confirm_;
  // When the selection last moved, for sliding a long name under its column.
  SelectionClock name_clock_;
  std::vector<InventoryRowState> rows_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_MULTI_SELL_PANEL_H_
