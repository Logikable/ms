#include "src/frontend/panels/character_panel.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/character_stats.h"
#include "src/character/job_name.h"
#include "src/character/progression.h"
#include "src/combat/damage.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/marquee.h"
#include "src/frontend/widgets/stat_rows.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// The Stats tab uses this width however wide the panel is. Extra width goes to
// the Skills tab, since a stat value pushed to the border would end up far from
// its label.
constexpr int kStatsWidth = kLeftColumnMin - 2;

// One line of the panel's heading block, centred over the content width.
std::string Centered(const std::string& s, int width) {
  int pad = std::max(0, (width - static_cast<int>(s.size())) / 2);
  return PadRight(std::string(pad, ' ') + s, width);
}

// A centred row where only the text gets the decorator, for a row the cursor
// can land on. Not CenteredRow: this panel pads every row to its own width, and
// a flexed row would centre itself in the window instead.
ftxui::Element CenteredCell(const std::string& label,
                            const ftxui::Decorator& decorator, int width) {
  int pad = std::max(0, (width - static_cast<int>(label.size())) / 2);
  int rest = std::max(0, width - pad - static_cast<int>(label.size()));
  return ftxui::hbox({
      ftxui::text(std::string(pad, ' ')),
      ftxui::text(label) | decorator,
      ftxui::text(std::string(rest, ' ')),
  });
}

// The outer tab labels, indexed by CharacterPanel::Tab.
const char* kTabLabels[] = {"Stats",   "Skills", "Hyper",
                            "Ability", "Buffs",  "Advance"};

// The Farm/Boss row's labels for the two Hyper Stat allocations, used wherever
// the player sees them.
constexpr char kFarmTabLabel[] = "Farm";
constexpr char kBossTabLabel[] = "Boss";

// Whether an Inner Ability line is kept through a reroll. Two columns wide
// either way, so the lock is in the same column on every row.
constexpr char kLockedGlyph[] = "\U0001F512";
constexpr char kUnlockedGlyph[] = "\U0001F513";

// The banner shown with the preset's rank, above its lines.
constexpr char kAbilityBannerGlyph[] = "\u2691";

// The Hyper tab's columns. The level sits between the two buttons and the name
// takes what is left, which lines the buttons up down the list however long a
// name is.
constexpr int kHyperLevelWidth = 2;
constexpr int kHyperButtonWidth = 3;

// The row's width without the name: the leading gutter, the [-], the level
// between single gaps, the [+] and the trailing gutter. That leaves 21 columns
// for the name, and the longest is 15.
constexpr int kHyperFixedWidth =
    1 + kHyperButtonWidth + 1 + kHyperLevelWidth + 1 + kHyperButtonWidth + 1;

// The same row without the buttons: the two gutters and the level between
// single gaps.
constexpr int kHyperReadOnlyWidth = 1 + 1 + kHyperLevelWidth + 1 + 1;

// The tag at the start of a buff row, shaped like the skill tags, saying what
// the buff costs. Green for an owned buff, like the passive tag, and the coin's
// yellow for a rented one that is still being charged.
constexpr char kBuffOwnedTag[] = "O: ";
constexpr char kBuffRentTag[] = "R: ";
constexpr int kBuffTagWidth = 3;

// The mark at the other end of the row for a buff that is on. A buff that is
// off is dimmed instead.
constexpr char kBuffOnGlyph[] = "✓";

// A buff row's width without the name: the leading gutter, the tag, a gap
// before the mark, the mark, and the trailing gutter.
constexpr int kBuffFixedWidth = 1 + kBuffTagWidth + 1 + 1 + 1;

// Roman numerals for the job advancement pages, indexed by stage (1..6).
const char* kStageNumerals[] = {"", "I", "II", "III", "IV", "V", "VI"};

// The beginner page has no stage and so no numeral. GMS draws it as a large
// circle, and this is the circle the token currencies already use.
constexpr char kBeginnerPageMark[] = "\u25cf";

// Hyper Skills belong to the 4th job's book (a 5th job keeps them rather than
// getting its own), so the H page always follows stage four.
constexpr int kHyperJobStage = 4;

// The width the SP counter takes at the end of the page bar: " 999 SP ".
constexpr int kSpCol = 8;

// The four AP stats, in display order. The index is stat_sel_.
struct AllocStat {
  const char* label;
  StatField field;
};
constexpr AllocStat kAllocStats[] = {
    {"STR", STAT_FIELD_STR},
    {"DEX", STAT_FIELD_DEX},
    {"INT", STAT_FIELD_INT},
    {"LUK", STAT_FIELD_LUK},
};
constexpr int kNumAllocStats = sizeof(kAllocStats) / sizeof(kAllocStats[0]);

// One extra stat: a label column and a number column against the right edge,
// one gutter from the border. The value's right edge is fixed and the gap
// before it shrinks. Padding the label instead would leave a long value no
// room, and Defense would then stick out a column past the rest.
ftxui::Element StatRow(const std::string& label, const std::string& value) {
  int gap = kStatsWidth - 2 - static_cast<int>(value.size());
  return ftxui::text(" " + PadRight(label, std::max(0, gap)) + value + " ");
}

// "STR: 13", with " (base+bonus)" appended when gear contributes.
std::string StatText(const std::string& label, int base, int bonus) {
  std::string s = label + ": " + std::to_string(base + bonus);
  if (bonus > 0) {
    s += " (" + std::to_string(base) + "+" + std::to_string(bonus) + ")";
  }
  return s;
}

// The skill row's columns, each a fixed width, so a long name scrolls inside
// its column instead of widening the panel:
//
//   " " + tag(4) + name + level + filler + "[+]" + " "
constexpr int kSkillPlusWidth = 3;

// Room for every row of a page, so combining the page and row into the name
// clock's key can't collide with another page's row.
constexpr int kSkillClockPageStride = 4096;

// What a skill row shows for its level: the skill's level and how much of it
// comes from the book's bonus. A skill nobody has opened shows a bare 0, since
// no bonus goes to a skill that hasn't been bought.
std::string SkillLevelText(const CharacterInstance& character,
                           const Skill& skill, int bonus) {
  int learned = character.skill_level(skill);
  int level = LevelWithBonus(skill, learned, bonus);
  std::string text = std::to_string(level);
  if (level > learned) {
    text += " (+" + std::to_string(level - learned) + ")";
  }
  return text;
}

// The AP-allocated and gear bonus values for one allocatable stat.
std::pair<int, int> AllocStatValues(StatField field, const AllocatedStats& a,
                                    const EquipStats& e) {
  switch (field) {
    case STAT_FIELD_STR:
      return {a.str(), e.str()};
    case STAT_FIELD_DEX:
      return {a.dex(), e.dex()};
    case STAT_FIELD_INT:
      return {a.int_(), e.int_()};
    case STAT_FIELD_LUK:
      return {a.luk(), e.luk()};
    default:
      return {0, 0};
  }
}

}  // namespace

CharacterPanel::CharacterPanel(CharacterInstance& character,
                               AccountInstance& account, int& panel_focus,
                               std::map<std::string, Skill> skills)
    : character_(character),
      account_(account),
      skills_(std::move(skills)),
      panel_focus_(panel_focus),
      was_focused_(panel_focus == kCharPanel) {
}

void CharacterPanel::NoteFocus() const {
  bool focused = panel_focus_ == kCharPanel;
  // An unnamed character has one thing waiting here, so tabbing in puts the
  // cursor on the name row instead of the tab bar. This lasts only until the
  // player moves the cursor; after that the panel keeps it where they left it.
  if (focused && !was_focused_ && !cursor_moved_ && !read_only_ &&
      character_.username() == kDefaultUsername) {
    zone_ = kZoneUsername;
  }
  was_focused_ = focused;
}

// `row_width` is the width the row lays out in, which is one less than the
// content width while the scroll bar takes a column.
int CharacterPanel::SkillNameWidth(int level_width, int row_width,
                                   bool has_plus) {
  return row_width - 1 - kSkillTagWidth - level_width -
         (has_plus ? kSkillPlusWidth : 0) - 1;
}

ftxui::Element CharacterPanel::AllocRow(const std::string& label, int base,
                                        int bonus, int index,
                                        bool content_focused) const {
  if (read_only_) {
    // Nothing to press or select: the row is just the number.
    return StatsAligned(ftxui::hbox({
        ftxui::text(" " + StatText(label, base, bonus)),
        ftxui::filler(),
    }));
  }
  bool selected = content_focused && stat_sel_ == index;
  // The cursor takes priority over the unavailable look, as on the skill rows:
  // a selected [+] inverts even with no AP, so the cursor stays visible while
  // the player reads down the stats.
  ftxui::Element plus = ftxui::text("[+]");
  if (selected) {
    plus = plus | ftxui::inverted;
  } else if (character_.proto().ap() == 0) {
    plus = plus | ftxui::dim;
  }
  return StatsAligned(ftxui::hbox({
      ftxui::text(" " + StatText(label, base, bonus)),
      ftxui::filler(),
      plus,
      ftxui::text(" "),
  }));
}

