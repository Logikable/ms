#include "src/frontend/screens/bank_panel.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/progression.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/inventory_list.h"
#include "src/frontend/widgets/item_columns.h"
#include "src/frontend/widgets/keys.h"
#include "src/item/currency.h"
#include "src/item/item.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// Each half, borders included, and the rows its list shows at once. Both
// fixed, and sized so the two halves together come to the shortest terminal
// the game is laid out for: a window that grew with what was put in it would
// move the other one's border.
constexpr int kHalfWidth = 110;
constexpr int kHalfRows = 9;

// The stops on a half's top row, left to right.
enum TopStop : int {
  kEquipChip = 0,
  kEtcChip = 1,
  kMesoStop = 2,
  kTraceStop = 3,
  kNumTopStops = 4,
};

// Where a menu hangs inside its half: past the name a row leads with, so it
// covers what an item is worth rather than which item it is.
constexpr int kMenuColumn = 40;

}  // namespace

BankPanel::BankPanel(CharacterInstance& character, AccountInstance& account,
                     const std::map<std::string, ItemPrototype>& items)
    : character_(character),
      account_(account),
      spell_trace_(FindItemByName(items, kSpellTraceName)),
      menu_({"Inspect", "Move", "Close"}),
      tab_menu_({"Sort", "Close"}) {
}

void BankPanel::Reset() {
  zone_ = BankZone::kBag;
  bag_ = Half();
  bank_ = Half();
  CloseMenu();
}

BankPanel::Half& BankPanel::half(BankZone zone) {
  return zone == BankZone::kBag ? bag_ : bank_;
}

const BankPanel::Half& BankPanel::half(BankZone zone) const {
  return zone == BankZone::kBag ? bag_ : bank_;
}

int BankPanel::RowCount(BankZone zone) const {
  const Half& side = half(zone);
  if (zone == BankZone::kBag) {
    return side.etc_tab ? static_cast<int>(character_.stackables().size())
                        : character_.inventory().size();
  }
  const BankInstance& bank = account_.bank();
  return side.etc_tab ? bank.stacks().size() : bank.equips().size();
}

int BankPanel::ClampedRow(BankZone zone) const {
  return std::clamp(half(zone).row, 0, std::max(0, RowCount(zone) - 1));
}

bool BankPanel::on_etc_tab() const {
  return here().etc_tab;
}

void BankPanel::NextZone() {
  zone_ = zone_ == BankZone::kBag ? BankZone::kBank : BankZone::kBag;
}

void BankPanel::MoveCursor(int delta) {
  Half& side = here();
  if (side.in_list) {
    return;  // Left and Right say nothing in a list of items.
  }
  side.top = StepCursor(side.top, delta, kNumTopStops);
  // Standing on a chip is what opens that tab: there is no second key for it.
  if (side.top == kEquipChip || side.top == kEtcChip) {
    side.etc_tab = side.top == kEtcChip;
  }
}

void BankPanel::MoveRow(int delta) {
  Half& side = here();
  // The top row is stop 0 of one ring and the list rows are the stops after
  // it, so Down off the last row returns to the bar and Up off the bar goes
  // to the last row.
  int rows = RowCount(zone_);
  int stop = side.in_list ? ClampedRow(zone_) + 1 : 0;
  int next = StepCursor(stop, delta, 1 + rows);
  side.in_list = next > 0;
  if (side.in_list) {
    side.row = next - 1;
    return;
  }
  // Coming back up lands on the chip of the tab being shown rather than
  // wherever the cursor left the row.
  side.top = side.etc_tab ? kEtcChip : kEquipChip;
}

BankCursor BankPanel::cursor() const {
  const Half& side = here();
  if (side.in_list) {
    if (RowCount(zone_) == 0) {
      return {BankCursor::Kind::kNothing, BankCurrency::kMeso, 0};
    }
    return {BankCursor::Kind::kRow, BankCurrency::kMeso, ClampedRow(zone_)};
  }
  if (side.top == kMesoStop || side.top == kTraceStop) {
    return {BankCursor::Kind::kCurrency,
            side.top == kMesoStop ? BankCurrency::kMeso
                                  : BankCurrency::kSpellTraces,
            0};
  }
  return {BankCursor::Kind::kTab, BankCurrency::kMeso, 0};
}

int64_t BankPanel::held(BankCurrency currency) const {
  const bool meso = currency == BankCurrency::kMeso;
  if (zone_ == BankZone::kBag) {
    return meso ? character_.meso() : character_.CountItem(kSpellTraceName);
  }
  return meso ? account_.bank().meso()
              : account_.bank().CountCurrency(kSpellTraceName);
}

