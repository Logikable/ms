#include "src/frontend/screens/bank_panel.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/progression.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/inventory_list.h"
#include "src/frontend/widgets/item_columns.h"
#include "src/frontend/widgets/keys.h"
#include "src/item/bank.h"
#include "src/item/currency.h"
#include "src/item/item.h"
#include "src/item/tradeable.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// The width of each half, borders included, and the rows its list shows at
// once. Both are fixed, sized so the two halves together fit the shortest
// terminal the game supports. A window that grew with its contents would move
// the other one's border.
constexpr int kHalfWidth = 110;
constexpr int kHalfRows = 9;

// The stops on a half's top row, left to right.
enum TopStop : int {
  kEquipChip = 0,
  kEtcChip = 1,
  kMesoStop = 2,
  kTraceStop = 3,
  kVPointStop = 4,
};

// Where a menu opens inside its half: past the name at the start of a row, so
// it covers the item's value rather than which item it is.
constexpr int kMenuColumn = 40;

}  // namespace

std::string BankCurrencyName(BankCurrency currency) {
  switch (currency) {
    case BankCurrency::kMeso:
      return "Meso";
    case BankCurrency::kSpellTraces:
      return "Spell Traces";
    case BankCurrency::kVPoints:
      return "V Points";
  }
  return "";
}

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

bool BankPanel::HasPages(BankZone zone) const {
  return zone == BankZone::kBank && !half(zone).etc_tab;
}

int BankPanel::RowCount(BankZone zone) const {
  const Half& side = half(zone);
  if (zone == BankZone::kBag) {
    return side.etc_tab ? static_cast<int>(character_.stackables().size())
                        : character_.inventory().size();
  }
  const BankInstance& bank = account_.bank();
  return side.etc_tab ? bank.stacks().size() : bank.page(side.page).size();
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
    return;  // Left and Right do nothing in a list of items
  }
  if (side.on_pages) {
    side.page = StepCursor(side.page, delta, kBankPages);
    side.row = 0;
    return;
  }
  side.top = StepCursor(side.top, delta, TopStops());
  // Moving onto a chip opens that tab; there is no separate key for it.
  if (side.top == kEquipChip || side.top == kEtcChip) {
    side.etc_tab = side.top == kEtcChip;
  }
}

void BankPanel::MoveRow(int delta) {
  Half& side = here();
  // The top row is stop 0 of one ring, the page row (if drawn) the next, and
  // the list rows are the stops after it, so Down from the last row returns to
  // the bar and Up from the bar goes to the last row.
  const int first_row = HasPages(zone_) ? 2 : 1;
  int rows = RowCount(zone_);
  int stop =
      side.in_list ? ClampedRow(zone_) + first_row : (side.on_pages ? 1 : 0);
  int next = StepCursor(stop, delta, first_row + rows);
  side.on_pages = first_row == 2 && next == 1;
  side.in_list = next >= first_row;
  if (side.in_list) {
    side.row = next - first_row;
    return;
  }
  if (side.on_pages) {
    return;
  }
  // Coming back up lands on the chip of the tab being shown, not wherever the
  // cursor was on the top row.
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
  if (side.on_pages) {
    return {BankCursor::Kind::kTab, BankCurrency::kMeso, 0};
  }
  switch (side.top) {
    case kMesoStop:
      return {BankCursor::Kind::kCurrency, BankCurrency::kMeso, 0};
    case kTraceStop:
      return {BankCursor::Kind::kCurrency, BankCurrency::kSpellTraces, 0};
    case kVPointStop:
      return {BankCursor::Kind::kCurrency, BankCurrency::kVPoints, 0};
  }
  return {BankCursor::Kind::kTab, BankCurrency::kMeso, 0};
}

int BankPanel::TopStops() const {
  return Unlocked(Feature::kVPoints, character_, account_) ? kVPointStop + 1
                                                           : kVPointStop;
}

int64_t BankPanel::held(BankCurrency currency) const {
  const bool bag = zone_ == BankZone::kBag;
  const BankInstance& bank = account_.bank();
  switch (currency) {
    case BankCurrency::kMeso:
      return bag ? character_.meso() : bank.meso();
    case BankCurrency::kSpellTraces:
      return bag ? character_.CountItem(kSpellTraceName)
                 : bank.CountCurrency(kSpellTraceName);
    case BankCurrency::kVPoints:
      return bag ? character_.v_points() : bank.v_points();
  }
  return 0;
}

