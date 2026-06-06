#include "src/frontend/character_panel.h"

#include <string>
#include <vector>

#include "absl/types/span.h"
#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/equip_stats.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

CharacterPanel::CharacterPanel(const CharacterInstance& character)
    : character_(character) {
}

std::string CharacterPanel::StatLine(const std::string& l1, int v1,
                                     const std::string& l2, int v2) {
  std::string left = l1 + ": " + std::to_string(v1);
  while ((int)left.size() < 12) {
    left += ' ';
  }
  return left + l2 + ": " + std::to_string(v2);
}

ftxui::Element CharacterPanel::Render() const {
  const Character& p = character_.proto();
  const AllocatedStats& a = p.allocated_stats();
  std::vector<EquipStats> equip_list;
  for (const std::pair<const EquipSlot, EquipInstance>& kv :
       character_.equipped()) {
    equip_list.push_back(kv.second.stats());
  }
  EquipStats e = SumEquipStats(absl::MakeSpan(equip_list));

  std::string lvl = std::to_string(p.level());
  while ((int)lvl.size() < 3) {
    lvl = " " + lvl;
  }

  // Build all content strings padded to exact column widths so borders align.
  std::string title = "Lv" + lvl + " " + JobName(p.job());
  {
    int pad = (24 - (int)title.size()) / 2;
    if (pad > 0) {
      title = std::string(pad, ' ') + title;
    }
    while ((int)title.size() < 24) {
      title += ' ';
    }
  }

  std::string hp_mp = " " + StatLine("HP", a.hp() + e.max_hp(), "MP", a.mp());
  while ((int)hp_mp.size() < 24) {
    hp_mp += ' ';
  }
  std::string str_dex =
      " " + StatLine("STR", a.str() + e.str(), "DEX", a.dex() + e.dex());
  while ((int)str_dex.size() < 24) {
    str_dex += ' ';
  }
  std::string int_luk =
      " " + StatLine("INT", a.int_() + e.int_(), "LUK", a.luk() + e.luk());
  while ((int)int_luk.size() < 24) {
    int_luk += ' ';
  }
  std::string att = " ATT: " + std::to_string(e.attack());
  while ((int)att.size() < 24) {
    att += ' ';
  }
  std::string matt = " MATT: " + std::to_string(e.magic_attack());
  while ((int)matt.size() < 24) {
    matt += ' ';
  }

  // AP value left-aligned with 1 space prefix in the 5-wide AP column.
  std::string ap_val = " " + std::to_string(p.ap());
  while ((int)ap_val.size() < 5) {
    ap_val += ' ';
  }

  // Each row is a literal string; ┼ appears at the exact junction columns so
  // no automerge is needed. Rows 2/6 are 32 wide (with AP balcony), rest 26.
  return ftxui::vbox({
      ftxui::text("╭ Character ─────────────╮"),
      ftxui::text("│" + title + "│"),
      ftxui::text("├────────────────────────┼─────╮"),
      ftxui::text("│" + hp_mp + "│ AP  │"),
      ftxui::text("│" + str_dex + "│" + ap_val + "│"),
      ftxui::text("│" + int_luk + "│     │"),
      ftxui::text("├────────────────────────┼─────╯"),
      ftxui::text("│" + att + "│"),
      ftxui::text("│" + matt + "│"),
      ftxui::text("╰────────────────────────╯"),
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
