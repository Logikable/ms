#include "src/frontend/widgets/panel_util.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/screen/string.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

constexpr int kSlotWidth = 10;
constexpr int kInfoWidth = 20;
constexpr int kScrollWidth = 6;

// Overrides nothing but Focusable(). ComponentBase's own OnRender and OnEvent
// already forward to a lone child, so everything else passes straight through.
class AlwaysFocusableComponent : public ftxui::ComponentBase {
 public:
  explicit AlwaysFocusableComponent(ftxui::Component child) {
    Add(std::move(child));
  }

  bool Focusable() const override {
    return true;
  }
};

// Fills a row with background color rather than block glyphs, so the label can
// sit on top of it without the two fighting over the same characters. The
// label takes one color over the fill and another past it; pass the same color
// twice to hold it steady as the bar moves.
//
// One row per label line, every row filled the same: a bar of two lines is one
// bar, not two stacked.
class ProgressBarNode : public ftxui::Node {
 public:
  ProgressBarNode(float frac, ftxui::Color fill,
                  std::vector<std::string> labels, ftxui::Color label_on_fill,
                  ftxui::Color label_off_fill)
      : frac_(std::clamp(frac, 0.0f, 1.0f)),
        fill_(fill),
        labels_(std::move(labels)),
        label_on_fill_(label_on_fill),
        label_off_fill_(label_off_fill) {
    if (labels_.empty()) {
      labels_.push_back("");
    }
  }

  void ComputeRequirement() override {
    requirement_.min_x = 1;
    requirement_.min_y = static_cast<int>(labels_.size());
  }

  void Render(ftxui::Screen& screen) override {
    const int width = box_.x_max - box_.x_min + 1;
    const int fill_end = box_.x_min + static_cast<int>(frac_ * width);
    for (int row = 0; row < static_cast<int>(labels_.size()); ++row) {
      const int y = box_.y_min + row;
      if (y > box_.y_max) {
        return;
      }
      RenderRow(screen, y, width, fill_end, labels_[row]);
    }
  }

 private:
  void RenderRow(ftxui::Screen& screen, int y, int width, int fill_end,
                 const std::string& label) {
    for (int x = box_.x_min; x <= box_.x_max; ++x) {
      ftxui::Pixel& px = screen.PixelAt(x, y);
      px.character = " ";
      px.background_color = x < fill_end ? fill_ : kBarEmpty;
    }

    const int label_len = static_cast<int>(label.size());
    const int label_x = box_.x_min + (width - label_len) / 2;
    for (int i = 0; i < label_len; ++i) {
      int x = label_x + i;
      if (x < box_.x_min || x > box_.x_max) {
        continue;
      }
      ftxui::Pixel& px = screen.PixelAt(x, y);
      px.character = std::string(1, label[i]);
      px.foreground_color = x < fill_end ? label_on_fill_ : label_off_fill_;
    }
  }

  float frac_;
  ftxui::Color fill_;
  std::vector<std::string> labels_;
  ftxui::Color label_on_fill_;
  ftxui::Color label_off_fill_;
};

// Lays its child out at the child's own size, from the corner of whatever box
// the parent hands over, and tells the parent it needs nothing in return.
class FloatingNode : public ftxui::Node {
 public:
  explicit FloatingNode(ftxui::Element child)
      : ftxui::Node(ftxui::Elements{std::move(child)}) {
  }

  void ComputeRequirement() override {
    ftxui::Node::ComputeRequirement();
    // The child still measured itself above; this drops that on the floor so
    // the parent sizes as though the child were not there.
    requirement_ = ftxui::Requirement();
  }

  void SetBox(ftxui::Box box) override {
    ftxui::Node::SetBox(box);
    children_[0]->SetBox(NaturalBox());
  }

  void Render(ftxui::Screen& screen) override {
    // Laid out a second time, because how much room there is to run off is not
    // known until the screen is in hand, and SetBox runs before that.
    children_[0]->SetBox(FitToScreen(NaturalBox(), screen));
    children_[0]->Render(screen);
  }

 private:
  // The child at its full size in the parent box's top-left corner, reaching
  // past the parent wherever it is the larger of the two.
  ftxui::Box NaturalBox() const {
    const ftxui::Requirement& req = children_[0]->requirement();
    ftxui::Box box;
    box.x_min = box_.x_min;
    box.x_max = box_.x_min + req.min_x - 1;
    box.y_min = box_.y_min;
    box.y_max = box_.y_min + req.min_y - 1;
    return box;
  }

  // Slides a box back until its bottom-right corner is on the screen -- a
  // float that leaves the terminal is not drawn at all, which is worse than
  // one sitting a little higher than it asked to.
  //
  // A float that fits then sits on the screen whole, since it can never hang
  // off both ends at once. One too big to fit has to be clipped somewhere, and
  // this gives up its top-left: an overlay is positioned by empty space above
  // and to the left of what it draws, so that is the end with nothing on it.
  static ftxui::Box FitToScreen(ftxui::Box box, const ftxui::Screen& screen) {
    int over_x = std::max(0, box.x_max - (screen.dimx() - 1));
    box.x_min -= over_x;
    box.x_max -= over_x;
    int over_y = std::max(0, box.y_max - (screen.dimy() - 1));
    box.y_min -= over_y;
    box.y_max -= over_y;
    return box;
  }
};

