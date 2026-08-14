#include "src/frontend/screens/inspect_panel.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/panel_util.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// The width of the stackable body. Fixed rather than fitted, so every
// description reads at the same width and the window does not resize as the
// cursor moves from one item to the next. The equip body sets its own width
// from its columns.
constexpr int kStackableWidth = 44;

// The set card. One width whatever the set holds, for the same reason the
// stackable body has one: the card sits beside the item, and a card that
// resized would walk the item panel across the screen.
constexpr int kSetWidth = 47;
constexpr int kSetSlotWidth = 11;
constexpr int kSetTierWidth = 15;

// A fraction as a percentage, with a whole number left whole: a set's figures
// are written round, and "+20.00%" says nothing "+20%" does not.
std::string SetPercent(double fraction) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f", std::round(fraction * 1000.0) / 10.0);
  std::string s = buf;
  if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) {
    s.resize(s.size() - 2);
  }
  return s + "%";
}

// Two levers a set states as one row when both halves agree. They virtually
// always come bundled -- a set that pays attack pays magic attack with it --
// and two rows saying the same number is two rows the player has to compare.
template <typename T>
struct LeverPair {
  const char* together;
  const char* first;
  const char* second;
  T (SkillEffect::*a)() const;
  T (SkillEffect::*b)() const;
};

const LeverPair<int> kFlatPairs[] = {
    {"Attack Power & Magic ATT", "Attack Power", "Magic ATT",
     &SkillEffect::attack, &SkillEffect::magic_attack},
};

const LeverPair<double> kPercentPairs[] = {
    {"Max HP & MP", "Max HP", "Max MP", &SkillEffect::max_hp_pct,
     &SkillEffect::max_mp_pct},
};

// The levers a set states on their own, in display order.
struct FlatLever {
  const char* label;
  int (SkillEffect::*fn)() const;
};

const FlatLever kFlatLevers[] = {
    {"DEF", &SkillEffect::def},
};

struct PercentLever {
  const char* label;
  double (SkillEffect::*fn)() const;
};

const PercentLever kPercentLevers[] = {
    {"ATT", &SkillEffect::attack_pct},
    {"Damage", &SkillEffect::damage_pct},
    {"Boss Damage", &SkillEffect::boss_pct},
    {"Ignore DEF", &SkillEffect::ied_pct},
    {"Critical Rate", &SkillEffect::crit_rate},
    {"Critical Damage", &SkillEffect::crit_dmg},
    {"Meso Drop Rate", &SkillEffect::meso_pct},
    {"Additional EXP", &SkillEffect::exp_pct},
};

struct StatValue {
  const char* label;
  int value;
};

// The four primary stats, which a set grants in equal shares or not at all.
void AppendStatLines(const SkillEffect& e, std::vector<std::string>& lines) {
  if (e.str() != 0 && e.str() == e.dex() && e.dex() == e.int_() &&
      e.int_() == e.luk()) {
    lines.push_back("All Stats +" + std::to_string(e.str()));
    return;
  }
  const StatValue kStats[] = {
      {"STR", e.str()}, {"DEX", e.dex()}, {"INT", e.int_()}, {"LUK", e.luk()}};
  for (const StatValue& stat : kStats) {
    if (stat.value != 0) {
      lines.push_back(std::string(stat.label) + " +" +
                      std::to_string(stat.value));
    }
  }
}

// A flat lever's own figure, so both kinds of pair share one shape.
std::string SetWhole(int value) {
  return std::to_string(value);
}

// Adds a pair as one row when both halves agree, and as a row each when they
// do not: a set is free to pay one side more than the other.
template <typename T, typename Format>
void AppendPairLines(const LeverPair<T>& pair, const SkillEffect& e,
                     Format format, std::vector<std::string>& lines) {
  T a = (e.*pair.a)();
  T b = (e.*pair.b)();
  if (a != T() && a == b) {
    lines.push_back(std::string(pair.together) + " +" + format(a));
    return;
  }
  if (a != T()) {
    lines.push_back(std::string(pair.first) + " +" + format(a));
  }
  if (b != T()) {
    lines.push_back(std::string(pair.second) + " +" + format(b));
  }
}

