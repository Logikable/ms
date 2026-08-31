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
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// The Stats tab lays out in these columns however wide the panel is: the
// extra a wide terminal brings goes to the Skills tab, and a stat's value
// chasing the border would leave it a screen away from its own label.
constexpr int kStatsWidth = kLeftColumnMin - 2;

// One line of the panel's heading block, centred over the content width.
std::string Centered(const std::string& s, int width) {
  int pad = std::max(0, (width - static_cast<int>(s.size())) / 2);
  return PadRight(std::string(pad, ' ') + s, width);
}

// A centred row whose text alone carries the decorator, for a row the cursor
// can land on. Not CenteredRow: every row of this panel is padded to
// its width, and a flexed row would centre itself in whatever width the
// window came out at instead. The padding is separate text so that inverting
// the label does not invert the whole row with it.
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
const char* kTabLabels[] = {"Stats", "Skills", "Hyper", "Ability", "Advance"};

// The Farm/Boss row, which names the two Hyper Stat allocations wherever the
// player meets them.
constexpr char kFarmTabLabel[] = "Farm";
constexpr char kBossTabLabel[] = "Boss";

// Whether an Inner Ability line is held through a reroll. Two columns wide
// either way, so the lock column is the same column on every row.
constexpr char kLockedGlyph[] = "\U0001F512";
constexpr char kUnlockedGlyph[] = "\U0001F513";

// The Hyper tab's columns. The level and the [+] are fixed and the name takes
// what is left, which is what puts exactly one column between the level and
// the [+] on every row -- the shape the skill rows have.
constexpr int kHyperLevelWidth = 2;
constexpr int kHyperPlusWidth = 3;

// What the row spends on everything but the name: the leading gutter, the
// level, the gap after it, the [+] and the trailing gutter.
constexpr int kHyperFixedWidth = 1 + kHyperLevelWidth + 1 + kHyperPlusWidth + 1;

// Roman numerals for the job-advancement tabs, indexed by stage (1..6).
const char* kStageNumerals[] = {"", "I", "II", "III", "IV", "V", "VI"};

// Columns the SP counter takes off the end of the stage bar's row: " 999 SP ".
constexpr int kSpCol = 8;

// The four AP-allocatable stats, in display order; the index is stat_sel_.
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

// One of the extra stats: a label column and a number column hard against the
// right edge, one gutter shy of the border. The main stats above them keep
// their own "LABEL: value" shape, since their [+] and their AP counter already
// own that edge.
//
// The value's RIGHT edge is fixed and the gap before it gives way. Padding the
// label instead leaves a long value nothing to take from, and Defense -- the
// one stat written "(base+bonus) total" -- then runs a column past every other
// value in the panel.
ftxui::Element StatRow(const std::string& label, const std::string& value) {
  int gap = kStatsWidth - 2 - static_cast<int>(value.size());
  return ftxui::text(" " + PadRight(label, std::max(0, gap)) + value + " ");
}

// "STR: 13" with an optional " (base+bonus)" suffix when gear contributes.
std::string StatText(const std::string& label, int base, int bonus) {
  std::string s = label + ": " + std::to_string(base + bonus);
  if (bonus > 0) {
    s += " (" + std::to_string(base) + "+" + std::to_string(bonus) + ")";
  }
  return s;
}

// The skill row's columns. Every one is a fixed width, so the row comes to
// exactly the row's width and a long name slides inside its column instead of
// pushing the panel wider -- which is what a name like "Final Attack:
// Crossbow" used to do to the whole Character panel.
//
//   " " + tag(4) + name + level + filler + "[+]" + " "
constexpr int kSkillPlusWidth = 3;

// Room for every row of a page, so that folding the page into the name
// clock's key cannot land on the key another page's row already holds.
constexpr int kSkillClockPageStride = 4096;

// What a skill row prints for its level: what the skill is worth, and how much
// of that its book lent. A skill nobody has opened is a bare 0 -- nothing is
// lent to a skill that has not been bought.
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

// The base (AP-allocated) and gear-bonus values for one allocatable stat.
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
      panel_focus_(panel_focus) {
}

// `row_width` is what the row has to lay out in, which is one short of the
// content width while the scroll bar holds a column.
int CharacterPanel::SkillNameWidth(int level_width, int row_width) {
  return row_width - 1 - kSkillTagWidth - level_width - kSkillPlusWidth - 1;
}

ftxui::Element CharacterPanel::AllocRow(const std::string& label, int base,
                                        int bonus, int index,
                                        bool content_focused) const {
  bool selected = content_focused && stat_sel_ == index;
  // The cursor outranks the unavailable cue, as on the skill rows: a selected
  // [+] inverts even with no AP to spend, so the cursor stays visible while
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
  // Skills belong to a job, so the tab waits for one: a Beginner standing at
  // level 10 is being offered an advancement, not a skill list.
  if (Unlocked(Feature::kSkills, character_, account_)) {
    tabs.push_back(kTabSkills);
  }
  // After Skills, and gone below the level Hyper Stats open at: a tab with no
  // points behind it is a list of things the player cannot have.
  if (Unlocked(Feature::kHyperStats, character_, account_)) {
    tabs.push_back(kTabHyper);
  }
  // Ability is the one tab gated on this character alone. The Feature table is
  // account-wide by construction, and here that would be a lie: an ability
  // below the unlock level grants nothing and cannot be rerolled, whoever else
  // on the account has been there.
  if (character_.inner_ability_unlocked()) {
    tabs.push_back(kTabAbility);
  }
  // The Advance tab exists only while there is an advancement to take, so it
  // arrives at level 10 and is gone the moment the player picks a job.
  if (character_.CanAdvanceJob()) {
    tabs.push_back(kTabAdvance);
  }
  return tabs;
}