// Appends `skill` to `out`, but only after whatever it waits on. Keyed by
// display name, which is what a requirement names and what a learned level is
// held under. Marking before the recursion rather than after is what stops a
// cycle in the data from recurring forever.
void EmitAfterRequirement(const Skill& skill,
                          const std::map<std::string, const Skill*>& by_name,
                          std::set<std::string>& emitted,
                          std::vector<const Skill*>& out) {
  if (!emitted.insert(skill.name()).second) {
    return;
  }
  if (skill.has_required_skill()) {
    std::map<std::string, const Skill*>::const_iterator it =
        by_name.find(skill.required_skill().skill_name());
    // A requirement naming a skill from another page is nothing this list can
    // order around, and the player will find it in the book it belongs to.
    if (it != by_name.end()) {
      EmitAfterRequirement(*it->second, by_name, emitted, out);
    }
  }
  out.push_back(&skill);
}

}  // namespace

const DisplayStat* DisplayStatFor(StatField field) {
  // Both tables spell a stat the same way, so the label is the join between
  // them and neither needs to know the other's order.
  std::string name = StatFieldName(field);
  if (name.empty()) {
    return nullptr;
  }
  for (const DisplayStat& stat : kDisplayStats) {
    if (name == stat.label) {
      return &stat;
    }
  }
  return nullptr;
}

std::string PadRight(const std::string& s, int width) {
  return ColumnWindow(s, 0, width);
}

std::string PadLeft(const std::string& s, int width) {
  int columns = TextColumns(s);
  if (columns >= width) {
    return s;
  }
  return std::string(width - columns, ' ') + s;
}

std::vector<std::string> WrapBalanced(const std::string& text, int width,
                                      int tail, int indent) {
  std::vector<std::string> words;
  std::istringstream stream(text);
  std::string word;
  while (stream >> word) {
    words.push_back(word);
  }
  if (words.empty()) {
    return {""};
  }
  // best[i] is the cost of laying out the words from i on: the lines it takes,
  // then the longest of them. Fewest lines wins first, and the balance is
  // settled among the layouts that take the same number.
  int count = static_cast<int>(words.size());
  std::vector<std::pair<int, int>> best(count + 1, {0, 0});
  std::vector<int> next(count + 1, count);
  for (int i = count - 1; i >= 0; --i) {
    best[i] = {count + 1, 0};
    // Every line but the first carries the margin, and a line starting at any
    // word but the first is never the first line.
    int line = i > 0 ? indent : 0;
    for (int j = i; j < count; ++j) {
      line += static_cast<int>(words[j].size()) + (j > i ? 1 : 0);
      // The last line of all is the one that shares its row with the value.
      int room = j + 1 == count ? width - tail : width;
      if (line > room && j > i) {
        break;
      }
      std::pair<int, int> cost = {best[j + 1].first + 1,
                                  std::max(line, best[j + 1].second)};
      if (cost < best[i]) {
        best[i] = cost;
        next[i] = j + 1;
      }
    }
  }
  std::vector<std::string> lines;
  for (int i = 0; i < count; i = next[i]) {
    std::string line = (i > 0 ? std::string(indent, ' ') : "") + words[i];
    for (int j = i + 1; j < next[i]; ++j) {
      line += " " + words[j];
    }
    lines.push_back(std::move(line));
  }
  return lines;
}

