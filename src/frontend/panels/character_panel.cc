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
#include "src/character/progression.h"
#include "src/combat/damage.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/marquee.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/frontend/widgets/stat_rows.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

constexpr int kContentWidth = 33;  // chars inside the window border

// One line of the panel's heading block, centred over the content width.
std::string Centered(const std::string& s) {
  int pad = std::max(0, (kContentWidth - static_cast<int>(s.size())) / 2);
  return PadRight(std::string(pad, ' ') + s, kContentWidth);
}

// The outer tab labels, indexed by CharacterPanel::Tab.
const char* kTabLabels[] = {"Stats", "Skills", "Advance"};

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

// The extra stats read as a label column and a number column hard against the
// right edge, one gutter shy of the border. The main stats above them keep
// their own "LABEL: value" shape, since their [+] and their AP counter already
// own that edge.

// The value's RIGHT edge is fixed and the gap before it gives way. Padding the
// label to a fixed width instead leaves a long value nothing to take from, and
// Defense -- the one stat written "(base+bonus) total" -- then runs a column
// past every other value in the panel.
ftxui::Element StatRow(const std::string& label, const std::string& value) {
  int gap = kContentWidth - 2 - static_cast<int>(value.size());
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
// exactly kContentWidth and a long name slides inside its column instead of
// pushing the panel wider -- which is what a name like "Final Attack:
// Crossbow" used to do to the whole Character panel.
//
//   " " + tag(4) + name + level + filler + "[+]" + " "
// " 20/20 " -- the widest a level reads, with the gutter either side that the
// row has no filler left to provide.
constexpr int kSkillLevelWidth = 7;
constexpr int kSkillPlusWidth = 3;
constexpr int kSkillNameWidth =
    kContentWidth - 1 - kSkillTagWidth - kSkillLevelWidth - kSkillPlusWidth - 1;

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

CharacterPanel::CharacterPanel(CharacterInstance& character, int& panel_focus,
                               std::map<std::string, Skill> skills)
    : character_(character),
      skills_(std::move(skills)),
      panel_focus_(panel_focus) {
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
  return ftxui::hbox({
      ftxui::text(" " + StatText(label, base, bonus)),
      ftxui::filler(),
      plus,
      ftxui::text(" "),
  });
}

std::vector<CharacterPanel::Tab> CharacterPanel::VisibleTabs() const {
  std::vector<Tab> tabs = {kTabStats};
  // Skills belong to a job, so the tab waits for one: a Beginner standing at
  // level 10 is being offered an advancement, not a skill list.
  if (Unlocked(Feature::kSkills, character_)) {
    tabs.push_back(kTabSkills);
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
    case kZoneTabs:
      return kZoneTabs;
    case kZoneStatRows:
      return ActiveTab() == kTabStats ? zone_ : kZoneTabs;
    case kZoneAdvTabs:
    case kZoneSkillRows:
      return ActiveTab() == kTabSkills ? zone_ : kZoneTabs;
    case kZoneJobRows:
      return ActiveTab() == kTabAdvance ? zone_ : kZoneTabs;
  }
  return kZoneTabs;
}

int CharacterPanel::RingStops() const {
  if (ActiveTab() == kTabStats) {
    // The tab bar, the four AP stats, and the View All Stats row under them --
    // which is not there while there are no combat stats for it to lead to.
    return 1 + kNumAllocStats + (ShowsCombatStats() ? 1 : 0);
  }
  if (ActiveTab() == kTabAdvance) {
    return 1 + static_cast<int>(
                   JobChoicesForStage(character_.proto().job(),
                                      character_.proto().job_stage() + 1)
                       .size());
  }
  if (character_.proto().job_stage() == 0) {
    // A Beginner's Skills tab has no advancement bar to stand on, let alone
    // skills under it. One stop, and the arrows have nowhere to take anybody.
    return 1;
  }
  return 2 + static_cast<int>(SkillsForStage(skill_tab_ + 1).size());
}

int CharacterPanel::CursorStop() const {
  switch (EffectiveZone()) {
    case kZoneTabs:
      return 0;
    case kZoneStatRows:
      return stat_sel_ + 1;
    case kZoneJobRows:
      return job_sel_ + 1;
    case kZoneAdvTabs:
      return 1;
    case kZoneSkillRows:
      return skill_sel_ + 2;
  }
  return 0;
}

void CharacterPanel::SetCursorStop(int stop) {
  if (stop == 0) {
    zone_ = kZoneTabs;
    return;
  }
  if (ActiveTab() == kTabStats) {
    zone_ = kZoneStatRows;
    stat_sel_ = stop - 1;
    return;
  }
  if (ActiveTab() == kTabAdvance) {
    zone_ = kZoneJobRows;
    job_sel_ = stop - 1;
    return;
  }
  if (stop == 1) {
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
  if (skill_sel_ != stop - 2) {
    RestartNameScroll();
  }
  skill_sel_ = stop - 2;
}

void CharacterPanel::RestartNameScroll() {
  skill_selected_at_ = std::chrono::steady_clock::now();
}

void CharacterPanel::MoveCursor(int delta) {
  // The advancement bar keeps whichever page the player left it on. Entering
  // the Skills tab used to snap it to the newest stage, which meant a
  // second-job character could not get back down onto their first book: every
  // Down from the tab bar put them on page II again.
  SetCursorStop(StepCursor(CursorStop(), delta, RingStops()));
}

std::string CharacterPanel::TabKey(Tab tab) const {
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
    character_.MarkTabSeen(key);
  }
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
    specs.push_back(
        {kTabLabels[tabs[i]], !key.empty() && !character_.TabSeen(key)});
  }
  // The panel is only kContentWidth wide and the tabs are already most of it,
  // so this is the bar most likely to need the scroll.
  return ftxui::hbox(
      {TabBar(specs, active, row_selected, kContentWidth), ftxui::filler()});
}

