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
// rather than dropping it: the card is the same height for every pot, and a
// player who bought one is owed the news.
constexpr char kOwnedText[] = "Unlocked permanently";

// Every row of `info` that has to fit inside the border, for the measuring
// below. The name is one of them: it is centred, not wrapped.
std::vector<std::string> CardLines(const ConsumableInfo& info) {
  std::vector<std::string> lines = {info.name};
  for (const char* effect : info.effects) {
    lines.push_back(effect);
  }
  lines.push_back(ConsumableRentText(info.type));
  lines.push_back(ConsumablePermanentText(info.type));
  return lines;
}

// The most effect rows any pot carries, which is the height every card keeps.
int MaxEffectRows() {
  int most = 0;
  for (const ConsumableInfo& info : AllConsumables()) {
    most = std::max(most, static_cast<int>(info.effects.size()));
  }
  return most;
}

// A row of the card's body: one gutter, the text, and whatever is left.
ftxui::Element BodyRow(const std::string& text, int content) {
  return ftxui::text(PadRight(" " + text, content));
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
  std::vector<ftxui::Element> rows = {
      CenteredRow(info->name) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, content),
      ThemedSeparator(),
  };
  for (const char* effect : info->effects) {
    rows.push_back(BodyRow(effect, content));
  }
  // Blank rows up to the tallest pot's list, so the card does not change
  // height as the cursor walks the tab.
  for (int i = static_cast<int>(info->effects.size()); i < MaxEffectRows();
       ++i) {
    rows.push_back(ftxui::text(std::string(content, ' ')));
  }
  rows.push_back(ThemedSeparator());
  // A bought pot is never charged again, so its rent is a fact about the pot
  // rather than a price this player pays: it dims, and the row under it says
  // so outright.
  ftxui::Element rent = BodyRow(ConsumableRentText(type_), content);
  if (owned_) {
    rent = std::move(rent) | ftxui::dim;
  }
  rows.push_back(std::move(rent));
  rows.push_back(
      BodyRow(owned_ ? kOwnedText : ConsumablePermanentText(type_), content));
  return ThemedWindow(" Pot Info ", ftxui::vbox(std::move(rows)));
}

}  // namespace ms
