/* BankPanel is the screen an item crosses between a character and the
 * account: the character's bag on top, the account's bank below it.
 *
 * The two halves are drawn the same way and out of the same widgets, because
 * they hold the same two tabs -- an Equip tab and an Etc tab, 128 slots
 * apiece. Tab and Shift+Tab move between them, and each half keeps its own
 * open tab and its own row, so coming back lands where you left.
 *
 * Each half's top row is a ring of four stops: the two tab chips, then the
 * meso and the spell traces. Landing on a chip opens that tab; Enter on a
 * balance asks how much of it to move. Down drops into the open tab's list and
 * Up off its first row comes back to the chip of the tab being shown.
 *
 * The panel is a view over both containers and moves things between them, but
 * it raises no dialog of its own: the controller reads the cursor and does the
 * asking.
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

// The two halves, in the order Tab walks them.
enum class BankZone {
  kBag,
  kBank,
};

// The two balances that cross on a line of their own rather than as a row.
enum class BankCurrency {
  kMeso,
  kSpellTraces,
};

// What the cursor is standing on, whichever half it is in.
struct BankCursor {
  enum class Kind {
    // A tab chip. Enter opens the {Sort, Close} menu.
    kTab,
    kCurrency,
    // A row of the open tab, `index` naming its place in that half's own list.
    kRow,
    // The open tab has nothing to stand on.
    kNothing,
  };
  Kind kind = Kind::kTab;
  BankCurrency currency = BankCurrency::kMeso;
  int index = 0;
};

// The entries of the menu Enter raises on a row.
enum BankMenuItem : int {
  kBankMenuInspect = 0,
  kBankMenuMove = 1,
  kBankMenuClose = 2,
};

// And of the one it raises on a tab chip, which is about the tab rather than
// about anything on it.
enum BankTabMenuItem : int {
  kBankTabMenuSort = 0,
  kBankTabMenuClose = 1,
};

class BankPanel {
 public:
  // `items` is the item catalog, which the spell trace balance needs: a
  // purse is keyed by prototype and a character with none of a currency has
  // no copy of it to hand over.
  BankPanel(CharacterInstance& character, AccountInstance& account,
            const std::map<std::string, ItemPrototype>& items);

  // Both halves on their Equip tab, the cursor on the bag's chip.
  void Reset();

  // Tab and Shift+Tab, which swap the halves -- with two of them the two keys
  // do the same thing. Left and Right walk a half's top row, Up and Down go
  // into its list and along it.
  void NextZone();
  void MoveCursor(int delta);
  void MoveRow(int delta);

  BankZone zone() const {
    return zone_;
  }
  BankCursor cursor() const;
  // Whether the half holding the cursor is showing its Etc tab.
  bool on_etc_tab() const;

  ftxui::Element Render() const;

  // Moves what the cursor is on to the other half, and returns the sentence
  // to raise when it could not go: one of the two halves full, or an item
  // bound to the character holding it. Empty on success, and on a cursor with
  // no item under it.
  std::string MoveSelected();
  // Moves `amount` of a currency from the half holding the cursor to the
  // other. Clamped to what that half has.
  void MoveCurrency(BankCurrency currency, int64_t amount);
  // What the half holding the cursor has of `currency`, which is what the
  // amount dialog opens on.
  int64_t held(BankCurrency currency) const;
  // Files the open tab of the half holding the cursor.
  void SortActiveTab();

  // The menu Enter raises on a row, and the {Sort, Close} one it raises on a
  // chip. Never open at once, so which is up is one flag.
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
  // Which entry the open menu's cursor is on.
  int menu_selected() const;
  // The item the cursor is on, for the inspect card. Null on anything else.
  const EquipTabItem* selected_equip() const;
  const StackableItem* selected_stack() const;

 private:
  // One half's own state: which tab it shows, where its cursor stands on the
  // top row, and which row it last stood on.
  struct Half {
    bool etc_tab = false;
    // A stop on the top row: the two chips, then the two balances.
    int top = 0;
    int row = 0;
    // Whether the cursor has dropped out of the top row into the list.
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

  // Rows the named half's open tab has.
  int RowCount(BankZone zone) const;
  // Where a cursor row lands, clamped to what is there.
  int ClampedRow(BankZone zone) const;

  // The two halves of MoveSelected, one per tab.
  std::string MoveEquip();
  std::string MoveStack();

  // One half, as a bordered window: its top row, then the open tab's list.
  ftxui::Element RenderHalf(BankZone zone) const;
  // That half's top row, and the list under it.
  ftxui::Element RenderTopRow(BankZone zone) const;
  ftxui::Element RenderList(BankZone zone) const;

  // Where the open menu hangs: the screen row of the cursor's own row, or the
  // row under the top row when the cursor is up there, and the column within
  // whichever half that is.
  int MenuRow() const;
  int MenuColumn() const;
  // Where `zone` should report its cursor row: the shared box for the half
  // holding the cursor, and a scratch one for the other.
  ftxui::Box& CursorBox(BankZone zone) const;

  CharacterInstance& character_;
  AccountInstance& account_;
  // The spell trace prototype, looked up once: a balance moving between two
  // purses needs it, and neither purse has a copy while it holds none.
  const ItemPrototype* spell_trace_ = nullptr;

  BankZone zone_ = BankZone::kBag;
  Half bag_;
  Half bank_;

  ItemMenu menu_;
  ItemMenu tab_menu_;
  bool menu_open_ = false;
  bool tab_menu_open_ = false;

  // When the selection last moved, for sliding a long name under its column.
  // Room for any row of either tab of either half in the one key.
  static constexpr int kHalfStride = 4096;
  mutable SelectionClock name_clock_;
  // Where the cursor's row and the half's top row landed, read from the
  // RENDER: a menu opens beside the row the player can see, and a position in
  // the data stops agreeing with that once the list scrolls.
  mutable ftxui::Box cursor_box_;
  mutable ftxui::Box scratch_box_;
  mutable ftxui::Box bar_box_;
  mutable ftxui::Box panel_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BANK_PANEL_H_
