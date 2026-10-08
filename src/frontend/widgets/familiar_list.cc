#include "src/frontend/widgets/familiar_list.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/character/familiar.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/item_row.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/protos/familiar.pb.h"

namespace ms {
namespace {

constexpr int kCursor = 2;
constexpr int kGap = 2;
// The blank column before a panel's right border.
constexpr int kGutter = 1;
constexpr char kInUseHeader[] = "In Use";

int LongestMob() {
  int widest = 0;
  for (const FamiliarSpecies& species : FamiliarRoster()) {
    widest = std::max(widest, TextColumns(species.name));
  }
  return widest;
}

// Values first and no "+", as in an item's potential column: "40% Boss".
std::string LineEffect(const FamiliarLine& line) {
  return FamiliarLineValueText(line).substr(1) + " " +
         FamiliarLineShortName(line.type());
}

std::string Centered(const std::string& text, int width) {
  const int lead = std::max(0, (width - TextColumns(text)) / 2);
  return PadRight(std::string(lead, ' ') + text, width);
}

}  // namespace

FamiliarColumns FitFamiliarColumns(int width, bool in_use) {
  FamiliarColumns columns;
  // A familiar starts named after its mob, so the column holds the longest of
  // those as well as the longest name a player can type.
  columns.mob = LongestMob();
  columns.name = std::max(kMaxUsernameLength, columns.mob);
  columns.level = 2;  // "Lv"
  columns.in_use = in_use;
  int used = kCursor + columns.name + kGap + columns.mob + kGap +
             columns.level + kGap + kGutter;
  if (in_use) {
    used += kGap + static_cast<int>(sizeof(kInUseHeader) - 1);
  }
  columns.potential = std::max(1, width - used);
  return columns;
}

std::string FamiliarHeader(const FamiliarColumns& columns) {
  std::string header =
      std::string(kCursor, ' ') + PadRight("Name", columns.name) + "  " +
      PadRight("Mob", columns.mob) + "  " + PadRight("Lv", columns.level) +
      "  " + PadRight("Potential", columns.potential);
  if (columns.in_use) {
    header += std::string("  ") + kInUseHeader;
  }
  return header + std::string(kGutter, ' ');
}

FamiliarCells FamiliarRowCells(const FamiliarBook& book,
                               const std::string& species,
                               const FamiliarColumns& columns, bool in_use) {
  FamiliarCells cells;
  cells.name = PadRight(FamiliarDisplayName(book, species), columns.name);
  cells.mob = PadRight(species, columns.mob);
  const int level = FamiliarLevel(book, species);
  cells.level = PadRight(std::to_string(level), columns.level);
  std::vector<std::string> effects;
  if (const Familiar* familiar = FindFamiliar(book, species)) {
    for (const FamiliarLine& line : familiar->lines()) {
      effects.push_back(LineEffect(line));
    }
  }
  if (effects.empty()) {
    effects.push_back("-");
  }
  cells.potential = JoinEffects(effects, columns.potential);
  cells.rank = FamiliarRank(level);
  if (columns.in_use) {
    cells.in_use = Centered(in_use ? "✓" : "", sizeof(kInUseHeader) - 1);
  }
  return cells;
}

ftxui::Element FamiliarRowElement(const std::string& cursor,
                                  const FamiliarCells& cells,
                                  ftxui::Element name) {
  std::vector<ftxui::Element> parts = {ftxui::text(cursor)};
  parts.push_back(name != nullptr ? std::move(name) : ftxui::text(cells.name));
  parts.push_back(ftxui::text("  " + cells.mob + "  " + cells.level + "  "));
  parts.push_back(ftxui::text(cells.potential) |
                  PotentialCellColor(cells.rank));
  if (!cells.in_use.empty()) {
    parts.push_back(ftxui::text("  " + cells.in_use));
  }
  parts.push_back(ftxui::text(std::string(kGutter, ' ')));
  return ftxui::hbox(std::move(parts));
}

}  // namespace ms
