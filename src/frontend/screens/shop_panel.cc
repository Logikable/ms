#include "src/frontend/screens/shop_panel.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/item.h"
#include "src/item/shop.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// Column widths. Name and level match the bag's equip tab, so the two lists
// line up and the same item reads the same way in both. The type column takes
// the longest name a weapon has ("Two-Handed Sword"), and the cost column a
// five-figure price and its coin.
constexpr int kNameWidth = 26;
constexpr int kTypeWidth = 16;
constexpr int kLevelWidth = 7;
constexpr int kCostWidth = 12;

// Stock rows on screen at once. Deep enough to hold most of a warrior's list --
// the longest any class has -- and short enough that the window still clears
// the bottom of a modest terminal.
constexpr int kVisibleRows = 15;

// Two leading spaces match the "  " / "> " cursor on the rows below.
ftxui::Element ColumnHeader() {
  return ftxui::text("  " + PadRight("Name", kNameWidth) + "  " +
                     PadRight("Type", kTypeWidth) + "  " +
                     PadRight("Level", kLevelWidth) +
                     PadLeft("🪙 Cost", kCostWidth));
}

// The Etc shelf has no type and no level to show, so the two columns between
// the name and the price become one: how many the player owns already. "Owned"
// is the word for that everywhere -- the buy dialog says it too.
ftxui::Element EtcColumnHeader() {
  return ftxui::text("  " + PadRight("Name", kNameWidth) + "  " +
                     PadRight("Owned", kTypeWidth + 2 + kLevelWidth) +
                     PadLeft("🪙 Cost", kCostWidth));
}

// The buy-back shelf holds both kinds at once, so it shows what only one of
// them has: an equip comes back as the one item it was, and a stack comes back
// as many. The type is left to Inspect -- a name the player chose to sell is
// one they already know.
ftxui::Element BuyBackColumnHeader() {
  return ftxui::text("  " + PadRight("Name", kNameWidth) + "  " +
                     PadRight("Qty", kTypeWidth + 2 + kLevelWidth) +
                     PadLeft("🪙 Cost", kCostWidth));
}

// The level cell, e.g. "Lv30  ". An item with no level requirement reads as
// level 1 rather than as a blank, matching the bag.
std::string LevelCell(const EquipPrototype& proto) {
  int level = proto.required_level();
  if (level <= 0) {
    level = 1;
  }
  return PadRight("Lv" + std::to_string(level), kLevelWidth);
}

}  // namespace

ShopPanel::ShopPanel(const CharacterInstance& character,
                     const std::map<std::string, EquipPrototype>& equips,
                     const std::map<std::string, ItemPrototype>& items)
    : character_(character),
      equips_(equips),
      items_(items),
      menu_({"Inspect", "Buy", "Close"}) {
  Reset();
}

int ShopPanel::RowCount() const {
  if (tab_ == kShopBuyBackTab) {
    return character_.buy_backs().size();
  }
  return static_cast<int>(stock_.size());
}

void ShopPanel::Restock() {
  // Rebuilt rather than kept, because the shop stocks what this character can
  // hold and that changes when they advance. Cheap: the catalog is small and
  // the screen opens on a keypress.
  stock_.clear();
  // The buy-back shelf is the character's, not the shop's. It is read where it
  // lives, so a sale made while the screen is open shows up without restocking
  // -- and nothing here filters it: what a player sold is theirs to buy back
  // whatever their class or level says now.
  if (tab_ == kShopBuyBackTab) {
    return;
  }
  if (tab_ == kShopEtcTab) {
    stock_ = ShopEtcStock(items_);
    return;
  }
  std::vector<std::string> shelf = tab_ == kShopSecondariesTab
                                       ? ShopSecondaryStock(equips_)
                                       : ShopWeaponStock(equips_);
  for (const std::string& key : shelf) {
    if (character_.MeetsJob(equips_.at(key))) {
      stock_.push_back(key);
    }
  }
}

void ShopPanel::Reset() {
  tab_ = kShopWeaponsTab;
  Restock();
  zone_ = kZoneList;
  selected_ = 0;
  first_visible_ = 0;
  menu_open_ = false;
}

void ShopPanel::StepTab(int direction) {
  int next = tab_ + direction;
  if (next < 0 || next >= kNumShopTabs) {
    return;  // the ends of the bar are walls, not wrapping points
  }
  tab_ = next;
  Restock();
  selected_ = 0;
  first_visible_ = 0;
}

int ShopPanel::CursorStop() const {
  return zone_ == kZoneTabs ? 0 : selected_ + 1;
}

void ShopPanel::MoveCursor(int delta) {
  int next = StepCursor(CursorStop(), delta, 1 + RowCount());
  if (next == 0) {
    // The window stays where it is: the cursor has left the list rather than
    // moved within it, and it comes back to the row it left.
    zone_ = kZoneTabs;
    return;
  }
  zone_ = kZoneList;
  selected_ = next - 1;
  ScrollToCursor();
}

