#include "src/frontend/screens/hyper_stat_inspect_panel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/hyper_stats.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// The same indent and gutter the skill card uses for its effect rows, so the
// two cards look like the same kind of thing.
constexpr int kEffectIndent = 3;
constexpr int kLabelGap = 2;
constexpr int kCardChrome = 2;  // the two border columns
// The blank column the widest value keeps before the right border, matching the
// one the indent leaves on the left. A card measured from its own text has no
// slack, so this has to be added.
constexpr int kRightGutter = 1;

// The widest name and the widest value any stat reaches at any level, which
// together set the width of every card. Measured rather than written down, so
// adding a stat widens the card without changing this file.
int LabelWidth() {
  int widest = 0;
  for (int i = 0; i < kNumHyperStats; ++i) {
    widest = std::max(widest, TextColumns(HyperStatName(kHyperStatOrder[i])));
  }
  return widest;
}

// Every level, not just the maximum: EXP moves in half steps below level 10 and
// whole percents above it, so its widest value ("+4.5%") is not its last one
// ("+10%"). Measuring only the maximum would make the card a column short.
int ValueWidth() {
  int widest = 0;
  for (int i = 0; i < kNumHyperStats; ++i) {
    for (int level = 1; level <= kMaxHyperStatLevel; ++level) {
      widest = std::max(
          widest, TextColumns(HyperStatBonusText(kHyperStatOrder[i], level)));
    }
  }
  return widest;
}

// The heading of a level block. The level the player already has is shown
// plain, and the next one shows its price.
std::string LevelHeading(int level, bool priced) {
  std::string heading = " Level " + std::to_string(level);
  if (!priced) {
    return heading;
  }
  int cost = HyperStatLevelCost(level);
  return heading + " - " + std::to_string(cost) +
         (cost == 1 ? " point" : " points");
}

// The widest a priced heading gets, which the highest level decides.
int HeadingWidth() {
  int widest = 0;
  for (int level = 1; level <= kMaxHyperStatLevel; ++level) {
    widest = std::max(widest, TextColumns(LevelHeading(level, true)));
  }
  return widest;
}

// The name above the maximum level, centred, like the skill card's heading
// block.
std::vector<ftxui::Element> Heading(HyperStatField field, int max_level,
                                    int content) {
  std::vector<ftxui::Element> rows;
  rows.push_back(CenteredRow(HyperStatName(field)) |
                 ftxui::size(ftxui::WIDTH, ftxui::EQUAL, content));
  rows.push_back(CenteredRow("Max Level: " + std::to_string(max_level)) |
                 ftxui::size(ftxui::WIDTH, ftxui::EQUAL, content));
  return rows;
}

// Adds one block to the card: a rule, the level heading, and the one line the
// stat is worth at that level.
void AppendLevelBlock(HyperStatField field, int level, bool priced, int content,
                      std::vector<ftxui::Element>& rows) {
  rows.push_back(ThemedSeparator());
  rows.push_back(ftxui::text(PadRight(LevelHeading(level, priced), content)));
  std::string line = std::string(kEffectIndent, ' ') +
                     PadRight(HyperStatName(field), LabelWidth() + kLabelGap) +
                     HyperStatBonusText(field, level);
  rows.push_back(ftxui::text(PadRight(line, content)));
}

}  // namespace

int HyperStatInspectPanel::Columns() {
  int effect = kEffectIndent + LabelWidth() + kLabelGap + ValueWidth();
  return kCardChrome + std::max(effect, HeadingWidth()) + kRightGutter;
}

void HyperStatInspectPanel::SetStat(HyperStatField field, int level,
                                    int max_level) {
  field_ = field;
  level_ = level;
  max_level_ = max_level;
}

ftxui::Element HyperStatInspectPanel::Render() const {
  if (field_ == HYPER_STAT_FIELD_UNSPECIFIED) {
    return ThemedWindow(" Hyper Stat ", EmptyState("no stat"));
  }
  int content = Columns() - kCardChrome;
  std::vector<ftxui::Element> rows = Heading(field_, max_level_, content);
  // A stat with nothing spent has no block for its current level, and one at
  // its maximum has no next block, the same rule as the skill card.
  if (level_ > 0) {
    AppendLevelBlock(field_, level_, /*priced=*/false, content, rows);
  }
  if (level_ < max_level_) {
    AppendLevelBlock(field_, level_ + 1, /*priced=*/true, content, rows);
  }
  return ThemedWindow(" Hyper Stat ", ftxui::vbox(std::move(rows)));
}

}  // namespace ms
