#include "src/frontend/screens/link_skill_panel.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character_stats.h"
#include "src/character/job_branch.h"
#include "src/character/link.h"
#include "src/character/stat_preset.h"
#include "src/frontend/screens/skill_inspect_panel.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// The width inside a window's border, and how a row divides it. Fixed, because
// the three windows share one header, and a column that grew with its contents
// would leave the header over the wrong one.
constexpr int kContentWidth = 98;
constexpr int kCaretWidth = 2;
constexpr int kNameWidth = 24;
// "12 (+2)", the widest a level and its bonus get.
constexpr int kLevelWidth = 7;
// The blank columns between cells, and the one kept inside the right border,
// since a row that touches it looks like it failed to draw. Both match the item
// list (see item_columns.h).
constexpr int kCellGap = 2;
constexpr int kGutter = 1;
constexpr int kEffectWidth = kContentWidth - kCaretWidth - kNameWidth -
                             kLevelWidth - 2 * kCellGap - kGutter;

// The rows the bottom window shows at once, whatever it holds. The two windows
// above it take 20 rows, so this is what the shortest supported terminal has
// left.
constexpr int kAllRows = 8;

// Where a menu opens inside the screen: past the name, so it covers the skill's
// value rather than which skill it is.
constexpr int kMenuColumn = 30;

// The separator between effects in a column: "Crit Rate +9%". It matches the
// item list, where a cell also lists several effects.
constexpr char kEffectSeparator[] = " · ";

std::string EffectText(const Skill& skill, int level) {
  std::string text;
  for (const SkillEffectLine& line : SkillEffectsAt(skill, level)) {
    if (!text.empty()) {
      text += kEffectSeparator;
    }
    text += line.label + " " + line.value;
  }
  return text;
}

}  // namespace

LinkSkillPanel::LinkSkillPanel(CharacterInstance& character,
                               const std::map<std::string, Skill>& skills)
    : character_(character), skills_(skills) {
}

void LinkSkillPanel::Reset() {
  zone_ = LinkZone::kMine;
  // The preset in use: the farming slot with the autoswap on, or the one in use
  // with it off. Where the player left the bar isn't remembered, so the screen
  // opens on what the character has equipped.
  preset_ = character_.SlotFor(PresetKind::kLinkSkills, Activity::kFarming);
  enabled_row_ = 0;
  all_row_ = 0;
  // On the bar rather than in the list: an empty preset has nothing to select,
  // and the bar is what the player came to the middle window for.
  enabled_in_list_ = false;
  CloseMenu();
}

const Skill* LinkSkillPanel::MineSkill() const {
  for (const Skill* skill :
       SkillsForAdvancement(skills_, JOB_ADVANCEMENT_LINK)) {
    if (BranchOf(skill->link_line()) == BranchOf(character_.proto().job())) {
      return skill;
    }
  }
  return nullptr;
}

std::vector<const Skill*> LinkSkillPanel::EnabledSkills() const {
  std::vector<const Skill*> held;
  for (const std::string& name : character_.link_skills(preset_)) {
    for (const Skill* skill :
         SkillsForAdvancement(skills_, JOB_ADVANCEMENT_LINK)) {
      // The autofill includes every line's skill, earned or not. One that
      // nobody has taken to 70 yet stays in the preset without drawing a row
      // reading 0.
      if (skill->name() == name &&
          character_.LinkSkillLevelOffered(*skill) > 0) {
        held.push_back(skill);
      }
    }
  }
  return held;
}

std::vector<const Skill*> LinkSkillPanel::AllSkills() const {
  std::vector<const Skill*> rest;
  const Skill* mine = MineSkill();
  std::vector<const Skill*> held = EnabledSkills();
  for (const Skill* skill :
       SkillsForAdvancement(skills_, JOB_ADVANCEMENT_LINK)) {
    // A line nobody on the account has taken past its first level grants
    // nothing, and a row reading 0 would only advertise a skill that isn't
    // available.
    if (skill == mine || character_.LinkSkillLevelOffered(*skill) <= 0 ||
        std::find(held.begin(), held.end(), skill) != held.end()) {
      continue;
    }
    rest.push_back(skill);
  }
  return rest;
}