// The MP display row with the character's unspent AP right-aligned, so the
// player can see how much there is to spend on the [+] rows below.
ftxui::Element CharacterPanel::MpRow(int mp, int ap) const {
  return ftxui::hbox({
      ftxui::text(" MP: " + std::to_string(mp)),
      ftxui::filler(),
      ftxui::text(std::to_string(ap) + " AP "),
  });
}

int CharacterPanel::ExtraStatsShown(int total) const {
  if (max_rows_ <= 0) {
    return total;
  }
  // The View All Stats row is paid for first, because it is where the stats
  // that did not fit have gone. A budget too small even for that still shows
  // it and lets the panel be clipped: a panel with no way out of it is worse.
  return std::max(0, std::min(total, max_rows_ - kStatsTabFixedRows - 1));
}

ftxui::Element CharacterPanel::RenderStatsTab(bool content_focused) const {
  const Character& p = character_.proto();
  const AllocatedStats& a = p.allocated_stats();

  // HP, MP and DEF carry passive-skill bonuses on top of the allocated and worn
  // values, so they come from the derived totals rather than a bare sum. The
  // stat rows do too: a skill's LUK belongs in the same column as a ring's.
  DerivedStats derived = DerivedStatsFor(character_, skills_);
  const EquipStats e = TotalEquipStats(character_, derived);

  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(
      PadRight(" HP: " + std::to_string(derived.max_hp), kContentWidth)));
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
  rows.push_back(PanelSeparator(highlighted_));
  std::vector<StatLine> extras = PanelExtraStatLines(character_, skills_);
  int shown = ExtraStatsShown(static_cast<int>(extras.size()));
  for (int i = 0; i < shown; ++i) {
    rows.push_back(StatRow(extras[i].label, extras[i].value));
  }
  // Closes the block, because whatever did not fit above it is on the screen
  // it opens -- and the cursor reaches it by walking off the foot of the
  // stats. It is the last row to go, not the first.
  //
  // Centred by hand and not with CenteredRow: every row of this panel is
  // padded to kContentWidth, and a flexed row would centre itself in whatever
  // width the window came out at instead. Only the label inverts, so the
  // padding either side of it cannot be part of the same text.
  const std::string kViewAll = "View All Stats";
  int pad = (kContentWidth - static_cast<int>(kViewAll.size())) / 2;
  ftxui::Element view_all = ftxui::text(kViewAll);
  if (content_focused && stat_sel_ == kNumAllocStats) {
    view_all = view_all | ftxui::inverted;
  }
  rows.push_back(ftxui::hbox({
      ftxui::text(std::string(pad, ' ')),
      std::move(view_all),
      ftxui::text(std::string(
          kContentWidth - pad - static_cast<int>(kViewAll.size()), ' ')),
  }));
  return ftxui::vbox(std::move(rows));
}

ftxui::Element CharacterPanel::RenderAdvTabBar(int stages,
                                               bool bar_focused) const {
  // One chip per unlocked stage, in the shared tab style. The selected stage's
  // SP is right-aligned on the same row, reading like the AP counter.
  std::vector<TabSpec> specs;
  for (int stage = 1; stage <= stages; ++stage) {
    specs.push_back({kStageNumerals[stage]});
  }
  // The SP counter shares the row, so the bar gets what is left of it.
  std::vector<ftxui::Element> row;
  row.push_back(TabBar(specs, skill_tab_, bar_focused, kContentWidth - kSpCol));
  row.push_back(ftxui::filler());
  row.push_back(
      ftxui::text(std::to_string(character_.sp(skill_tab_ + 1)) + " SP "));
  return ftxui::hbox(std::move(row));
}

