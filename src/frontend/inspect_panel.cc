#include "src/frontend/inspect_panel.h"

#include <set>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {

void InspectPanel::SetItem(const EquipInstance* item) {
  item_ = item;
}

ftxui::Element InspectPanel::Render() const {
  if (item_ == nullptr) {
    return ftxui::window(ftxui::text(" Inspect "), ftxui::text(" (none)"));
  }

  const EquipPrototype& proto = item_->prototype();
  const EquipStats& base = proto.base_stats();
  const EquipStats& scroll = item_->proto().scroll_stats();

  int level = proto.required_level() > 0 ? proto.required_level() : 1;

  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(StarBar(item_->stars(), item_->max_stars())) |
                 ftxui::hcenter);
  rows.push_back(ftxui::text(proto.name()) | ftxui::hcenter);
  rows.push_back(ftxui::separator());
  // Trailing space on each text row keeps the right border one column clear.
  rows.push_back(ftxui::text(" Req Lev: " + std::to_string(level) + " "));
  rows.push_back(FormatJobCategories(proto));
  rows.push_back(ftxui::separator());

  if (proto.equip_type() != EQUIP_TYPE_UNSPECIFIED) {
    rows.push_back(
        ftxui::text(" Type: " + FormatEquipType(proto.equip_type()) + " "));
  }
  if (proto.attack_speed() != ATTACK_SPEED_UNSPECIFIED) {
    rows.push_back(ftxui::text(
        " Attack Speed: " + FormatAttackSpeed(proto.attack_speed()) + " "));
  }

  bool any_stat = false;
  auto AddRow = [&](const std::string& label, int b, int s) {
    std::string line = StatLine(label, b, s);
    if (line.empty()) {
      return;
    }
    rows.push_back(ftxui::text(line));
    any_stat = true;
  };
  AddRow("ATT", base.attack(), scroll.attack());
  AddRow("MATT", base.magic_attack(), scroll.magic_attack());
  AddRow("STR", base.str(), scroll.str());
  AddRow("DEX", base.dex(), scroll.dex());
  AddRow("INT", base.int_(), scroll.int_());
  AddRow("LUK", base.luk(), scroll.luk());
  AddRow("HP", base.max_hp(), scroll.max_hp());
  AddRow("DEF", base.def(), scroll.def());

  if (!any_stat) {
    rows.push_back(ftxui::text(" (no stats) "));
  }

  rows.push_back(ftxui::separator());
  int slots = item_->proto().remaining_upgrade_slots();
  rows.push_back(
      ftxui::text(" Remaining Enhancements: " + std::to_string(slots) + " "));

  return ftxui::window(ftxui::text(" Inspect "), ftxui::vbox(std::move(rows)));
}

std::string InspectPanel::StarBar(int stars, int max_stars) {
  std::string bar;
  for (int i = 0; i < max_stars; ++i) {
    if (i > 0 && i % 5 == 0) {
      bar += ' ';
    }
    bar += (i < stars) ? "★" : "☆";
  }
  return bar;
}

std::string InspectPanel::StatLine(const std::string& label, int base,
                                   int scroll) {
  if (base == 0 && scroll == 0) {
    return "";
  }
  int total = base + scroll;
  return " " + label + "  +" + std::to_string(total) + " (" +
         std::to_string(base) + " +" + std::to_string(scroll) + ") ";
}

std::string InspectPanel::FormatEquipType(EquipType type) {
  switch (type) {
    case EQUIP_TYPE_ONE_HANDED_SWORD:
      return "One-Handed Sword";
    default:
      return "";
  }
}

std::string InspectPanel::FormatAttackSpeed(AttackSpeed speed) {
  // Stage number matches the proto enum value (SLOWER=1 … FASTEST_3=10).
  int stage = static_cast<int>(speed);
  std::string name;
  switch (speed) {
    case ATTACK_SPEED_SLOWER:
      name = "Slower";
      break;
    case ATTACK_SPEED_SLOW_1:
      name = "Slow 1";
      break;
    case ATTACK_SPEED_SLOW_2:
      name = "Slow 2";
      break;
    case ATTACK_SPEED_AVERAGE:
      name = "Average";
      break;
    case ATTACK_SPEED_FAST_1:
      name = "Fast 1";
      break;
    case ATTACK_SPEED_FAST_2:
      name = "Fast 2";
      break;
    case ATTACK_SPEED_FASTER:
      name = "Faster";
      break;
    case ATTACK_SPEED_FASTEST_1:
      name = "Fastest 1";
      break;
    case ATTACK_SPEED_FASTEST_2:
      name = "Fastest 2";
      break;
    case ATTACK_SPEED_FASTEST_3:
      name = "Fastest 3";
      break;
    default:
      return "";
  }
  return "Stage " + std::to_string(stage) + " (" + name + ")";
}

ftxui::Element InspectPanel::FormatJobCategories(const EquipPrototype& proto) {
  std::set<EquipJobCategory> cats;
  for (int i = 0; i < proto.equip_job_categories_size(); ++i) {
    cats.insert(static_cast<EquipJobCategory>(proto.equip_job_categories(i)));
  }
  bool universal = cats.empty() || cats.count(EQUIP_JOB_CATEGORY_UNIVERSAL);

  struct Entry {
    const char* name;
    EquipJobCategory cat;
  };
  const Entry kEntries[] = {
      {"Beginner", EQUIP_JOB_CATEGORY_BEGINNER},
      {"Warrior", EQUIP_JOB_CATEGORY_WARRIOR},
      {"Bowman", EQUIP_JOB_CATEGORY_BOWMAN},
      {"Magician", EQUIP_JOB_CATEGORY_MAGICIAN},
      {"Thief", EQUIP_JOB_CATEGORY_THIEF},
      {"Pirate", EQUIP_JOB_CATEGORY_PIRATE},
  };

  std::vector<ftxui::Element> elems;
  elems.push_back(ftxui::text(" "));
  bool first = true;
  for (const Entry& entry : kEntries) {
    if (!first) {
      elems.push_back(ftxui::text(" / "));
    }
    first = false;
    ftxui::Element e = ftxui::text(entry.name);
    if (!universal && !cats.count(entry.cat)) {
      e = e | ftxui::dim;
    }
    elems.push_back(e);
  }
  elems.push_back(ftxui::text(" "));
  return ftxui::hbox(std::move(elems));
}

}  // namespace ms
