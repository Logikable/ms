/* BankPanel is the screen for moving items between a character and the account:
 * the character's bag on top and the account's bank below it.
 *
 * The two halves are drawn the same way with the same widgets, because they
 * have the same two tabs, Equip and Etc. Tab and Shift+Tab move between the
 * halves, and each half keeps its own open tab and row, so coming back lands
 * where you left.
 *
 * Each half's top row is a ring of four stops: the two tab chips, then meso and
 * spell traces. Moving onto a chip opens that tab. Enter on a balance asks how
 * much to move. Down moves into the open tab's list, and Up from its first row
 * returns to the chip of the tab being shown.
 *
 * The panel shows both containers and moves items between them, but it opens no
 * dialogs itself: the controller reads the cursor and does the asking.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BANK_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BANK_PANEL_H_

#include <chrono>
#include <cstdint>
#include <map>
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

namespace ms {

// The two halves, in the order Tab moves through them.
enum class BankZone {
  kBag,
  kBank,
};

// The two balances, which move on a line of their own instead of as a row.
enum class BankCurrency {
  kMeso,
  kSpellTraces,
};

// What the cursor is on, in whichever half.
struct BankCursor {
  enum class Kind {
    // A tab chip. Enter opens the {Sort, Close} menu.
    kTab,
    kCurrency,
    // A row of the open tab, with `index` its position in that half's list.
    kRow,
    // The open tab has nothing to select.
    kNothing,
  };
  Kind kind = Kind::kTab;
  BankCurrency currency = BankCurrency::kMeso;
  int index = 0;
};

// The entries of the menu Enter opens on a row.
enum BankMenuItem : int {
  kBankMenuInspect = 0,
  kBankMenuMove = 1,
  kBankMenuClose = 2,
};

// The entries of the menu Enter opens on a tab chip, which is about the tab
// rather than anything in it.
enum BankTabMenuItem : int {
  kBankTabMenuSort = 0,
  kBankTabMenuClose = 1,
};

class BankPanel {
 public:
  // `items` is the item catalog, which the spell trace balance needs: a purse
  // is keyed by prototype, and a character with none of a currency has no copy
  // of it to hand over.
  BankPanel(CharacterInstance& character, AccountInstance& account,
            const std::map<std::string, ItemPrototype>& items);

  // Both halves on their Equip tab, with the cursor on the bag's chip.
  void Reset();

  // Tab and Shift+Tab, which switch halves; with two halves both keys do the
  // same thing. Left and Right move along a half's top row, and Up and Down
  // move into and along its list.
  void NextZone();
  void MoveCursor(int delta);
  void MoveRow(int delta);

  BankZone zone() const {
    return zone_;
  }
  BankCursor cursor() const;
  // Whether the half with the cursor is showing its Etc tab.
  bool on_etc_tab() const;

  ftxui::Element Render() const;

  // Moves the item under the cursor to the other half, and returns the message
  // to show when it can't: one half is full, or the item is bound to the
  // character holding it. Empty on success, and when no item is under the
  // cursor.
  std::string MoveSelected();
  // Moves `amount` of a currency from the half with the cursor to the other,
  // clamped to what that half has.
  void MoveCurrency(BankCurrency currency, int64_t amount);
  // How much of `currency` the half with the cursor has, which the amount
  // dialog starts from.
  int64_t held(BankCurrency currency) const;
  // Sorts the open tab of the half with the cursor.
  void SortActiveTab();

  // The menu Enter opens on a row, and the {Sort, Close} menu it opens on a
  // chip. They are never open at once, so one flag says which is up.
  void OpenMenu();
  void OpenTabMenu();
  void CloseMenu() {
    menu_open_ = false;
    tab_menu_open_ = false;
  }
  void MoveMenuCursor(int delta);
  bool menu_open() const {
    return menu_open_;
  }
  bool tab_menu_open() const {
    return tab_menu_open_;
  }
  // The entry under the open menu's cursor.
  int menu_selected() const;
  // The item under the cursor, for the inspect card. Null on anything else.
  const EquipTabItem* selected_equip() const;
  const StackableItem* selected_stack() const;

 private:
  // One half's own state: which tab it shows, where its cursor is on the top
  // row, and which row it was last on.
  struct Half {
    bool etc_tab = false;
    // A stop on the top row: the two chips, then the two balances.
    int top = 0;
    int row = 0;
    // Whether the cursor has moved from the top row into the list.
    bool in_list = false;
  };

  Half& half(BankZone zone);
  const Half& half(BankZone zone) const;
  const Half& here() const {
    return half(zone_);
  }
  Half& here() {
    return half(zone_);
  }

  // The number of rows in the named half's open tab.
  int RowCount(BankZone zone) const;
  // The cursor row, clamped to the rows that exist.
  int ClampedRow(BankZone zone) const;

  // The two parts of MoveSelected, one per tab.
  std::string MoveEquip();
  std::string MoveStack();

  // One half as a bordered window: its top row, then the open tab's list.
  ftxui::Element RenderHalf(BankZone zone) const;
  // That half's top row, and the list below it.
  ftxui::Element RenderTopRow(BankZone zone) const;
  ftxui::Element RenderList(BankZone zone) const;

  // Where the open menu goes: the screen row of the cursor's row, or the row
  // below the top row when the cursor is there, and the column within that
  // half.
  int MenuRow() const;
  int MenuColumn() const;
  // Where `zone` should report its cursor row: the shared box for the half with
  // the cursor, and a scratch box for the other.
  ftxui::Box& CursorBox(BankZone zone) const;

  CharacterInstance& character_;
  AccountInstance& account_;
  // The spell trace prototype, looked up once. Moving the balance between two
  // purses needs it, and a purse holding none has no copy.
  const ItemPrototype* spell_trace_ = nullptr;

  BankZone zone_ = BankZone::kBag;
  Half bag_;
  Half bank_;

  ItemMenu menu_;
  ItemMenu tab_menu_;
  bool menu_open_ = false;
  bool tab_menu_open_ = false;

  // For the selection clock, which scrolls a long name: large enough that every
  // row of either tab of either half gets its own key.
  static constexpr int kHalfStride = 4096;
  mutable SelectionClock name_clock_;
  // Where the cursor's row and the half's top row were drawn, read from the
  // render. A menu opens beside the row the player can see, and a position in
  // the data stops matching that once the list scrolls.
  mutable ftxui::Box cursor_box_;
  mutable ftxui::Box scratch_box_;
  mutable ftxui::Box bar_box_;
  mutable ftxui::Box panel_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BANK_PANEL_H_