std::vector<const Skill*> CharacterPanel::SkillsForStage(int stage) const {
  // A stage's page shows exactly the skills of the advancement this character's
  // job is at there -- so a Swordman never sees an Archer's skills, and vice
  // versa. An unreached or undefined advancement has no skills.
  return SkillsForAdvancement(
      skills_, AdvancementForJobStage(character_.proto().job(), stage));
}

ftxui::Element CharacterPanel::RenderSkillRow(const Skill& skill, int index,
                                              bool rows_focused) const {
  // What the player bought, not what Combat Orders granted on top of it: this
  // column stands beside the [+] and is the ledger for the SP spent. The
  // granted level is real in the stats and in the fight, and Combat Orders'
  // own page is where the player reads what it hands out.
  int level = character_.skill_level(skill);
  bool selected = rows_focused && skill_sel_ == index;
  bool maxed = level >= skill.max_level();
  bool has_sp = character_.sp(StageForAdvancement(skill.job_advancement())) > 0;
  // A skill still waiting on another one is not a skill this character has
  // yet, so the whole row dims -- name included. Running out of SP dims the
  // [+] alone, because that is a thing about the moment rather than about the
  // skill.
  bool locked = !character_.MeetsSkillRequirement(skill);

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
  std::string window = ScrollingWindow(
      skill.name(), kSkillNameWidth,
      selected ? std::chrono::steady_clock::now() - skill_selected_at_
               : std::chrono::steady_clock::duration::zero());
  // The padding a short name is given rides outside the cursor, so the
  // highlight still covers the name and stops -- a fixed-width bar would say
  // the empty column after a short name was part of what Enter opens.
  int lit = std::min(static_cast<int>(skill.name().size()), kSkillNameWidth);
  ftxui::Element name = ftxui::text(window.substr(0, lit));
  if (selected && skill_col_ == kColName) {
    name = name | ftxui::inverted;
  } else if (locked) {
    name = name | ftxui::dim;
  }
  ftxui::Element name_pad = ftxui::text(window.substr(lit));
  ftxui::Element level_text = ftxui::text(PadRight(
      " " + std::to_string(level) + "/" + std::to_string(skill.max_level()),
      kSkillLevelWidth));
  if (locked) {
    level_text = level_text | ftxui::dim;
  }
  ftxui::Element plus = ftxui::text("[+]");
  if (selected && skill_col_ == kColPlus) {
    plus = plus | ftxui::inverted;
  } else if (maxed || !has_sp || locked) {
    plus = plus | ftxui::dim;
  }
  return ftxui::hbox({
      ftxui::text(" "),
      tag_text,
      name,
      name_pad,
      level_text,
      ftxui::filler(),
      plus,
      ftxui::text(" "),
  });
}

ftxui::Element CharacterPanel::RenderSkillsTab(bool bar_focused,
                                               bool rows_focused) const {
  int stages = character_.proto().job_stage();
  if (stages == 0) {
    return ftxui::text(PadRight(" No advancements yet.", kContentWidth)) |
           ftxui::dim;
  }
  std::vector<ftxui::Element> rows;
  rows.push_back(RenderAdvTabBar(stages, bar_focused));
  rows.push_back(PanelSeparator(highlighted_));
  std::vector<const Skill*> skills = SkillsForStage(skill_tab_ + 1);
  if (skills.empty()) {
    rows.push_back(ftxui::text(PadRight(" No skills yet.", kContentWidth)) |
                   ftxui::dim);
  } else {
    for (int i = 0; i < static_cast<int>(skills.size()); ++i) {
      rows.push_back(RenderSkillRow(*skills[i], i, rows_focused));
    }
  }
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
        ftxui::text(PadRight(cursor + JobName(jobs[i]), kContentWidth));
    if (job_sel_ == i) {
      row = std::move(row) | ftxui::reflect(job_cursor_box_);
    }
    rows.push_back(std::move(row));
  }
  return ftxui::vbox(std::move(rows));
}