std::vector<CharacterPanel::Tab> CharacterPanel::VisibleTabs() const {
  std::vector<Tab> tabs = {kTabStats};
  // Every character starts with a book (the beginner's), so the tab only waits
  // for the account to reach its level.
  if (Unlocked(Feature::kSkills, character_, account_)) {
    tabs.push_back(kTabSkills);
  }
  // After Skills, and hidden below the Hyper Stats level: a tab with no points
  // behind it would just list things the player can't have.
  if (Unlocked(Feature::kHyperStats, character_, account_)) {
    tabs.push_back(kTabHyper);
  }
  // Ability is the only tab gated on this character alone. The Feature table is
  // account-wide, and an ability below the unlock level grants nothing, whoever
  // else on the account got there.
  if (character_.inner_ability_unlocked()) {
    tabs.push_back(kTabAbility);
  }
  // Buffs is also gated on this character, for the same reason: a buff below
  // its own level can't be turned on or bought, whoever else on the account got
  // there. Neither Buffs nor Advance appears on a read-only panel: Buffs lists
  // what is in a bag, which a character sheet doesn't include, and someone
  // else's advancement isn't the reader's to take.
  if (character_.consumables_unlocked() && !read_only_) {
    tabs.push_back(kTabBuffs);
  }
  // The Advance tab exists only while there is an advancement to take, so it
  // appears at level 10 and disappears once the player picks a job.
  if (character_.CanAdvanceJob() && !read_only_) {
    tabs.push_back(kTabAdvance);
  }
  return tabs;
}

CharacterPanel::Tab CharacterPanel::ActiveTab() const {
  std::vector<Tab> tabs = VisibleTabs();
  if (active_tab_ < 0) {
    return kTabStats;
  }
  // Clamped rather than reset: the only tab that ever disappears is the last
  // one on the bar, so a player on it should land next to where it was. Taking
  // the advancement leaves them on Skills.
  return tabs[std::min(active_tab_, static_cast<int>(tabs.size()) - 1)];
}

CharacterPanel::Zone CharacterPanel::EffectiveZone() const {
  switch (zone_) {
    case kZoneUsername:
    case kZoneTabs:
      return zone_;
    case kZonePresets:
      return ShowsPresetBar() ? zone_ : kZoneTabs;
    case kZoneStatRows:
      return ActiveTab() == kTabStats ? zone_ : kZoneTabs;
    case kZoneHyperRows:
    case kZoneHyperReset:
      return ActiveTab() == kTabHyper ? zone_ : kZoneTabs;
    case kZoneAbilityRows:
    case kZoneAbilityReroll:
      return ActiveTab() == kTabAbility ? zone_ : kZoneTabs;
    case kZoneBuffRows:
      return ActiveTab() == kTabBuffs ? zone_ : kZoneTabs;
    case kZoneAdvTabs:
    case kZoneSkillRows:
      return ActiveTab() == kTabSkills ? zone_ : kZoneTabs;
    case kZoneVReset:
      // A page change can strand the zone as well as a tab change, since only
      // the V page has a button under its rows.
      return ActiveTab() == kTabSkills && ShowsVReset() ? zone_ : kZoneTabs;
    case kZoneJobRows:
      return ActiveTab() == kTabAdvance ? zone_ : kZoneTabs;
  }
  return kZoneTabs;
}

int CharacterPanel::StatStops() const {
  return read_only_ ? 0 : kNumAllocStats;
}

int CharacterPanel::FirstStatStop() const {
  // The name and the tab bar are stops 0 and 1, and the Farm/Boss row takes the
  // next one when shown.
  return ShowsPresetBar() ? 3 : 2;
}

int CharacterPanel::RingStops() const {
  // The username row is stop 0 on every tab and the tab bar is stop 1. That
  // puts the name one step up from the bar and one step down from the bottom
  // row without a separate rule for either.
  if (ActiveTab() == kTabStats) {
    // The name and tab bar, the Farm/Boss row if shown, the four AP stats, and
    // the View All Stats row below them, which is absent while there are no
    // combat stats to lead to.
    return 2 + (ShowsPresetBar() ? 1 : 0) + StatStops() +
           (ShowsCombatStats() ? 1 : 0);
  }
  if (ActiveTab() == kTabHyper) {
    // The name and tab bar, the Farm/Boss row, one stop per stat, and [Reset].
    return 3 + kNumHyperStats + (read_only_ ? 0 : 1);
  }
  if (ActiveTab() == kTabAbility) {
    // The same shape: the name and tab bar, the Farm/Boss row, one stop per
    // line, and [Reroll]. Read-only keeps the Farm/Boss row and nothing below
    // it, since the reader can't change the locks.
    return read_only_ ? 3 : 3 + AbilityRows() + 1;
  }
  if (ActiveTab() == kTabBuffs) {
    // The name, the tab bar, and one stop per buff. No Farm/Boss row: a buff
    // belongs to the character, not an allocation.
    return 2 + static_cast<int>(BuffsShown().size());
  }
  if (ActiveTab() == kTabAdvance) {
    return 2 + static_cast<int>(
                   JobChoicesForStage(character_.proto().job(),
                                      character_.proto().job_stage() + 1)
                       .size());
  }
  // The name, the tab bar, the page bar, one stop per row, and the [Reset]
  // under the V page's nodes.
  return 3 + SkillRowCount() + (ShowsVReset() ? 1 : 0);
}

int CharacterPanel::CursorStop() const {
  switch (EffectiveZone()) {
    case kZoneUsername:
      return 0;
    case kZoneTabs:
      return 1;
    case kZonePresets:
      return 2;
    case kZoneStatRows:
      // Clamped to the stops that exist: with the AP rows gone, the zone holds
      // only the View All Stats row, whatever stat_sel_ says.
      return std::min(stat_sel_, StatStops()) + FirstStatStop();
    case kZoneJobRows:
      return job_sel_ + 2;
    case kZoneAdvTabs:
      return 2;
    case kZoneSkillRows:
      return skill_sel_ + 3;
    case kZoneVReset:
      return SkillRowCount() + 3;
    case kZoneHyperRows:
      return hyper_sel_ + 3;
    case kZoneHyperReset:
      return kNumHyperStats + 3;
    case kZoneAbilityRows:
      return ability_sel_ + 3;
    case kZoneAbilityReroll:
      return AbilityRows() + 3;
    case kZoneBuffRows:
      return buff_sel_ + 2;
  }
  return 0;
}

void CharacterPanel::SetCursorStop(int stop) {
  if (stop == 0) {
    zone_ = kZoneUsername;
    return;
  }
  if (stop == 1) {
    zone_ = kZoneTabs;
    return;
  }
  if (ActiveTab() == kTabStats) {
    if (ShowsPresetBar() && stop == 2) {
      zone_ = kZonePresets;
      return;
    }
    zone_ = kZoneStatRows;
    stat_sel_ = read_only_ ? kNumAllocStats : stop - FirstStatStop();
    return;
  }
  if (ActiveTab() == kTabHyper) {
    if (stop == 2) {
      zone_ = kZonePresets;
      return;
    }
    if (stop == kNumHyperStats + 3) {
      zone_ = kZoneHyperReset;
      return;
    }
    zone_ = kZoneHyperRows;
    hyper_sel_ = stop - 3;
    return;
  }
  if (ActiveTab() == kTabAbility) {
    if (stop == 2) {
      zone_ = kZonePresets;
      return;
    }
    if (stop == AbilityRows() + 3) {
      zone_ = kZoneAbilityReroll;
      return;
    }
    zone_ = kZoneAbilityRows;
    ability_sel_ = stop - 3;
    return;
  }
  if (ActiveTab() == kTabBuffs) {
    zone_ = kZoneBuffRows;
    buff_sel_ = stop - 2;
    return;
  }
  if (ActiveTab() == kTabAdvance) {
    zone_ = kZoneJobRows;
    job_sel_ = stop - 2;
    return;
  }
  if (stop == 2) {
    zone_ = kZoneAdvTabs;
    return;
  }
  if (ShowsVReset() && stop == SkillRowCount() + 3) {
    zone_ = kZoneVReset;
    return;
  }
  if (zone_ != kZoneSkillRows) {
    // Arriving from outside the rows, so land on the name, the leftmost column,
    // where the eye starts. Moving from row to row keeps the current column.
    skill_col_ = kColName;
  }
  zone_ = kZoneSkillRows;
  skill_sel_ = stop - 3;
}

void CharacterPanel::MoveCursor(int delta) {
  // The page bar keeps the page the player left it on, so a 2nd job character
  // can still go back to their 1st job book.
  SetCursorStop(StepCursor(CursorStop(), delta, RingStops()));
}