void ShopPanel::ScrollToCursor() {
  if (selected_ < first_visible_) {
    first_visible_ = selected_;
  } else if (selected_ >= first_visible_ + kVisibleRows) {
    first_visible_ = selected_ - kVisibleRows + 1;
  }
}

void ShopPanel::OpenMenu() {
  if (zone_ == kZoneTabs) {
    // Nothing to open a menu on: the cursor is on the bar, not on an item.
    return;
  }
  if (selected_item() == nullptr && selected_stackable() == nullptr &&
      selected_buy_back() == nullptr) {
    return;
  }
  menu_.Reset();
  menu_open_ = true;
}

bool ShopPanel::menu_open() const {
  return menu_open_;
}

Screen ShopPanel::OnMenuEvent(ftxui::Event event) {
  if (IsBack(event)) {
    menu_open_ = false;
    return kShop;
  }
  if (event == ftxui::Event::ArrowUp) {
    menu_.Up();
    return kShopMenu;
  }
  if (event == ftxui::Event::ArrowDown) {
    menu_.Down();
    return kShopMenu;
  }
  if (IsForward(event)) {
    // Closed on the way out whichever entry was chosen, so the screen it opens
    // is not drawn with the menu still standing over the list behind it.
    menu_open_ = false;
    if (menu_.selected() == kShopMenuInspect) {
      return kShopInspect;
    }
    if (menu_.selected() == kShopMenuBuy) {
      return kShopBuy;
    }
    return kShop;
  }
  // Swallow everything else: the menu is modal over the list.
  return kShopMenu;
}

const BuyBackEntry* ShopPanel::selected_buy_back() const {
  if (tab_ != kShopBuyBackTab || selected_ < 0 ||
      selected_ >= character_.buy_backs().size()) {
    return nullptr;
  }
  return &character_.buy_backs().Get(selected_);
}

const EquipPrototype* ShopPanel::selected_item() const {
  if (tab_ == kShopEtcTab || tab_ == kShopBuyBackTab || selected_ < 0 ||
      selected_ >= static_cast<int>(stock_.size())) {
    return nullptr;
  }
  return &equips_.at(stock_[selected_]);
}

const ItemPrototype* ShopPanel::selected_stackable() const {
  if (tab_ != kShopEtcTab || selected_ < 0 ||
      selected_ >= static_cast<int>(stock_.size())) {
    return nullptr;
  }
  return &items_.at(stock_[selected_]);
}

bool ShopPanel::OnEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  // Left and Right belong to the bar, and only while the cursor is standing on
  // it -- in the list they would be a keypress that quietly changed the list
  // under the cursor.
  if (zone_ == kZoneTabs &&
      (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight)) {
    StepTab(event == ftxui::Event::ArrowLeft ? -1 : 1);
    return true;
  }
  return false;
}

ftxui::Element ShopPanel::RenderTabBar() const {
  // Chips are white while the bar holds the cursor and theme-blue otherwise,
  // which is how the player tells the arrow keys are on the bar.
  bool focused = zone_ == kZoneTabs;
  std::vector<ftxui::Element> chips;
  chips.push_back(TabChip("Weapons", /*active=*/tab_ == kShopWeaponsTab,
                          /*row_focused=*/focused));
  chips.push_back(TabChip("Secondaries", /*active=*/tab_ == kShopSecondariesTab,
                          /*row_focused=*/focused));
  chips.push_back(TabChip("Etc", /*active=*/tab_ == kShopEtcTab,
                          /*row_focused=*/focused));
  chips.push_back(TabChip("Buy-Back", /*active=*/tab_ == kShopBuyBackTab,
                          /*row_focused=*/focused));
  // The meso sits in what the chips leave rather than over the whole row: a
  // third chip took the bar out to where a centred counter was drawn on top of
  // it, and a fourth would reach further still.
  chips.push_back(ftxui::filler());
  chips.push_back(ftxui::text(FormatMeso(character_.meso())) |
                  ftxui::color(kTheme));
  chips.push_back(ftxui::filler());
  return ftxui::hbox(std::move(chips));
}

// The price is red when the player cannot pay it, so the list answers "what can
// I buy" without arithmetic on every row.
ftxui::Element ShopPanel::RenderEtcRow(const ItemPrototype& item,
                                       const std::string& cursor) const {
  ftxui::Element cost =
      ftxui::text(PadLeft(FormatMeso(item.shop_price()), kCostWidth));
  if (item.shop_price() > character_.meso()) {
    cost = std::move(cost) | ftxui::color(kRed);
  }
  return ftxui::hbox({
      ftxui::text(cursor + PadRight(item.name(), kNameWidth) + "  " +
                  PadRight(FormatWithCommas(character_.CountStackable(item)),
                           kTypeWidth + 2 + kLevelWidth)),
      std::move(cost),
      ftxui::text(" "),
  });
}

