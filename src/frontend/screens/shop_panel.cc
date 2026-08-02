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

namespace ms {
namespace {

// Column widths. Name, slot, level and job match the bag's equip tab, so the
// two lists line up and the same item reads the same way in both. The cost
// column fits a five-figure price and its coin.
constexpr int kNameWidth = 26;
constexpr int kSlotWidth = 10;
constexpr int kLevelWidth = 7;
constexpr int kJobWidth = 13;
constexpr int kCostWidth = 12;

// Two leading spaces match the "  " / "> " cursor on the rows below.
ftxui::Element ColumnHeader() {
  return ftxui::text("  " + PadRight("Name", kNameWidth) + "  " +
                     PadRight("Equip Slot", kSlotWidth) + "  " +
                     PadRight("Level", kLevelWidth) +
                     PadRight("Job", kJobWidth) +
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
                     const std::map<std::string, EquipPrototype>& equips)
    : character_(character),
      equips_(equips),
      stock_(ShopStock(equips)),
      menu_({"Inspect", "Buy", "Close"}) {
}

void ShopPanel::Reset() {
  selected_ = 0;
  menu_open_ = false;
}

void ShopPanel::OpenMenu() {
  if (selected_item() == nullptr) {
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
  if (selected_ < 0 || selected_ >= static_cast<int>(stock_.size())) {
    return nullptr;
  }
  return &equips_.at(stock_[selected_]);
}

bool ShopPanel::OnEvent(ftxui::Event event) {
  int count = static_cast<int>(stock_.size());
  if (event == ftxui::Event::ArrowUp) {
    selected_ = std::max(0, selected_ - 1);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    selected_ = std::min(count - 1, selected_ + 1);
    return true;
  }
  return false;
}

ftxui::Element ShopPanel::Render() const {
  std::vector<ftxui::Element> chips;
  // One tab, and it is always the focused bar: the list below takes the keys
  // but there is nowhere else for the highlight to be.
  chips.push_back(TabChip("Equips", /*active=*/true, /*row_focused=*/true));
  ftxui::Element tab_row = ftxui::dbox({
      ftxui::hbox(std::move(chips)),
      ftxui::text(FormatMeso(character_.meso())) | ftxui::color(kTheme) |
          ftxui::hcenter,
  });

  std::vector<ftxui::Element> rows;
  rows.push_back(std::move(tab_row));
  rows.push_back(ThemedSeparator());
  rows.push_back(ColumnHeader());
  rows.push_back(ThemedSeparator());
  if (stock_.empty()) {
    rows.push_back(EmptyState("nothing for sale", /*gutter=*/2));
  }
  for (int i = 0; i < static_cast<int>(stock_.size()); ++i) {
    const EquipPrototype& proto = equips_.at(stock_[i]);
    std::string cursor = "  ";
    if (i == selected_) {
      cursor = "> ";
    }
    // Each of the three requirements is coloured by whether this character
    // meets it, so the row says which one is in the way rather than only that
    // something is. Same rule and same colour as the bag's equip tab.
    ftxui::Element level = ftxui::text(LevelCell(proto));
    if (!character_.MeetsLevel(proto)) {
      level = std::move(level) | ftxui::color(kRed);
    }
    ftxui::Element job =
        ftxui::text(PadRight(FormatJobCategories(proto), kJobWidth));
    if (!character_.MeetsJob(proto)) {
      job = std::move(job) | ftxui::color(kRed);
    }
    // The price is coloured by whether the player can pay it, so the list
    // answers "what can I buy" without arithmetic on every row.
    ftxui::Element cost =
        ftxui::text(PadLeft(FormatMeso(proto.shop_price()), kCostWidth));
    if (proto.shop_price() > character_.meso()) {
      cost = std::move(cost) | ftxui::color(kRed);
    }
    rows.push_back(ftxui::hbox({
        ftxui::text(cursor + PadRight(proto.name(), kNameWidth) + "  " +
                    PadRight(FormatSlot(proto.equip_slot()), kSlotWidth) +
                    "  "),
        std::move(level),
        std::move(job),
        std::move(cost),
        ftxui::text(" "),
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
  return ftxui::dbox({
      std::move(window),
      menu_.Render(MenuRow(), kMenuCol),
  });
}

int ShopPanel::MenuRow() const {
  // +5 rows: the window's top border, the tab row, its separator, the column
  // header, its separator. The last row is that plus the stock, and the menu
  // needs its own borders as well as an entry each.
  int first_item_row = 5;
  int last_row = first_item_row + static_cast<int>(stock_.size());
  int menu_height = kNumShopMenuItems + 2;
  int item_row = first_item_row + selected_;
  // Hangs below the item, opening on its row, whenever the list has that much
  // left under it.
  if (item_row + menu_height <= last_row) {
    return item_row;
  }
  // Otherwise it opens upward and *ends* on the item's row rather than
  // starting there. The menu asks for every row above it as well as its own,
  // so letting it start low would make the overlay taller than the window and
  // stretch the whole panel; sliding it up while still hanging downward would
  // leave it floating clear of the item it belongs to. Flipping keeps it
  // against the item, which is what a menu does at an edge.
  return std::max(0, item_row - menu_height + 1);
}

}  // namespace ms