void BankPanel::MoveCurrency(BankCurrency currency, int64_t amount) {
  amount = std::clamp<int64_t>(amount, 0, held(currency));
  const bool meso = currency == BankCurrency::kMeso;
  if (amount == 0 || (!meso && spell_trace_ == nullptr)) {
    return;
  }
  BankInstance& bank = account_.mutable_bank();
  const bool from_bag = zone_ == BankZone::kBag;
  // Taken from one side before it is given to the other, so a purse that
  // refuses cannot hand over what it still holds.
  bool taken = from_bag ? (meso ? character_.SpendMeso(amount)
                                : character_.SpendItem(kSpellTraceName, amount))
                        : (meso ? bank.SpendMeso(amount)
                                : bank.SpendCurrency(kSpellTraceName, amount));
  if (!taken) {
    return;
  }
  if (from_bag) {
    if (meso) {
      bank.AddMeso(amount);
    } else {
      bank.AddItem(*spell_trace_, static_cast<int>(amount));
    }
    return;
  }
  if (meso) {
    character_.AddMeso(amount);
  } else {
    character_.AddItem(*spell_trace_, static_cast<int>(amount));
  }
}

std::string BankPanel::MoveSelected() {
  if (cursor().kind != BankCursor::Kind::kRow) {
    return "";
  }
  std::string error = here().etc_tab ? MoveStack() : MoveEquip();
  if (!error.empty()) {
    return error;
  }
  // The row that slid up into this place is the next item; when the tab has
  // run out there is nothing to stand on and the cursor climbs to the chip.
  Half& side = here();
  int rows = RowCount(zone_);
  if (rows == 0) {
    side.in_list = false;
    side.top = side.etc_tab ? kEtcChip : kEquipChip;
  } else {
    side.row = std::min(side.row, rows - 1);
  }
  return "";
}

std::string BankPanel::MoveEquip() {
  int index = ClampedRow(zone_);
  BankInstance& bank = account_.mutable_bank();
  if (zone_ == BankZone::kBag) {
    if (bank.equips().full()) {
      return "Bank full.";
    }
    bank.AddEquip(character_.TakeEquip(index));
    return "";
  }
  if (character_.inventory().full()) {
    return "Inventory full.";
  }
  character_.PickUp(bank.TakeEquip(index));
  return "";
}

std::string BankPanel::MoveStack() {
  int index = ClampedRow(zone_);
  BankInstance& bank = account_.mutable_bank();
  // The whole stack crosses, so both ends are asked for room enough for all
  // of it: a move that left half a stack behind would be a split, not a move.
  if (zone_ == BankZone::kBag) {
    const StackableItem& stack = character_.stackables()[index];
    if (bank.RoomFor(stack.prototype()) < stack.count()) {
      return "Bank full.";
    }
    ItemPrototype proto = stack.prototype();
    int count = character_.TakeStack(index, stack.count());
    bank.AddItem(proto, count);
    return "";
  }
  const StackableItem& stack = bank.stacks()[index];
  if (character_.RoomFor(stack.prototype()) < stack.count()) {
    return "Inventory full.";
  }
  ItemPrototype proto = stack.prototype();
  int count = bank.TakeStack(index, stack.count());
  character_.AddItem(proto, count);
  return "";
}

void BankPanel::SortActiveTab() {
  if (zone_ == BankZone::kBag) {
    here().etc_tab ? character_.SortStackTab() : character_.SortEquipTab();
    return;
  }
  BankInstance& bank = account_.mutable_bank();
  if (here().etc_tab) {
    bank.SortStacks();
  } else {
    bank.SortEquips([this](const EquipPrototype& proto) {
      return character_.CanEquip(proto);
    });
  }
}

void BankPanel::OpenMenu() {
  if (cursor().kind != BankCursor::Kind::kRow) {
    return;
  }
  menu_.Reset();
  menu_open_ = true;
  tab_menu_open_ = false;
}

void BankPanel::OpenTabMenu() {
  tab_menu_.Reset();
  tab_menu_open_ = true;
  menu_open_ = false;
}

void BankPanel::MoveMenuCursor(int delta) {
  ItemMenu& menu = tab_menu_open_ ? tab_menu_ : menu_;
  delta < 0 ? menu.Up() : menu.Down();
}

int BankPanel::menu_selected() const {
  return tab_menu_open_ ? tab_menu_.selected() : menu_.selected();
}

const EquipTabItem* BankPanel::selected_equip() const {
  if (cursor().kind != BankCursor::Kind::kRow || here().etc_tab) {
    return nullptr;
  }
  int index = ClampedRow(zone_);
  return zone_ == BankZone::kBag ? &character_.inventory()[index]
                                 : &account_.bank().equips()[index];
}

const StackableItem* BankPanel::selected_stack() const {
  if (cursor().kind != BankCursor::Kind::kRow || !here().etc_tab) {
    return nullptr;
  }
  int index = ClampedRow(zone_);
  return zone_ == BankZone::kBag ? &character_.stackables()[index]
                                 : &account_.bank().stacks()[index];
}