std::string CharacterPanel::TabKey(Tab tab) const {
  if (tab == kTabHyper) {
    // Gold on the bar until the player has opened it once. One key for the tab,
    // not one per level, since it appears once and stays.
    return kHyperTabKey;
  }
  if (tab == kTabAbility) {
    // Gold for the first character on the account to reach it and quiet for
    // every one after, because MarkSeen is account-wide, so the player hears
    // about Inner Ability once.
    return kAbilityTabKey;
  }
  if (tab == kTabBuffs) {
    // The same as Ability: one key for the account, since what a buff is only
    // needs announcing once.
    return kBuffsTabKey;
  }
  if (tab == kTabAdvance) {
    // Keyed by the stage being advanced into, so the tab that returns at level
    // 30 is news again instead of counting as seen from level 10.
    return AdvanceTabKey(character_.proto().job_stage() + 1);
  }
  if (tab == kTabSkills &&
      Unlocked(Feature::kLinkSkills, character_, account_)) {
    // The first step of the Link Skills trail, which runs through this tab.
    // Below that level the tab announces nothing, since the player was on it
    // when it appeared (see RenderTabBar).
    return LinkTrailKey(LinkTrailStep::kSkillsTab);
  }
  // Stats has been there since the start of the game, and Skills appears with
  // the player already on it (see RenderTabBar). Neither has anything to
  // announce, so neither uses a save key.
  return "";
}

void CharacterPanel::MarkActiveTabSeen() {
  if (read_only_) {
    return;
  }
  std::string key = TabKey(ActiveTab());
  if (!key.empty()) {
    account_.MarkSeen(key);
  }
  // The trail's second step: the Link Skills row is on the beginner page, so
  // opening that page passes the row.
  if (ShowsLinkRow()) {
    FollowedToLinkSkills(LinkTrailStep::kBeginnerPage, account_);
  }
}

bool CharacterPanel::PresetBarNamesSlots() const {
  return ActiveTab() == kTabHyper || ActiveTab() == kTabAbility;
}

int CharacterPanel::PresetChips() const {
  // The Stats tab's row names the two activities rather than the slots behind
  // them, since a character can only be doing one of two things.
  return PresetBarNamesSlots() ? kNumStatPresets : 2;
}

StatPreset CharacterPanel::PresetBarSelection() const {
  return StatPresetAt(std::min(IndexOf(hyper_preset_), PresetChips() - 1));
}

bool CharacterPanel::ShowsPresetBar() const {
  // The Ability tab never appears below the Hyper Stats level, so its row needs
  // no further gate.
  if (ActiveTab() == kTabAbility) {
    return true;
  }
  // The Stats tab shows numbers rather than spending points, so its row appears
  // only while the autoswap has two allocations to tell apart. With the
  // autoswap off there is one allocation in play and nothing to pick.
  if (ActiveTab() == kTabStats && !character_.autoswap_presets()) {
    return false;
  }
  // Otherwise only the tabs whose numbers come from an allocation show it. The
  // Advance tab lists jobs, and the Skills tab has its own bar.
  if (ActiveTab() != kTabStats && ActiveTab() != kTabHyper) {
    return false;
  }
  return Unlocked(Feature::kHyperStats, character_, account_);
}

bool CharacterPanel::ShowsSecondTabRow() const {
  if (ActiveTab() == kTabSkills) {
    // Every character has the beginner page, so every one has the page bar.
    return true;
  }
  return ShowsPresetBar();
}

int CharacterPanel::StatsTabFixedRows() const {
  return kStatsTabFixedRows + (ShowsPresetBar() ? 1 : 0);
}

int CharacterPanel::SkillsTabFixedRows() const {
  return kSkillsTabFixedRows + (ShowsVReset() ? 2 : 0);
}

Activity CharacterPanel::SelectedActivity() const {
  return PresetBarSelection() == StatPreset::kSecond ? Activity::kBossing
                                                     : Activity::kFarming;
}

ftxui::Element CharacterPanel::RenderPresetBar(
    bool bar_focused, const std::string& trailing) const {
  std::vector<TabSpec> specs;
  if (PresetBarNamesSlots()) {
    const PresetKind kind = ActiveTab() == kTabAbility
                                ? PresetKind::kInnerAbility
                                : PresetKind::kHyperStats;
    for (int i = 0; i < PresetChips(); ++i) {
      const StatPreset slot = StatPresetAt(i);
      specs.push_back({PresetSlotLabel(slot, character_.autoswap_presets(),
                                       character_.SlotInUse(kind) == slot)});
    }
  } else {
    specs = {{kFarmTabLabel}, {kBossTabLabel}};
  }
  int active = IndexOf(PresetBarSelection());
  // The chips get what the trailing text and its gutter leave. Three chips
  // never come close to filling even the narrowest space, so this bar never
  // scrolls however large the number beside it gets.
  int width = ContentWidth();
  if (!trailing.empty()) {
    width -= static_cast<int>(ftxui::string_width(trailing)) + 1;
  }
  std::vector<ftxui::Element> row = {TabBar(specs, active, bar_focused, width) |
                                         ftxui::reflect(preset_row_box_),
                                     ftxui::filler()};
  if (!trailing.empty()) {
    // The counter reads like the AP and SP counters: what is left to spend, on
    // the row above where it is spent.
    row.push_back(ftxui::text(trailing + " "));
  }
  return ftxui::hbox(std::move(row));
}

ftxui::Element CharacterPanel::RenderTabBar(bool row_selected) const {
  // A left-aligned row of chips in the shared tab style.
  std::vector<Tab> tabs = VisibleTabs();
  std::vector<TabSpec> specs;
  int active = 0;
  for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
    // A tab with no key never announces itself; Seen("") would return false and
    // leave it gold forever. Skills has no key: it takes the index the Advance
    // tab leaves, so an advancement leaves the player on it, and gold on a tab
    // already being read says nothing.
    std::string key = TabKey(tabs[i]);
    if (tabs[i] == ActiveTab()) {
      active = i;
    }
    specs.push_back({kTabLabels[tabs[i]],
                     !read_only_ && !key.empty() && !account_.Seen(key)});
  }
  // The tabs take up most of a narrow panel, so this bar is the one most likely
  // to scroll, and a scrolling bar uses every column it gets, so the blank
  // column inside the right border is taken off first.
  return ftxui::hbox({TabBar(specs, active, row_selected, ContentWidth() - 1),
                      ftxui::filler()});
}

// The MP row, with the character's unspent AP right-aligned so the player can
// see how much there is to spend on the [+] rows below.
ftxui::Element CharacterPanel::MpRow(int mp, int ap) const {
  return StatsAligned(ftxui::hbox({
      ftxui::text(" MP: " + std::to_string(mp)),
      ftxui::filler(),
      ftxui::text(std::to_string(ap) + " AP "),
  }));
}

ftxui::Element CharacterPanel::StatsAligned(ftxui::Element row) const {
  // Centred: the block doesn't stretch with the terminal, since a value pushed
  // to the border would leave its label behind. An odd column goes to the label
  // side, which lines up the right-hand column with the Skills tab's [+] when
  // the slack is one column or none.
  //
  // The padding is text rather than filler(), because a row that right-aligns
  // something ends in its own filler, and two fillers would split the slack.
  int slack = std::max(0, ContentWidth() - kStatsWidth);
  return ftxui::hbox({
      ftxui::text(std::string((slack + 1) / 2, ' ')),
      std::move(row) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kStatsWidth),
      ftxui::text(std::string(slack / 2, ' ')),
  });
}

int CharacterPanel::ExtraStatsShown(int total) const {
  if (max_rows_ <= 0) {
    return total;
  }
  // The View All Stats row is paid for first, because it leads to the stats
  // that didn't fit. A budget too small even for that still shows it and lets
  // the panel be clipped, since a panel with no way to the rest is worse.
  return std::max(0, std::min(total, max_rows_ - StatsTabFixedRows() - 1));
}

bool CharacterPanel::ShowsExtrasRule() const {
  // The rule is the first thing the extras block gives up. A budget with no
  // room for it is one row less than the panel's fixed rows, which is the row
  // the combat panel below needs on a 24-row terminal.
  return max_rows_ <= 0 || max_rows_ >= StatsTabFixedRows() + 1;
}