CharacterPanel::Tab CharacterPanel::ActiveTab() const {
  std::vector<Tab> tabs = VisibleTabs();
  if (active_tab_ < 0 || active_tab_ >= static_cast<int>(tabs.size())) {
    return kTabStats;
  }
  return tabs[active_tab_];
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
    case kZoneAdvTabs:
    case kZoneSkillRows:
      return ActiveTab() == kTabSkills ? zone_ : kZoneTabs;
    case kZoneJobRows:
      return ActiveTab() == kTabAdvance ? zone_ : kZoneTabs;
  }
  return kZoneTabs;
}

int CharacterPanel::FirstStatStop() const {
  // The tab bar and the name are stops 0 and 1, and the Farm/Boss row takes
  // the next one when it is there.
  return ShowsPresetBar() ? 3 : 2;
}

int CharacterPanel::RingStops() const {
  // The username row is stop 0 of every tab, and the tab bar stop 1, which is
  // what puts the name one step up from the bar and one step down off the
  // bottom row -- neither is a rule of its own.
  if (ActiveTab() == kTabStats) {
    // The two of them, the Farm/Boss row if it is there, the four AP stats,
    // and the View All Stats row under them -- which is not there while there
    // are no combat stats to lead to.
    return 2 + (ShowsPresetBar() ? 1 : 0) + kNumAllocStats +
           (ShowsCombatStats() ? 1 : 0);
  }
  if (ActiveTab() == kTabHyper) {
    // The two of them, the Farm/Boss row, a stop per stat, and [Reset].
    return 3 + kNumHyperStats + 1;
  }
  if (ActiveTab() == kTabAbility) {
    // The same shape: the two of them, the Farm/Boss row, a stop per line, and
    // [Reroll].
    return 3 + AbilityRows() + 1;
  }
  if (ActiveTab() == kTabAdvance) {
    return 2 + static_cast<int>(
                   JobChoicesForStage(character_.proto().job(),
                                      character_.proto().job_stage() + 1)
                       .size());
  }
  if (character_.proto().job_stage() == 0) {
    // A Beginner's Skills tab has no advancement bar to stand on, let alone
    // skills under it. The name and the bar, and nothing below them.
    return 2;
  }
  return 3 + static_cast<int>(SkillsForPage(skill_tab_).size());
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
      return stat_sel_ + FirstStatStop();
    case kZoneJobRows:
      return job_sel_ + 2;
    case kZoneAdvTabs:
      return 2;
    case kZoneSkillRows:
      return skill_sel_ + 3;
    case kZoneHyperRows:
      return hyper_sel_ + 3;
    case kZoneHyperReset:
      return kNumHyperStats + 3;
    case kZoneAbilityRows:
      return ability_sel_ + 3;
    case kZoneAbilityReroll:
      return AbilityRows() + 3;
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
    stat_sel_ = stop - FirstStatStop();
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
  if (ActiveTab() == kTabAdvance) {
    zone_ = kZoneJobRows;
    job_sel_ = stop - 2;
    return;
  }
  if (stop == 2) {
    zone_ = kZoneAdvTabs;
    return;
  }
  if (zone_ != kZoneSkillRows) {
    // Arriving from outside the rows, so land on the name -- the leftmost
    // column, the way the eye reads the row. Walking from one row to the next
    // keeps whichever column the cursor was already in.
    skill_col_ = kColName;
  }
  zone_ = kZoneSkillRows;
  skill_sel_ = stop - 3;
}

void CharacterPanel::MoveCursor(int delta) {
  // The advancement bar keeps whichever page the player left it on. Entering
  // the Skills tab used to snap it to the newest stage, which meant a
  // second-job character could not get back down onto their first book: every
  // Down from the tab bar put them on page II again.
  SetCursorStop(StepCursor(CursorStop(), delta, RingStops()));
}

std::string CharacterPanel::TabKey(Tab tab) const {
  if (tab == kTabHyper) {
    // Gold on the bar until the player has opened it once. One key for the
    // tab, not one per level: it arrives once and stays.
    return kHyperTabKey;
  }
  if (tab == kTabAbility) {
    // Gold for the first character on the account to reach it, and quiet for
    // every one after -- MarkSeen is account-wide, so a player is told about
    // Inner Ability once.
    return kAbilityTabKey;
  }
  if (tab == kTabAdvance) {
    // The stage being advanced INTO, so the tab that comes back at level 30 is
    // news again rather than riding on the one taken at 10.
    return AdvanceTabKey(character_.proto().job_stage() + 1);
  }
  // Stats has been there since the first frame of the game, and Skills arrives
  // with the player already standing on it -- see RenderTabBar. Neither has
  // anything to announce, and neither wastes a key in the save on saying so.
  return "";
}

