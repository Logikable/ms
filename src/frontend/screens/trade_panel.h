/* TradePanel is the screen where two players offer items: your offer at the top
 * left, theirs at the top right, and your bag along the bottom.
 *
 * Their window has no name until they join. One player always reaches this
 * screen first, and an empty window under the other player's name would look
 * like a trade they had already refused. Their side is mirrored: the name, the
 * currencies and the acceptance all sit against the right border, so the two
 * offers read outward from the middle. Only the top row is mirrored, since
 * right-aligning a column of item names would make it hard to read.
 *
 * What you have offered is kept here in your own terms (an equip by its row in
 * the bag, a stack by name) rather than read back from the network. That lets
 * the bag below show what is left: an offered item is gone from it, a stack
 * shows the rest of itself, and the two always add up to what you own. The
 * exchange also reads it to know which items to remove.
 *
 * The panel only displays. It draws the last trade state it was given and never
 * asks the connection for anything: the controller reads what the cursor is on
 * and does the asking.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_TRADE_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_TRADE_PANEL_H_

#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "src/account.h"
#include "src/character/character.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/frontend/widgets/marquee.h"
#include "src/item/item.h"
#include "src/protos/item.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// What each side can offer besides items.
enum class TradeCurrency {
  kMeso,
  kSpellTraces,
};

// The three windows, in the order Tab moves through them.
enum class TradeZone {
  kMine,
  kTheirs,
  kBag,
};

// What the cursor is on, in whichever window. The controller reads this to know
// which menu Enter opens and what the choice applies to.
struct TradeCursor {
  enum class Kind {
    // A window with nothing in it to select.
    kNothing,
    // One of your own two currencies.
    kCurrency,
    // Your Accept button.
    kAccept,
    // A row of your offer, their offer, or your bag.
    kOffered,
    kTheirs,
    kBag,
  };
  Kind kind = Kind::kNothing;
  TradeCurrency currency = TradeCurrency::kMeso;
  // For kOffered and kTheirs, the row of that offer. For kBag, the position in
  // the character's own list (the equip tab or the stacks, whichever tab is
  // open), so a caller never has to redo the panel's filtering.
  int index = 0;
};

// What this player has offered, in their own terms: an equip by its row in the
// bag, a stack by name and count. Equips come first, in the order offered, then
// stacks. Both offer windows are drawn in this order, and a cursor row refers
// to it.
struct OwnTradeOffer {
  int64_t meso = 0;
  int64_t spell_traces = 0;
  std::vector<int> equips;
  std::vector<TradeStack> stacks;

  int items() const {
    return static_cast<int>(equips.size() + stacks.size());
  }
  // What is sent over the network: the same offer with each equip's full state,
  // so the other end rebuilds the actual item rather than a fresh copy of its
  // prototype.
  TradeOffer ToWire(const CharacterInstance& character) const;
};

// The entries of the menu Enter opens on a row, in display order. Which of the
// middle two appears depends on the window: the bag offers, your own side takes
// back, and theirs can only be inspected.
enum TradeMenuItem : int {
  kTradeMenuInspect = 0,
  kTradeMenuOffer = 1,
  kTradeMenuRemove = 2,
  kTradeMenuClose = 3,
};

class TradePanel {
 public:
  TradePanel(const CharacterInstance& character,
             const AccountInstance& account);

  // The trade as the panel should draw it. Passed in every frame.
  void SetTrade(const TradeState& trade);
  // An empty table with the cursor on your own meso. Call when the screen
  // opens, since the offer is built here and must not outlive its trade.
  void Reset();

  // Tab and Shift+Tab, which move between the three windows.
  void NextZone(int delta);
  // Left and Right, which move along the top row of your own window and do
  // nothing elsewhere.
  void MoveCursor(int delta);
  // Up and Down: into a list, along it, and back out at its top.
  void MoveRow(int delta);

  ftxui::Element Render() const;

  // Opens the menu for a row, placed beside it. Does nothing when the cursor
  // isn't on a row, since a currency and the Accept button are actions
  // themselves.
  void OpenMenu();
  void CloseMenu() {
    menu_open_ = false;
  }
  void MoveMenuCursor(int delta);
  bool menu_open() const {
    return menu_open_;
  }
  // The menu entry under the cursor, as a TradeMenuItem.
  int menu_selected() const {
    return menu_.selected();
  }

  TradeZone zone() const {
    return zone_;
  }
  TradeCursor cursor() const;
  // Whether the open bag tab is Etc. The controller checks this to know which
  // of the character's two lists a bag cursor refers to.
  bool on_etc_tab() const {
    return etc_tab_;
  }

  // How much of a currency you have, and how much is already offered. The
  // amount dialog starts from this.
  int64_t held(TradeCurrency currency) const;
  int64_t offered(TradeCurrency currency) const;
  // How many of the `index`-th stack aren't offered yet, and how many are.
  int stack_left(int index) const;
  int stack_offered(int index) const;

  const OwnTradeOffer& own() const {
    return own_;
  }
  void PutUpCurrency(TradeCurrency currency, int64_t amount);
  // Offers the bag's `index`-th equip, or `count` of its `index`-th stack; a
  // count of zero removes the stack from the offer. There is no limit on how
  // much can be offered, since both offer windows scroll.
  void PutUpEquip(int index);
  void PutUpStack(int index, int count);
  // Removes the `row`-th item you offered.
  void TakeBack(int row);

 private:
  // One row of an offer window: the item, and the quantity for a stack. An
  // equip leaves the quantity empty, which is what distinguishes the two kinds
  // of row.
  struct OfferRow {
    std::string name;
    std::string quantity;
  };

  // One side's window: the top row, then what is offered.
  ftxui::Element RenderMine() const;
  ftxui::Element RenderTheirs() const;
  // The two currencies, the Accept button and the acceptance mark. Your own row
  // draws them left to right with the button; theirs is mirrored and has no
  // button.
  ftxui::Element RenderMyTopRow() const;
  ftxui::Element RenderTheirTopRow() const;
  // The Name and Quantity table in an offer window, padded to its fixed height.
  // `rows` is what is offered, already named.
  ftxui::Element RenderOfferTable(const std::vector<OfferRow>& rows, int cursor,
                                  bool focused, ftxui::Box& cursor_box) const;
  // Where `zone` should report its cursor row: the shared box for the window
  // with the cursor, and a scratch box for the others.
  ftxui::Box& CursorBox(TradeZone zone) const;
  // What each side has offered, named. Yours is read from your own bag, and
  // theirs from the network, which includes the names, so nothing here needs a
  // catalog.
  std::vector<OfferRow> MyRows() const;
  std::vector<OfferRow> TheirRows() const;
  // Your bag: the tab bar and the currencies on one row, then the open tab's
  // list of what is left. An offered equip is gone from it, and an offered
  // stack shows the rest of itself.
  ftxui::Element RenderBag() const;

  // Where the menu opens: the screen row of the cursor's row, and the column
  // within whichever window that is.
  int MenuRow() const;
  int MenuColumn() const;

  // The bag rows the open tab draws, as positions in the character's own list.
  // The Equip tab leaves out what is offered; the Etc tab leaves out what can't
  // be traded and what is entirely offered.
  std::vector<int> BagRows() const;
  // The cursor row, clamped to the rows that exist.
  int ClampedRow(int row, int rows) const;
  // Whether the cursor has moved into the list of its window.
  bool in_list() const;

  const CharacterInstance& character_;
  const AccountInstance& account_;
  TradeState trade_;
  OwnTradeOffer own_;

  TradeZone zone_ = TradeZone::kMine;
  // The cursor's position on your own top row: the two currencies, then the
  // Accept button.
  int top_ = 0;
  // The cursor's row in each window's list, and whether it is in your own list
  // at all (the other two windows have nothing above their lists).
  bool own_list_ = false;
  int own_row_ = 0;
  int their_row_ = 0;
  int bag_row_ = 0;
  bool etc_tab_ = false;
  ItemMenu menu_;
  bool menu_open_ = false;
  // For the bag's selection clock, which scrolls a long name: larger than any
  // row index on one tab, so the two tabs can't collide in its key.
  static constexpr int kTabStride = 4096;
  mutable SelectionClock name_clock_;
  // Where the bag's tab row and the cursor's row were drawn, read from the
  // render. A menu opens beside the row the player can see, and a position in
  // the data stops matching that once the list scrolls.
  mutable ftxui::Box bar_box_;
  mutable ftxui::Box cursor_box_;
  mutable ftxui::Box scratch_box_;
  mutable ftxui::Box panel_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_TRADE_PANEL_H_