ftxui::Element CharacterPanel::RenderStatsTab(bool bar_focused,
                                              bool rows_focused) const {
  const Character& p = character_.proto();
  const AllocatedStats& a = p.allocated_stats();
  bool content_focused = rows_focused;

  // HP, MP and DEF include passive skill bonuses on top of allocated and worn
  // values, so they come from the derived totals rather than a plain sum. The
  // stat rows do too: LUK from a skill belongs in the same column as LUK from a
  // ring.
  DerivedStats derived = DerivedStatsFor(character_, skills_, /*buffs_up=*/{},
                                         /*allies=*/{}, SelectedActivity());
  const EquipStats e = TotalEquipStats(character_, derived);

  std::vector<ftxui::Element> rows;
  if (ShowsPresetBar()) {
    rows.push_back(RenderPresetBar(bar_focused, ""));
    rows.push_back(PanelSeparator(highlighted_));
  }
  rows.push_back(StatsAligned(ftxui::text(
      PadRight(" HP: " + std::to_string(derived.max_hp), kStatsWidth))));
  rows.push_back(MpRow(derived.max_mp, p.ap()));
  for (int i = 0; i < kNumAllocStats; ++i) {
    std::pair<int, int> v = AllocStatValues(kAllocStats[i].field, a, e);
    rows.push_back(
        AllocRow(kAllocStats[i].label, v.first, v.second, i, content_focused));
  }
  // A Beginner's tab ends at the AP rows. Nothing below applies yet, and a rule
  // with nothing under it looks like something failed to draw.
  if (!ShowsCombatStats()) {
    return ftxui::vbox(std::move(rows));
  }
  if (ShowsExtrasRule()) {
    rows.push_back(PanelSeparator(highlighted_));
  }
  std::vector<StatLine> extras =
      PanelExtraStatLines(character_, account_, skills_, SelectedActivity());
  int shown = ExtraStatsShown(static_cast<int>(extras.size()));
  // A rule with nothing under it looks like a row that failed to draw, so it is
  // cut too rather than left at the end.
  while (shown > 0 && extras[shown - 1].rule) {
    --shown;
  }
  for (int i = 0; i < shown; ++i) {
    if (extras[i].rule) {
      rows.push_back(PanelSeparator(highlighted_));
      continue;
    }
    rows.push_back(StatsAligned(StatRow(extras[i].label, extras[i].value)));
  }
  // Ends the block. Whatever didn't fit above is on the screen this row opens,
  // and the cursor reaches it by moving past the last stat. This is the last
  // row to be dropped, not the first.
  bool selected = content_focused && stat_sel_ == kNumAllocStats;
  rows.push_back(CenteredCell("View All Stats",
                              selected ? ftxui::inverted : ftxui::nothing,
                              ContentWidth()));
  return ftxui::vbox(std::move(rows));
}

std::string CharacterPanel::PoolText() const {
  // V Points buy a node's next level rather than one level per point, so the
  // pool is shown in full, and what it buys is on each row.
  if (IsVPage(SelectedSkillPage())) {
    return FormatWithCommas(character_.v_points()) + " VP";
  }
  if (IsHyperPage(SelectedSkillPage())) {
    return std::to_string(character_.hyper_sp()) + " SP";
  }
  if (IsBeginnerPage(SelectedSkillPage())) {
    // No pool: nothing on the page is bought.
    return "";
  }
  return std::to_string(character_.sp(SelectedSkillPage())) + " SP";
}

ftxui::Element CharacterPanel::RenderAdvTabBar(bool bar_focused) const {
  // One chip per page in the shared tab style: the stages by numeral, the Hyper
  // page by H. That page's points are right-aligned on the same row.
  std::vector<TabSpec> specs;
  for (int page = 0; page < SkillPages(); ++page) {
    if (IsVPage(page)) {
      specs.push_back({"V"});
    } else if (IsHyperPage(page)) {
      specs.push_back({"H"});
    } else if (IsBeginnerPage(page)) {
      // The beginner book has no numeral. GMS marks the page with a circle, and
      // the token mark is the circle this game already uses. It turns gold
      // while the Link Skills row on it is waiting to be found.
      specs.push_back(
          {kBeginnerPageMark, LeadToLinkSkills(LinkTrailStep::kBeginnerPage,
                                               character_, account_)});
    } else {
      specs.push_back({kStageNumerals[page]});
    }
  }
  // The points counter shares the row, so the bar gets what is left.
  std::string pool = PoolText();
  std::vector<ftxui::Element> row;
  row.push_back(
      TabBar(specs, SelectedSkillPage(), bar_focused,
             ContentWidth() - std::max<int>(kSpCol, pool.size() + 1)));
  row.push_back(ftxui::filler());
  row.push_back(ftxui::text(pool + " "));
  return ftxui::hbox(std::move(row));
}

CharacterPanel::LevelColumn CharacterPanel::MeasureLevelColumn(
    const std::vector<const Skill*>& skills) const {
  LevelColumn column;
  column.bonus = BonusSkillLevels(character_, skills_);
  int widest = 1;  // "0", the narrowest a level ever is
  for (const Skill* skill : skills) {
    widest = std::max(
        widest, static_cast<int>(
                    SkillLevelText(character_, *skill, column.bonus).size()));
  }
  column.width = 1 + widest + 1;
  return column;
}

JobAdvancement CharacterPanel::HyperAdvancement() const {
  return AdvancementForJobStage(
      character_.proto().job(),
      std::min(character_.proto().job_stage(), kHyperJobStage));
}

bool CharacterPanel::HasHyperPage() const {
  // The lowest level any of this character's Hyper Skills unlocks at. Read from
  // the catalog rather than written as a number, so the page appears with the
  // first skill on it however the data changes.
  for (const Skill* skill :
       SkillsForAdvancement(skills_, HyperAdvancement(), /*hyper=*/true)) {
    if (character_.proto().level() >= skill->required_level()) {
      return true;
    }
  }
  return false;
}

int CharacterPanel::NumberedSkillPages() const {
  // The numbered pages are the books SP buys. The 5th job's nodes aren't one of
  // them: they are on the V page, whatever advancement they are filed under.
  return std::min(character_.proto().job_stage(), kLastSpJobStage);
}

JobAdvancement CharacterPanel::VAdvancement() const {
  return AdvancementForJobStage(character_.proto().job(),
                                character_.proto().job_stage());
}

bool CharacterPanel::HasVPage() const {
  return character_.v_matrix_unlocked() &&
         !VNodesFor(skills_, VAdvancement()).empty();
}

bool CharacterPanel::ShowsSkillPlus() const {
  return !read_only_ && !IsBeginnerPage(SelectedSkillPage());
}

CharacterPanel::SkillCol CharacterPanel::EffectiveSkillCol() const {
  return ShowsSkillPlus() ? skill_col_ : kColName;
}

int CharacterPanel::SelectedSkillPage() const {
  // Until the player moves the bar, the tab opens on the character's 1st job
  // book: the beginner page has nothing to spend on, and a party member's sheet
  // arrives after the panel that draws it is built. A Beginner has only that
  // page and stays on it.
  if (!skill_page_chosen_ && NumberedSkillPages() > 0) {
    return 1;
  }
  return std::min(skill_tab_, SkillPages() - 1);
}

int CharacterPanel::SkillPages() const {
  // The beginner page is page 0 for every character, since they start with that
  // book and always keep it.
  return 1 + NumberedSkillPages() + (HasHyperPage() ? 1 : 0) +
         (HasVPage() ? 1 : 0);
}

bool CharacterPanel::IsHyperPage(int page) const {
  return HasHyperPage() && page == NumberedSkillPages() + 1;
}

bool CharacterPanel::IsVPage(int page) const {
  return HasVPage() && page == SkillPages() - 1;
}

bool CharacterPanel::ShowsVReset() const {
  // The SP books have no [Reset], since their points are permanent. Only the V
  // page refunds.
  return !read_only_ && ActiveTab() == kTabSkills &&
         character_.proto().job_stage() > 0 && IsVPage(SelectedSkillPage()) &&
         !SkillsForPage(SelectedSkillPage()).empty();
}

bool CharacterPanel::ShowsLinkRow() const {
  return !read_only_ && ActiveTab() == kTabSkills &&
         IsBeginnerPage(SelectedSkillPage());
}

int CharacterPanel::SkillIndexFor(int row) const {
  return ShowsLinkRow() ? row - 1 : row;
}

int CharacterPanel::SkillRowCount() const {
  return static_cast<int>(SkillsForPage(SelectedSkillPage()).size()) +
         (ShowsLinkRow() ? 1 : 0);
}

std::vector<const Skill*> CharacterPanel::SkillsForPage(int page) const {
  // A page shows only the skills of the advancement this character's job is at,
  // so a Swordman never sees an Archer's. The Hyper page follows their current
  // advancement, which is the book its skills upgrade.
  std::set<std::string> toggles_on(character_.proto().active_skill().begin(),
                                   character_.proto().active_skill().end());
  if (IsVPage(page)) {
    return VNodesFor(skills_, VAdvancement());
  }
  if (IsHyperPage(page)) {
    return SkillsForAdvancement(skills_, HyperAdvancement(), /*hyper=*/true,
                                toggles_on);
  }
  // Page 0 is the beginner book, which has no job stage. The pages after it are
  // the stages in order.
  JobAdvancement book =
      IsBeginnerPage(page)
          ? JOB_ADVANCEMENT_BEGINNER
          : AdvancementForJobStage(character_.proto().job(), page);
  return SkillsForAdvancement(skills_, book, /*hyper=*/false, toggles_on);
}

bool CharacterPanel::SkillLocked(const Skill& skill) const {
  return !character_.MeetsSkillRequirement(skill) ||
         character_.proto().level() < skill.required_level();
}