std::string DropChance(double per_kill) {
  double pct = std::clamp(per_kill, 0.0, 1.0) * 100.0;
  if (pct > 0.0 && pct < 0.001) {
    return "<0.001%";
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.3f", pct);
  std::string text(buffer);
  // Trailing zeros carry no information here, and the point with nothing
  // after it carries less.
  text.erase(text.find_last_not_of('0') + 1);
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  return text + "%";
}

std::string FormatWithCommas(int64_t n) {
  std::string digits = std::to_string(n < 0 ? -n : n);
  int pos = static_cast<int>(digits.size()) - 3;
  while (pos > 0) {
    digits.insert(pos, ",");
    pos -= 3;
  }
  return n < 0 ? "-" + digits : digits;
}

std::string FormatCompact(int64_t n) {
  struct Unit {
    int64_t scale;
    const char* suffix;
  };
  static const Unit kUnits[] = {
      {1000000000000000LL, "Q"},
      {1000000000000LL, "T"},
      {1000000000LL, "B"},
      {1000000LL, "M"},
  };
  int64_t magnitude = n < 0 ? -n : n;
  for (const Unit& unit : kUnits) {
    if (magnitude < 2 * unit.scale) {
      continue;
    }
    double value = static_cast<double>(magnitude) / unit.scale;
    // Three digits of it, wherever the point falls, and no trailing zeros: a
    // column of numbers is read for its size, not its last decimal.
    int decimals = value >= 100.0 ? 0 : (value >= 10.0 ? 1 : 2);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    std::string text = buf;
    if (text.find('.') != std::string::npos) {
      text.erase(text.find_last_not_of('0') + 1);
      if (text.back() == '.') {
        text.pop_back();
      }
    }
    return (n < 0 ? "-" : "") + text + unit.suffix;
  }
  return FormatWithCommas(n);
}

std::string FormatMeso(int64_t meso) {
  return "🪙 " + FormatWithCommas(meso);
}

void AppendStat(std::string& out, int val, const std::string& label) {
  if (val <= 0) {
    return;
  }
  if (!out.empty()) {
    out += "  ";
  }
  out += "+" + std::to_string(val) + " " + label;
}

std::string FormatWeaponList(const std::vector<EquipType>& types) {
  // A weapon that comes in both hands' versions. Naming the two of them is how
  // the data says "any sword", but "One-Handed Sword / Two-Handed Sword" is
  // neither how a description writes it nor narrow enough for a column.
  struct WeaponPair {
    EquipType one_handed;
    EquipType two_handed;
    const char* name;
  };
  const WeaponPair kWeaponPairs[] = {
      {EQUIP_TYPE_ONE_HANDED_SWORD, EQUIP_TYPE_TWO_HANDED_SWORD, "Sword"},
      {EQUIP_TYPE_ONE_HANDED_AXE, EQUIP_TYPE_TWO_HANDED_AXE, "Axe"},
      {EQUIP_TYPE_ONE_HANDED_BLUNT, EQUIP_TYPE_TWO_HANDED_BLUNT, "Blunt"},
  };
  std::set<EquipType> listed(types.begin(), types.end());

  // Walked in the order they were given, so a collapsed pair lands where its
  // first half was named.
  std::string result;
  std::set<EquipType> written;
  for (EquipType type : types) {
    if (written.count(type) > 0) {
      continue;
    }
    written.insert(type);
    std::string name = FormatEquipType(type);
    for (const WeaponPair& pair : kWeaponPairs) {
      // Only a list holding the whole pair collapses: one hand's version alone
      // stays the weapon it names.
      if ((type == pair.one_handed || type == pair.two_handed) &&
          listed.count(pair.one_handed) > 0 &&
          listed.count(pair.two_handed) > 0) {
        name = pair.name;
        written.insert(pair.one_handed);
        written.insert(pair.two_handed);
        break;
      }
    }
    if (name.empty()) {
      continue;
    }
    if (!result.empty()) {
      result += " / ";
    }
    result += name;
  }
  return result;
}

// The Vengeance forms standing right now, keyed by the skill each takes the
// place of. A form whose toggle is switched off is not here, and so is not on
// the page at all.
std::map<std::string, const Skill*> FormsShowing(
    const std::map<std::string, Skill>& catalog,
    const std::set<std::string>& toggles_on) {
  std::map<std::string, const Skill*> showing;
  for (const std::pair<const std::string, Skill>& entry : catalog) {
    const Skill& skill = entry.second;
    if (!skill.replaces_skill_name().empty() &&
        toggles_on.count(skill.toggle_skill_name()) > 0) {
      showing[skill.replaces_skill_name()] = &skill;
    }
  }
  return showing;
}

std::vector<const Skill*> SkillsForAdvancement(
    const std::map<std::string, Skill>& catalog, JobAdvancement advancement,
    bool hyper, const std::set<std::string>& toggles_on) {
  std::vector<const Skill*> result;
  if (advancement == JOB_ADVANCEMENT_UNSPECIFIED) {
    return result;
  }
  for (const std::pair<const std::string, Skill>& entry : catalog) {
    // A form takes its parent's row below rather than a row of its own: it
    // carries that skill's skill_order, so listing both would be two skills
    // at one place in the book.
    if (entry.second.job_advancement() == advancement &&
        entry.second.hyper() == hyper &&
        entry.second.replaces_skill_name().empty()) {
      result.push_back(&entry.second);
    }
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const Skill* a, const Skill* b) {
                     return a->skill_order() < b->skill_order();
                   });

  std::map<std::string, const Skill*> by_name;
  for (const Skill* skill : result) {
    by_name[skill->name()] = skill;
  }
  std::vector<const Skill*> ordered;
  std::set<std::string> emitted;
  for (const Skill* skill : result) {
    EmitAfterRequirement(*skill, by_name, emitted, ordered);
  }
  std::map<std::string, const Skill*> showing =
      FormsShowing(catalog, toggles_on);
  for (const Skill*& skill : ordered) {
    std::map<std::string, const Skill*>::const_iterator form =
        showing.find(skill->name());
    if (form != showing.end()) {
      skill = form->second;
    }
  }
  return ordered;
}

ftxui::Color MarkColor(CurrencyColor color) {
  switch (color) {
    case CURRENCY_COLOR_THEME:
      return kTheme;
    case CURRENCY_COLOR_ORANGE:
      return kOrange;
    default:
      return kTheme;
  }
}