void CharacterPanel::MarkActiveTabSeen() {
  std::string key = TabKey(ActiveTab());
  if (!key.empty()) {
    account_.MarkSeen(key);
  }
}

bool CharacterPanel::ShowsPresetBar() const {
  // The Ability tab is never there below the level Hyper Stats open at, so its
  // row is not gated on anything further.
  if (ActiveTab() == kTabAbility) {
    return true;
  }
  // Otherwise only the tabs whose numbers come out of an allocation carry it.
  // The Advance tab lists jobs, and the Skills tab has a bar of its own.
  if (ActiveTab() != kTabStats && ActiveTab() != kTabHyper) {
    return false;
  }
  return Unlocked(Feature::kHyperStats, character_, account_);
}

bool CharacterPanel::ShowsSecondTabRow() const {
  if (ActiveTab() == kTabSkills) {
    return character_.proto().job_stage() > 0;
  }
  return ShowsPresetBar();
}

int CharacterPanel::StatsTabFixedRows() const {
  return kStatsTabFixedRows + (ShowsPresetBar() ? 1 : 0);
}

ftxui::Element CharacterPanel::RenderPresetBar(
    bool bar_focused, const std::string& trailing) const {
  std::vector<TabSpec> specs = {{kFarmTabLabel}, {kBossTabLabel}};
  int active = hyper_preset_ == StatPreset::kBossing ? 1 : 0;
  // The chips take what the trailing text and its gutter leave. Two four-letter
  // labels never come near even the narrowest of that, so the bar does not
  // scroll here whatever the number beside it grows to.
  int width = ContentWidth();
  if (!trailing.empty()) {
    width -= static_cast<int>(ftxui::string_width(trailing)) + 1;
  }
  std::vector<ftxui::Element> row = {TabBar(specs, active, bar_focused, width),
                                     ftxui::filler()};
  if (!trailing.empty()) {
    // The counter reads like the AP and SP counters above it: what there is
    // left to spend, on the row the spending happens under.
    row.push_back(ftxui::text(trailing + " "));
  }
  return ftxui::hbox(std::move(row));
}

ftxui::Element CharacterPanel::RenderTabBar(bool row_selected) const {
  // Left-aligned chip row in the shared tab style.
  std::vector<Tab> tabs = VisibleTabs();
  std::vector<TabSpec> specs;
  int active = 0;
  for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
    // A tab with no key never announces itself. Asking TabSeen("") instead
    // would answer no and leave those tabs permanently gold.
    //
    // Skills is one of them, and not for want of being new. It takes the exact
    // index the Advance tab vacates, so an advancement leaves the player
    // standing on it -- gold on a tab they are already reading says nothing,
    // and would sit there until they arrowed away and back to clear it.
    std::string key = TabKey(tabs[i]);
    if (tabs[i] == ActiveTab()) {
      active = i;
    }
    specs.push_back({kTabLabels[tabs[i]], !key.empty() && !account_.Seen(key)});
  }
  // The tabs are most of a narrow panel's width, so this is the bar most
  // likely to need the scroll.
  return ftxui::hbox(
      {TabBar(specs, active, row_selected, ContentWidth()), ftxui::filler()});
}

// The MP display row with the character's unspent AP right-aligned, so the
// player can see how much there is to spend on the [+] rows below.
ftxui::Element CharacterPanel::MpRow(int mp, int ap) const {
  return StatsAligned(ftxui::hbox({
      ftxui::text(" MP: " + std::to_string(mp)),
      ftxui::filler(),
      ftxui::text(std::to_string(ap) + " AP "),
  }));
}

ftxui::Element CharacterPanel::StatsAligned(ftxui::Element row) const {
  // Centred in the panel: the block does not spread with the terminal -- a
  // value chasing the border would leave its label at the other end of the
  // row -- so the room a wide panel brings is blank either side of it.
  //
  // An odd column goes to the side the labels are on, which puts the block's
  // right-hand column in the Skills tab's [+] column on the widths where the
  // slack is a column or none. Wider than that the two part company, which is
  // what centring costs.
  //
  // The padding is text rather than filler(): a row that right-aligns
  // something ends in a filler of its own, and two of them would share the
  // slack between them and drag the column off its neighbours'.
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
  // The View All Stats row is paid for first, because it is where the stats
  // that did not fit have gone. A budget too small even for that still shows
  // it and lets the panel be clipped: a panel with no way out of it is worse.
  return std::max(0, std::min(total, max_rows_ - StatsTabFixedRows() - 1));
}

bool CharacterPanel::ShowsExtrasRule() const {
  // The rule is the first thing the extras block gives up. A budget with no
  // room for it is one row shorter than the panel's chrome, which is the row
  // the combat panel below needs on a 24-row terminal.
  return max_rows_ <= 0 || max_rows_ >= StatsTabFixedRows() + 1;
}