ftxui::Element CharacterPanel::RenderSkillRow(const Skill& skill, int index,
                                              const LevelColumn& column,
                                              bool rows_focused,
                                              int row_width) const {
  int learned = character_.skill_level(skill);
  bool selected = rows_focused && skill_sel_ == index;
  bool maxed = learned >= SkillMaxLevel(skill);
  bool has_sp = character_.LevelsAffordable(skill) > 0;
  // A skill waiting on another skill or on a level isn't one the character has,
  // so the whole row dims. Running out of SP dims only the [+], since that is
  // about the moment rather than the skill.
  bool locked = SkillLocked(skill);

  KindTag tag = TagFor(skill);
  ftxui::Element tag_text = ftxui::text(tag.text) | ftxui::color(tag.color);
  if (locked) {
    tag_text = tag_text | ftxui::dim;
  }

  // Only the name inverts: Enter opens the skill, so the highlight covers the
  // skill and nothing else. A locked skill still opens, since its screen tells
  // the player what is blocking it.
  //
  // A name too long for the column scrolls while the row is selected and is cut
  // otherwise. The column is a fixed width either way.
  int name_width = SkillNameWidth(column.width, row_width, ShowsSkillPlus());
  std::string window =
      ScrollingWindow(skill.name(), name_width,
                      selected ? name_clock_.Elapsed()
                               : std::chrono::steady_clock::duration::zero());
  // A short name's padding is outside the cursor, so the highlight covers only
  // the name. A fixed-width highlight would suggest the blank space after a
  // short name is part of what Enter opens.
  int lit = std::min(static_cast<int>(skill.name().size()), name_width);
  ftxui::Element name = ftxui::text(window.substr(0, lit));
  if (selected && EffectiveSkillCol() == kColName) {
    name = name | ftxui::inverted;
  } else if (locked) {
    name = name | ftxui::dim;
  }
  ftxui::Element name_pad = ftxui::text(window.substr(lit));
  // Right-aligned, so the gap a short level leaves falls between the name and
  // the level rather than after it, and it supplies the trailing gutter the row
  // has no filler left for.
  ftxui::Element level_text =
      ftxui::text(PadLeft(SkillLevelText(character_, skill, column.bonus),
                          column.width - 1) +
                  " ");
  if (locked) {
    level_text = level_text | ftxui::dim;
  }
  std::vector<ftxui::Element> cells = {
      ftxui::text(" "), tag_text, name, name_pad, level_text, ftxui::filler(),
  };
  if (ShowsSkillPlus()) {
    ftxui::Element plus = ftxui::text("[+]");
    if (selected && EffectiveSkillCol() == kColPlus) {
      plus = plus | ftxui::inverted;
    } else if (maxed || !has_sp || locked) {
      plus = plus | ftxui::dim;
    }
    cells.push_back(std::move(plus));
  }
  cells.push_back(ftxui::text(" "));
  ftxui::Element row = ftxui::hbox(std::move(cells));
  if (selected) {
    row = std::move(row) | ftxui::reflect(skill_cursor_box_);
  }
  return row;
}

int CharacterPanel::SkillRowsShown(int total) const {
  if (max_rows_ <= 0) {
    return total;
  }
  // At least one row however small the budget: a page cut to nothing says less
  // than one cut short, and the cursor needs somewhere to be.
  return std::max(1, std::min(total, max_rows_ - SkillsTabFixedRows()));
}

int CharacterPanel::FirstSkillRow(int total, int selected, int visible) const {
  return ScrollWindowStart(total, selected, visible);
}

std::vector<int> CharacterPanel::SkillLines(
    int page, const std::vector<const Skill*>& skills) const {
  std::vector<int> breaks =
      IsVPage(page) ? VNodeSectionBreaks(skills) : std::vector<int>();
  // Counted in cursor rows rather than book skills, since the Link Skills row
  // comes first on the beginner page and isn't one of its skills.
  std::vector<int> lines;
  int row = 0;
  if (IsBeginnerPage(page) && ShowsLinkRow()) {
    lines.push_back(row++);
  }
  std::size_t next = 0;
  for (int i = 0; i < static_cast<int>(skills.size()); ++i) {
    if (next < breaks.size() && breaks[next] == i) {
      lines.push_back(-1);
      ++next;
    }
    lines.push_back(row++);
  }
  return lines;
}

ftxui::Element CharacterPanel::RenderLinkRow(bool selected) const {
  // No kind tag and no level: nothing here is bought, and the account's
  // progress is on the screen Enter opens. The name still starts where every
  // skill's does; the tag's columns are left blank rather than reused. Gold
  // until the row has been opened once.
  ftxui::Element name = ftxui::text("Link Skills");
  if (selected) {
    name = std::move(name) | ftxui::inverted;
  } else if (LeadToLinkSkills(LinkTrailStep::kLinkRow, character_, account_)) {
    name = std::move(name) | ftxui::color(kGold);
  }
  return ftxui::hbox({
      ftxui::text(" " + std::string(kSkillTagWidth, ' ')),
      std::move(name),
      ftxui::filler(),
      ftxui::text(" "),
  });
}

ftxui::Element CharacterPanel::RenderSkillsTab(bool bar_focused,
                                               bool rows_focused,
                                               bool reset_focused) const {
  std::vector<ftxui::Element> rows;
  rows.push_back(RenderAdvTabBar(bar_focused));
  rows.push_back(PanelSeparator(highlighted_));
  std::vector<const Skill*> skills = SkillsForPage(SelectedSkillPage());
  if (SkillRowCount() == 0) {
    rows.push_back(ftxui::text(PadRight(" No skills yet.", ContentWidth())) |
                   ftxui::dim);
    return ftxui::vbox(std::move(rows));
  }
  // The page is part of the key along with the row, so the same row on another
  // page counts as a different skill and starts from the beginning.
  name_clock_.Follow(SelectedSkillPage() * kSkillClockPageStride + skill_sel_,
                     rows_focused);
  // Counted in drawn lines rather than skills, so the window, the scroll bar
  // and the row budget all measure the same thing. A rule between two sections
  // of the V page takes a line just as a skill does.
  std::vector<int> lines = SkillLines(SelectedSkillPage(), skills);
  int total = static_cast<int>(lines.size());
  int visible = SkillRowsShown(total);
  int cursor = static_cast<int>(
      std::find(lines.begin(), lines.end(), skill_sel_) - lines.begin());
  int first = FirstSkillRow(total, cursor, visible);
  // Empty while the whole book fits, and then the rows keep their full width.
  // The level column is measured over the whole book rather than the window, so
  // scrolling doesn't shift the names sideways.
  std::vector<ftxui::Element> cells = ScrollBarCells(total, first, visible);
  int row_width = cells.empty() ? ContentWidth() : ContentWidth() - 1;
  LevelColumn column = MeasureLevelColumn(skills);
  for (int i = 0; i < visible; ++i) {
    int line = lines[first + i];
    ftxui::Element row;
    if (line < 0) {
      row = PanelSeparator(highlighted_);
    } else if (ShowsLinkRow() && line == 0) {
      row = RenderLinkRow(rows_focused && skill_sel_ == line);
    } else {
      row = RenderSkillRow(*skills[SkillIndexFor(line)], line, column,
                           rows_focused, row_width);
    }
    if (cells.empty()) {
      rows.push_back(std::move(row));
      continue;
    }
    rows.push_back(ftxui::hbox({
        std::move(row) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, row_width),
        std::move(cells[i]) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1),
    }));
  }
  // The way back to an unspent matrix, under its own rule rather than among the
  // nodes it undoes, exactly like the Hyper tab's bottom rows. Both always draw
  // whatever the budget; the nodes are what a short terminal cuts.
  if (ShowsVReset()) {
    rows.push_back(PanelSeparator(highlighted_));
    rows.push_back(CenteredCell(
        "[Reset]", reset_focused ? ftxui::inverted : ftxui::nothing,
        ContentWidth()));
  }
  return ftxui::vbox(std::move(rows));
}

bool CharacterPanel::CanRaiseHyperStat(HyperStatField field) const {
  if (!HyperStatUnlocked(field, character_.proto().level())) {
    return false;
  }
  int level = character_.hyper_stat_level(field, hyper_preset_);
  if (level >= character_.max_hyper_stat_level()) {
    return false;
  }
  return HyperStatLevelCost(level + 1) <=
         character_.hyper_stat_points_left(hyper_preset_);
}

bool CharacterPanel::CanLowerHyperStat(HyperStatField field) const {
  return character_.hyper_stat_level(field, hyper_preset_) > 0;
}

int CharacterPanel::HyperRowsShown() const {
  if (max_rows_ <= 0) {
    return kNumHyperStats;
  }
  // At least one row however small the budget: the cursor needs somewhere to
  // be, and the rule and [Reset] below it are never dropped.
  return std::max(1, std::min(kNumHyperStats, max_rows_ - kHyperTabFixedRows));
}

int CharacterPanel::FirstHyperRow(int visible) const {
  return ScrollWindowStart(kNumHyperStats, hyper_sel_, visible);
}