KindTag TagFor(const Skill& skill) {
  // Orange rather than red for the attack tag: red is the colour that says a
  // thing is refused (colors.h), and every attack skill carrying it on a
  // screen that dims what cannot be learned spent the alarm on something that
  // is never a problem.
  switch (skill.kind()) {
    case SKILL_KIND_ATTACK:
    case SKILL_KIND_ACTIVE:
      return {"A:  ", kGold};
    case SKILL_KIND_AUTO_ATTACK:
      // Purple rather than another yellow: an auto-attack is not a shade of
      // active, and two tags a step apart in the same hue read as one.
      return {"AA: ", kPurple};
    case SKILL_KIND_PASSIVE:
      return {"P:  ", kGreen};
    default:
      return {"    ", kGray};
  }
}

std::string FormatEquipSet(EquipSetName set) {
  switch (set) {
    case EQUIP_SET_NAME_FROZEN:
      return "Frozen Set";
    case EQUIP_SET_NAME_BOSS_ACCESSORY:
      return "Boss Accessory Set";
    default:
      return "";
  }
}

std::string FormatSlot(EquipSlot slot) {
  switch (slot) {
    case EQUIP_SLOT_PRIMARY_WEAPON:
      return "Weapon";
    case EQUIP_SLOT_PROJECTILE:
      return "Projectile";
    case EQUIP_SLOT_SECONDARY:
      return "Secondary";
    case EQUIP_SLOT_HAT:
      return "Hat";
    case EQUIP_SLOT_TOP:
      return "Top";
    case EQUIP_SLOT_BOTTOM:
      return "Bottom";
    case EQUIP_SLOT_CAPE:
      return "Cape";
    // Short of the full "Face Accessory": the slot column is ten columns wide.
    case EQUIP_SLOT_FACE_ACCESSORY:
      return "Face";
    case EQUIP_SLOT_EYE_ACCESSORY:
      return "Eye";
    // The family, not the slot: this is what a ring in the bag and a ring
    // named by a set are. Which of the four one is worn in is FormatWornSlot.
    case EQUIP_SLOT_RING:
    case EQUIP_SLOT_RING_2:
    case EQUIP_SLOT_RING_3:
    case EQUIP_SLOT_RING_4:
      return "Ring";
    case EQUIP_SLOT_PENDANT:
    case EQUIP_SLOT_PENDANT_2:
      return "Pendant";
    case EQUIP_SLOT_BELT:
      return "Belt";
    case EQUIP_SLOT_SHOULDER:
      return "Shoulder";
    case EQUIP_SLOT_POCKET:
      return "Pocket";
    case EQUIP_SLOT_EARRINGS:
      return "Earrings";
    case EQUIP_SLOT_GLOVES:
      return "Gloves";
    case EQUIP_SLOT_SHOES:
      return "Shoes";
    case EQUIP_SLOT_BADGE:
      return "Badge";
    case EQUIP_SLOT_EMBLEM:
      return "Emblem";
    case EQUIP_SLOT_MEDAL:
      return "Medal";
    case EQUIP_SLOT_HEART:
      return "Heart";
    // All six read alike: the item's own name is what says which area it is
    // from, and the slot column has ten columns to say the rest.
    case EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY:
    case EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND:
    case EQUIP_SLOT_SYMBOL_LACHELEIN:
    case EQUIP_SLOT_SYMBOL_ARCANA:
    case EQUIP_SLOT_SYMBOL_MORASS:
    case EQUIP_SLOT_SYMBOL_ESFERA:
      return "Symbol";
    default:
      return "";
  }
}

std::string FormatWornSlot(EquipSlot slot) {
  std::string name = FormatSlot(slot);
  if (SlotFamily(slot).size() == 1 || name.empty()) {
    return name;
  }
  return name + " " + std::to_string(SlotIndex(slot) + 1);
}

std::string AttackSpeedName(AttackSpeed speed) {
  switch (speed) {
    case ATTACK_SPEED_SLOWER:
      return "Slower";
    case ATTACK_SPEED_SLOW_1:
      return "Slow 1";
    case ATTACK_SPEED_SLOW_2:
      return "Slow 2";
    case ATTACK_SPEED_AVERAGE:
      return "Average";
    case ATTACK_SPEED_FAST_1:
      return "Fast 1";
    case ATTACK_SPEED_FAST_2:
      return "Fast 2";
    case ATTACK_SPEED_FASTER:
      return "Faster";
    case ATTACK_SPEED_FASTEST_1:
      return "Fastest 1";
    case ATTACK_SPEED_FASTEST_2:
      return "Fastest 2";
    case ATTACK_SPEED_FASTEST_3:
      return "Fastest 3";
    default:
      return "";
  }
}

