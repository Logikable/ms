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
                     PadRight("Weapon Type", kTypeWidth) + "  " +
                     PadRight("Level", kLevelWidth) +
                     PadLeft("🪙 Cost", kCostWidth));
}

// The Etc shelf has no type and no level to show, so the two columns between
// the name and the price become one: how many the player is already carrying.
ftxui::Element EtcColumnHeader() {
  return ftxui::text("  " + PadRight("Name", kNameWidth) + "  " +
                     PadRight("Held", kTypeWidth + 2 + kLevelWidth) +
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

void ShopPanel::Restock() {
  // Rebuilt rather than kept, because the shop stocks what this character can
  // hold and that changes when they advance. Cheap: the catalog is small and
  // the screen opens on a keypress.
  stock_.clear();
  if (tab_ == kShopEtcTab) {
    stock_ = ShopEtcStock(items_);
    return;
  }
  for (const std::string& key : ShopStock(equips_)) {
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
  int next =
      StepCursor(CursorStop(), delta, 1 + static_cast<int>(stock_.size()));
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
  if (selected_item() == nullptr && selected_stackable() == nullptr) {
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

const EquipPrototype* ShopPanel::selected_item() const {
  if (tab_ != kShopWeaponsTab || selected_ < 0 ||
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

ftxui::Element ShopPanel::Render() const {
  std::vector<ftxui::Element> chips;
  // White while the bar holds the cursor and theme-blue otherwise, which is how
  // the player tells whether the arrow keys are on the bar or in the list.
  chips.push_back(TabChip("Weapons", /*active=*/tab_ == kShopWeaponsTab,
                          /*row_focused=*/zone_ == kZoneTabs));
  chips.push_back(TabChip("Etc", /*active=*/tab_ == kShopEtcTab,
                          /*row_focused=*/zone_ == kZoneTabs));
  ftxui::Element tab_row = ftxui::dbox({
      ftxui::hbox(std::move(chips)),
      ftxui::text(FormatMeso(character_.meso())) | ftxui::color(kTheme) |
          ftxui::hcenter,
  });

  std::vector<ftxui::Element> rows;
  rows.push_back(std::move(tab_row));
  rows.push_back(ThemedSeparator());
  rows.push_back(tab_ == kShopEtcTab ? EtcColumnHeader() : ColumnHeader());
  rows.push_back(ThemedSeparator());
  if (stock_.empty()) {
    rows.push_back(EmptyState("nothing for sale", /*gutter=*/2));
  }
  std::vector<ftxui::Element> item_rows;
  int last =
      std::min(static_cast<int>(stock_.size()), first_visible_ + kVisibleRows);
  for (int i = first_visible_; i < last; ++i) {
    std::string cursor_cell = "  ";
    if (zone_ == kZoneList && i == selected_) {
      cursor_cell = "> ";
    }
    if (tab_ == kShopEtcTab) {
      const ItemPrototype& item = items_.at(stock_[i]);
      ftxui::Element cost =
          ftxui::text(PadLeft(FormatMeso(item.shop_price()), kCostWidth));
      if (item.shop_price() > character_.meso()) {
        cost = std::move(cost) | ftxui::color(kRed);
      }
      item_rows.push_back(ftxui::hbox({
          ftxui::text(
              cursor_cell + PadRight(item.name(), kNameWidth) + "  " +
              PadRight(FormatWithCommas(character_.CountStackable(item)),
                       kTypeWidth + 2 + kLevelWidth)),
          std::move(cost),
          ftxui::text(" "),
      }));
      continue;
    }
    const EquipPrototype& proto = equips_.at(stock_[i]);
    // The level is coloured by whether this character has reached it, on the
    // bag's rule and in the bag's colour. There is no class to colour: the list
    // holds nothing this character is the wrong class for.
    ftxui::Element level = ftxui::text(LevelCell(proto));
    if (!character_.MeetsLevel(proto)) {
      level = std::move(level) | ftxui::color(kRed);
    }
    // The price is coloured by whether the player can pay it, so the list
    // answers "what can I buy" without arithmetic on every row.
    ftxui::Element cost =
        ftxui::text(PadLeft(FormatMeso(proto.shop_price()), kCostWidth));
    if (proto.shop_price() > character_.meso()) {
      cost = std::move(cost) | ftxui::color(kRed);
    }
    item_rows.push_back(ftxui::hbox({
        ftxui::text(cursor_cell + PadRight(proto.name(), kNameWidth) + "  " +
                    PadRight(FormatEquipType(proto.equip_type()), kTypeWidth) +
                    "  "),
        std::move(level),
        std::move(cost),
        ftxui::text(" "),
    }));
  }
  if (!item_rows.empty()) {
    rows.push_back(ftxui::hbox({
        ftxui::vbox(std::move(item_rows)),
        ScrollBar(static_cast<int>(stock_.size()), first_visible_,
                  kVisibleRows),
    }));
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
