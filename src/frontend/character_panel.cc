#include "src/frontend/character_panel.h"

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character_stats.h"
#include "src/frontend/colors.h"
#include "src/frontend/panel_util.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

constexpr int kContentWidth = 33;  // chars inside the window border

enum Tab : int { kTabStats = 0, kTabSkills = 1 };

// Roman numerals for the job-advancement tabs, indexed by stage (1..6).
const char* kStageNumerals[] = {"", "I", "II", "III", "IV", "V", "VI"};

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

// "STR: 13" with an optional " (base+bonus)" suffix when gear contributes.
std::string StatText(const std::string& label, int base, int bonus) {
  std::string s = label + ": " + std::to_string(base + bonus);
  if (bonus > 0) {
    s += " (" + std::to_string(base) + "+" + std::to_string(bonus) + ")";
  }
  return s;
}

// A non-allocatable display row: one-space border gutter, then "label: value".
ftxui::Element DisplayRow(const std::string& label, int value) {
  return ftxui::text(
      PadRight(" " + label + ": " + std::to_string(value), kContentWidth));
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

CharacterPanel::CharacterPanel(const CharacterInstance& character,
                               int& panel_focus,
                               std::map<std::string, Skill> skills)
    : character_(character),
      skills_(std::move(skills)),
      panel_focus_(panel_focus) {
}

ftxui::Element CharacterPanel::AllocRow(const std::string& label, int base,
                                        int bonus, int index,
                                        bool content_focused) const {
  bool selected = content_focused && stat_sel_ == index;
  std::string text = " " + StatText(label, base, bonus);
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
      ftxui::text(text),
      ftxui::filler(),
      plus,
      ftxui::text(" "),
  });
}

