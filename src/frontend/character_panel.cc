#include "src/frontend/character_panel.h"

#include <functional>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/colors.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

constexpr int kContentWidth = 24;  // chars between the │ borders
constexpr int kStatLineLeft = 12;  // left field width in StatLine
constexpr int kApWidth = 6;        // chars in the AP balcony column

}  // namespace

CharacterPanel::CharacterPanel(const CharacterInstance& character,
                               int& panel_focus)
    : character_(character), panel_focus_(panel_focus) {
}

std::string CharacterPanel::StatLine(const std::string& l1, int v1,
                                     const std::string& l2, int v2) {
  std::string left = l1 + ": " + std::to_string(v1);
  while ((int)left.size() < kStatLineLeft) {
    left += ' ';
  }
  return left + l2 + ": " + std::to_string(v2);
}

ftxui::Element CharacterPanel::Render() const {
  const Character& p = character_.proto();
  const AllocatedStats& a = p.allocated_stats();
  const EquipStats& e = character_.equip_stats();

  std::string lvl = std::to_string(p.level());
  while ((int)lvl.size() < 3) {
    lvl = " " + lvl;
  }

  // Build all content strings padded to exact column widths so borders align.
  std::string title = "Lv" + lvl + " " + JobName(p.job());
  {
    int pad = (kContentWidth - (int)title.size()) / 2;
    if (pad > 0) {
      title = std::string(pad, ' ') + title;
    }
    while ((int)title.size() < kContentWidth) {
      title += ' ';
    }
  }

  std::string hp_mp = " " + StatLine("HP", a.hp() + e.max_hp(), "MP", a.mp());
  while ((int)hp_mp.size() < kContentWidth) {
    hp_mp += ' ';
  }
  std::string str_dex =
      " " + StatLine("STR", a.str() + e.str(), "DEX", a.dex() + e.dex());
  while ((int)str_dex.size() < kContentWidth) {
    str_dex += ' ';
  }
  std::string int_luk =
      " " + StatLine("INT", a.int_() + e.int_(), "LUK", a.luk() + e.luk());
  while ((int)int_luk.size() < kContentWidth) {
    int_luk += ' ';
  }
  std::string att = " ATT: " + std::to_string(e.attack());
  while ((int)att.size() < kContentWidth) {
    att += ' ';
  }
  std::string matt = " MATT: " + std::to_string(e.magic_attack());
  while ((int)matt.size() < kContentWidth) {
    matt += ' ';
  }

  // AP value with caret prefix when the char panel is focused.
  bool ap_focused = panel_focus_ == kCharPanel;
  std::string ap_val = (ap_focused ? "> " : "  ") + std::to_string(p.ap());
  while ((int)ap_val.size() < kApWidth) {
    ap_val += ' ';
  }
  ftxui::Element ap_cell = ftxui::text(ap_val);
  if (ap_focused) {
    ap_cell = ap_cell | ftxui::focus;
  }

  // Each row is a literal string; ┼ appears at the exact junction columns so
  // no automerge is needed. Rows 2/6 are 32 wide (with AP balcony), rest 26.
  // Border chars colored kTheme via B(); content inherits color(White) on vbox.
  const ftxui::Color kB = kTheme;
  auto B = [kB](const std::string& s) -> ftxui::Element {
    return ftxui::text(s) | ftxui::color(kB);
  };
  return ftxui::vbox({
             B("╭ Character ─────────────╮"),
             ftxui::hbox({B("│"), ftxui::text(title), B("│")}),
             B("├────────────────────────┼──────╮"),
             ftxui::hbox({B("│"), ftxui::text(hp_mp), B("│"),
                          ftxui::text("  AP  "), B("│")}),
             ftxui::hbox(
                 {B("│"), ftxui::text(str_dex), B("│"), ap_cell, B("│")}),
             ftxui::hbox({B("│"), ftxui::text(int_luk), B("│      │")}),
             B("├────────────────────────┼──────╯"),
             ftxui::hbox({B("│"), ftxui::text(att), B("│")}),
             ftxui::hbox({B("│"), ftxui::text(matt), B("│")}),
             B("╰────────────────────────╯"),
         }) |
         ftxui::color(ftxui::Color::White);
}

ftxui::Component CharacterPanel::MakeComponent(std::function<void()> on_ap) {
  // Renderer(bool) overload is Focusable(), unlike Renderer() — required so
  // Container::Tab's Focused() check passes when panel_focus_ == kCharPanel.
  ftxui::Component renderer =
      ftxui::Renderer([this](bool /*focused*/) { return Render(); });
  return ftxui::CatchEvent(renderer, [this, on_ap](ftxui::Event event) {
    if (panel_focus_ == kCharPanel && event == ftxui::Event::Return &&
        character_.proto().ap() > 0) {
      on_ap();
      return true;
    }
    return false;
  });
}

std::string CharacterPanel::JobName(Job job) {
  switch (job) {
    case JOB_BEGINNER:
      return "Beginner";
    case JOB_WARRIOR:
      return "Warrior";
    default:
      return "Unknown";
  }
}

}  // namespace ms
