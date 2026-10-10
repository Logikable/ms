#include "src/frontend/screens/legion_panel.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/legion.h"
#include "src/character/stat_preset.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/keys.h"
#include "src/protos/save.pb.h"

namespace ms {
namespace {

// The sixteen stats in the order the grid lists them, the first eight above
// the rule.
constexpr LegionStat kBaseStats[] = {
    LEGION_STAT_STR,    LEGION_STAT_DEX,          LEGION_STAT_INT,
    LEGION_STAT_LUK,    LEGION_STAT_MAX_HP,       LEGION_STAT_MAX_MP,
    LEGION_STAT_ATTACK, LEGION_STAT_MAGIC_ATTACK,
};
constexpr LegionStat kExpandedStats[] = {
    LEGION_STAT_STATUS_RESISTANCE,
    LEGION_STAT_EXP,
    LEGION_STAT_CRIT_RATE,
    LEGION_STAT_BOSS_DAMAGE,
    LEGION_STAT_NORMAL_DAMAGE,
    LEGION_STAT_BUFF_DURATION,
    LEGION_STAT_IED,
    LEGION_STAT_CRIT_DAMAGE,
};

const char* kTabLabels[] = {"Grid", "Members"};
constexpr int kNumTabs = 2;

// A stat row: gutter, name, amount, gap, [-], points, [+], gutter. The amount
// sits by the buttons with a gap either side, and the name takes the rest.
constexpr int kGutter = 2;
// "+3,750" and "+0.75%", the widest amounts.
constexpr int kAmountWidth = 6;
constexpr int kAmountGap = 3;
constexpr int kButtonWidth = 3;
// "40/40".
constexpr int kPointsWidth = 5;
constexpr int kNameWidth = LegionPanel::kContentWidth - 2 * kGutter -
                           kAmountWidth - kAmountGap - 2 * kButtonWidth -
                           kPointsWidth - 2;

// The Members columns.
constexpr int kCellGap = 2;
constexpr int kMemberNameWidth = kMaxUsernameLength;
constexpr int kLevelWidth = 5;         // "Level"
constexpr int kRankWidth = 4;          // "Rank"
constexpr int kPointsColumnWidth = 6;  // "Points"

// Where the preset menu opens: past the chips, as on the Hyper tab.
constexpr int kMenuColumn = 22;

}  // namespace

LegionPanel::LegionPanel(GameState& state) : state_(state) {
}

void LegionPanel::Reset() {
  tab_ = LegionTab::kGrid;
  zone_ = LegionZone::kTabs;
  stat_ = LEGION_STAT_STR;
  on_plus_ = true;
  preset_ = state_.character.SlotFor(PresetKind::kLegion, Activity::kFarming);
  first_member_ = 0;
  preset_menu_open_ = false;
}

std::vector<LegionStat> LegionPanel::OpenStats() const {
  const int rank = state_.character.legion_summary().rank;
  std::vector<LegionStat> open(std::begin(kBaseStats), std::end(kBaseStats));
  for (LegionStat stat : kExpandedStats) {
    if (LegionStatCap(stat, rank) > 0) {
      open.push_back(stat);
    }
  }
  return open;
}

void LegionPanel::MoveRow(int delta) {
  if (tab_ == LegionTab::kMembers) {
    const int total = static_cast<int>(Members().size());
    first_member_ =
        std::clamp(first_member_ + delta, 0, std::max(0, total - kMemberRows));
    return;
  }
  // The ring: tabs, presets, each open stat, [Reset].
  const std::vector<LegionStat> stats = OpenStats();
  const int count = static_cast<int>(stats.size()) + 3;
  int at = 0;
  switch (zone_) {
    case LegionZone::kTabs:
      at = 0;
      break;
    case LegionZone::kPresets:
      at = 1;
      break;
    case LegionZone::kStat: {
      auto it = std::find(stats.begin(), stats.end(), stat_);
      at = it == stats.end() ? 2 : 2 + static_cast<int>(it - stats.begin());
      break;
    }
    case LegionZone::kReset:
      at = count - 1;
      break;
  }
  at = StepCursor(at, delta, count);
  if (at == 0) {
    zone_ = LegionZone::kTabs;
  } else if (at == 1) {
    zone_ = LegionZone::kPresets;
  } else if (at == count - 1) {
    zone_ = LegionZone::kReset;
  } else {
    zone_ = LegionZone::kStat;
    stat_ = stats[at - 2];
  }
}

void LegionPanel::MoveColumn(int delta) {
  switch (zone_) {
    case LegionZone::kTabs:
      tab_ = static_cast<LegionTab>(
          std::clamp(static_cast<int>(tab_) + delta, 0, kNumTabs - 1));
      return;
    case LegionZone::kPresets:
      preset_ = StatPresetAt(
          std::clamp(IndexOf(preset_) + delta, 0, kNumStatPresets - 1));
      return;
    case LegionZone::kStat:
      on_plus_ = delta > 0;
      return;
    case LegionZone::kReset:
      return;
  }
}

bool LegionPanel::Activate() {
  if (tab_ != LegionTab::kGrid) {
    return false;
  }
  switch (zone_) {
    case LegionZone::kTabs:
      return false;
    case LegionZone::kPresets:
      preset_menu_.Reset();
      // As on the Hyper tab: nothing to put in use while the autoswap chooses,
      // or on the one already in use. The entry stays, dimmed.
      if (state_.character.autoswap_presets() ||
          state_.character.SlotInUse(PresetKind::kLegion) == preset_) {
        preset_menu_.Disable(kPresetMenuUse);
      }
      preset_menu_open_ = true;
      return false;
    case LegionZone::kStat:
      Spend(on_plus_ ? 1 : -1);
      return false;
    case LegionZone::kReset:
      return true;
  }
  return false;
}

void LegionPanel::MoveMenuCursor(int delta) {
  delta < 0 ? preset_menu_.Up() : preset_menu_.Down();
}

std::map<LegionStat, int> LegionPanel::Effective() const {
  const LegionSummary summary = state_.character.legion_summary();
  return EffectiveLegionPoints(PresetOf(state_.account.legion(), preset_),
                               summary.rank, summary.points);
}

int LegionPanel::PointsLeft() const {
  int spent = 0;
  for (const auto& [stat, points] : Effective()) {
    spent += points;
  }
  return std::max(0, state_.character.legion_summary().points - spent);
}

void LegionPanel::Spend(int delta) {
  const LegionSummary summary = state_.character.legion_summary();
  const std::map<LegionStat, int> effective = Effective();
  Legion& legion = *state_.account.mutable_legion();
  LegionPreset& preset = PresetOf(legion, preset_);
  preset.clear_points();
  for (const auto& [stat, points] : effective) {
    (*preset.mutable_points())[stat] = points;
  }
  SpendLegionPoints(legion, preset_, stat_, delta, summary);
  Mirror();
}

void LegionPanel::ResetPreset() {
  ResetLegionPreset(*state_.account.mutable_legion(), preset_);
  Mirror();
}

void LegionPanel::Mirror() {
  state_.character.set_legion(state_.account.legion());
}

std::vector<LegionMemberRow> LegionPanel::Members() const {
  std::vector<LegionMemberRow> rows;
  const Character& played = state_.character.proto();
  rows.push_back({played.name(), played.job(), played.level()});
  for (const CharacterSave& save : state_.inactive_characters) {
    const Character& other = save.character();
    rows.push_back({other.name(), other.job(), other.level()});
  }
  rows.erase(std::remove_if(rows.begin(), rows.end(),
                            [](const LegionMemberRow& row) {
                              return CharacterRankFor(row.level) ==
                                     CharacterRank::kNone;
                            }),
             rows.end());
  // Stable, as SummarizeLegion's own order decides nothing between equal
  // levels: either of two tied characters gives the same points.
  std::stable_sort(rows.begin(), rows.end(),
                   [](const LegionMemberRow& a, const LegionMemberRow& b) {
                     return a.level > b.level;
                   });
  const int members = state_.character.legion_summary().members;
  for (int i = members; i < static_cast<int>(rows.size()); ++i) {
    rows[i].gives_points = false;
  }
  return rows;
}

ftxui::Element LegionPanel::RenderTabs() const {
  std::vector<TabSpec> specs;
  for (const char* label : kTabLabels) {
    specs.push_back({label});
  }
  return TabBar(specs, static_cast<int>(tab_), zone_ == LegionZone::kTabs,
                kContentWidth);
}

ftxui::Element LegionPanel::RenderPresetBar() const {
  const bool autoswap = state_.character.autoswap_presets();
  const StatPreset in_use = state_.character.SlotInUse(PresetKind::kLegion);
  std::vector<TabSpec> specs;
  for (int i = 0; i < kNumStatPresets; ++i) {
    const StatPreset slot = StatPresetAt(i);
    specs.push_back(
        {PresetSlotLabel(slot, autoswap, in_use == slot, PresetKind::kLegion)});
  }
  // The counter reads like the Hyper tab's: what is left, on the row above
  // where it is spent.
  const std::string trailing = std::to_string(PointsLeft()) + " Points";
  const int width =
      kContentWidth - static_cast<int>(ftxui::string_width(trailing)) - 1;
  return ftxui::hbox({
      TabBar(specs, IndexOf(preset_), zone_ == LegionZone::kPresets, width) |
          ftxui::reflect(bar_box_),
      ftxui::filler(),
      ftxui::text(trailing + " "),
  });
}

ftxui::Element LegionPanel::RenderStat(
    LegionStat stat, const std::map<LegionStat, int>& effective) const {
  const LegionSummary summary = state_.character.legion_summary();
  const int cap = LegionStatCap(stat, summary.rank);
  const bool locked = cap <= 0;
  const auto found = effective.find(stat);
  const int points = found == effective.end() ? 0 : found->second;
  const bool selected = zone_ == LegionZone::kStat && stat_ == stat;

  const std::string points_text =
      std::to_string(points) + "/" + std::to_string(cap);
  ftxui::Element minus = ftxui::text("[-]");
  if (selected && !on_plus_) {
    minus = std::move(minus) | ftxui::inverted;
  } else if (locked || points <= 0) {
    minus = std::move(minus) | ftxui::dim;
  }
  ftxui::Element plus = ftxui::text("[+]");
  if (selected && on_plus_) {
    plus = std::move(plus) | ftxui::inverted;
  } else if (locked || points >= cap || PointsLeft() <= 0) {
    plus = std::move(plus) | ftxui::dim;
  }
  ftxui::Element row = ftxui::hbox({
      ftxui::text(std::string(kGutter, ' ')),
      ftxui::text(PadRight(LegionStatName(stat), kNameWidth)),
      ftxui::text(PadLeft(LegionStatBonusText(stat, points), kAmountWidth)),
      ftxui::text(std::string(kAmountGap, ' ')),
      std::move(minus),
      ftxui::text(" " + PadLeft(points_text, kPointsWidth) + " "),
      std::move(plus),
      ftxui::text(std::string(kGutter, ' ')),
  });
  // A stat the rank hasn't opened isn't one the Legion has yet, so the whole
  // row dims, as a locked Hyper Stat's does.
  return locked ? std::move(row) | ftxui::dim : row;
}

ftxui::Element LegionPanel::RenderGrid() const {
  const std::map<LegionStat, int> effective = Effective();
  // Straight under the tab row, with no rule: the presets are its subtabs.
  std::vector<ftxui::Element> rows = {RenderPresetBar(), ThemedSeparator()};
  for (LegionStat stat : kBaseStats) {
    rows.push_back(RenderStat(stat, effective));
  }
  rows.push_back(ThemedSeparator());
  for (LegionStat stat : kExpandedStats) {
    rows.push_back(RenderStat(stat, effective));
  }
  // Under its own rule rather than among the rows it undoes, as on the Hyper
  // tab.
  rows.push_back(ThemedSeparator());
  rows.push_back(ftxui::hbox({
      ftxui::filler(),
      ActionButton("Reset", zone_ == LegionZone::kReset),
      ftxui::filler(),
  }));
  return ftxui::vbox(std::move(rows));
}

ftxui::Element LegionPanel::RenderMembers() const {
  std::vector<ftxui::Element> rows = {
      ThemedSeparator(),
      ftxui::text(std::string(kGutter, ' ') +
                  PadRight("Name", kMemberNameWidth + kCellGap) +
                  PadRight("Level", kLevelWidth + kCellGap) +
                  PadRight("Rank", kRankWidth + kCellGap) +
                  PadRight("Points", kPointsColumnWidth + kCellGap) + "Effect"),
      ThemedSeparator(),
  };
  const std::vector<LegionMemberRow> members = Members();
  const int total = static_cast<int>(members.size());
  const int first =
      std::clamp(first_member_, 0, std::max(0, total - kMemberRows));
  std::vector<ftxui::Element> cells = ScrollBarCells(total, first, kMemberRows);
  for (int i = 0; i < kMemberRows; ++i) {
    ftxui::Element row;
    if (first + i < total) {
      const LegionMemberRow& member = members[first + i];
      const CharacterRank rank = CharacterRankFor(member.level);
      // Only who they are and their points dim: their job effect counts
      // either way.
      const int points = member.gives_points ? LegionPointsFor(rank) : 0;
      ftxui::Element who = ftxui::text(
          PadRight(member.name, kMemberNameWidth + kCellGap) +
          PadRight(std::to_string(member.level), kLevelWidth + kCellGap) +
          PadRight(CharacterRankName(rank), kRankWidth + kCellGap) +
          PadRight(std::to_string(points), kPointsColumnWidth + kCellGap));
      if (!member.gives_points) {
        who = std::move(who) | ftxui::dim;
      }
      row = ftxui::hbox({
          ftxui::text(std::string(kGutter, ' ')),
          std::move(who),
          ftxui::text(
              LegionJobEffectText(LegionJobEffectFor(member.job, rank))),
          ftxui::filler(),
      });
    } else if (i == 0) {
      row = EmptyState("no ranked characters", kGutter);
    } else {
      row = ftxui::text("");
    }
    if (cells.empty()) {
      rows.push_back(std::move(row));
      continue;
    }
    rows.push_back(ftxui::hbox({
        std::move(row) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth - 1),
        std::move(cells[i]) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1),
    }));
  }
  return ftxui::vbox(std::move(rows));
}

ftxui::Element LegionPanel::Render() const {
  ftxui::Element body =
      tab_ == LegionTab::kGrid ? RenderGrid() : RenderMembers();
  ftxui::Element window =
      ThemedWindow(" Legion ",
                   ftxui::vbox({RenderTabs(), std::move(body)}) |
                       ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth) |
                       ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kContentRows)) |
      ftxui::reflect(panel_box_);
  if (!preset_menu_open_) {
    return window;
  }
  // One row below the preset row, in the window's own coordinates, so the
  // highlighted entry sits beside the chips it is about.
  const int row = bar_box_.y_min + 1 - panel_box_.y_min;
  return ftxui::dbox({
      std::move(window),
      Floating(preset_menu_.Render(row, kMenuColumn)),
  });
}

}  // namespace ms