ftxui::Element CharacterPanel::Render() const {
  const Character& p = character_.proto();

  // Right-aligned in three columns so the job name doesn't shuffle sideways
  // as the character levels past 9 and 99.
  std::string lvl = PadLeft(std::to_string(p.level()), 3);
  std::string title = Centered("Lv" + lvl + " " + ShortJobName(p.job()));

  std::string power =
      Centered(CombatPowerText(CharacterCombatPower(character_, skills_)));

  bool focused = panel_focus_ == kCharPanel;
  Zone zone = EffectiveZone();
  bool tab_row_selected = focused && zone == kZoneTabs;
  ftxui::Element content;
  if (ActiveTab() == kTabSkills) {
    content = RenderSkillsTab(focused && zone == kZoneAdvTabs,
                              focused && zone == kZoneSkillRows);
  } else if (ActiveTab() == kTabAdvance) {
    content = RenderAdvanceTab(focused && zone == kZoneJobRows);
  } else {
    content = RenderStatsTab(focused && zone == kZoneStatRows);
  }

  return AccentWindow(" Character ",
                      ftxui::vbox({
                          ftxui::text(title),
                          ftxui::text(power),
                          PanelSeparator(highlighted_),
                          RenderTabBar(tab_row_selected),
                          PanelSeparator(highlighted_),
                          content,
                      }),
                      PanelAccent(highlighted_), focused);
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

bool CharacterPanel::OnAdvanceTabEvent(
    const ftxui::Event& event, const std::function<void(Job)>& on_advance) {
  // Job rows: Up/Down walk them, and off either end is the tab bar.
  std::vector<Job> jobs = JobChoicesForStage(
      character_.proto().job(), character_.proto().job_stage() + 1);
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (IsForward(event)) {
    if (on_advance && job_sel_ < static_cast<int>(jobs.size())) {
      on_advance(jobs[job_sel_]);
    }
    return true;
  }
  return false;
}

bool CharacterPanel::OnStatsTabEvent(
    const ftxui::Event& event,
    const std::function<void(StatField)>& on_allocate,
    const std::function<void()>& on_all_stats) {
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
    if (on_all_stats) {
      on_all_stats();
    }
    return true;
  }
  if (character_.proto().ap() > 0) {
    on_allocate(kAllocStats[stat_sel_].field);
    return true;
  }
  return false;
}

bool CharacterPanel::OnViewAllStatsRow() const {
  return ShowsCombatStats() && EffectiveZone() == kZoneStatRows &&
         stat_sel_ == kNumAllocStats;
}

bool CharacterPanel::ShowsCombatStats() const {
  return Unlocked(Feature::kCombatStats, character_);
}

bool CharacterPanel::OnSkillsTabEvent(
    const ftxui::Event& event,
    const std::function<void(const Skill&)>& on_learn,
    const std::function<void(const Skill&)>& on_inspect) {
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
        RestartNameScroll();  // the same row on another page is another skill
      }
      return true;
    }
    if (event == ftxui::Event::ArrowRight) {
      if (skill_tab_ < character_.proto().job_stage() - 1) {
        skill_tab_++;
        RestartNameScroll();
      }
      return true;
    }
    return false;
  }
  // Skill rows: Up/Down walk them, Up off the top returns to the advancement
  // bar and Down off the bottom carries on round to the outer tab bar.
  // Left/Right pick the column, which is what the Enter below acts on --
  // switching advancement tabs belongs to the bar above.
  std::vector<const Skill*> skills = SkillsForStage(skill_tab_ + 1);
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
      if (on_inspect) {
        on_inspect(skill);
      }
      return true;
    }
    bool maxed = character_.skill_level(skill) >= skill.max_level();
    int stage = StageForAdvancement(skill.job_advancement());
    if (on_learn && !maxed && character_.sp(stage) > 0) {
      on_learn(skill);
    }
    return true;
  }
  return false;
}

ftxui::Component CharacterPanel::MakeComponent(
    std::function<void(StatField)> on_allocate,
    std::function<void(const Skill&)> on_learn,
    std::function<void(Job)> on_advance,
    std::function<void(const Skill&)> on_inspect,
    std::function<void()> on_all_stats) {
  // Renderer(bool) overload is Focusable(), unlike Renderer() -- required so
  // Container::Tab's Focused() check passes when panel_focus_ == kCharPanel.
  ftxui::Component renderer =
      ftxui::Renderer([this](bool /*focused*/) { return Render(); });
  return ftxui::CatchEvent(
      renderer, [this, on_allocate, on_learn, on_advance, on_inspect,
                 on_all_stats](ftxui::Event event) {
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
        if (zone_ == kZoneTabs) {
          return OnTabsEvent(event);
        }
        if (ActiveTab() == kTabStats) {
          return OnStatsTabEvent(event, on_allocate, on_all_stats);
        }
        if (ActiveTab() == kTabAdvance) {
          return OnAdvanceTabEvent(event, on_advance);
        }
        return OnSkillsTabEvent(event, on_learn, on_inspect);
      });
}

}  // namespace ms