ftxui::Element CharacterPanel::RenderStatsTab(bool bar_focused,
                                              bool rows_focused) const {
  const Character& p = character_.proto();
  const AllocatedStats& a = p.allocated_stats();
  bool content_focused = rows_focused;

  // HP, MP and DEF carry passive-skill bonuses on top of the allocated and worn
  // values, so they come from the derived totals rather than a bare sum. The
  // stat rows do too: a skill's LUK belongs in the same column as a ring's.
  DerivedStats derived = DerivedStatsFor(character_, skills_, /*buffs_up=*/{},
                                         /*allies=*/{}, hyper_preset_);
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
  // A Beginner's tab ends at the AP rows: nothing below fills in yet, and a
  // separator with nothing under it reads as something that failed to draw.
  if (!ShowsCombatStats()) {
    return ftxui::vbox(std::move(rows));
  }
  if (ShowsExtrasRule()) {
    rows.push_back(PanelSeparator(highlighted_));
  }
  std::vector<StatLine> extras =
      PanelExtraStatLines(character_, account_, skills_, hyper_preset_);
  int shown = ExtraStatsShown(static_cast<int>(extras.size()));
  // A rule with nothing under it reads as a row that failed to draw, so the
  // cut takes it too rather than leaving it on the end.
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
  // Closes the block, because whatever did not fit above it is on the screen
  // it opens -- and the cursor reaches it by walking off the foot of the
  // stats. It is the last row to go, not the first.
  //
  bool selected = content_focused && stat_sel_ == kNumAllocStats;
  rows.push_back(CenteredCell("View All Stats",
                              selected ? ftxui::inverted : ftxui::nothing,
                              ContentWidth()));
  return ftxui::vbox(std::move(rows));
}

ftxui::Element CharacterPanel::RenderAdvTabBar(bool bar_focused) const {
  // One chip per page, in the shared tab style: the stages by their numeral,
  // and the Hyper page by an H, which is what GMS calls it. The selected
  // page's points are right-aligned on the same row, reading like the AP
  // counter.
  std::vector<TabSpec> specs;
  for (int page = 0; page < SkillPages(); ++page) {
    specs.push_back({IsHyperPage(page) ? "H" : kStageNumerals[page + 1]});
  }
  // The SP counter shares the row, so the bar gets what is left of it.
  std::vector<ftxui::Element> row;
  row.push_back(
      TabBar(specs, skill_tab_, bar_focused, ContentWidth() - kSpCol));
  row.push_back(ftxui::filler());
  int points = IsHyperPage(skill_tab_) ? character_.hyper_sp()
                                       : character_.sp(skill_tab_ + 1);
  row.push_back(ftxui::text(std::to_string(points) + " SP "));
  return ftxui::hbox(std::move(row));
}

CharacterPanel::LevelColumn CharacterPanel::MeasureLevelColumn(
    const std::vector<const Skill*>& skills) const {
  LevelColumn column;
  column.bonus = BonusSkillLevels(character_, skills_);
  int widest = 1;  // "0", which is the narrowest a level ever reads
  for (const Skill* skill : skills) {
    widest = std::max(
        widest, static_cast<int>(
                    SkillLevelText(character_, *skill, column.bonus).size()));
  }
  column.width = 1 + widest + 1;
  return column;
}

bool CharacterPanel::HasHyperPage() const {
  // The lowest level any of this character's Hyper Skills opens at. Asked of
  // the catalog rather than written as a number, so the page arrives the level
  // the first skill on it does however the data moves.
  for (const Skill* skill : SkillsForAdvancement(
           skills_,
           AdvancementForJobStage(character_.proto().job(),
                                  character_.proto().job_stage()),
           /*hyper=*/true)) {
    if (character_.proto().level() >= skill->required_level()) {
      return true;
    }
  }
  return false;
}

int CharacterPanel::SkillPages() const {
  return character_.proto().job_stage() + (HasHyperPage() ? 1 : 0);
}

bool CharacterPanel::IsHyperPage(int page) const {
  return HasHyperPage() && page == character_.proto().job_stage();
}

