/* MultiSellPanel is the screen for selling many items at once. It shows the bag
 * (Equip and Etc, without the shop tab) with a sale mark on the left of every
 * row and what that row sells for on the right. The running total is in the tab
 * header beside the player's meso, and [Confirm] at the bottom opens an "Are
 * you sure?" dialog over the list.
 *
 * The panel marks and totals but never sells. Reset() opens it on the row the
 * player chose Multi-Sell on, with that row already marked. OnEvent returns the
 * ConfirmChoice every dialog returns, and the caller passes basket() to
 * SellBasket().
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

// What the player has marked for sale: inventory indices for equips, and
// indices into the character's stacks for Etc rows. The screen is modal and
// nothing changes under it, so an index is enough to identify an item.
struct SaleBasket {
  std::set<int> equips;
  std::set<int> etc;

  const std::set<int>& For(int tab) const;
  std::set<int>& For(int tab);
  bool empty() const;
};

// What one basket entry sells for: the whole stack for a stackable, and nothing
// for the trace of a destroyed item, which is a record of an item rather than
// an item. `item` is what the basket holds: an inventory index on Equip, a
// stack index on Etc.
int64_t RowSellValue(const CharacterInstance& character, int tab, int item);

// What everything in `basket` sells for, without selling anything.
int64_t BasketTotal(const CharacterInstance& character,
                    const SaleBasket& basket);

// Sells everything in `basket` and returns the meso received. Rows are sold
// from last to first for two reasons: removing a row shifts every row after it,
// so going forwards would sell the wrong items, and the buyback shelf lists the
// newest first, so reverse order leaves it in bag order.
int64_t SellBasket(CharacterInstance& character, const SaleBasket& basket);

class MultiSellPanel {
 public:
  MultiSellPanel(const CharacterInstance& character,
                 const AccountInstance& account);

  // Opens the screen on `tab` with `item` marked, since what the player chose
  // Multi-Sell on starts in the basket. `item` is an inventory index on Equip
  // and a stack index on Etc: what the basket holds, not the row it is drawn
  // on.
  void Reset(int tab, int item);
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

 private:
  // The vertical focus zones, as one ring: the tab bar at the top, the rows
  // below it, and the buttons at the bottom.
  enum Zone { kZoneTabs, kZoneList, kZoneButtons };

  // The number of rows on the active tab.
  int ListCount() const;
  // The cursor's position in the ring: the tab bar is stop 0, the rows are the
  // stops after it, and the button row is last.
  int CursorStop() const;
  void MoveCursor(int delta);
  // Moves one tab along the bar, stopping at the ends as in the bag.
  void StepTab(int direction);
  // Marks or unmarks the row under the cursor.
  void ToggleMark();
  // Whether `row` is a real row. Everything in the bag can be sold, so nothing
  // else keeps a row out of the basket.
  bool Markable(int row) const;
  // The basket key for the `row`-th row of the active tab: the row itself on
  // Equip, and the stack it shows on Etc, which lists only some of them.
  int BasketKey(int row) const;

  // The tab bar, with the player's meso and the running total beside it.
  ftxui::Element RenderHeader() const;
  ftxui::Element RenderList();
  ftxui::Element RenderEquipTab();
  ftxui::Element RenderStackTab();
  // The mark column of `row`, and the price column beside it.
  ftxui::Element MarkCell(int row) const;
  ftxui::Element PriceCell(int row) const;

  const CharacterInstance& character_;
  // The account decides which upgrade columns the list draws, not the item: a
  // mechanic the player hasn't unlocked gets no column here, as in the bag.
  const AccountInstance& account_;
  SaleBasket basket_;
  Zone zone_ = kZoneList;
  int active_tab_ = kEquipTab;
  int selected_ = 0;
  bool cancel_focused_ = false;
  ConfirmPrompt confirm_;
  // When the selection last moved, for scrolling a long name.
  SelectionClock name_clock_;
  std::vector<InventoryRowState> rows_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_MULTI_SELL_PANEL_H_