void BankPanel::MoveCurrency(BankCurrency currency, int64_t amount) {
  amount = std::clamp<int64_t>(amount, 0, held(currency));
  if (amount == 0) {
    return;
  }
  BankInstance& bank = account_.mutable_bank();
  const bool from_bag = zone_ == BankZone::kBag;
  // Taken from one side before being given to the other, so a purse that
  // refuses can't hand over what it still holds.
  switch (currency) {
    case BankCurrency::kMeso:
      if (from_bag ? character_.SpendMeso(amount) : bank.SpendMeso(amount)) {
        from_bag ? bank.AddMeso(amount) : character_.AddMeso(amount);
      }
      return;
    case BankCurrency::kSpellTraces:
      if (spell_trace_ == nullptr) {
        return;
      }
      if (from_bag ? character_.SpendItem(kSpellTraceName, amount)
                   : bank.SpendCurrency(kSpellTraceName, amount)) {
        from_bag ? bank.AddItem(*spell_trace_, static_cast<int>(amount))
                 : character_.AddItem(*spell_trace_, static_cast<int>(amount));
      }
      return;
    case BankCurrency::kVPoints:
      if (from_bag ? character_.SpendVPoints(amount)
                   : bank.SpendVPoints(amount)) {
        from_bag ? bank.AddVPoints(amount) : character_.AddVPoints(amount);
      }
      return;
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
  // The row that moved up into this place is the next item. When the tab is
  // empty there is nothing to select, and the cursor moves up to the page, or
  // to the chip.
  Half& side = here();
  int rows = RowCount(zone_);
  if (rows == 0) {
    side.in_list = false;
    side.on_pages = HasPages(zone_);
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
    if (!CanTrade(character_.inventory()[index].prototype())) {
      return "Symbols can't be stored.";
    }
    if (bank.PageFull(bank_.page)) {
      return "This page is full.";
    }
    bank.AddEquip(bank_.page, character_.TakeEquip(index));
    return "";
  }
  if (character_.inventory().full()) {
    return "Inventory full.";
  }
  character_.PickUp(bank.TakeEquip(bank_.page, index));
  return "";
}

std::string BankPanel::MoveStack() {
  int index = ClampedRow(zone_);
  BankInstance& bank = account_.mutable_bank();
  // The whole stack moves, so both sides must have room for all of it. Moving
  // only part of a stack would be a split, not a move.
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
    bank.SortEquips(bank_.page, [this](const EquipPrototype& proto) {
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
                                 : &account_.bank().page(bank_.page)[index];
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
  const bool here_now = zone == zone_ && !side.in_list && !side.on_pages;
  std::vector<TabSpec> tabs = {{"Equip"}, {"Etc"}};
  int balance_cursor = kNoBalance;
  if (here_now && side.top == kMesoStop) {
    balance_cursor = kMesoBalance;
  } else if (here_now && side.top == kTraceStop) {
    balance_cursor = kTraceBalance;
  } else if (here_now && side.top == kVPointStop) {
    balance_cursor = kVPointBalance;
  }
  const BankInstance& bank = account_.bank();
  const bool bag = zone == BankZone::kBag;
  std::optional<int64_t> v_points;
  if (TopStops() > kVPointStop) {
    v_points = bag ? character_.v_points() : bank.v_points();
  }
  ftxui::Element balances =
      RenderBalances(bag ? character_.meso() : bank.meso(),
                     bag ? character_.CountItem(kSpellTraceName)
                         : bank.CountCurrency(kSpellTraceName),
                     character_, account_, balance_cursor, v_points);
  // A chip is lit only while the cursor is on it. A tab that is merely open
  // keeps the theme's inversion, so the two halves never both appear to have
  // the cursor.
  bool on_chip = here_now && !side.on_pages && side.top <= kEtcChip;
  return RenderBagTabBar(tabs, side.etc_tab ? kEtcChip : kEquipChip, balances,
                         on_chip, /*highlighted=*/false, ftxui::text(""),
                         kHalfWidth - 2,
                         zone == zone_ ? bar_box_ : scratch_box_);
}

ftxui::Element BankPanel::RenderPageRow(BankZone zone) const {
  std::vector<TabSpec> pages;
  for (int i = 1; i <= kBankPages; ++i) {
    pages.push_back({std::to_string(i)});
  }
  const Half& side = half(zone);
  return TabBar(pages, side.page, zone == zone_ && side.on_pages,
                kHalfWidth - 2) |
         ftxui::reflect(page_box_);
}

ftxui::Element BankPanel::RenderList(BankZone zone) const {
  const Half& side = half(zone);
  const bool focused = zone == zone_ && side.in_list;
  int rows = RowCount(zone);
  int cursor = ClampedRow(zone);
  // The half and the tab are both part of the key, so the same row in another
  // list counts as a different name and starts from the beginning.
  if (focused) {
    name_clock_.Follow((zone == BankZone::kBank ? 2 : 0) * kHalfStride +
                           (side.etc_tab ? kHalfStride : 0) +
                           side.page * kBankPageCapacity + cursor,
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
  const InventoryInstance& items =
      zone == BankZone::kBag ? character_.inventory() : bank.page(side.page);
  return RenderEquipList(
      character_, items, AllRows(rows), cursor, focused,
      FitItemColumns(kHalfWidth - 2,
                     EquipListOptions(character_, account_, items)),
      CursorBox(zone), /*highlighted=*/false, elapsed);
}

ftxui::Element BankPanel::RenderHalf(BankZone zone) const {
  // The page row takes one of the list's rows, so the half keeps its height:
  // the screen has no row to spare.
  const bool pages = HasPages(zone);
  std::vector<ftxui::Element> rows = {RenderTopRow(zone)};
  if (pages) {
    rows.push_back(RenderPageRow(zone));
  }
  // The header, its rule and the rows, which make up the list.
  rows.push_back(RenderList(zone) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL,
                                                kHalfRows + 2 - pages));
  ftxui::Element body = ftxui::vbox(std::move(rows)) |
                        ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kHalfWidth);
  return ThemedWindow(zone == BankZone::kBag ? " Inventory " : " Bank ",
                      std::move(body), zone == zone_);
}

int BankPanel::MenuRow() const {
  // Both boxes are where the render placed them, in screen coordinates, and the
  // menu is placed from the panel's own corner, so the panel's top is
  // subtracted. One row back from the cursor, so the highlighted entry sits
  // beside what the menu is about rather than below it.
  int row = here().in_list    ? cursor_box_.y_min - 1
            : here().on_pages ? page_box_.y_min + 1
                              : bar_box_.y_min + 1;
  return row - panel_box_.y_min;
}

int BankPanel::MenuColumn() const {
  return kMenuColumn;
}

ftxui::Element BankPanel::Render() const {
  // Reflected so a menu can be placed beside a row inside it. The lists report
  // where they landed on the screen, and a floating menu is placed from the
  // panel's own corner.
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