std::string FormatEquipType(EquipType type) {
  switch (type) {
    case EQUIP_TYPE_ONE_HANDED_SWORD:
      return "One-Handed Sword";
    case EQUIP_TYPE_BOW:
      return "Bow";
    case EQUIP_TYPE_CROSSBOW:
      return "Crossbow";
    case EQUIP_TYPE_STAFF:
      return "Staff";
    case EQUIP_TYPE_DAGGER:
      return "Dagger";
    case EQUIP_TYPE_CLAW:
      return "Claw";
    case EQUIP_TYPE_THROWING_STAR:
      return "Throwing Star";
    case EQUIP_TYPE_ARROW_FOR_BOW:
      return "Arrow for Bow";
    case EQUIP_TYPE_ARROW_FOR_CROSSBOW:
      return "Arrow for Crossbow";
    case EQUIP_TYPE_TWO_HANDED_SWORD:
      return "Two-Handed Sword";
    case EQUIP_TYPE_ONE_HANDED_AXE:
      return "One-Handed Axe";
    case EQUIP_TYPE_TWO_HANDED_AXE:
      return "Two-Handed Axe";
    case EQUIP_TYPE_ONE_HANDED_BLUNT:
      return "One-Handed Blunt";
    case EQUIP_TYPE_TWO_HANDED_BLUNT:
      return "Two-Handed Blunt";
    case EQUIP_TYPE_SPEAR:
      return "Spear";
    case EQUIP_TYPE_POLEARM:
      return "Polearm";
    case EQUIP_TYPE_MEDALLION:
      return "Medallion";
    case EQUIP_TYPE_ROSARY:
      return "Rosary";
    case EQUIP_TYPE_IRON_CHAIN:
      return "Iron Chain";
    // Three types, one name. Which branch's book it is shows in its own name
    // -- calling it a "Fire/Poison Magic Book" here would say it twice.
    case EQUIP_TYPE_MAGIC_BOOK_FIRE_POISON:
    case EQUIP_TYPE_MAGIC_BOOK_ICE_LIGHTNING:
    case EQUIP_TYPE_MAGIC_BOOK_HOLY:
      return "Magic Book";
    case EQUIP_TYPE_ARROW_FLETCHING:
      return "Arrow Fletching";
    case EQUIP_TYPE_BOW_THIMBLE:
      return "Bow Thimble";
    case EQUIP_TYPE_CHARM:
      return "Charm";
    case EQUIP_TYPE_DAGGER_SCABBARD:
      return "Dagger Scabbard";
    default:
      return "";  // not yet implemented for other types
  }
}

bool IsActive(const Skill& skill) {
  return skill.kind() != SKILL_KIND_PASSIVE;
}

std::string FormatJobCategories(const EquipPrototype& proto) {
  for (int i = 0; i < proto.equip_job_categories_size(); ++i) {
    if (static_cast<EquipJobCategory>(proto.equip_job_categories(i)) ==
        EQUIP_JOB_CATEGORY_UNIVERSAL) {
      return "All";
    }
  }
  std::string result;
  for (int i = 0; i < proto.equip_job_categories_size(); ++i) {
    if (!result.empty()) {
      result += "/";
    }
    switch (static_cast<EquipJobCategory>(proto.equip_job_categories(i))) {
      case EQUIP_JOB_CATEGORY_BEGINNER:
        result += "Beginner";
        break;
      case EQUIP_JOB_CATEGORY_WARRIOR:
        result += "Warrior";
        break;
      case EQUIP_JOB_CATEGORY_BOWMAN:
        result += "Bowman";
        break;
      case EQUIP_JOB_CATEGORY_MAGICIAN:
        result += "Magician";
        break;
      case EQUIP_JOB_CATEGORY_THIEF:
        result += "Thief";
        break;
      case EQUIP_JOB_CATEGORY_PIRATE:
        result += "Pirate";
        break;
      default:
        break;
    }
  }
  if (result.empty()) {
    return "All";
  }
  return result;
}

std::string StatFieldName(StatField field) {
  switch (field) {
    case STAT_FIELD_STR:
      return "STR";
    case STAT_FIELD_DEX:
      return "DEX";
    case STAT_FIELD_INT:
      return "INT";
    case STAT_FIELD_LUK:
      return "LUK";
    case STAT_FIELD_HP:
      return "HP";
    case STAT_FIELD_MP:
      return "MP";
    default:
      return "";
  }
}

const HyperStatField kHyperStatOrder[] = {
    HYPER_STAT_FIELD_STR,           HYPER_STAT_FIELD_DEX,
    HYPER_STAT_FIELD_INT,           HYPER_STAT_FIELD_LUK,
    HYPER_STAT_FIELD_MAX_HP,        HYPER_STAT_FIELD_CRIT_RATE,
    HYPER_STAT_FIELD_CRIT_DAMAGE,   HYPER_STAT_FIELD_IED,
    HYPER_STAT_FIELD_DAMAGE,        HYPER_STAT_FIELD_BOSS_DAMAGE,
    HYPER_STAT_FIELD_NORMAL_DAMAGE, HYPER_STAT_FIELD_ATTACK,
    HYPER_STAT_FIELD_EXP,           HYPER_STAT_FIELD_ARCANE_FORCE,
};
const int kNumHyperStats = sizeof(kHyperStatOrder) / sizeof(kHyperStatOrder[0]);