ftxui::Element CharacterPanel::RenderTabBar(bool row_selected) const {
  // Left-aligned chip row in the shared tab style: theme-colored labels, the
  // active one highlighted. When the tab bar holds focus the active chip goes
  // white to mark the selected row; otherwise it keeps the theme-blue invert.
  const char* labels[2] = {"Stats", "Skills"};
  std::vector<ftxui::Element> chips;
  for (int i = 0; i < 2; ++i) {
    ftxui::Element chip =
        ftxui::text(std::string(" ") + labels[i] + " ") | ftxui::color(kTheme);
    if (i == active_tab_ && row_selected) {
      chip = ftxui::text(std::string(" ") + labels[i] + " ") |
             ftxui::color(ftxui::Color::Black) |
             ftxui::bgcolor(ftxui::Color::White);
    } else if (i == active_tab_) {
      chip = chip | ftxui::inverted;
    }
    chips.push_back(std::move(chip));
  }
  chips.push_back(ftxui::filler());
  return ftxui::hbox(std::move(chips));
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

ftxui::Element CharacterPanel::RenderStatsTab(bool content_focused) const {
  const Character& p = character_.proto();
  const AllocatedStats& a = p.allocated_stats();
  const EquipStats& e = character_.equip_stats();

  // HP and DEF carry passive-skill bonuses on top of the allocated and worn
  // values, so they come from the derived totals rather than a bare sum.
  DerivedStats derived = DerivedStatsFor(character_, skills_);

  std::vector<ftxui::Element> rows;
  rows.push_back(DisplayRow("HP", derived.max_hp));
  rows.push_back(MpRow(a.mp(), p.ap()));
  for (int i = 0; i < kNumAllocStats; ++i) {
    std::pair<int, int> v = AllocStatValues(kAllocStats[i].field, a, e);
    rows.push_back(
        AllocRow(kAllocStats[i].label, v.first, v.second, i, content_focused));
  }
  rows.push_back(ThemedSeparator());
  rows.push_back(DisplayRow("ATT", e.attack()));
  rows.push_back(DisplayRow("MATT", e.magic_attack()));
  rows.push_back(DisplayRow("DEF", derived.def));
  return ftxui::vbox(std::move(rows));
}

ftxui::Element CharacterPanel::RenderAdvTabBar(int stages,
                                               bool bar_focused) const {
  // One chip per unlocked stage, in the shared tab style; the selected chip is
  // white while the bar holds focus, else the theme-blue invert. "SP: N" for
  // the selected stage is right-aligned on the same row.
  std::vector<ftxui::Element> row;
  for (int stage = 1; stage <= stages; ++stage) {
    std::string label = std::string(" ") + kStageNumerals[stage] + " ";
    ftxui::Element chip = ftxui::text(label) | ftxui::color(kTheme);
    if (stage - 1 == skill_tab_ && bar_focused) {
      chip = ftxui::text(label) | ftxui::color(ftxui::Color::Black) |
             ftxui::bgcolor(ftxui::Color::White);
    } else if (stage - 1 == skill_tab_) {
      chip = chip | ftxui::inverted;
    }
    row.push_back(std::move(chip));
  }
  row.push_back(ftxui::filler());
  row.push_back(ftxui::text(
      "SP: " + std::to_string(character_.sp(skill_tab_ + 1)) + " "));
  return ftxui::hbox(std::move(row));
}

std::vector<const Skill*> CharacterPanel::SkillsForStage(int stage) const {
  std::vector<const Skill*> result;
  for (const std::pair<const std::string, Skill>& entry : skills_) {
    if (entry.second.stage() == stage) {
      result.push_back(&entry.second);
    }
  }
  return result;
}

ftxui::Element CharacterPanel::RenderSkillRow(const Skill& skill, int index,
                                              bool rows_focused) const {
  int level = character_.skill_level(skill);
  std::string label = " " + skill.name() + "  " + std::to_string(level) + "/" +
                      std::to_string(skill.max_level());
  bool selected = rows_focused && skill_sel_ == index;
  bool maxed = level >= skill.max_level();
  bool has_sp = character_.sp(skill.stage()) > 0;
  ftxui::Element plus = ftxui::text("[+]");
  if (selected) {
    plus = plus | ftxui::inverted;
  } else if (maxed || !has_sp) {
    plus = plus | ftxui::dim;
  }
  return ftxui::hbox({
      ftxui::text(label),
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
  rows.push_back(ThemedSeparator());
  std::vector<const Skill*> skills = SkillsForStage(skill_tab_ + 1);
  if (skills.empty()) {
    rows.push_back(ftxui::text(PadRight(" No skills yet.", kContentWidth)) |
                   ftxui::dim);
  } else {
    for (int i = 0; i < (int)skills.size(); ++i) {
      rows.push_back(RenderSkillRow(*skills[i], i, rows_focused));
    }
  }
  return ftxui::vbox(std::move(rows));
}

ftxui::Element CharacterPanel::Render() const {
  const Character& p = character_.proto();

  std::string lvl = std::to_string(p.level());
  while ((int)lvl.size() < 3) {
    lvl = " " + lvl;
  }
  std::string raw_title = "Lv" + lvl + " " + JobName(p.job());
  int pad = std::max(0, (kContentWidth - (int)raw_title.size()) / 2);
  std::string title =
      PadRight(std::string(pad, ' ') + raw_title, kContentWidth);

  bool focused = panel_focus_ == kCharPanel;
  bool tab_row_selected = focused && zone_ == kZoneTabs;
  ftxui::Element content =
      active_tab_ == kTabSkills
          ? RenderSkillsTab(focused && zone_ == kZoneAdvTabs,
                            focused && zone_ == kZoneSkillRows)
          : RenderStatsTab(focused && zone_ == kZoneStatRows);

  return ThemedWindow(" Character ",
                      ftxui::vbox({
                          ftxui::text(title),
                          ThemedSeparator(),
                          RenderTabBar(tab_row_selected),
                          ThemedSeparator(),
                          content,
                      }),
                      focused);
}

bool CharacterPanel::OnTabsEvent(const ftxui::Event& event) {
  // Top zone: Left/Right switch tabs, Down enters the active tab's content.
  if (event == ftxui::Event::ArrowLeft) {
    active_tab_ = kTabStats;
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    active_tab_ = kTabSkills;
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    if (active_tab_ == kTabStats) {
      zone_ = kZoneStatRows;
      stat_sel_ = 0;
    } else if (character_.proto().job_stage() > 0) {
      // Skills content starts at the advancement bar, on the current stage.
      zone_ = kZoneAdvTabs;
      skill_tab_ = character_.proto().job_stage() - 1;
    }
    return true;
  }
  return false;
}

bool CharacterPanel::OnStatsTabEvent(
    const ftxui::Event& event,
    const std::function<void(StatField)>& on_allocate) {
  // Stat rows: Up/Down walk them; Up off STR returns to the tab bar. Left/Right
  // do nothing here -- they belong to the tab bar.
  if (event == ftxui::Event::ArrowUp) {
    if (stat_sel_ == 0) {
      zone_ = kZoneTabs;
    } else {
      stat_sel_--;
    }
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    if (stat_sel_ < kNumAllocStats - 1) {
      stat_sel_++;
    }
    return true;
  }
  if (IsForward(event) && character_.proto().ap() > 0) {
    on_allocate(kAllocStats[stat_sel_].field);
    return true;
  }
  return false;
}

bool CharacterPanel::OnSkillsTabEvent(
    const ftxui::Event& event,
    const std::function<void(const Skill&)>& on_learn) {
  if (zone_ == kZoneAdvTabs) {
    // Advancement bar: Left/Right switch tabs, Up returns to the outer tabs.
    if (event == ftxui::Event::ArrowUp) {
      zone_ = kZoneTabs;
      return true;
    }
    if (event == ftxui::Event::ArrowLeft) {
      if (skill_tab_ > 0) {
        skill_tab_--;
      }
      return true;
    }
    if (event == ftxui::Event::ArrowRight) {
      if (skill_tab_ < character_.proto().job_stage() - 1) {
        skill_tab_++;
      }
      return true;
    }
    if (event == ftxui::Event::ArrowDown) {
      // Descend only when this stage has a skill to put the cursor on. Whether
      // there is SP to spend doesn't matter -- the rows are worth reading
      // either way.
      int stage = skill_tab_ + 1;
      if (!SkillsForStage(stage).empty()) {
        zone_ = kZoneSkillRows;
        skill_sel_ = 0;
      }
      return true;
    }
    return false;
  }
  // Skill rows: Up/Down walk them, Up off the top returns to the advancement
  // bar. Left/Right do nothing -- they belong to that bar.
  std::vector<const Skill*> skills = SkillsForStage(skill_tab_ + 1);
  if (event == ftxui::Event::ArrowUp) {
    if (skill_sel_ == 0) {
      zone_ = kZoneAdvTabs;
    } else {
      skill_sel_--;
    }
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    if (skill_sel_ < (int)skills.size() - 1) {
      skill_sel_++;
    }
    return true;
  }
  if (IsForward(event)) {
    const Skill& skill = *skills[skill_sel_];
    bool maxed = character_.skill_level(skill) >= skill.max_level();
    if (on_learn && !maxed && character_.sp(skill.stage()) > 0) {
      on_learn(skill);
    }
    return true;
  }
  return false;
}

ftxui::Component CharacterPanel::MakeComponent(
    std::function<void(StatField)> on_allocate,
    std::function<void(const Skill&)> on_learn) {
  // Renderer(bool) overload is Focusable(), unlike Renderer() -- required so
  // Container::Tab's Focused() check passes when panel_focus_ == kCharPanel.
  ftxui::Component renderer =
      ftxui::Renderer([this](bool /*focused*/) { return Render(); });
  return ftxui::CatchEvent(renderer,
                           [this, on_allocate, on_learn](ftxui::Event event) {
                             if (panel_focus_ != kCharPanel) {
                               return false;
                             }
                             // Route by zone: the shared tab bar, else the
                             // active tab's content (only Stats reaches
                             // kZoneStatRows, only Skills the skill zones).
                             if (zone_ == kZoneTabs) {
                               return OnTabsEvent(event);
                             }
                             if (active_tab_ == kTabStats) {
                               return OnStatsTabEvent(event, on_allocate);
                             }
                             return OnSkillsTabEvent(event, on_learn);
                           });
}

}  // namespace ms