ftxui::Box& BankPanel::CursorBox(BankZone zone) const {
  return zone == zone_ ? cursor_box_ : scratch_box_;
}

ftxui::Element BankPanel::RenderTopRow(BankZone zone) const {
  const Half& side = half(zone);
  const bool here_now = zone == zone_ && !side.in_list;
  std::vector<TabSpec> tabs = {{"Equip"}, {"Etc"}};
  int balance_cursor = kNoBalance;
  if (here_now && side.top == kMesoStop) {
    balance_cursor = kMesoBalance;
  } else if (here_now && side.top == kTraceStop) {
    balance_cursor = kTraceBalance;
  }
  const BankInstance& bank = account_.bank();
  ftxui::Element balances = RenderBalances(
      zone == BankZone::kBag ? character_.meso() : bank.meso(),
      zone == BankZone::kBag ? character_.CountItem(kSpellTraceName)
                             : bank.CountCurrency(kSpellTraceName),
      character_, account_, balance_cursor);
  // A chip is lit only while the cursor is on it: a tab that is merely OPEN
  // keeps the theme invert, so the two halves never claim the cursor at once.
  bool on_chip = here_now && side.top <= kEtcChip;
  return RenderBagTabBar(tabs, side.etc_tab ? kEtcChip : kEquipChip, balances,
                         on_chip, /*highlighted=*/false, ftxui::text(""),
                         kHalfWidth - 2,
                         zone == zone_ ? bar_box_ : scratch_box_);
}

ftxui::Element BankPanel::RenderList(BankZone zone) const {
  const Half& side = half(zone);
  const bool focused = zone == zone_ && side.in_list;
  int rows = RowCount(zone);
  int cursor = ClampedRow(zone);
  // The half and the tab both ride in the key, so the same row of another
  // list counts as a different name and starts from its own head.
  if (focused) {
    name_clock_.Follow((zone == BankZone::kBank ? 2 : 0) * kHalfStride +
                           (side.etc_tab ? kHalfStride : 0) + cursor,
                       true);
  }
  std::chrono::steady_clock::duration elapsed =
      focused ? name_clock_.Elapsed()
              : std::chrono::steady_clock::duration::zero();
  const BankInstance& bank = account_.bank();
  if (side.etc_tab) {
    const std::vector<StackableItem>& stacks = zone == BankZone::kBag
                                                   ? character_.stackables()
                                                   : bank.stacks().items();
    return RenderStackList(stacks, AllRows(rows), cursor, focused,
                           CursorBox(zone), /*highlighted=*/false, elapsed);
  }
  ItemListOptions options;
  options.bag = true;
  options.scrolling = Unlocked(Feature::kScrolling, character_, account_);
  options.star_force = Unlocked(Feature::kStarForce, character_, account_);
  options.potential = Unlocked(Feature::kPotential, character_, account_);
  return RenderEquipList(
      character_,
      zone == BankZone::kBag ? character_.inventory() : bank.equips(),
      AllRows(rows), cursor, focused, FitItemColumns(kHalfWidth - 2, options),
      CursorBox(zone), /*highlighted=*/false, elapsed);
}

ftxui::Element BankPanel::RenderHalf(BankZone zone) const {
  ftxui::Element body =
      ftxui::vbox({
          RenderTopRow(zone),
          // The header, its rule and the rows, which are all the list is.
          RenderList(zone) |
              ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kHalfRows + 2),
      }) |
      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kHalfWidth);
  return ThemedWindow(zone == BankZone::kBag ? " Inventory " : " Bank ",
                      std::move(body), zone == zone_);
}

int BankPanel::MenuRow() const {
  // The row under the top row while the cursor is up there, and the cursor's
  // own row once it is down in the list.
  return here().in_list ? cursor_box_.y_min : bar_box_.y_min + 2;
}

int BankPanel::MenuColumn() const {
  return panel_box_.x_min + kMenuColumn;
}

ftxui::Element BankPanel::Render() const {
  // Reflected so a menu can be put beside a row inside it: what the lists
  // report is where they landed on the SCREEN, and a floating menu is placed
  // from the panel's own corner.
  ftxui::Element screen = ftxui::vbox({
                              RenderHalf(BankZone::kBag),
                              RenderHalf(BankZone::kBank),
                          }) |
                          ftxui::reflect(panel_box_);
  if (!menu_open_ && !tab_menu_open_) {
    return screen;
  }
  const ItemMenu& menu = tab_menu_open_ ? tab_menu_ : menu_;
  return ftxui::dbox({
      std::move(screen),
      Floating(menu.Render(MenuRow(), MenuColumn())),
  });
}

}  // namespace ms