// The level is red on the bag's rule and in the bag's colour. There is no class
// to colour: the list holds nothing this character is the wrong class for.
ftxui::Element ShopPanel::RenderEquipRow(const EquipPrototype& proto,
                                         const std::string& cursor) const {
  ftxui::Element level = ftxui::text(LevelCell(proto));
  if (!character_.MeetsLevel(proto)) {
    level = std::move(level) | ftxui::color(kRed);
  }
  ftxui::Element cost =
      ftxui::text(PadLeft(FormatMeso(proto.shop_price()), kCostWidth));
  if (proto.shop_price() > character_.meso()) {
    cost = std::move(cost) | ftxui::color(kRed);
  }
  return ftxui::hbox({
      ftxui::text(cursor + PadRight(proto.name(), kNameWidth) + "  " +
                  PadRight(FormatEquipType(proto.equip_type()), kTypeWidth) +
                  "  "),
      std::move(level),
      std::move(cost),
      ftxui::text(" "),
  });
}

ftxui::Element ShopPanel::RenderBuyBackRow(const BuyBackEntry& entry,
                                           const std::string& cursor) const {
  // The name is the item's own, and a trace's name already says it is one.
  std::string name;
  std::string qty;
  if (entry.has_equip()) {
    const EquipPrototype* proto =
        FindEquipByName(equips_, entry.equip().equip_name());
    name = proto == nullptr ? entry.equip().equip_name() : proto->name();
    if (entry.equip().trace()) {
      name += " Trace";
    }
  } else {
    name = entry.stack().name();
    qty = FormatWithCommas(entry.stack().count());
  }
  ftxui::Element cost =
      ftxui::text(PadLeft(FormatMeso(entry.unit_price()), kCostWidth));
  if (entry.unit_price() > character_.meso()) {
    cost = std::move(cost) | ftxui::color(kRed);
  }
  return ftxui::hbox({
      ftxui::text(cursor + PadRight(name, kNameWidth) + "  " +
                  PadRight(qty, kTypeWidth + 2 + kLevelWidth)),
      std::move(cost),
      ftxui::text(" "),
  });
}

std::vector<ftxui::Element> ShopPanel::RenderStock() const {
  std::vector<ftxui::Element> item_rows;
  int last = std::min(RowCount(), first_visible_ + kVisibleRows);
  for (int i = first_visible_; i < last; ++i) {
    std::string cursor = zone_ == kZoneList && i == selected_ ? "> " : "  ";
    if (tab_ == kShopBuyBackTab) {
      item_rows.push_back(
          RenderBuyBackRow(character_.buy_backs().Get(i), cursor));
    } else if (tab_ == kShopEtcTab) {
      item_rows.push_back(RenderEtcRow(items_.at(stock_[i]), cursor));
    } else {
      item_rows.push_back(RenderEquipRow(equips_.at(stock_[i]), cursor));
    }
  }
  if (item_rows.empty()) {
    return {};
  }
  return {ftxui::hbox({
      ftxui::vbox(std::move(item_rows)),
      ScrollBar(RowCount(), first_visible_, kVisibleRows),
  })};
}

ftxui::Element ShopPanel::Render() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(RenderTabBar());
  rows.push_back(ThemedSeparator());
  if (tab_ == kShopBuyBackTab) {
    rows.push_back(BuyBackColumnHeader());
  } else if (tab_ == kShopEtcTab) {
    rows.push_back(EtcColumnHeader());
  } else {
    rows.push_back(ColumnHeader());
  }
  rows.push_back(ThemedSeparator());
  if (RowCount() == 0) {
    // The game's one word for a list with nothing in it. A shelf is empty for
    // a reason the player can already see -- the tab they are standing on.
    rows.push_back(EmptyState("empty", /*gutter=*/2));
  }
  for (ftxui::Element& row : RenderStock()) {
    rows.push_back(std::move(row));
  }
  ftxui::Element window = ThemedWindow(" Shop ", ftxui::vbox(std::move(rows)));
  if (!menu_open_) {
    return window;
  }
  // Anchored inside the panel rather than on the terminal, because the shop is
  // centred and so has no fixed place on screen to measure from.
  //
  // kMenuCol clears the border and the name column, so the menu covers what the
  // item asks for rather than what it is called.
  constexpr int kMenuCol = 1 + 2 + kNameWidth;
  // Floated, so a menu opened on one of the last few items hangs out past the
  // bottom border instead of stretching the window down to hold it. Sliding it
  // up to fit would leave it clear of the item it belongs to, which reads as a
  // menu for some other row.
  return ftxui::dbox({
      std::move(window),
      Floating(menu_.Render(MenuRow(), kMenuCol)),
  });
}

int ShopPanel::MenuRow() const {
  // +5 rows: the window's top border, the tab row, its separator, the column
  // header, its separator. Measured from the top of the window, so it is the
  // cursor's place within the scrolled view and not its place in the stock.
  constexpr int kFirstItemRow = 5;
  return kFirstItemRow + selected_ - first_visible_;
}

}  // namespace ms