std::string HyperStatName(HyperStatField field) {
  static_assert(HyperStatField_ARRAYSIZE == 16,
                "a new Hyper Stat needs a name and a place in the order");
  switch (field) {
    case HYPER_STAT_FIELD_STR:
      return "STR";
    case HYPER_STAT_FIELD_DEX:
      return "DEX";
    case HYPER_STAT_FIELD_INT:
      return "INT";
    case HYPER_STAT_FIELD_LUK:
      return "LUK";
    case HYPER_STAT_FIELD_MAX_HP:
      return "HP";
    case HYPER_STAT_FIELD_CRIT_RATE:
      return "Critical Rate";
    case HYPER_STAT_FIELD_CRIT_DAMAGE:
      return "Critical Damage";
    case HYPER_STAT_FIELD_IED:
      return "Ignore Defense";
    case HYPER_STAT_FIELD_DAMAGE:
      return "Damage";
    case HYPER_STAT_FIELD_BOSS_DAMAGE:
      return "Boss Damage";
    case HYPER_STAT_FIELD_NORMAL_DAMAGE:
      return "Normal Damage";
    case HYPER_STAT_FIELD_ATTACK:
      return "Attack & MATT";
    case HYPER_STAT_FIELD_EXP:
      return "Experience";
    case HYPER_STAT_FIELD_ARCANE_FORCE:
      return "Arcane Force";
    default:
      return "";
  }
}

std::string HyperStatBonusText(HyperStatField field, int level) {
  // Whole percents, except EXP's half-point steps, so the trailing zeros go.
  bool percent =
      field != HYPER_STAT_FIELD_STR && field != HYPER_STAT_FIELD_DEX &&
      field != HYPER_STAT_FIELD_INT && field != HYPER_STAT_FIELD_LUK &&
      field != HYPER_STAT_FIELD_ATTACK &&
      field != HYPER_STAT_FIELD_ARCANE_FORCE;
  double bonus = HyperStatBonus(field, level);
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.1f", bonus);
  std::string text(buffer);
  if (text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0) {
    text.resize(text.size() - 2);
  }
  return "+" + text + (percent ? "%" : "");
}

int ItemNameWidthFor(int width) {
  return std::clamp(width - kItemListGutter - kItemListFixedWidth,
                    kItemNameWidth, kItemNameMax);
}

std::string ItemNameCell(const std::string& name,
                         std::chrono::steady_clock::duration elapsed,
                         int name_width) {
  return ScrollingWindow(name, name_width, elapsed);
}

std::string FormatItemEntry(const std::string& name,
                            const std::string& slot_label,
                            const std::string& info, int scroll_pass,
                            int scroll_slots, int stars,
                            std::chrono::steady_clock::duration elapsed,
                            int name_width) {
  // The slot count rides along so a row says how far the item can still go,
  // not only how far it has come.
  std::string scrolls = scroll_pass < 0
                            ? "-"
                            : "+" + std::to_string(scroll_pass) + "/" +
                                  std::to_string(scroll_slots);
  std::string star_force = stars < 0 ? "-" : std::to_string(stars) + "\u2605";
  return ItemNameCell(name, elapsed, name_width) + "  " +
         PadRight(slot_label, kSlotWidth) + "  " + PadRight(info, kInfoWidth) +
         "  " + PadRight(scrolls, kScrollWidth) + "  " + star_force;
}

std::string FormatItemEntry(const std::string& name,
                            const std::string& slot_label,
                            const std::string& info,
                            const EquipPrototype& proto, const Equip& state,
                            std::chrono::steady_clock::duration elapsed,
                            int name_width) {
  // An upgrade the item refuses outright reads "-": a zero there would look
  // like a ledger standing ready to be spent.
  int slots = TotalUpgradeSlots(proto, state);
  int pass = slots > 0 ? state.scroll_successes() : -1;
  int stars = Supports(proto, UPGRADE_STAR_FORCE) ? state.stars() : -1;
  return FormatItemEntry(name, slot_label, info, pass, slots, stars, elapsed,
                         name_width);
}

ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label) {
  return ProgressBar(frac, fill, std::vector<std::string>{label});
}

ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label, ftxui::Color label_color) {
  return std::make_shared<ProgressBarNode>(
      frac, fill, std::vector<std::string>{label}, label_color, label_color);
}

ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::vector<std::string>& labels) {
  return std::make_shared<ProgressBarNode>(
      frac, fill, labels, ftxui::Color::Black, ftxui::Color::White);
}

ftxui::Element Floating(ftxui::Element element) {
  return std::make_shared<FloatingNode>(std::move(element));
}

ftxui::Element ResultWindow(const std::string& title,
                            const std::string& subject,
                            std::vector<ftxui::Element> body,
                            ftxui::Color accent) {
  std::vector<ftxui::Element> rows;
  rows.push_back(CenteredRow(subject));
  rows.push_back(AccentSeparator(accent));
  for (ftxui::Element& row : body) {
    rows.push_back(std::move(row));
  }
  rows.push_back(AccentSeparator(accent));
  rows.push_back(CenteredRow(ContinueButton()));
  return AccentWindow(title, ftxui::vbox(std::move(rows)), accent);
}