std::vector<const Skill*> CharacterPanel::SkillsForPage(int page) const {
  // A page shows exactly the skills of the advancement this character's job is
  // at -- so a Swordman never sees an Archer's skills, and vice versa. An
  // unreached or undefined advancement has none. The Hyper page hangs off the
  // advancement the character is at now, which is the book its skills upgrade.
  int stage = IsHyperPage(page) ? character_.proto().job_stage() : page + 1;
  std::set<std::string> toggles_on(character_.proto().active_skill().begin(),
                                   character_.proto().active_skill().end());
  return SkillsForAdvancement(
      skills_, AdvancementForJobStage(character_.proto().job(), stage),
      IsHyperPage(page), toggles_on);
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
  bool maxed = learned >= skill.max_level();
  bool has_sp = character_.SpFor(skill) > 0;
  // A skill still waiting on another one, or on a level, is not a skill this
  // character has yet, so the whole row dims -- name included. Running out of
  // SP dims the [+] alone, because that is a thing about the moment rather
  // than about the skill.
  bool locked = SkillLocked(skill);

  KindTag tag = TagFor(skill);
  ftxui::Element tag_text = ftxui::text(tag.text) | ftxui::color(tag.color);
  if (locked) {
    tag_text = tag_text | ftxui::dim;
  }

  // Only the name inverts -- the tag beside it is not part of what Enter
  // opens. Enter opens the skill, so the highlight covers the skill and
  // nothing else; the level beside it is a fact about the row, not a second
  // thing to press. A locked skill still opens -- the screen behind it is
  // where the player finds out what is holding it up.
  // A name too long for the column slides under it while the row is selected,
  // and sits cut when it is not. The column is a fixed width either way, which
  // is what keeps a long name from widening the whole panel.
  int name_width = SkillNameWidth(column.width, row_width);
  std::string window =
      ScrollingWindow(skill.name(), name_width,
                      selected ? name_clock_.Elapsed()
                               : std::chrono::steady_clock::duration::zero());
  // The padding a short name is given rides outside the cursor, so the
  // highlight still covers the name and stops -- a fixed-width bar would say
  // the empty column after a short name was part of what Enter opens.
  int lit = std::min(static_cast<int>(skill.name().size()), name_width);
  ftxui::Element name = ftxui::text(window.substr(0, lit));
  if (selected && skill_col_ == kColName) {
    name = name | ftxui::inverted;
  } else if (locked) {
    name = name | ftxui::dim;
  }
  ftxui::Element name_pad = ftxui::text(window.substr(lit));
  // Right-aligned, so the gap a short level leaves sits between the name and
  // the level rather than after it -- and the trailing gutter the row has no
  // filler left to provide.
  ftxui::Element level_text =
      ftxui::text(PadLeft(SkillLevelText(character_, skill, column.bonus),
                          column.width - 1) +
                  " ");
  if (locked) {
    level_text = level_text | ftxui::dim;
  }
  ftxui::Element plus = ftxui::text("[+]");
  if (selected && skill_col_ == kColPlus) {
    plus = plus | ftxui::inverted;
  } else if (maxed || !has_sp || locked) {
    plus = plus | ftxui::dim;
  }
  ftxui::Element row = ftxui::hbox({
      ftxui::text(" "),
      tag_text,
      name,
      name_pad,
      level_text,
      ftxui::filler(),
      plus,
      ftxui::text(" "),
  });
  if (selected) {
    row = std::move(row) | ftxui::reflect(skill_cursor_box_);
  }
  return row;
}

int CharacterPanel::SkillRowsShown(int total) const {
  if (max_rows_ <= 0) {
    return total;
  }
  // At least one row however small the budget: a page cut to nothing says
  // less than a page cut short, and the cursor has to have somewhere to be.
  return std::max(1, std::min(total, max_rows_ - kSkillsTabFixedRows));
}

int CharacterPanel::FirstSkillRow(int total, int visible) const {
  return ScrollWindowStart(total, skill_sel_, visible);
}

ftxui::Element CharacterPanel::RenderSkillsTab(bool bar_focused,
                                               bool rows_focused) const {
  if (character_.proto().job_stage() == 0) {
    return ftxui::text(PadRight(" No advancements yet.", ContentWidth())) |
           ftxui::dim;
  }
  std::vector<ftxui::Element> rows;
  rows.push_back(RenderAdvTabBar(bar_focused));
  rows.push_back(PanelSeparator(highlighted_));
  std::vector<const Skill*> skills = SkillsForPage(skill_tab_);
  if (skills.empty()) {
    rows.push_back(ftxui::text(PadRight(" No skills yet.", ContentWidth())) |
                   ftxui::dim);
    return ftxui::vbox(std::move(rows));
  }
  // The page rides in the key beside the row, so the same row of another page
  // counts as a different skill and starts from its own head.
  name_clock_.Follow(skill_tab_ * kSkillClockPageStride + skill_sel_,
                     rows_focused);
  int total = static_cast<int>(skills.size());
  int visible = SkillRowsShown(total);
  int first = FirstSkillRow(total, visible);
  // Empty while the whole book fits, and then the rows keep their full width.
  // The level column is measured over the whole book rather than the window,
  // so scrolling does not shuffle the names sideways.
  std::vector<ftxui::Element> cells = ScrollBarCells(total, first, visible);
  int row_width = cells.empty() ? ContentWidth() : ContentWidth() - 1;
  LevelColumn column = MeasureLevelColumn(skills);
  for (int i = 0; i < visible; ++i) {
    ftxui::Element row = RenderSkillRow(*skills[first + i], first + i, column,
                                        rows_focused, row_width);
    if (cells.empty()) {
      rows.push_back(std::move(row));
      continue;
    }
    rows.push_back(ftxui::hbox({
        std::move(row) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, row_width),
        std::move(cells[i]) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1),
    }));
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

int CharacterPanel::HyperRowsShown() const {
  if (max_rows_ <= 0) {
    return kNumHyperStats;
  }
  // At least one row however small the budget: the cursor has to have
  // somewhere to be, and the rule and [Reset] under it never give way.
  return std::max(1, std::min(kNumHyperStats, max_rows_ - kHyperTabFixedRows));
}

