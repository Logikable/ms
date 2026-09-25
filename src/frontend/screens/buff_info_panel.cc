#include "src/frontend/screens/buff_info_panel.h"

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

// The two border columns and the gutter on each side of the text between them.
constexpr int kCardChrome = 4;

// What an owned buff's price row shows instead of a price. The row stays rather
// than disappearing, since a player who bought the buff should be told.
constexpr char kOwnedText[] = "Unlocked permanently";

// Every row of `info` that has to fit inside the border, for measuring.
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

int BuffInfoPanel::Columns() {
  int widest = TextColumns(kOwnedText);
  for (const ConsumableInfo& info : AllConsumables()) {
    for (const std::string& line : CardLines(info)) {
      widest = std::max(widest, TextColumns(line));
    }
  }
  return kCardChrome + widest;
}

void BuffInfoPanel::SetBuff(ConsumableType type, bool owned) {
  type_ = type;
  owned_ = owned;
}

ftxui::Element BuffInfoPanel::Render() const {
  const ConsumableInfo* info = ConsumableInfoFor(type_);
  if (info == nullptr) {
    return ThemedWindow(" Buff Info ", EmptyState("no buff"));
  }
  const int content = Columns() - 2;
  // Every row is centred on the card's single width, which the widest line of
  // the widest buff sets.
  auto row = [content](const std::string& text) {
    return CenteredRow(text) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, content);
  };
  std::vector<ftxui::Element> rows = {row(info->name), ThemedSeparator()};
  for (const char* effect : info->effects) {
    rows.push_back(row(effect));
  }
  rows.push_back(ThemedSeparator());
  // A bought buff is never charged again, so its rent is only a fact about the
  // buff, not a price this player pays: it is dimmed, and the row below says
  // so.
  ftxui::Element rent = row(ConsumableRentText(type_));
  if (owned_) {
    rent = std::move(rent) | ftxui::dim;
  }
  rows.push_back(std::move(rent));
  rows.push_back(row(owned_ ? kOwnedText : ConsumablePermanentText(type_)));
  return ThemedWindow(" Buff Info ", ftxui::vbox(std::move(rows)));
}

}  // namespace ms