std::vector<const Skill*> LinkSkillPanel::RowsHere() const {
  switch (zone_) {
    case LinkZone::kMine: {
      const Skill* mine = MineSkill();
      return mine == nullptr ? std::vector<const Skill*>()
                             : std::vector<const Skill*>{mine};
    }
    case LinkZone::kEnabled:
      return EnabledSkills();
    case LinkZone::kAll:
      return AllSkills();
  }
  return {};
}

int LinkSkillPanel::RowHere() const {
  switch (zone_) {
    // The top window has one row, so its cursor never moves.
    case LinkZone::kMine:
      return 0;
    case LinkZone::kEnabled:
      return enabled_row_;
    case LinkZone::kAll:
      return all_row_;
  }
  return 0;
}

int LinkSkillPanel::ClampedRow(const std::vector<const Skill*>& rows) const {
  return std::clamp(RowHere(), 0, std::max<int>(0, rows.size() - 1));
}

int LinkSkillPanel::StopsHere() const {
  int rows = static_cast<int>(RowsHere().size());
  // The preset bar is the stop above the middle window's rows, and the only
  // stop an empty preset still has.
  return zone_ == LinkZone::kEnabled ? rows + 1 : rows;
}

void LinkSkillPanel::NextZone(int delta) {
  const LinkZone kOrder[] = {LinkZone::kMine, LinkZone::kEnabled,
                             LinkZone::kAll};
  int at = 0;
  for (int i = 0; i < 3; ++i) {
    if (kOrder[i] == zone_) {
      at = i;
    }
  }
  zone_ = kOrder[StepCursor(at, delta, 3)];
}

void LinkSkillPanel::MoveRow(int delta) {
  std::vector<const Skill*> rows = RowsHere();
  int stops = StopsHere();
  if (stops <= 0) {
    return;
  }
  if (zone_ == LinkZone::kMine) {
    return;  // One row, and nothing above or below it in the window.
  }
  if (zone_ == LinkZone::kAll) {
    all_row_ = StepCursor(ClampedRow(rows), delta, stops);
    return;
  }
  // Stop 0 is the preset bar and the rows are the stops after it, so Up from
  // the first row lands on the bar and Down from the last wraps back to it.
  int stop = enabled_in_list_ ? ClampedRow(rows) + 1 : 0;
  int next = StepCursor(stop, delta, stops);
  enabled_in_list_ = next > 0;
  enabled_row_ = enabled_in_list_ ? next - 1 : enabled_row_;
}

void LinkSkillPanel::MovePreset(int delta) {
  if (zone_ != LinkZone::kEnabled || enabled_in_list_) {
    return;
  }
  preset_ = StatPresetAt(
      std::clamp(IndexOf(preset_) + delta, 0, kNumStatPresets - 1));
  enabled_row_ = 0;
}

LinkCursor LinkSkillPanel::cursor() const {
  if (zone_ == LinkZone::kEnabled && !enabled_in_list_) {
    return {LinkCursor::Kind::kPreset, nullptr};
  }
  std::vector<const Skill*> rows = RowsHere();
  if (rows.empty()) {
    return {LinkCursor::Kind::kNothing, nullptr};
  }
  return {LinkCursor::Kind::kSkill, rows[ClampedRow(rows)]};
}

int LinkSkillPanel::SelectedLevel() const {
  const Skill* skill = cursor().skill;
  return skill == nullptr ? 0 : LevelOf(*skill);
}

bool LinkSkillPanel::AddSelected() {
  const Skill* skill = cursor().skill;
  if (skill == nullptr) {
    return false;
  }
  return character_.EquipLinkSkill(skill->name(), preset_);
}

void LinkSkillPanel::RemoveSelected() {
  const Skill* skill = cursor().skill;
  if (skill == nullptr) {
    return;
  }
  character_.UnequipLinkSkill(skill->name(), preset_);
  // The row that moved up into this place is the next skill. When the preset is
  // empty there is nothing to select, and the cursor moves up to the bar.
  int rows = static_cast<int>(EnabledSkills().size());
  if (rows == 0) {
    enabled_in_list_ = false;
    return;
  }
  enabled_row_ = std::min(enabled_row_, rows - 1);
}

ItemMenu& LinkSkillPanel::OpenMenuHere() {
  switch (zone_) {
    case LinkZone::kMine:
      return mine_menu_;
    case LinkZone::kEnabled:
      return enabled_menu_;
    case LinkZone::kAll:
      break;
  }
  return all_menu_;
}

const ItemMenu& LinkSkillPanel::OpenMenuHere() const {
  return const_cast<LinkSkillPanel*>(this)->OpenMenuHere();
}