ftxui::Element DialogWindow(const std::string& title,
                            std::vector<ftxui::Element> body,
                            ftxui::Element buttons, ftxui::Color accent) {
  std::vector<ftxui::Element> rows = std::move(body);
  rows.push_back(AccentSeparator(accent));
  rows.push_back(CenteredRow(std::move(buttons)));
  return AccentWindow(title, ftxui::vbox(std::move(rows)), accent);
}

ftxui::Element EmptyState(const std::string& what, int gutter) {
  return ftxui::text(std::string(gutter, ' ') + "(" + what + ")");
}

std::string AdvanceTabKey(int stage) {
  return "advance:" + std::to_string(stage);
}

std::string EquipGiftTabKey(int stage) {
  return "equip_gift:" + std::to_string(stage);
}

ftxui::Element TabChip(const std::string& label, bool active, bool row_focused,
                       bool unseen) {
  ftxui::Element chip = ftxui::text(" " + label + " ");
  if (active && row_focused) {
    return chip | ftxui::color(ftxui::Color::Black) |
           ftxui::bgcolor(ftxui::Color::White);
  }
  chip = chip | ftxui::color(unseen ? kYellow : kTheme);
  if (active) {
    chip = chip | ftxui::inverted;
  }
  return chip;
}

// The mark standing where a bar runs off its edge, and the column it is drawn
// in -- reserved on both sides whether or not there is a mark to put there, so
// the chips hold still as the bar scrolls under them.
namespace {

constexpr char kMoreLeft[] = "‹";   // a single left angle
constexpr char kMoreRight[] = "›";  // and its mirror
constexpr int kMoreWidth = 1;

int ChipWidth(const TabSpec& tab) {
  return ftxui::string_width(tab.label) + 2;  // TabChip pads a space each side
}

// The leftmost chip a window on to `tabs` can start at and still reach
// `active` within `budget` columns. Being leftmost makes it a plain function
// of the selection: stepping right moves the window by the least it can, and
// stepping back moves it to exactly where it was on the way out.
int FirstVisible(const std::vector<TabSpec>& tabs, int active, int budget) {
  int first = 0;
  while (first < active) {
    int used = 0;
    for (int i = first; i <= active; ++i) {
      used += ChipWidth(tabs[i]);
    }
    if (used <= budget) {
      break;
    }
    ++first;
  }
  return first;
}

ftxui::Element MoreMark(const char* glyph, bool show) {
  return ftxui::text(show ? glyph : " ") | ftxui::color(kTheme);
}

}  // namespace

ftxui::Element TabBar(const std::vector<TabSpec>& tabs, int active,
                      bool row_focused, int width) {
  int end = static_cast<int>(tabs.size());
  int whole = 0;
  for (const TabSpec& tab : tabs) {
    whole += ChipWidth(tab);
  }
  // A bar that fits is drawn as it always was, marks and all left off. Their
  // two columns would otherwise indent every bar in the game for the sake of
  // the one that scrolls.
  bool scrolls = width > 0 && whole > width;

  int first = 0;
  int last = end - 1;
  if (scrolls) {
    int budget = width - 2 * kMoreWidth;
    first = FirstVisible(tabs, std::clamp(active, 0, last), budget);
    int used = 0;
    for (last = first; last < end; ++last) {
      used += ChipWidth(tabs[last]);
      if (used > budget) {
        break;
      }
    }
    --last;  // the one that did not fit, or the end of the list
    // A budget too small for even one chip still shows the chip the player is
    // on: a bar that draws nothing says less than one that overflows.
    last = std::max(last, first);
  }

  std::vector<ftxui::Element> chips;
  if (scrolls) {
    chips.push_back(MoreMark(kMoreLeft, first > 0));
  }
  for (int i = first; i <= last; ++i) {
    chips.push_back(
        TabChip(tabs[i].label, i == active, row_focused, tabs[i].unseen));
  }
  if (scrolls) {
    chips.push_back(MoreMark(kMoreRight, last < end - 1));
  }
  return ftxui::hbox(std::move(chips));
}

ftxui::Element ActionButton(const std::string& label, bool focused) {
  ftxui::Element button = ftxui::text("[" + label + "]");
  if (focused) {
    button = button | ftxui::inverted;
  }
  return button;
}

ftxui::Element ContinueButton(const std::string& label) {
  return ActionButton(label, /*focused=*/true);
}

ftxui::Element ButtonRow(const std::string& go, const std::string& leave,
                         bool go_focused, bool leave_focused, bool go_enabled) {
  ftxui::Element go_button = ActionButton(go, go_focused);
  if (!go_enabled) {
    go_button = std::move(go_button) | ftxui::dim;
  }
  return ftxui::hbox({
      ftxui::text(" "),
      std::move(go_button),
      ftxui::text("   "),
      ActionButton(leave, leave_focused),
      ftxui::text(" "),
  });
}

