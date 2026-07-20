#include "src/frontend/character_panel.h"

#include <algorithm>
#include <functional>
#include <string>
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

}  // namespace

CharacterPanel::CharacterPanel(const CharacterInstance& character,
                               int& panel_focus)
    : character_(character), panel_focus_(panel_focus) {
}

std::string CharacterPanel::StatRow(const std::string& label, int base,
                                    int bonus) {
  std::string s = " " + label + ": " + std::to_string(base + bonus);
  if (bonus > 0) {
    s += " (" + std::to_string(base) + "+" + std::to_string(bonus) + ")";
  }
  return PadRight(s, kContentWidth);
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

ftxui::Element CharacterPanel::RenderStatsTab() const {
  const Character& p = character_.proto();
  const AllocatedStats& a = p.allocated_stats();
  const EquipStats& e = character_.equip_stats();
  return ftxui::vbox({
      ftxui::text(PadRight(" HP: " + std::to_string(a.hp() + e.max_hp()),
                           kContentWidth)),
      ftxui::text(PadRight(" MP: " + std::to_string(a.mp()), kContentWidth)),
      ftxui::text(StatRow("STR", a.str(), e.str())),
      ftxui::text(StatRow("DEX", a.dex(), e.dex())),
      ftxui::text(StatRow("INT", a.int_(), e.int_())),
      ftxui::text(StatRow("LUK", a.luk(), e.luk())),
      ThemedSeparator(),
      ftxui::text(
          PadRight(" ATT: " + std::to_string(e.attack()), kContentWidth)),
      ftxui::text(PadRight(" MATT: " + std::to_string(e.magic_attack()),
                           kContentWidth)),
  });
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

  ftxui::Element content =
      active_tab_ == kTabSkills ? RenderSkillsTab() : RenderStatsTab();

  return ThemedWindow(" Character ",
                      ftxui::vbox({
                          ftxui::text(title),
                          ThemedSeparator(),
                          RenderTabBar(),
                          ThemedSeparator(),
                          content,
                      }),
                      /*focused=*/panel_focus_ == kCharPanel);
}

ftxui::Component CharacterPanel::MakeComponent(std::function<void()> on_ap) {
  // Renderer(bool) overload is Focusable(), unlike Renderer() -- required so
  // Container::Tab's Focused() check passes when panel_focus_ == kCharPanel.
  ftxui::Component renderer =
      ftxui::Renderer([this](bool /*focused*/) { return Render(); });
  return ftxui::CatchEvent(renderer, [this, on_ap](ftxui::Event event) {
    if (panel_focus_ != kCharPanel) {
      return false;
    }
    if (event == ftxui::Event::ArrowLeft) {
      active_tab_ = kTabStats;
      return true;
    }
    if (event == ftxui::Event::ArrowRight) {
      active_tab_ = kTabSkills;
      return true;
    }
    if (IsForward(event) && active_tab_ == kTabStats &&
        character_.proto().ap() > 0) {
      on_ap();
      return true;
    }
    return false;
  });
}

}  // namespace ms
