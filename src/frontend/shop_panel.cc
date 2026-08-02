#include "src/frontend/shop_panel.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/colors.h"
#include "src/frontend/panel_util.h"
#include "src/protos/equip.pb.h"
#include "src/item/shop.h"

namespace ms {
namespace {

// Widths of the two columns. The name column fits the longest name the shop
// stocks with room to spare; the cost column fits a five-figure price and its
// coin.
constexpr int kNameWidth = 26;
constexpr int kCostWidth = 12;

// Two leading spaces match the "  " / "> " cursor on the rows below.
ftxui::Element ColumnHeader() {
  return ftxui::text("  " + PadRight("Name", kNameWidth) +
                     PadLeft("🪙 Cost", kCostWidth));
}

}  // namespace

ShopPanel::ShopPanel(const CharacterInstance& character,
                     const std::map<std::string, EquipPrototype>& equips)
    : character_(character), equips_(equips), stock_(ShopStock(equips)) {
}

void ShopPanel::Reset() {
  selected_ = 0;
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
    std::string cursor = i == selected_ ? "> " : "  ";
    // The price is coloured by whether the player can pay it, so the list
    // answers "what can I buy" without arithmetic on every row.
    ftxui::Element cost =
        ftxui::text(PadLeft(FormatMeso(proto.shop_price()), kCostWidth));
    if (proto.shop_price() > character_.meso()) {
      cost = cost | ftxui::color(kRed);
    }
    rows.push_back(ftxui::hbox({
        ftxui::text(cursor + PadRight(proto.name(), kNameWidth)),
        std::move(cost),
        ftxui::text(" "),
    }));
  }
  return ThemedWindow(" Shop ", ftxui::vbox(std::move(rows)));
}

}  // namespace ms