ftxui::Element CharacterPanel::RenderHyperRow(HyperStatField field, int index,
                                              bool rows_focused,
                                              int row_width) const {
  bool selected = rows_focused && hyper_sel_ == index;
  // A stat the character's level hasn't unlocked isn't one they have yet, so
  // the whole row dims, as a locked skill's does. Running out of points dims
  // only the [+], since that is about the moment, not the stat.
  bool locked = !HyperStatUnlocked(field, character_.proto().level());
  int level = character_.hyper_stat_level(field, hyper_preset_);

  // Only the name inverts, as on a skill row: Enter opens the stat, so the
  // highlight covers only the stat. The padding is outside it, which keeps the
  // highlight off the blank space after a short name.
  std::string text = HyperStatName(field);
  int name_width = std::max(
      1, row_width - (read_only_ ? kHyperReadOnlyWidth : kHyperFixedWidth));
  int lit = std::min(static_cast<int>(text.size()), name_width);
  ftxui::Element name = ftxui::text(text.substr(0, lit));
  if (selected && hyper_col_ == kHyperColName) {
    name = std::move(name) | ftxui::inverted;
  } else if (locked) {
    name = std::move(name) | ftxui::dim;
  }
  // Right-aligned between single gutters, so both buttons sit one column from
  // the level however wide the panel is.
  ftxui::Element level_text =
      ftxui::text(" " + PadLeft(std::to_string(level), kHyperLevelWidth) + " ");
  if (locked) {
    level_text = std::move(level_text) | ftxui::dim;
  }
  // The cursor takes priority over the unavailable look, as on the skill rows:
  // a selected button inverts even with nothing to spend or give back, so the
  // cursor stays visible while the player reads down the list.
  std::vector<ftxui::Element> cells = {
      ftxui::text(" "),
      std::move(name),
      ftxui::text(std::string(name_width - lit, ' ')),
  };
  if (!read_only_) {
    ftxui::Element minus = ftxui::text("[-]");
    if (selected && hyper_col_ == kHyperColMinus) {
      minus = std::move(minus) | ftxui::inverted;
    } else if (!CanLowerHyperStat(field)) {
      minus = std::move(minus) | ftxui::dim;
    }
    cells.push_back(std::move(minus));
  }
  cells.push_back(std::move(level_text));
  if (!read_only_) {
    ftxui::Element plus = ftxui::text("[+]");
    if (selected && hyper_col_ == kHyperColPlus) {
      plus = std::move(plus) | ftxui::inverted;
    } else if (!CanRaiseHyperStat(field)) {
      plus = std::move(plus) | ftxui::dim;
    }
    cells.push_back(std::move(plus));
  }
  cells.push_back(ftxui::text(" "));
  return ftxui::hbox(std::move(cells)) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, row_width);
}

ftxui::Element CharacterPanel::RenderHyperTab(bool bar_focused,
                                              bool rows_focused,
                                              bool reset_focused) const {
  std::vector<ftxui::Element> rows;
  rows.push_back(RenderPresetBar(
      bar_focused,
      std::to_string(character_.hyper_stat_points_left(hyper_preset_)) +
          " Points"));
  rows.push_back(PanelSeparator(highlighted_));

  int visible = HyperRowsShown();
  int first = FirstHyperRow(visible);
  // Empty while all fourteen fit, and then the rows keep their full width.
  std::vector<ftxui::Element> cells =
      ScrollBarCells(kNumHyperStats, first, visible);
  int row_width = cells.empty() ? kStatsWidth : kStatsWidth - 1;
  for (int i = 0; i < visible; ++i) {
    ftxui::Element row = RenderHyperRow(kHyperStatOrder[first + i], first + i,
                                        rows_focused, row_width);
    if (!cells.empty()) {
      row = ftxui::hbox({
          std::move(row),
          std::move(cells[i]) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1),
      });
    }
    rows.push_back(StatsAligned(std::move(row)));
  }
  // The way back to nothing spent, which is free, so it sits under its own rule
  // rather than among the rows it undoes. Both always draw whatever the budget,
  // since a screen with no way back is worse than a shorter list.
  if (!read_only_) {
    rows.push_back(PanelSeparator(highlighted_));
    rows.push_back(CenteredCell(
        "[Reset]", reset_focused ? ftxui::inverted : ftxui::nothing,
        ContentWidth()));
  }
  return ftxui::vbox(std::move(rows));
}

int CharacterPanel::AbilityRows() const {
  return character_.ability(hyper_preset_).lines_size();
}

bool CharacterPanel::CanRerollAbility() const {
  const int64_t cost = character_.ability_reset_cost(hyper_preset_);
  return cost > 0 && character_.honor() >= cost;
}

ftxui::Element CharacterPanel::RenderAbilityRow(const AbilityLine& line,
                                                int index,
                                                bool rows_focused) const {
  // The name and amount form one centred phrase: "All Stats +10" reads as the
  // player's line, while a name at one end and a number at the other would read
  // as two table columns.
  const std::string label =
      AbilityLineName(line.type()) + " " + AbilityLineValueText(line);
  const int pad =
      std::max(0, (ContentWidth() - static_cast<int>(label.size())) / 2);

  // The lock never takes the rank's colour: the colour belongs to the line, not
  // to what holds it.
  ftxui::Element lock_cell =
      ftxui::text(line.locked() ? kLockedGlyph : kUnlockedGlyph);
  if (rows_focused && ability_sel_ == index) {
    // The cursor inverts what Enter acts on, as the [+] on a stat row does.
    lock_cell = std::move(lock_cell) | ftxui::inverted;
  }
  return ftxui::hbox({
             ftxui::text(std::string(pad, ' ')),
             ftxui::text(label) | ftxui::color(RarityColor(line.rank())),
             ftxui::filler(),
             std::move(lock_cell),
             ftxui::text(" "),
         }) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, ContentWidth());
}

ftxui::Element CharacterPanel::RenderAbilityTab(bool bar_focused,
                                                bool rows_focused,
                                                bool reroll_focused) const {
  std::vector<ftxui::Element> rows;
  // Honor is the one balance a character sheet doesn't include. It rises with
  // every kill, so including it would resend the whole sheet that often.
  rows.push_back(RenderPresetBar(
      bar_focused,
      read_only_ ? "" : FormatWithCommas(character_.honor()) + " Honor"));
  rows.push_back(PanelSeparator(highlighted_));

  const AbilityPreset& preset = character_.ability(hyper_preset_);
  // The preset's rank, with its banner, left-aligned above the lines: the rank
  // the three lines can roll up to, shown once instead of worked out from them.
  const std::string rank = AbilityRankName(preset.rank());
  if (!rank.empty()) {
    rows.push_back(ftxui::hbox({
                       ftxui::text(std::string(" ") + kAbilityBannerGlyph +
                                   "  " + rank + " Ability"),
                       ftxui::filler(),
                   }) |
                   ftxui::color(RarityColor(preset.rank())) |
                   ftxui::size(ftxui::WIDTH, ftxui::EQUAL, ContentWidth()));
  }
  for (int i = 0; i < preset.lines_size(); ++i) {
    rows.push_back(RenderAbilityRow(preset.lines(i), i, rows_focused));
  }

  if (read_only_) {
    return ftxui::vbox(std::move(rows));
  }
  // The reroll's price, under its own rule. Written without commas, since it is
  // a price to compare against the pool on the row above rather than a total to
  // read.
  rows.push_back(PanelSeparator(highlighted_));
  const bool affordable = CanRerollAbility();
  // Red on the value the player can't afford, so the grey button below needs no
  // explanation.
  rows.push_back(CenteredCell(
      "Honor Cost " +
          std::to_string(character_.ability_reset_cost(hyper_preset_)),
      affordable ? ftxui::nothing : ftxui::color(kRed), ContentWidth()));
  ftxui::Element reroll = CenteredCell(
      "[Reroll]", reroll_focused ? ftxui::inverted : ftxui::nothing,
      ContentWidth());
  if (!affordable) {
    reroll = std::move(reroll) | ftxui::dim;
  }
  rows.push_back(std::move(reroll));
  return ftxui::vbox(std::move(rows));
}

std::vector<const ConsumableInfo*> CharacterPanel::BuffsShown() const {
  std::vector<const ConsumableInfo*> buffs;
  for (const ConsumableInfo& info : AllConsumables()) {
    if (character_.proto().level() >= info.unlock_level) {
      buffs.push_back(&info);
    }
  }
  return buffs;
}