int CharacterPanel::FirstHyperRow(int visible) const {
  return ScrollWindowStart(kNumHyperStats, hyper_sel_, visible);
}

ftxui::Element CharacterPanel::RenderHyperRow(HyperStatField field, int index,
                                              bool rows_focused,
                                              int row_width) const {
  bool selected = rows_focused && hyper_sel_ == index;
  // A stat the character's level holds shut is not a stat they have yet, so
  // the whole row dims -- as a locked skill's does. Running out of points
  // dims the [+] alone: that is about the moment, not about the stat.
  bool locked = !HyperStatUnlocked(field, character_.proto().level());
  int level = character_.hyper_stat_level(field, hyper_preset_);

  // Only the name inverts, as on a skill row: Enter opens the stat, so the
  // highlight covers the stat and stops. The padding rides outside it, which
  // is what keeps the bar off the empty column after a short name.
  std::string text = HyperStatName(field);
  int name_width = std::max(1, row_width - kHyperFixedWidth);
  int lit = std::min(static_cast<int>(text.size()), name_width);
  ftxui::Element name = ftxui::text(text.substr(0, lit));
  if (selected && hyper_col_ == kColName) {
    name = std::move(name) | ftxui::inverted;
  } else if (locked) {
    name = std::move(name) | ftxui::dim;
  }
  // Right-aligned with a single gutter after it, so the [+] on every row sits
  // one column off the level however wide the panel came out.
  ftxui::Element level_text =
      ftxui::text(PadLeft(std::to_string(level), kHyperLevelWidth) + " ");
  if (locked) {
    level_text = std::move(level_text) | ftxui::dim;
  }
  // The cursor outranks the unavailable cue, as on the skill rows: a selected
  // [+] inverts even with nothing to spend, so the cursor stays visible while
  // the player reads down the list.
  ftxui::Element plus = ftxui::text("[+]");
  if (selected && hyper_col_ == kColPlus) {
    plus = std::move(plus) | ftxui::inverted;
  } else if (!CanRaiseHyperStat(field)) {
    plus = std::move(plus) | ftxui::dim;
  }
  return ftxui::hbox({
             ftxui::text(" "),
             std::move(name),
             ftxui::text(std::string(name_width - lit, ' ')),
             std::move(level_text),
             std::move(plus),
             ftxui::text(" "),
         }) |
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
  // The way back to nothing spent, and it is free -- so it sits under a rule
  // of its own rather than among the rows it undoes. Both are drawn whatever
  // the budget: a screen with no way off it is worse than a shorter list.
  rows.push_back(PanelSeparator(highlighted_));
  rows.push_back(CenteredCell("[Reset]",
                              reset_focused ? ftxui::inverted : ftxui::nothing,
                              ContentWidth()));
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
  const RowColors colors = RarityColors(line.rank(), line.locked());
  // The lock keeps its two columns on every row, so the values above and below
  // one that has no lock still end in the same place.
  const char* lock = "  ";
  if (AbilityLineLockable(line)) {
    lock = line.locked() ? kLockedGlyph : kUnlockedGlyph;
  }
  ftxui::Element lock_cell = ftxui::text(lock);
  if (rows_focused && ability_sel_ == index) {
    // The cursor inverts what Enter answers, as the [+] on a stat row does --
    // the blank of an unlockable line included, which is still where the
    // cursor is.
    lock_cell = std::move(lock_cell) | ftxui::inverted;
  }
  return ftxui::hbox({
             ftxui::text(" " + AbilityLineName(line.type())),
             ftxui::filler(),
             ftxui::text(AbilityLineValueText(line) + " "),
             std::move(lock_cell),
             ftxui::text(" "),
         }) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, ContentWidth()) |
         ftxui::bgcolor(colors.background) | ftxui::color(colors.text);
}

ftxui::Element CharacterPanel::RenderAbilityTab(bool bar_focused,
                                                bool rows_focused,
                                                bool reroll_focused) const {
  std::vector<ftxui::Element> rows;
  rows.push_back(RenderPresetBar(
      bar_focused, FormatWithCommas(character_.honor()) + " Honor"));
  rows.push_back(PanelSeparator(highlighted_));

  const AbilityPreset& preset = character_.ability(hyper_preset_);
  for (int i = 0; i < preset.lines_size(); ++i) {
    rows.push_back(RenderAbilityRow(preset.lines(i), i, rows_focused));
  }

  // What the reroll below asks, under a rule of its own. Written plain rather
  // than with commas: it is a price to weigh against the pool on the row
  // above, not a total to read off.
  rows.push_back(PanelSeparator(highlighted_));
  const bool affordable = CanRerollAbility();
  ftxui::Element cost = ftxui::text(
      std::to_string(character_.ability_reset_cost(hyper_preset_)) + " ");
  if (!affordable) {
    // Red on the one value the player falls short of, so the greyed button
    // below it does not have to be asked why.
    cost = std::move(cost) | ftxui::color(kRed);
  }
  rows.push_back(ftxui::hbox({
      ftxui::text(" Honor Cost"),
      ftxui::filler(),
      std::move(cost),
  }));
  ftxui::Element reroll = CenteredCell(
      "[Reroll]", reroll_focused ? ftxui::inverted : ftxui::nothing,
      ContentWidth());
  if (!affordable) {
    reroll = std::move(reroll) | ftxui::dim;
  }
  rows.push_back(std::move(reroll));
  return ftxui::vbox(std::move(rows));
}

