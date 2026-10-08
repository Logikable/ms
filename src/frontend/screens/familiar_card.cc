#include "src/frontend/screens/familiar_card.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/familiar.h"
#include "src/frontend/screens/inspect_panel.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/scroll_card.h"

namespace ms {
namespace {

// The equip card's width, so the two read as the same card.
constexpr int kContentWidth = kEquipCardWidth - 3;

// One diamond per level, filled in the familiar's rank colour, where an equip
// has its star bar.
ftxui::Element LevelBar(int level) {
  std::vector<ftxui::Element> parts;
  for (int i = 0; i < kFamiliarMaxLevel; ++i) {
    const bool filled = i < level;
    parts.push_back(
        ftxui::text(filled ? "◆" : "◇") |
        ftxui::color(filled ? RarityColor(FamiliarRank(level)) : kGray));
  }
  return ftxui::hbox(std::move(parts));
}

std::string ExpText(int level) {
  if (level >= kFamiliarMaxLevel) {
    return "MAX";
  }
  return FormatWithCommas(FamiliarLevelCost(level + 1)) + " to Lv " +
         std::to_string(level + 1);
}

}  // namespace

void FamiliarCard::SetFamiliar(const FamiliarBook* book,
                               const std::string& species) {
  if (species != species_) {
    card_.Reset();
  }
  book_ = book;
  species_ = species;
}

ftxui::Element FamiliarCard::Render(bool focused,
                                    const std::string& title) const {
  if (book_ == nullptr || species_.empty()) {
    return ThemedWindow(title, EmptyState("no familiar"), focused) |
           ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kEquipCardWidth);
  }
  const int level = FamiliarLevel(*book_, species_);
  CardRows rows;
  // The bar and name stay put, as an equip's do, and the rest scrolls. The
  // body is never empty: ScrollCard needs a row to scroll.
  rows.head = {
      TextRow(CenteredRow(LevelBar(level))),
      TextRow(CenteredRow(FamiliarDisplayName(*book_, species_))),
      RuleRow(ThemedSeparator()),
  };
  rows.body = {
      // A trailing space on each text row keeps one blank column before the
      // right border.
      TextRow(ftxui::text(" Mob: " + species_ + " ")),
      TextRow(ftxui::text(" Level: " + std::to_string(level) + " / " +
                          std::to_string(kFamiliarMaxLevel) + " ")),
      TextRow(ftxui::text(" EXP: " + ExpText(level) + " ")),
  };
  const Familiar* familiar = FindFamiliar(*book_, species_);
  if (familiar != nullptr && !familiar->lines().empty()) {
    const PotentialRank rank = FamiliarRank(level);
    rows.body.push_back(RuleRow(ThemedSeparator()));
    rows.body.push_back(
        TextRow(ftxui::text(" " + PotentialRankName(rank) + " Potential ") |
                ftxui::color(RarityColor(rank))));
    // The rank dot an equip's lines carry: a line a rank below has that
    // rank's colour.
    for (const FamiliarLine& line : familiar->lines()) {
      rows.body.push_back(TextRow(ftxui::hbox({
          ftxui::text(" ◼") | ftxui::color(RarityColor(line.rank())),
          ftxui::text("  " + FamiliarLineName(line.type()) + "  " +
                      FamiliarLineValueText(line) + " "),
      })));
    }
  }
  return card_.Render(title, std::move(rows), kContentWidth, focused);
}

}  // namespace ms
