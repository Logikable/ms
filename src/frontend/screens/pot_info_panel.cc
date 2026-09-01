#include "src/frontend/screens/pot_info_panel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/consumables.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// The two border columns, and the gutter either side of the text between them.
constexpr int kCardChrome = 4;

// What an owned pot's price row says in place of the price. It keeps the row
// rather than dropping it: a player who bought one is owed the news.
constexpr char kOwnedText[] = "Unlocked permanently";

// Every row of `info` that has to fit inside the border, for the measuring
// below.
std::vector<std::string> CardLines(const ConsumableInfo& info) {
  std::vector<std::string> lines = {info.name};
  for (const char* effect : info.effects) {
    lines.push_back(effect);
  }
  lines.push_back(ConsumableRentText(info.type));
  lines.push_back(ConsumablePermanentText(info.type));
  return lines;
}

}  // namespace

std::string ConsumableRentText(ConsumableType type) {
  const ConsumableInfo* info = ConsumableInfoFor(type);
  if (info == nullptr) {
    return "";
  }
  return FormatMeso(info->price) +
         (info->per_second ? " per second while farming" : " per boss entry");
}

std::string ConsumablePermanentText(ConsumableType type) {
  const ConsumableInfo* info = ConsumableInfoFor(type);
  if (info == nullptr) {
    return "";
  }
  return FormatMeso(info->permanent_price) + " to unlock permanently";
}

int PotInfoPanel::Columns() {
  int widest = TextColumns(kOwnedText);
  for (const ConsumableInfo& info : AllConsumables()) {
    for (const std::string& line : CardLines(info)) {
      widest = std::max(widest, TextColumns(line));
    }
  }
  return kCardChrome + widest;
}

void PotInfoPanel::SetPot(ConsumableType type, bool owned) {
  type_ = type;
  owned_ = owned;
}

ftxui::Element PotInfoPanel::Render() const {
  const ConsumableInfo* info = ConsumableInfoFor(type_);
  if (info == nullptr) {
    return ThemedWindow(" Pot Info ", EmptyState("no pot"));
  }
  const int content = Columns() - 2;
  // Every row is centred on the card's one width, which the widest line of the
  // widest pot sets.
  auto row = [content](const std::string& text) {
    return CenteredRow(text) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, content);
  };
  std::vector<ftxui::Element> rows = {row(info->name), ThemedSeparator()};
  for (const char* effect : info->effects) {
    rows.push_back(row(effect));
  }
  rows.push_back(ThemedSeparator());
  // A bought pot is never charged again, so its rent is a fact about the pot
  // rather than a price this player pays: it dims, and the row under it says
  // so outright.
  ftxui::Element rent = row(ConsumableRentText(type_));
  if (owned_) {
    rent = std::move(rent) | ftxui::dim;
  }
  rows.push_back(std::move(rent));
  rows.push_back(row(owned_ ? kOwnedText : ConsumablePermanentText(type_)));
  return ThemedWindow(" Pot Info ", ftxui::vbox(std::move(rows)));
}

}  // namespace ms