ftxui::Element CharacterPanel::RenderBuffRow(const ConsumableInfo& info,
                                             int index,
                                             bool rows_focused) const {
  const bool selected = rows_focused && buff_sel_ == index;
  const bool owned = character_.ConsumableOwned(info.type);
  // A buff that is off costs and does nothing, so the whole row dims, the same
  // thing dim means for a skill the character doesn't have yet.
  const bool on = character_.ConsumableActive(info.type);

  ftxui::Element tag = ftxui::text(owned ? kBuffOwnedTag : kBuffRentTag) |
                       ftxui::color(owned ? kGreen : kYellow);
  if (!on) {
    tag = std::move(tag) | ftxui::dim;
  }

  // A name too long for the column scrolls while the row is selected, like a
  // skill name, and the padding is outside the cursor so the highlight covers
  // only the name.
  const int name_width = ContentWidth() - kBuffFixedWidth;
  const std::string window =
      ScrollingWindow(info.name, name_width,
                      selected ? buff_clock_.Elapsed()
                               : std::chrono::steady_clock::duration::zero());
  const int lit =
      std::min(static_cast<int>(TextColumns(info.name)), name_width);
  ftxui::Element name = ftxui::text(ColumnWindow(window, 0, lit));
  if (selected) {
    name = std::move(name) | ftxui::inverted;
  } else if (!on) {
    name = std::move(name) | ftxui::dim;
  }

  ftxui::Element mark =
      ftxui::text(on ? kBuffOnGlyph : " ") | ftxui::color(kGreen);

  ftxui::Element row = ftxui::hbox({
      ftxui::text(" "),
      std::move(tag),
      std::move(name),
      ftxui::text(ColumnWindow(window, lit, name_width - lit)),
      ftxui::text(" "),
      std::move(mark),
      ftxui::text(" "),
  });
  if (selected) {
    row = std::move(row) | ftxui::reflect(buff_cursor_box_);
  }
  return std::move(row) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, ContentWidth());
}

ftxui::Element CharacterPanel::RenderBuffsTab(bool rows_focused) const {
  std::vector<const ConsumableInfo*> buffs = BuffsShown();
  buff_clock_.Follow(buff_sel_, rows_focused);
  std::vector<ftxui::Element> rows;
  for (int i = 0; i < static_cast<int>(buffs.size()); ++i) {
    rows.push_back(RenderBuffRow(*buffs[i], i, rows_focused));
  }
  return ftxui::vbox(std::move(rows));
}

ftxui::Element CharacterPanel::RenderAdvanceTab(bool content_focused) const {
  int stage = character_.proto().job_stage() + 1;
  std::vector<Job> jobs = JobChoicesForStage(character_.proto().job(), stage);
  std::vector<ftxui::Element> rows;
  for (int i = 0; i < static_cast<int>(jobs.size()); ++i) {
    // A caret instead of the [+] other tabs use, since nothing is spent here:
    // the player is choosing a job.
    std::string cursor = content_focused && job_sel_ == i ? " > " : "   ";
    ftxui::Element row = ftxui::text(
        PadRight(cursor + AdvancementName(jobs[i], stage), ContentWidth()));
    if (job_sel_ == i) {
      row = std::move(row) | ftxui::reflect(job_cursor_box_);
    }
    rows.push_back(std::move(row));
  }
  return ftxui::vbox(std::move(rows));
}

ftxui::Element CharacterPanel::RenderUsername(bool row_selected) const {
  if (username_field_.editing()) {
    // A caret after the typed text, so an empty field still looks like a field
    // instead of a blank row.
    return CenteredCell(username_field_.text() + "_", ftxui::inverted,
                        ContentWidth());
  }
  const std::string& name = character_.username();
  if (name == kDefaultUsername && !row_selected) {
    // Dim while it is still the placeholder, since it isn't a name yet.
    return CenteredCell(name, ftxui::dim, ContentWidth());
  }
  return CenteredCell(name, row_selected ? ftxui::inverted : ftxui::nothing,
                      ContentWidth());
}

ftxui::Element CharacterPanel::Render() const {
  NoteFocus();
  const Character& p = character_.proto();

  // Right-aligned in three columns, so the job name doesn't shift sideways as
  // the character passes levels 9 and 99.
  std::string lvl = PadLeft(std::to_string(p.level()), 3);
  std::string title =
      Centered("Lv" + lvl + " " + ShortJobName(p.job()), ContentWidth());

  std::string power = Centered(CombatPowerText(CharacterCombatPower(
                                   character_, skills_, SelectedActivity())),
                               ContentWidth());

  bool focused = panel_focus_ == kCharPanel;
  Zone zone = EffectiveZone();
  bool tab_row_selected = focused && zone == kZoneTabs;
  bool name_row_selected = focused && zone == kZoneUsername;
  ftxui::Element content;
  if (ActiveTab() == kTabSkills) {
    content = RenderSkillsTab(focused && zone == kZoneAdvTabs,
                              focused && zone == kZoneSkillRows,
                              focused && zone == kZoneVReset);
  } else if (ActiveTab() == kTabHyper) {
    content = RenderHyperTab(focused && zone == kZonePresets,
                             focused && zone == kZoneHyperRows,
                             focused && zone == kZoneHyperReset);
  } else if (ActiveTab() == kTabAbility) {
    content = RenderAbilityTab(focused && zone == kZonePresets,
                               focused && zone == kZoneAbilityRows,
                               focused && zone == kZoneAbilityReroll);
  } else if (ActiveTab() == kTabBuffs) {
    content = RenderBuffsTab(focused && zone == kZoneBuffRows);
  } else if (ActiveTab() == kTabAdvance) {
    content = RenderAdvanceTab(focused && zone == kZoneJobRows);
  } else {
    content = RenderStatsTab(focused && zone == kZonePresets,
                             focused && zone == kZoneStatRows);
  }

  std::vector<ftxui::Element> rows = {
      RenderUsername(name_row_selected),
      ftxui::text(title),
      ftxui::text(power),
      PanelSeparator(highlighted_),
      RenderTabBar(tab_row_selected),
  };
  // A second row of tabs sits directly under the first, as in the shop. The
  // rule goes under the pair rather than between them, and the tab that has the
  // second row draws it.
  if (!ShowsSecondTabRow()) {
    rows.push_back(PanelSeparator(highlighted_));
  }
  rows.push_back(content);
  return AccentWindow(" Character ", ftxui::vbox(std::move(rows)),
                      PanelAccent(highlighted_), focused,
                      account_.panel_title_blink());
}

bool CharacterPanel::OnUsernameEvent(const ftxui::Event& event) {
  if (username_field_.editing()) {
    TextEntry entry = username_field_.OnEvent(event);
    if (entry == TextEntry::kCommitted) {
      character_.SetUsername(username_field_.text());
      return true;
    }
    // Up and Down discard the typed name and then move, as they would have if
    // the field had never been open. Escape only closes it.
    if (entry == TextEntry::kCancelled &&
        (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown)) {
      MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    }
    return true;
  }
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (IsForward(event) && !read_only_) {
    username_field_.BeginEdit();
    return true;
  }
  return false;
}

bool CharacterPanel::OnTabsEvent(const ftxui::Event& event) {
  // Top zone: Left and Right move between tabs, and Up and Down enter the
  // active tab's content.
  if (event == ftxui::Event::ArrowLeft) {
    active_tab_ = std::max(0, active_tab_ - 1);
    MarkActiveTabSeen();
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    active_tab_ =
        std::min(static_cast<int>(VisibleTabs().size()) - 1, active_tab_ + 1);
    MarkActiveTabSeen();
    return true;
  }
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    // Down enters the tab's content at its first row and Up at its last, since
    // the bar is a stop in the same ring. Where the first row is depends on the
    // tab.
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  return false;
}

bool CharacterPanel::OnAdvanceTabEvent(const ftxui::Event& event,
                                       const CharacterPanelActions& actions) {
  // Job rows: Up and Down move through them, and past either end is the tab
  // bar.
  std::vector<Job> jobs = JobChoicesForStage(
      character_.proto().job(), character_.proto().job_stage() + 1);
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (IsForward(event)) {
    if (actions.advance && job_sel_ < static_cast<int>(jobs.size())) {
      actions.advance(jobs[job_sel_]);
    }
    return true;
  }
  return false;
}

bool CharacterPanel::OnPresetBarEvent(const ftxui::Event& event,
                                      const CharacterPanelActions& actions) {
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  // Stops at the ends, like every tab bar in this panel.
  if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
    int step = event == ftxui::Event::ArrowLeft ? -1 : 1;
    int at = IndexOf(PresetBarSelection()) + step;
    hyper_preset_ = StatPresetAt(std::clamp(at, 0, PresetChips() - 1));
    return true;
  }
  if (IsForward(event) && PresetBarNamesSlots() && actions.preset_menu) {
    actions.preset_menu(ActiveTab() == kTabAbility ? PresetKind::kInnerAbility
                                                   : PresetKind::kHyperStats,
                        PresetBarSelection());
    return true;
  }
  return false;
}

