#include "src/frontend/screens/inspect_panel.h"

#include <set>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/panel_util.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// The width of the stackable body. Fixed rather than fitted, so every
// description reads at the same width and the window does not resize as the
// cursor moves from one item to the next. The equip body sets its own width
// from its columns.
constexpr int kStackableWidth = 44;

}  // namespace

void InspectPanel::SetItem(const EquipTabItem* item) {
  item_ = item;
  stackable_ = nullptr;
}

void InspectPanel::SetItem(const ItemPrototype* item) {
  stackable_ = item;
  item_ = nullptr;
}

ftxui::Element InspectPanel::Render() const {
  if (stackable_ != nullptr) {
    return ThemedWindow(" Inspect ", RenderStackable()) |
           ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kStackableWidth);
  }
  if (item_ == nullptr) {
    return ThemedWindow(" Inspect ", EmptyState("no item"));
  }
  return ThemedWindow(" Inspect ", RenderEquip());
}

// A stack has no stats, no stars and no slots. Its name and what it is for is
// the whole of what there is to say about it.
ftxui::Element InspectPanel::RenderStackable() const {
  ftxui::Element description;
  if (stackable_->description().empty()) {
    description = CenteredRow(EmptyState("no description", /*gutter=*/0));
  } else {
    // paragraph wraps on spaces, so a description longer than the window
    // spills onto another line rather than off the edge. Spaced off both
    // borders by hand: every other row in the game carries its own gutter in
    // its string, and a paragraph has no string to put one in.
    description = ftxui::hbox({
        ftxui::text(" "),
        ftxui::paragraph(stackable_->description()),
        ftxui::text(" "),
    });
  }
  return ftxui::vbox({
      CenteredRow(stackable_->name()),
      ThemedSeparator(),
      std::move(description),
  });
}

ftxui::Element InspectPanel::RenderEquip() const {
  const Equip& item_state = item_->equip_state();

  const EquipPrototype& proto = item_->prototype();
  const EquipStats& base = proto.base_stats();
  const EquipStats& scroll = item_state.scroll_stats();

  int level = proto.required_level() > 0 ? proto.required_level() : 1;

  std::vector<ftxui::Element> rows;
  // An item that refuses star force gets no bar at all. A row of empty stars
  // reads as a bar waiting to be filled, which is the opposite of the truth.
  if (Supports(proto, UPGRADE_STAR_FORCE)) {
    rows.push_back(CenteredRow(StarBar(item_->stars(), item_->max_stars())));
  }
  rows.push_back(CenteredRow(item_->name()));
  rows.push_back(ThemedSeparator());
  // Trailing space on each text row keeps the right border one column clear.
  rows.push_back(ftxui::text(" Req Lev: " + std::to_string(level) + " "));
  rows.push_back(FormatJobCategories(proto));
  rows.push_back(ThemedSeparator());

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
    rows.push_back(EmptyState("no stats"));
  }

  if (proto.upgrade_slots() > 0) {
    int pass = item_state.scroll_successes();
    int left = item_state.remaining_upgrade_slots();
    int restore = proto.upgrade_slots() - pass - left;
    rows.push_back(ThemedSeparator());
    std::string scroll_label =
        pass == 1 ? " Successful Scroll " : " Successful Scrolls ";
    std::string restore_label = restore == 1 ? " Restore) " : " Restores) ";
    rows.push_back(ftxui::text(" " + std::to_string(pass) + scroll_label));
    rows.push_back(ftxui::text(" (" + std::to_string(left) + " Left, " +
                               std::to_string(restore) + restore_label));
  }

  return ftxui::vbox(std::move(rows));
}

ftxui::Element InspectPanel::StarBar(int stars, int max_stars) {
  const ftxui::Color kFilled = kYellow;
  const ftxui::Color kEmpty = kGray;
  std::vector<ftxui::Element> parts;
  for (int i = 0; i < max_stars; ++i) {
    if (i > 0 && i % 5 == 0) {
      parts.push_back(ftxui::text(" "));
    }
    bool filled = i < stars;
    parts.push_back(ftxui::text(filled ? "★" : "☆") |
                    ftxui::color(filled ? kFilled : kEmpty));
  }
  return ftxui::hbox(std::move(parts));
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
  const ftxui::Color kScrollColor = kPurple;
  const ftxui::Color kSfColor = kOrange;
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

std::string InspectPanel::FormatAttackSpeed(AttackSpeed speed) {
  // Stage number matches the proto enum value (SLOWER=1 … FASTEST_3=10).
  int stage = static_cast<int>(speed);
  std::string name = AttackSpeedName(speed);
  if (name.empty()) {
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
