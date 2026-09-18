/* TradePanel is the screen two players put things on: your offer at the top
 * left, theirs at the top right, your bag along the bottom.
 *
 * Their window wears no name until they have joined -- one player is on this
 * screen first, every time, and an empty window under their name would read as
 * a trade they had already refused. Their side is MIRRORED: the name, the
 * currencies and the acceptance all hug the right border, so the two offers
 * are read outward from the middle. Only the top row mirrors; right-aligning
 * a column of item names would wreck the scan.
 *
 * What you have put up is held here in your own terms -- an equip by its row
 * in the bag, a stack by name -- rather than read back off the wire. That is
 * what lets the bag below show WHAT IS LEFT: an item on the table is gone from
 * it, a stack shows the rest of itself, and the two always add up to what you
 * own. It is also what the exchange reads to know which items to take away.
 *
 * The panel is a view. It draws the trade it was last handed and asks the
 * connection for nothing: the controller reads what the cursor is on and does
 * the asking.
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

// What a side of a trade can put up beside its items.
enum class TradeCurrency {
  kMeso,
  kSpellTraces,
};

// The three windows, in the order Tab walks them.
enum class TradeZone {
  kMine,
  kTheirs,
  kBag,
};

// What the cursor is standing on, whichever window it is in. The controller
// reads this to know which menu Enter opens and what the answer acts on.
struct TradeCursor {
  enum class Kind {
    // A window with nothing in it to stand on.
    kNothing,
    // One of your own two currencies.
    kCurrency,
    // Your Accept button.
    kAccept,
    // A row of what you have put up, of what they have, or of your bag.
    kOffered,
    kTheirs,
    kBag,
  };
  Kind kind = Kind::kNothing;
  TradeCurrency currency = TradeCurrency::kMeso;
  // For kOffered and kTheirs, the row of that offer. For kBag, the place in
  // the character's OWN list -- the equip tab or the stacks, whichever tab is
  // open -- so a caller never has to walk the panel's filtering again.
  int index = 0;
};

// What this player has put on the table, in their own terms: an equip by its
// row in the bag, a stack by name and how many. Equips lead, in the order they
// were put up, and the stacks follow; that is the order both offer windows are
// drawn in and the row a cursor names.
struct OwnTradeOffer {
  int64_t meso = 0;
  int64_t spell_traces = 0;
  std::vector<int> equips;
  std::vector<TradeStack> stacks;

  int items() const {
    return static_cast<int>(equips.size() + stacks.size());
  }
  // What goes on the wire: the same offer with each equip's whole state in
  // it, so the other end rebuilds the drop rather than a fresh copy of the
  // prototype it names.
  TradeOffer ToWire(const CharacterInstance& character) const;
};

// The entries of the menu Enter raises on a row, in bar order. Which of the
// middle two stands depends on the window the row is in: the bag offers, your
// own side takes back, and theirs is only ever read.
enum TradeMenuItem : int {
  kTradeMenuInspect = 0,
  kTradeMenuOffer = 1,
  kTradeMenuRemove = 2,
  kTradeMenuClose = 3,
};

// Whether a stack may cross at all. Tokens and soul shards are bound to the
// player who earned them, and a spell trace has its own line at the top of
// the offer, so listing it as an item too would be two doors to one thing.
bool Tradeable(const ItemPrototype& proto);

class TradePanel {
 public:
  TradePanel(const CharacterInstance& character,
             const AccountInstance& account);

  // The trade as the panel should draw it. Handed in every frame.
  void SetTrade(const TradeState& trade);
  // An empty table with the cursor on your own meso. Call when the screen
  // opens: the offer is built here, so it must not outlive its trade.
  void Reset();

  // Tab and Shift+Tab, which move between the three windows.
  void NextZone(int delta);
  // Left and Right, which walk the top row of your own window and do nothing
  // anywhere else.
  void MoveCursor(int delta);
  // Up and Down: down into a list, along it, and back out of its top.
  void MoveRow(int delta);

  ftxui::Element Render() const;

  // The menu Enter raises on a row, anchored to it. Quiet on a cursor that is
  // not on a row: a currency and the Accept button are their own actions.
  void OpenMenu();
  void CloseMenu() {
    menu_open_ = false;
  }
  void MoveMenuCursor(int delta);
  bool menu_open() const {
    return menu_open_;
  }
  // Which entry the cursor is on, as a TradeMenuItem.
  int menu_selected() const {
    return menu_.selected();
  }

  TradeZone zone() const {
    return zone_;
  }
  TradeCursor cursor() const;
  // Whether the open bag tab is the Etc one. The controller asks to tell which
  // of the character's two lists a bag cursor points into.
  bool on_etc_tab() const {
    return etc_tab_;
  }

  // What a currency is worth in your hands, and how much of it is already on
  // the table. What the amount overlay opens on.
  int64_t held(TradeCurrency currency) const;
  int64_t offered(TradeCurrency currency) const;
  // How many of the `index`-th stack are not on the table yet, and how many of
  // it are.
  int stack_left(int index) const;
  int stack_offered(int index) const;

  const OwnTradeOffer& own() const {
    return own_;
  }
  void PutUpCurrency(TradeCurrency currency, int64_t amount);
  // Puts the bag's `index`-th equip up, or `count` of its `index`-th stack --
  // a count of zero taking the stack off the table. The table takes as much as
  // the player has: both offer windows scroll.
  void PutUpEquip(int index);
  void PutUpStack(int index, int count);
  // Takes the `row`-th thing you put up back off the table.
  void TakeBack(int row);

 private:
  // One row of an offer window: what it is, and how many of it where a stack
  // says so. An equip leaves the quantity empty, which is what tells the two
  // kinds of row apart.
  struct OfferRow {
    std::string name;
    std::string quantity;
  };

  // One side's window: the top row, then what is on the table.
  ftxui::Element RenderMine() const;
  ftxui::Element RenderTheirs() const;
  // The two currencies, the Accept button and the acceptance mark. `mine`
  // draws them left to right with the button; theirs is the mirror, and has
  // no button of its own.
  ftxui::Element RenderMyTopRow() const;
  ftxui::Element RenderTheirTopRow() const;
  // The Name and Quantity table an offer window holds, padded out to its
  // fixed height. `rows` is what is on the table, already named.
  ftxui::Element RenderOfferTable(const std::vector<OfferRow>& rows, int cursor,
                                  bool focused, ftxui::Box& cursor_box) const;
  // Where `zone` should report the row its cursor is on: the shared box for
  // the window that holds the cursor, and a scratch one for every other.
  ftxui::Box& CursorBox(TradeZone zone) const;
  // What each side has on the table, named. Yours is read off your own bag,
  // theirs off the wire -- which carries the name, so nothing here has to hold
  // a catalog.
  std::vector<OfferRow> MyRows() const;
  std::vector<OfferRow> TheirRows() const;
  // Your bag: the tab bar and the currencies on one row, then the open tab's
  // list of WHAT IS LEFT -- an offered equip is gone from it and an offered
  // stack shows the rest of itself.
  ftxui::Element RenderBag() const;

  // Where the menu hangs: the row of the screen the cursor's own row is drawn
  // on, and the column within whichever window that is.
  int MenuRow() const;
  int MenuColumn() const;

  // The bag rows the open tab draws, as places in the character's own list.
  // The Equip tab drops what is on the table; the Etc tab drops what cannot
  // cross and what is wholly on it.
  std::vector<int> BagRows() const;
  // Where a cursor row lands, clamped to what is there.
  int ClampedRow(int row, int rows) const;
  // Whether the cursor has dropped into the list of the window it is in.
  bool in_list() const;

  const CharacterInstance& character_;
  const AccountInstance& account_;
  TradeState trade_;
  OwnTradeOffer own_;

  TradeZone zone_ = TradeZone::kMine;
  // Where the cursor stands on your own top row: the two currencies, then the
  // Accept button.
  int top_ = 0;
  // The row the cursor is on in each window's list, and whether it is down in
  // your own at all -- the other two windows have nothing above their lists.
  bool own_list_ = false;
  int own_row_ = 0;
  int their_row_ = 0;
  int bag_row_ = 0;
  bool etc_tab_ = false;
  ItemMenu menu_;
  bool menu_open_ = false;
  // When the bag's selection last moved, for sliding a long name under its
  // column, and room for any row under one tab so the two tabs cannot collide
  // in its key.
  static constexpr int kTabStride = 4096;
  mutable SelectionClock name_clock_;
  // Where the bag's tab row and the cursor's row landed, read from the RENDER:
  // a menu opens beside the row the player can see, and a position in the data
  // stops agreeing with that once the list scrolls.
  mutable ftxui::Box bar_box_;
  mutable ftxui::Box cursor_box_;
  mutable ftxui::Box scratch_box_;
  mutable ftxui::Box panel_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_TRADE_PANEL_H_