bool CharacterPanel::OnStatsTabEvent(const ftxui::Event& event,
                                     const CharacterPanelActions& actions) {
  if (zone_ == kZonePresets) {
    return OnPresetBarEvent(event, actions);
  }
  // Stat rows: Up and Down move through them, and past either end is the tab
  // bar. Left and Right do nothing here; they belong to the tab bar.
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (!IsForward(event)) {
    return false;
  }
  if (OnViewAllStatsRow()) {
    if (actions.all_stats) {
      actions.all_stats();
    }
    return true;
  }
  if (!read_only_ && character_.proto().ap() > 0) {
    if (actions.allocate) {
      actions.allocate(kAllocStats[stat_sel_].field);
    }
    return true;
  }
  return false;
}

bool CharacterPanel::OnViewAllStatsRow() const {
  return ShowsCombatStats() && EffectiveZone() == kZoneStatRows &&
         stat_sel_ == kNumAllocStats;
}

bool CharacterPanel::ShowsCombatStats() const {
  return Unlocked(Feature::kCombatStats, character_, account_);
}

bool CharacterPanel::OnHyperTabEvent(const ftxui::Event& event,
                                     const CharacterPanelActions& actions) {
  if (zone_ == kZonePresets) {
    return OnPresetBarEvent(event, actions);
  }
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  // Left and Right move across the three columns and stop at the ends, like the
  // tab bars. The [Reset] button is one column wide, so it ignores both.
  // Read-only has one column, the name, since the two buttons aren't drawn.
  if (event == ftxui::Event::ArrowLeft && zone_ == kZoneHyperRows) {
    hyper_col_ = read_only_
                     ? kHyperColName
                     : static_cast<HyperCol>(std::max(0, hyper_col_ - 1));
    return true;
  }
  if (event == ftxui::Event::ArrowRight && zone_ == kZoneHyperRows) {
    hyper_col_ = read_only_ ? kHyperColName
                            : static_cast<HyperCol>(
                                  std::min<int>(kHyperColPlus, hyper_col_ + 1));
    return true;
  }
  if (!IsForward(event)) {
    return false;
  }
  if (zone_ == kZoneHyperReset) {
    if (actions.hyper_reset) {
      actions.hyper_reset();
    }
    return true;
  }
  HyperStatField field = kHyperStatOrder[hyper_sel_];
  if (hyper_col_ == kHyperColName) {
    // Never gated: a stat the character's level hasn't unlocked is one they
    // most want to read about.
    if (actions.hyper_inspect) {
      actions.hyper_inspect(field);
    }
    return true;
  }
  if (hyper_col_ == kHyperColMinus) {
    if (actions.hyper_lower && CanLowerHyperStat(field)) {
      actions.hyper_lower(field);
    }
    return true;
  }
  if (actions.hyper_allocate && CanRaiseHyperStat(field)) {
    actions.hyper_allocate(field);
  }
  return true;
}

bool CharacterPanel::OnAbilityTabEvent(const ftxui::Event& event,
                                       const CharacterPanelActions& actions) {
  if (zone_ == kZonePresets) {
    return OnPresetBarEvent(event, actions);
  }
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (!IsForward(event)) {
    return false;
  }
  if (zone_ == kZoneAbilityReroll) {
    // A pool that can't pay already shows the red cost and the grey button.
    // Enter on it does nothing rather than opening a dialog to repeat what is
    // on screen.
    if (actions.ability_reroll && CanRerollAbility()) {
      actions.ability_reroll();
    }
    return true;
  }
  // LockAbilityLine decides whether a third lock is too many.
  const AbilityPreset& preset = character_.ability(hyper_preset_);
  if (actions.ability_lock && ability_sel_ < preset.lines_size()) {
    actions.ability_lock(ability_sel_);
  }
  return true;
}

bool CharacterPanel::OnBuffsTabEvent(const ftxui::Event& event,
                                     const CharacterPanelActions& actions) {
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (!IsForward(event)) {
    return false;
  }
  std::vector<const ConsumableInfo*> buffs = BuffsShown();
  if (buff_sel_ >= static_cast<int>(buffs.size())) {
    return true;
  }
  // Everything a buff offers is on its menu, including turning it on. The row
  // has no room for a second stop, and nothing here is pressed often enough to
  // need one.
  if (actions.buff_menu) {
    actions.buff_menu(buffs[buff_sel_]->type);
  }
  return true;
}

void CharacterPanel::StepSkillPage(int delta) {
  skill_tab_ =
      std::max(0, std::min(SkillPages() - 1, SelectedSkillPage() + delta));
  skill_page_chosen_ = true;
  // Landing on a page counts as reading it, the same rule as the outer tab bar.
  MarkActiveTabSeen();
}

bool CharacterPanel::OnSkillsTabEvent(const ftxui::Event& event,
                                      const CharacterPanelActions& actions) {
  // Up and Down move through every zone here: the page bar, the skill rows and
  // the V page's [Reset], wrapping out to the bars above and below.
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  // Page bar: Left and Right switch stages.
  if (zone_ == kZoneAdvTabs) {
    if (event == ftxui::Event::ArrowLeft) {
      StepSkillPage(-1);
      return true;
    }
    if (event == ftxui::Event::ArrowRight) {
      StepSkillPage(1);
      return true;
    }
    return false;
  }
  // The V page's [Reset] ignores Left and Right, since it is one column wide,
  // like the Hyper tab's.
  if (zone_ == kZoneVReset) {
    if (!IsForward(event)) {
      return false;
    }
    if (actions.v_reset) {
      actions.v_reset();
    }
    return true;
  }
  // Skill rows: Left and Right pick the column Enter acts on.
  std::vector<const Skill*> skills = SkillsForPage(SelectedSkillPage());
  if (event == ftxui::Event::ArrowLeft) {
    skill_col_ = kColName;
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    // A page with no [+] has no second column to move to.
    skill_col_ = ShowsSkillPlus() ? kColPlus : kColName;
    return true;
  }
  if (IsForward(event)) {
    if (ShowsLinkRow() && skill_sel_ == 0) {
      FollowedToLinkSkills(LinkTrailStep::kLinkRow, account_);
      if (actions.link_skills) {
        actions.link_skills();
      }
      return true;
    }
    // The page can have fewer skills than the row the cursor was last on, since
    // switching pages doesn't reset it.
    if (SkillIndexFor(skill_sel_) >= static_cast<int>(skills.size())) {
      return true;
    }
    const Skill& skill = *skills[SkillIndexFor(skill_sel_)];
    if (EffectiveSkillCol() == kColName) {
      // Never gated: a maxed skill with no SP still has a description and a
      // level table worth reading.
      if (actions.menu) {
        actions.menu(skill);
      }
      return true;
    }
    bool maxed = character_.skill_level(skill) >= SkillMaxLevel(skill);
    if (actions.learn && !maxed && !SkillLocked(skill) &&
        character_.LevelsAffordable(skill) > 0) {
      actions.learn(skill);
    }
    return true;
  }
  return false;
}

bool CharacterPanel::RouteEvent(const ftxui::Event& event,
                                const CharacterPanelActions& actions) {
  // Route by zone: the shared tab bar, otherwise the active tab's content (only
  // Stats reaches kZoneStatRows, and only Skills reaches the skill zones).
  if (zone_ == kZoneUsername) {
    return OnUsernameEvent(event);
  }
  if (zone_ == kZoneTabs) {
    return OnTabsEvent(event);
  }
  if (ActiveTab() == kTabStats) {
    return OnStatsTabEvent(event, actions);
  }
  if (ActiveTab() == kTabHyper) {
    return OnHyperTabEvent(event, actions);
  }
  if (ActiveTab() == kTabAbility) {
    return OnAbilityTabEvent(event, actions);
  }
  if (ActiveTab() == kTabBuffs) {
    return OnBuffsTabEvent(event, actions);
  }
  if (ActiveTab() == kTabAdvance) {
    return OnAdvanceTabEvent(event, actions);
  }
  return OnSkillsTabEvent(event, actions);
}

ftxui::Component CharacterPanel::MakeComponent(CharacterPanelActions actions) {
  // The Renderer(bool) overload is Focusable(), unlike Renderer(). It is needed
  // so Container::Tab's Focused() check passes when panel_focus_ == kCharPanel.
  ftxui::Component renderer =
      ftxui::Renderer([this](bool /*focused*/) { return Render(); });
  return ftxui::CatchEvent(
      renderer, [this, actions = std::move(actions)](ftxui::Event event) {
        if (panel_focus_ != kCharPanel) {
          return false;
        }
        // The tab bar may have changed since the last key (taking the
        // advancement does that), so settle where the cursor is before reading
        // the event. Clamped to the last tab, as ActiveTab clamps, since the
        // tab that disappeared was at the end.
        int tabs = static_cast<int>(VisibleTabs().size());
        if (active_tab_ >= tabs) {
          active_tab_ = tabs - 1;
        }
        zone_ = EffectiveZone();
        int tab_before = active_tab_;
        Zone zone_before = zone_;
        int stop_before = CursorStop();
        bool handled = RouteEvent(event, actions);
        if (active_tab_ != tab_before || EffectiveZone() != zone_before ||
            CursorStop() != stop_before) {
          cursor_moved_ = true;
        }
        return handled;
      });
}

}  // namespace ms