int ScrollWindowStart(int total, int selected, int visible) {
  if (visible <= 0 || total <= visible) {
    return 0;
  }
  // (visible - 1) / 2 rather than visible / 2, which is what yframe uses: an
  // even window puts the cursor a row above its middle either way.
  int first = selected - (visible - 1) / 2;
  return std::max(0, std::min(first, total - visible));
}

std::vector<ftxui::Element> ScrollBarCells(int total, int first_visible,
                                           int visible) {
  if (visible <= 0 || total <= visible) {
    return {};
  }
  // Counted in half rows, so the thumb can start and end halfway down a cell.
  // This is ftxui's own arithmetic, kept so the bar matches the bag's.
  int size = std::max(1, 2 * visible * visible / total);
  int start = 2 * first_visible * visible / total;
  std::vector<ftxui::Element> cells;
  for (int row = 0; row < visible; ++row) {
    bool top = start <= 2 * row && 2 * row <= start + size;
    bool bottom = start <= 2 * row + 1 && 2 * row + 1 <= start + size;
    const char* glyph = top ? (bottom ? "┃" : "╹") : (bottom ? "╻" : " ");
    cells.push_back(ftxui::text(glyph));
  }
  return cells;
}

ftxui::Element ScrollBar(int total, int first_visible, int visible) {
  std::vector<ftxui::Element> cells =
      ScrollBarCells(total, first_visible, visible);
  if (cells.empty()) {
    return ftxui::text("");
  }
  return ftxui::vbox(std::move(cells));
}

int StepCursor(int current, int delta, int stops) {
  if (stops <= 0) {
    return 0;
  }
  // Modulo twice, because C++ gives a negative remainder a negative sign: the
  // first % may land below zero, and adding stops before the second brings it
  // back into the ring. Written for any delta rather than just the one step
  // every caller passes, so a caller that ever wants two is not a special case.
  return ((current + delta) % stops + stops) % stops;
}

ftxui::Color PanelAccent(bool highlighted) {
  return highlighted ? kYellow : kTheme;
}

ftxui::Element AccentWindow(const std::string& title, ftxui::Element content,
                            ftxui::Color accent, bool focused) {
  ftxui::Element title_el = ftxui::text(title) | ftxui::color(accent);
  if (focused) {
    title_el = title_el | ftxui::inverted;
  }
  return ftxui::window(std::move(title_el),
                       std::move(content) | ftxui::color(ftxui::Color::White)) |
         ftxui::color(accent);
}

ftxui::Element ThemedWindow(const std::string& title, ftxui::Element content,
                            bool focused) {
  return AccentWindow(title, std::move(content), kTheme, focused);
}

ftxui::Element CenteredRow(ftxui::Element row) {
  // hcenter on its own does not do this. It centres within the width the widest
  // row of the window sets -- and the row that sets that width is, by
  // definition, the one flush against both borders.
  return ftxui::hbox({
             ftxui::text(" "),
             std::move(row),
             ftxui::text(" "),
         }) |
         ftxui::hcenter;
}

ftxui::Element CenteredRow(const std::string& text) {
  return CenteredRow(ftxui::text(text));
}

ftxui::Element AccentSeparator(ftxui::Color accent) {
  return ftxui::separator() | ftxui::color(accent);
}

ftxui::Element PanelSeparator(bool highlighted) {
  return AccentSeparator(PanelAccent(highlighted));
}

ftxui::Element RedUnless(ftxui::Element cell, bool ok) {
  return ok ? cell : std::move(cell) | ftxui::color(kRed);
}

ftxui::Element ThemedSeparator() {
  return AccentSeparator(kTheme);
}

ftxui::Component AlwaysFocusable(ftxui::Component child) {
  return ftxui::Make<AlwaysFocusableComponent>(std::move(child));
}

ftxui::Component WrappingList(ftxui::Component list, int& selected,
                              std::function<int()> count) {
  // Held as a pointer rather than a captured reference: the lambda outlives
  // this call by the life of the component, and a reference captured into it
  // would be one more thing to reason about than an address that cannot itself
  // be rebound.
  int* cursor = &selected;
  return ftxui::CatchEvent(
      std::move(list), [cursor, count = std::move(count)](ftxui::Event event) {
        bool up = event == ftxui::Event::ArrowUp;
        bool down = event == ftxui::Event::ArrowDown;
        if (!up && !down) {
          return false;
        }
        int stops = count();
        if (stops <= 0) {
          // Swallowed rather than passed down. An ftxui::Menu with no entries
          // still moves its index on an arrow, which leaves the cursor
          // pointing at row -1 of a list that has no rows -- and the panels
          // above read that index to decide what the player is looking at.
          return true;
        }
        if (up && *cursor <= 0) {
          *cursor = StepCursor(0, -1, stops);
          return true;
        }
        if (down && *cursor >= stops - 1) {
          *cursor = StepCursor(stops - 1, 1, stops);
          return true;
        }
        // A step through the middle, which is the menu's own business.
        return false;
      });
}

}  // namespace ms