ftxui::Element CharacterPanel::RenderAdvanceTab(bool content_focused) const {
  std::vector<Job> jobs = JobChoicesForStage(
      character_.proto().job(), character_.proto().job_stage() + 1);
  std::vector<ftxui::Element> rows;
  for (int i = 0; i < static_cast<int>(jobs.size()); ++i) {
    // A caret rather than the [+] the other tabs use: there is nothing to
    // spend here, only one of four things to become.
    std::string cursor = content_focused && job_sel_ == i ? " > " : "   ";
    ftxui::Element row =
        ftxui::text(PadRight(cursor + JobName(jobs[i]), ContentWidth()));
    if (job_sel_ == i) {
      row = std::move(row) | ftxui::reflect(job_cursor_box_);
    }
    rows.push_back(std::move(row));
  }
  return ftxui::vbox(std::move(rows));
}

ftxui::Element CharacterPanel::RenderUsername(bool row_selected) const {
  if (username_field_.editing()) {
    // A caret after what has been typed, so an empty field is still a field
    // rather than a blank row.
    return CenteredCell(username_field_.text() + "_", ftxui::inverted,
                        ContentWidth());
  }
  const std::string& name = character_.username();
  if (name == kDefaultUsername && !row_selected) {
    // Dim while it is still the invitation: it is not a name yet.
    return CenteredCell(name, ftxui::dim, ContentWidth());
  }
  return CenteredCell(name, row_selected ? ftxui::inverted : ftxui::nothing,
                      ContentWidth());
}