void LinkSkillPanel::OpenMenu() {
  LinkCursor at = cursor();
  if (at.kind == LinkCursor::Kind::kNothing) {
    return;
  }
  if (at.kind == LinkCursor::Kind::kPreset) {
    preset_menu_.Reset();
    // Nothing to put in use while the autoswap is choosing, and nothing to do
    // to the preset already in use. The entry stays visible either way, just
    // like the Hyper tab's.
    if (character_.autoswap_presets() ||
        character_.SlotInUse(PresetKind::kLinkSkills) == preset_) {
      preset_menu_.Disable(kPresetMenuUse);
    }
    preset_menu_open_ = true;
    menu_open_ = false;
    return;
  }
  OpenMenuHere().Reset();
  menu_open_ = true;
  preset_menu_open_ = false;
}

void LinkSkillPanel::CloseMenu() {
  menu_open_ = false;
  preset_menu_open_ = false;
}

void LinkSkillPanel::MoveMenuCursor(int delta) {
  ItemMenu& menu = preset_menu_open_ ? preset_menu_ : OpenMenuHere();
  delta < 0 ? menu.Up() : menu.Down();
}

LinkMenuChoice LinkSkillPanel::menu_choice() const {
  int chosen = OpenMenuHere().selected();
  if (chosen == 0) {
    return LinkMenuChoice::kInspect;
  }
  // The entry between Inspect and Close, which the top window doesn't have: a
  // skill held for free can't be added or removed.
  if (chosen == 1 && zone_ == LinkZone::kEnabled) {
    return LinkMenuChoice::kRemove;
  }
  if (chosen == 1 && zone_ == LinkZone::kAll) {
    return LinkMenuChoice::kAdd;
  }
  return LinkMenuChoice::kClose;
}

ftxui::Element LinkSkillPanel::BlankRow() const {
  return ftxui::text(std::string(kContentWidth, ' '));
}

ftxui::Element LinkSkillPanel::RenderHeader() const {
  return ftxui::text(std::string(kCaretWidth, ' ') +
                     PadRight("Name", kNameWidth + kCellGap) +
                     PadRight("Level", kLevelWidth + kCellGap) + "Effect");
}

int LinkSkillPanel::LevelOf(const Skill& skill) const {
  return LevelWithBonus(skill, character_.LinkSkillLevelOffered(skill),
                        BonusSkillLevels(character_, skills_));
}

std::string LinkSkillPanel::LevelText(const Skill& skill) const {
  int learned = character_.LinkSkillLevelOffered(skill);
  int level = LevelOf(skill);
  std::string text = std::to_string(level);
  if (level > learned) {
    text += " (+" + std::to_string(level - learned) + ")";
  }
  return text;
}

ftxui::Element LinkSkillPanel::RenderRow(const Skill& skill, bool on_cursor,
                                         ftxui::Box& box) const {
  std::chrono::steady_clock::duration elapsed =
      on_cursor ? name_clock_.Elapsed()
                : std::chrono::steady_clock::duration::zero();
  // The caret is the only mark: these rows are one list read top to bottom, so
  // a band behind the selected row would repeat what "> " already shows.
  return ftxui::hbox({
             ftxui::text(on_cursor ? "> " : "  "),
             ftxui::text(ScrollingWindow(skill.name(), kNameWidth, elapsed)),
             ftxui::text(std::string(kCellGap, ' ')),
             ftxui::text(PadRight(LevelText(skill), kLevelWidth)),
             ftxui::text(std::string(kCellGap, ' ')),
             ftxui::text(ScrollingWindow(EffectText(skill, LevelOf(skill)),
                                         kEffectWidth, elapsed)),
             ftxui::text(std::string(kGutter, ' ')),
         }) |
         ftxui::reflect(box);
}

ftxui::Element LinkSkillPanel::RenderPresetBar() const {
  const bool focused = zone_ == LinkZone::kEnabled && !enabled_in_list_;
  std::vector<TabSpec> specs;
  for (int i = 0; i < kNumStatPresets; ++i) {
    const StatPreset slot = StatPresetAt(i);
    specs.push_back(
        {PresetSlotLabel(slot, character_.autoswap_presets(),
                         character_.SlotInUse(PresetKind::kLinkSkills) == slot,
                         PresetKind::kLinkSkills)});
  }
  return ftxui::hbox({TabBar(specs, IndexOf(preset_), focused, kContentWidth) |
                          ftxui::reflect(bar_box_),
                      ftxui::filler()});
}