// What one tier of a set pays, a line per lever. A lever with no row here is
// a bonus the player is paid and never told about, which the set data test
// watches for.
std::vector<std::string> EffectLines(const SkillEffect& e) {
  std::vector<std::string> lines;
  AppendStatLines(e, lines);
  for (const FlatLever& lever : kFlatLevers) {
    int value = (e.*lever.fn)();
    if (value != 0) {
      lines.push_back(std::string(lever.label) + " +" + std::to_string(value));
    }
  }
  for (const LeverPair<double>& pair : kPercentPairs) {
    AppendPairLines(pair, e, SetPercent, lines);
  }
  for (const LeverPair<int>& pair : kFlatPairs) {
    AppendPairLines(pair, e, SetWhole, lines);
  }
  for (const PercentLever& lever : kPercentLevers) {
    double value = (e.*lever.fn)();
    if (value != 0.0) {
      lines.push_back(std::string(lever.label) + " +" + SetPercent(value));
    }
  }
  return lines;
}

}  // namespace

void InspectPanel::SetItem(const EquipTabItem* item) {
  item_ = item;
  stackable_ = nullptr;
}

void InspectPanel::SetItem(const ItemPrototype* item) {
  stackable_ = item;
  item_ = nullptr;
}

void InspectPanel::UseCharacter(const CharacterInstance& character) {
  character_ = &character;
}

ftxui::Element InspectPanel::Render() const {
  if (stackable_ != nullptr) {
    return ThemedWindow(" Inspect ", RenderStackable()) |
           ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kStackableWidth);
  }
  if (item_ == nullptr) {
    return ThemedWindow(" Inspect ", EmptyState("no item"));
  }
  ftxui::Element window = ThemedWindow(" Inspect ", RenderEquip());
  const EquipSet* set = SetOfItem();
  if (set == nullptr) {
    return window;
  }
  return ftxui::hbox({std::move(window), RenderSetEffect(*set)});
}

const EquipSet* InspectPanel::SetOfItem() const {
  if (item_ == nullptr || character_ == nullptr) {
    return nullptr;
  }
  // By display name, as the character counts what is worn. A trace of a set
  // piece is a piece of that set: it is the same item, waiting to be recovered.
  const std::string& name = item_->prototype().name();
  for (const std::pair<const std::string, EquipSet>& entry :
       character_->equip_sets()) {
    for (const EquipSetMember& member : entry.second.members()) {
      if (!member.name().empty() && member.name() == name) {
        return &entry.second;
      }
    }
  }
  return nullptr;
}

ftxui::Element InspectPanel::RenderSetEffect(const EquipSet& set) const {
  int worn = character_->PiecesWornOf(set);
  std::vector<ftxui::Element> rows;
  rows.push_back(CenteredRow(FormatEquipSet(set.name())));
  rows.push_back(ThemedSeparator());
  for (const EquipSetMember& member : set.members()) {
    // A slot with no item written yet says what it is waiting for. It counts
    // toward no tier, so the set reads as unfinished rather than as broken.
    std::string fills =
        member.name().empty() ? "Choose 1 " + member.family() : member.name();
    rows.push_back(ftxui::text(
        " " + PadRight(FormatSlot(member.slot()), kSetSlotWidth) + fills));
  }
  rows.push_back(ThemedSeparator());
  for (const EquipSetTier& tier : set.tiers()) {
    std::string label = std::to_string(tier.pieces()) + " Set Effect";
    for (const std::string& line : EffectLines(tier.effect())) {
      ftxui::Element row =
          ftxui::text(" " + PadRight(label, kSetTierWidth) + line);
      // Dimmed until the pieces are on: the card is what the set would pay,
      // and the stats page is what the character has.
      if (worn < tier.pieces()) {
        row = row | ftxui::dim;
      }
      rows.push_back(row);
      // Only the first line of a tier is labelled; the rest hang under it.
      label.clear();
    }
  }
  return ThemedWindow(" Set Effect ", ftxui::vbox(std::move(rows))) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kSetWidth);
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
