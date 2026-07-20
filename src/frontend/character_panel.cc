#include "src/frontend/character_panel.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/colors.h"
#include "src/frontend/panel_util.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

constexpr int kContentWidth = 33;  // chars inside the window border

enum Tab : int { kTabStats = 0, kTabSkills = 1 };

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

// A non-allocatable display row: two-space cursor gutter, then "label: value".
ftxui::Element DisplayRow(const std::string& label, int value) {
  return ftxui::text(
      PadRight("  " + label + ": " + std::to_string(value), kContentWidth));
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
                               int& panel_focus)
    : character_(character), panel_focus_(panel_focus) {
}

ftxui::Element CharacterPanel::AllocRow(const std::string& label, int base,
                                        int bonus, int index,
                                        bool content_active) const {
  bool selected = content_active && stat_sel_ == index;
  std::string text = (selected ? "> " : "  ") + StatText(label, base, bonus);
  if (!selected) {
    return ftxui::text(PadRight(text, kContentWidth));
  }
  bool has_ap = character_.proto().ap() > 0;
  ftxui::Element plus = ftxui::text("[+]");
  ftxui::Element max = ftxui::text("[Max]");
  if (!has_ap) {
    plus = plus | ftxui::dim;
    max = max | ftxui::dim;
  } else if (button_sel_ == 0) {
    plus = plus | ftxui::inverted;
  } else {
    max = max | ftxui::inverted;
  }
  return ftxui::hbox({
      ftxui::text(text),
      ftxui::filler(),
      plus,
      ftxui::text("  "),
      max,
      ftxui::text(" "),
  });
}

ftxui::Element CharacterPanel::RenderTabBar() const {
  // Left-aligned chip row in the shared tab style: theme-colored labels, the
  // active one inverted for a blue highlight. Matches the inventory tabs.
  const char* labels[2] = {"Stats", "Skills"};
  std::vector<ftxui::Element> chips;
  for (int i = 0; i < 2; ++i) {
    ftxui::Element chip =
        ftxui::text(std::string(" ") + labels[i] + " ") | ftxui::color(kTheme);
    if (i == active_tab_) {
      chip = chip | ftxui::inverted;
    }
    chips.push_back(std::move(chip));
  }
  chips.push_back(ftxui::filler());
  return ftxui::hbox(std::move(chips));
}

ftxui::Element CharacterPanel::RenderStatsTab(bool focused) const {
  const Character& p = character_.proto();
  const AllocatedStats& a = p.allocated_stats();
  const EquipStats& e = character_.equip_stats();
  // The cursor is in the content (rather than on the tab bar) only when the
  // panel is focused and Down was pressed off the tab bar.
  bool content_active = focused && !on_tab_bar_;

  std::vector<ftxui::Element> rows;
  rows.push_back(DisplayRow("HP", a.hp() + e.max_hp()));
  rows.push_back(DisplayRow("MP", a.mp()));
  for (int i = 0; i < kNumAllocStats; ++i) {
    std::pair<int, int> v = AllocStatValues(kAllocStats[i].field, a, e);
    rows.push_back(
        AllocRow(kAllocStats[i].label, v.first, v.second, i, content_active));
  }
  rows.push_back(ThemedSeparator());
  rows.push_back(DisplayRow("ATT", e.attack()));
  rows.push_back(DisplayRow("MATT", e.magic_attack()));
  return ftxui::vbox(std::move(rows));
}

ftxui::Element CharacterPanel::RenderSkillsTab() const {
  const Character& p = character_.proto();
  std::string sp = "SP: " + std::to_string(character_.sp(p.job_stage())) + " ";
  return ftxui::vbox({
      ftxui::hbox({ftxui::filler(), ftxui::text(sp)}),
      ThemedSeparator(),
      ftxui::text(PadRight(" No skills yet.", kContentWidth)),
  });
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
  ftxui::Element content =
      active_tab_ == kTabSkills ? RenderSkillsTab() : RenderStatsTab(focused);

  return ThemedWindow(" Character ",
                      ftxui::vbox({
                          ftxui::text(title),
                          ThemedSeparator(),
                          RenderTabBar(),
                          ThemedSeparator(),
                          content,
                      }),
                      focused);
}

ftxui::Component CharacterPanel::MakeComponent(
    std::function<void(StatField, bool)> on_allocate) {
  // Renderer(bool) overload is Focusable(), unlike Renderer() -- required so
  // Container::Tab's Focused() check passes when panel_focus_ == kCharPanel.
  ftxui::Component renderer =
      ftxui::Renderer([this](bool /*focused*/) { return Render(); });
  return ftxui::CatchEvent(renderer, [this, on_allocate](ftxui::Event event) {
    if (panel_focus_ != kCharPanel) {
      return false;
    }
    if (on_tab_bar_) {
      if (event == ftxui::Event::ArrowLeft) {
        active_tab_ = kTabStats;
        return true;
      }
      if (event == ftxui::Event::ArrowRight) {
        active_tab_ = kTabSkills;
        return true;
      }
      // The Skills tab has no content cursor yet, so Down only enters Stats.
      if (event == ftxui::Event::ArrowDown && active_tab_ == kTabStats) {
        on_tab_bar_ = false;
        stat_sel_ = 0;
        button_sel_ = 0;
        return true;
      }
      return false;
    }
    // Cursor is in the Stats content, on one of the allocatable rows.
    if (event == ftxui::Event::ArrowUp) {
      if (stat_sel_ == 0) {
        on_tab_bar_ = true;
      } else {
        stat_sel_--;
        button_sel_ = 0;
      }
      return true;
    }
    if (event == ftxui::Event::ArrowDown) {
      if (stat_sel_ < kNumAllocStats - 1) {
        stat_sel_++;
        button_sel_ = 0;
      }
      return true;
    }
    if (event == ftxui::Event::ArrowLeft) {
      button_sel_ = 0;
      return true;
    }
    if (event == ftxui::Event::ArrowRight) {
      button_sel_ = 1;
      return true;
    }
    if (IsForward(event) && character_.proto().ap() > 0) {
      on_allocate(kAllocStats[stat_sel_].field, button_sel_ == 1);
      return true;
    }
    return false;
  });
}

}  // namespace ms