ftxui::Element LinkSkillPanel::RenderMine() const {
  const bool focused = zone_ == LinkZone::kMine;
  const Skill* mine = MineSkill();
  std::vector<ftxui::Element> rows = {RenderHeader(), ThemedSeparator()};
  if (mine == nullptr) {
    rows.push_back(EmptyState("no job line", kCaretWidth));
  } else {
    if (focused) {
      name_clock_.Follow(0, true);
    }
    rows.push_back(
        RenderRow(*mine, focused, focused ? cursor_box_ : scratch_box_));
  }
  return ThemedWindow(
      " My Skill ",
      ftxui::vbox(std::move(rows)) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth),
      focused);
}

ftxui::Element LinkSkillPanel::RenderEnabled() const {
  const bool focused = zone_ == LinkZone::kEnabled && enabled_in_list_;
  std::vector<const Skill*> held = EnabledSkills();
  int cursor = ClampedRow(held);
  if (focused) {
    // The preset is part of the key, so the same row in another preset counts
    // as a different skill and starts from the beginning.
    name_clock_.Follow(1 + IndexOf(preset_) * kMaxEquippedLinkSkills + cursor,
                       true);
  }
  std::vector<ftxui::Element> rows = {RenderPresetBar()};
  for (int i = 0; i < kMaxEquippedLinkSkills; ++i) {
    if (i >= static_cast<int>(held.size())) {
      // The unused slots of a preset are drawn so the window stays one size,
      // and so an empty preset says so on its first row.
      rows.push_back(i == 0 ? EmptyState("none equipped", kCaretWidth)
                            : BlankRow());
      continue;
    }
    rows.push_back(
        RenderRow(*held[i], focused && i == cursor,
                  focused && i == cursor ? cursor_box_ : scratch_box_));
  }
  return ThemedWindow(
      " Enabled Skills ",
      ftxui::vbox(std::move(rows)) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth),
      zone_ == LinkZone::kEnabled);
}

ftxui::Element LinkSkillPanel::RenderAll() const {
  const bool focused = zone_ == LinkZone::kAll;
  std::vector<const Skill*> rest = AllSkills();
  int cursor = ClampedRow(rest);
  if (focused) {
    name_clock_.Follow(1 + kNumStatPresets * kMaxEquippedLinkSkills + cursor,
                       true);
  }
  int total = static_cast<int>(rest.size());
  int first = ScrollWindowStart(total, cursor, kAllRows);
  std::vector<ftxui::Element> cells = ScrollBarCells(total, first, kAllRows);
  std::vector<ftxui::Element> rows;
  // Always kAllRows rows, so the window stays one size whatever the account has
  // left to offer, the same rule as the middle window's slots.
  for (int i = 0; i < kAllRows; ++i) {
    if (first + i >= total) {
      rows.push_back(i == 0 ? EmptyState("nothing left to add", kCaretWidth)
                            : BlankRow());
      continue;
    }
    const Skill& skill = *rest[first + i];
    ftxui::Element row =
        RenderRow(skill, focused && first + i == cursor,
                  focused && first + i == cursor ? cursor_box_ : scratch_box_);
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
  return ThemedWindow(
      " All Skills ",
      ftxui::vbox(std::move(rows)) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth),
      focused);
}

int LinkSkillPanel::MenuRow() const {
  // Both boxes are where the render placed them, in screen coordinates, and the
  // menu is placed from the screen's own corner, so the screen's top is
  // subtracted. One row back from the cursor, so the highlighted entry sits
  // beside what the menu is about rather than below it.
  int row = preset_menu_open_ ? bar_box_.y_min + 1 : cursor_box_.y_min - 1;
  return row - panel_box_.y_min;
}

ftxui::Element LinkSkillPanel::Render() const {
  ftxui::Element screen = ftxui::vbox({
                              RenderMine(),
                              RenderEnabled(),
                              RenderAll(),
                          }) |
                          ftxui::reflect(panel_box_);
  if (!menu_open_ && !preset_menu_open_) {
    return screen;
  }
  const ItemMenu& menu = preset_menu_open_ ? preset_menu_ : OpenMenuHere();
  return ftxui::dbox({
      std::move(screen),
      Floating(menu.Render(MenuRow(), kMenuColumn)),
  });
}

}  // namespace ms