ftxui::Element CharacterPanel::Render() const {
  const Character& p = character_.proto();

  // Right-aligned in three columns so the job name doesn't shuffle sideways
  // as the character levels past 9 and 99.
  std::string lvl = PadLeft(std::to_string(p.level()), 3);
  std::string title =
      Centered("Lv" + lvl + " " + ShortJobName(p.job()), ContentWidth());

  std::string power = Centered(
      CombatPowerText(CharacterCombatPower(character_, skills_, hyper_preset_)),
      ContentWidth());

  bool focused = panel_focus_ == kCharPanel;
  Zone zone = EffectiveZone();
  bool tab_row_selected = focused && zone == kZoneTabs;
  bool name_row_selected = focused && zone == kZoneUsername;
  ftxui::Element content;
  if (ActiveTab() == kTabSkills) {
    content = RenderSkillsTab(focused && zone == kZoneAdvTabs,
                              focused && zone == kZoneSkillRows);
  } else if (ActiveTab() == kTabHyper) {
    content = RenderHyperTab(focused && zone == kZonePresets,
                             focused && zone == kZoneHyperRows,
                             focused && zone == kZoneHyperReset);
  } else if (ActiveTab() == kTabAbility) {
    content = RenderAbilityTab(focused && zone == kZonePresets,
                               focused && zone == kZoneAbilityRows,
                               focused && zone == kZoneAbilityReroll);
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
  // A second row of tabs sits straight under the first, the way the shop
  // stacks its two. The rule goes under the pair rather than between them,
  // and the tab that draws the second row draws it.
  if (!ShowsSecondTabRow()) {
    rows.push_back(PanelSeparator(highlighted_));
  }
  rows.push_back(content);
  return AccentWindow(" Character ", ftxui::vbox(std::move(rows)),
                      PanelAccent(highlighted_), focused);
}

bool CharacterPanel::OnUsernameEvent(const ftxui::Event& event) {
  if (username_field_.editing()) {
    TextEntry entry = username_field_.OnEvent(event);
    if (entry == TextEntry::kCommitted) {
      character_.SetUsername(username_field_.text());
      return true;
    }
    // Up and Down leave the old name and then move, the way they would have
    // had the field never been open. Escape only closes it.
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
  if (IsForward(event)) {
    username_field_.BeginEdit();
    return true;
  }
  return false;
}

bool CharacterPanel::OnTabsEvent(const ftxui::Event& event) {
  // Top zone: Left/Right walk the tabs, Up and Down enter the active tab's
  // content.
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
    // Down enters the tab's content at its first row; Up enters it at its
    // last, the bar being a stop in the same ring. Where "first row" is
    // depends on the tab -- the Skills tab's content starts at the
    // advancement bar rather than at a skill.
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  return false;
}

bool CharacterPanel::OnAdvanceTabEvent(const ftxui::Event& event,
                                       const CharacterPanelActions& actions) {
  // Job rows: Up/Down walk them, and off either end is the tab bar.
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

bool CharacterPanel::OnPresetBarEvent(const ftxui::Event& event) {
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    hyper_preset_ = StatPreset::kFarming;
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    hyper_preset_ = StatPreset::kBossing;
    return true;
  }
  return false;
}

bool CharacterPanel::OnStatsTabEvent(const ftxui::Event& event,
                                     const CharacterPanelActions& actions) {
  if (zone_ == kZonePresets) {
    return OnPresetBarEvent(event);
  }
  // Stat rows: Up/Down walk them, and off either end is the tab bar. Left/Right
  // do nothing here -- they belong to the tab bar.
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
  if (character_.proto().ap() > 0) {
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
    return OnPresetBarEvent(event);
  }
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  // Left/Right pick the column, as they do on a skill row. The [Reset] button
  // is one button wide, so it hears neither.
  if (event == ftxui::Event::ArrowLeft && zone_ == kZoneHyperRows) {
    hyper_col_ = kColName;
    return true;
  }
  if (event == ftxui::Event::ArrowRight && zone_ == kZoneHyperRows) {
    hyper_col_ = kColPlus;
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
  if (hyper_col_ == kColName) {
    // Never gated: a stat the character's level holds shut is a stat they
    // most want to read about.
    if (actions.hyper_inspect) {
      actions.hyper_inspect(field);
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
    return OnPresetBarEvent(event);
  }
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (!IsForward(event)) {
    return false;
  }
  if (zone_ == kZoneAbilityReroll) {
    // A pool that cannot pay says so with the red cost and the greyed button.
    // Enter on it does nothing rather than raising a dialog to repeat what is
    // already on the screen.
    if (actions.ability_reroll && CanRerollAbility()) {
      actions.ability_reroll();
    }
    return true;
  }
  // A line with no lock on the row has none to toggle, so Enter passes over
  // it. Whether a third lock is one too many is LockAbilityLine's to answer.
  const AbilityPreset& preset = character_.ability(hyper_preset_);
  if (actions.ability_lock && ability_sel_ < preset.lines_size() &&
      AbilityLineLockable(preset.lines(ability_sel_))) {
    actions.ability_lock(ability_sel_);
  }
  return true;
}

bool CharacterPanel::OnSkillsTabEvent(const ftxui::Event& event,
                                      const CharacterPanelActions& actions) {
  if (zone_ == kZoneAdvTabs) {
    // Advancement bar: Left/Right switch stages; Up returns to the outer tabs
    // and Down descends to the skills, or back to the outer tabs when this
    // stage has none to put the cursor on.
    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
      MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
      return true;
    }
    if (event == ftxui::Event::ArrowLeft) {
      if (skill_tab_ > 0) {
        skill_tab_--;
      }
      return true;
    }
    if (event == ftxui::Event::ArrowRight) {
      if (skill_tab_ < SkillPages() - 1) {
        skill_tab_++;
      }
      return true;
    }
    return false;
  }
  // Skill rows: Up/Down walk them, Up off the top returns to the advancement
  // bar and Down off the bottom carries on round to the outer tab bar.
  // Left/Right pick the column, which is what the Enter below acts on --
  // switching advancement tabs belongs to the bar above.
  std::vector<const Skill*> skills = SkillsForPage(skill_tab_);
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    skill_col_ = kColName;
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    skill_col_ = kColPlus;
    return true;
  }
  if (IsForward(event)) {
    // The stage can have fewer skills than the row the cursor last sat on --
    // switching advancement tabs does not reset it.
    if (skill_sel_ >= static_cast<int>(skills.size())) {
      return true;
    }
    const Skill& skill = *skills[skill_sel_];
    if (skill_col_ == kColName) {
      // Never gated: a maxed skill with no SP behind it still has a
      // description and a level table worth reading.
      if (actions.menu) {
        actions.menu(skill);
      }
      return true;
    }
    bool maxed = character_.skill_level(skill) >= skill.max_level();
    if (actions.learn && !maxed && !SkillLocked(skill) &&
        character_.SpFor(skill) > 0) {
      actions.learn(skill);
    }
    return true;
  }
  return false;
}

ftxui::Component CharacterPanel::MakeComponent(CharacterPanelActions actions) {
  // Renderer(bool) overload is Focusable(), unlike Renderer() -- required so
  // Container::Tab's Focused() check passes when panel_focus_ == kCharPanel.
  ftxui::Component renderer =
      ftxui::Renderer([this](bool /*focused*/) { return Render(); });
  return ftxui::CatchEvent(
      renderer, [this, actions = std::move(actions)](ftxui::Event event) {
        if (panel_focus_ != kCharPanel) {
          return false;
        }
        // The tab bar can have been rewritten since the last key -- taking the
        // advancement does exactly that -- so settle where the cursor is
        // standing before reading the event.
        if (active_tab_ >= static_cast<int>(VisibleTabs().size())) {
          active_tab_ = kTabStats;
        }
        zone_ = EffectiveZone();
        // Route by zone: the shared tab bar, else the active tab's content
        // (only Stats reaches kZoneStatRows, only Skills the skill zones).
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
        if (ActiveTab() == kTabAdvance) {
          return OnAdvanceTabEvent(event, actions);
        }
        return OnSkillsTabEvent(event, actions);
      });
}

}  // namespace ms
