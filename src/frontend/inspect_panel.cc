#include "src/frontend/inspect_panel.h"

#include <set>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_util.h"
#include "src/protos/equip.pb.h"

namespace ms {

void InspectPanel::SetItem(const EquipTabItem* item) {
  item_ = item;
}

ftxui::Element InspectPanel::Render() const {
  if (item_ == nullptr) {
    return ftxui::window(ftxui::text(" Inspect "), ftxui::text(" (none)"));
  }

  const Equip& item_state = item_->equip_state();

  const EquipPrototype& proto = item_->prototype();
  const EquipStats& base = proto.base_stats();
  const EquipStats& scroll = item_state.scroll_stats();
  int stars = item_->stars();
  int max_stars = item_->max_stars();

  int level = proto.required_level() > 0 ? proto.required_level() : 1;

  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(StarBar(stars, max_stars)) | ftxui::hcenter);
  rows.push_back(ftxui::text(item_->name()) | ftxui::hcenter);
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

  EquipStats sf = item_->StarForceStatGains();

  bool any_stat = false;
  auto AddRow = [&](const std::string& label, int base, int scroll,
                    int star_force) {
    ftxui::Element elem = StatLine(label, base, scroll, star_force);
    if (elem == nullptr) {
      return;
    }
    rows.push_back(elem);
    any_stat = true;
  };
  for (const DisplayStat& stat : kDisplayStats) {
    AddRow(stat.label, stat.GetFrom(base), stat.GetFrom(scroll),
           stat.GetFrom(sf));
  }

  if (!any_stat) {
    rows.push_back(ftxui::text(" (no stats) "));
  }

  if (proto.upgrade_slots() > 0) {
    int pass = item_state.scroll_successes();
    int left = item_state.remaining_upgrade_slots();
    int restore = proto.upgrade_slots() - pass - left;
    rows.push_back(ftxui::separator());
    std::string scroll_label =
        pass == 1 ? " Successful Scroll " : " Successful Scrolls ";
    std::string restore_label = restore == 1 ? " Restore) " : " Restores) ";
    rows.push_back(ftxui::text(" " + std::to_string(pass) + scroll_label));
    rows.push_back(ftxui::text(" (" + std::to_string(left) + " Left, " +
                               std::to_string(restore) + restore_label));
  }

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

ftxui::Element InspectPanel::StatLine(const std::string& label, int base,
                                      int scroll, int sf) {
  if (base == 0 && scroll == 0 && sf == 0) {
    return nullptr;
  }
  int total = base + scroll + sf;
  // Base-only: no breakdown needed, plain text.
  if (scroll == 0 && sf == 0) {
    return ftxui::text(" " + label + "  +" + std::to_string(total) + " ");
  }
  // Breakdown: base in default color, scroll in amber, SF in periwinkle.
  const ftxui::Color kScrollColor = ftxui::Color::RGB(173, 163, 255);
  const ftxui::Color kSfColor = ftxui::Color::RGB(255, 198, 50);
  std::vector<ftxui::Element> parts;
  parts.push_back(
      ftxui::text(" " + label + "  +" + std::to_string(total) + " ("));
  parts.push_back(ftxui::text(std::to_string(base)));
  if (scroll > 0) {
    parts.push_back(ftxui::text(" +" + std::to_string(scroll)) |
                    ftxui::color(kScrollColor));
  }
  if (sf > 0) {
    parts.push_back(ftxui::text(" +" + std::to_string(sf)) |
                    ftxui::color(kSfColor));
  }
  parts.push_back(ftxui::text(") "));
  return ftxui::hbox(std::move(parts));
}

std::string InspectPanel::FormatEquipType(EquipType type) {
  switch (type) {
    case EQUIP_TYPE_ONE_HANDED_SWORD:
      return "One-Handed Sword";
    default:
      return "";  // not yet implemented for other types
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
