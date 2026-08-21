#include "src/frontend/widgets/panel_util.h"

#include <algorithm>
#include <cstdint>
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
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

constexpr int kSlotWidth = 10;
constexpr int kInfoWidth = 20;

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
  if (static_cast<int>(s.size()) >= width) {
    return s.substr(0, width);
  }
  return s + std::string(width - static_cast<int>(s.size()), ' ');
}

std::string PadLeft(const std::string& s, int width) {
  if (static_cast<int>(s.size()) >= width) {
    return s;
  }
  return std::string(width - s.size(), ' ') + s;
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

std::string FormatWithCommas(int64_t n) {
  std::string digits = std::to_string(n < 0 ? -n : n);
  int pos = static_cast<int>(digits.size()) - 3;
  while (pos > 0) {
    digits.insert(pos, ",");
    pos -= 3;
  }
  return n < 0 ? "-" + digits : digits;
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

std::vector<const Skill*> SkillsForAdvancement(
    const std::map<std::string, Skill>& catalog, JobAdvancement advancement) {
  std::vector<const Skill*> result;
  if (advancement == JOB_ADVANCEMENT_UNSPECIFIED) {
    return result;
  }
  for (const std::pair<const std::string, Skill>& entry : catalog) {
    if (entry.second.job_advancement() == advancement) {
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
  return ordered;
}

ftxui::Color MarkColor(CurrencyColor color) {
  switch (color) {
    case CURRENCY_COLOR_ICE_BLUE:
      return kIceBlue;
    case CURRENCY_COLOR_AMBER:
      return kAmber;
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
      return {"A:  ", kOrange};
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
    default:
      return "";
  }
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

std::string JobName(Job job) {
  switch (job) {
    case JOB_BEGINNER:
      return "Beginner";
    case JOB_SWORDMAN:
      return "Swordman";
    case JOB_ARCHER:
      return "Archer";
    case JOB_MAGICIAN:
      return "Magician";
    case JOB_ROGUE:
      return "Rogue";
    case JOB_FIGHTER:
      return "Fighter";
    case JOB_PAGE:
      return "Page";
    case JOB_SPEARMAN:
      return "Spearman";
    case JOB_HUNTER:
      return "Hunter";
    case JOB_CROSSBOWMAN:
      return "Crossbowman";
    case JOB_ICE_LIGHTNING_WIZARD:
      return "Ice/Lightning Wizard";
    case JOB_FIRE_POISON_WIZARD:
      return "Fire/Poison Wizard";
    case JOB_CLERIC:
      return "Cleric";
    case JOB_ASSASSIN:
      return "Assassin";
    case JOB_BANDIT:
      return "Bandit";
    case JOB_BERSERKER:
      return "Berserker";
    case JOB_CRUSADER:
      return "Crusader";
    case JOB_WHITE_KNIGHT:
      return "White Knight";
    case JOB_RANGER:
      return "Ranger";
    case JOB_SNIPER:
      return "Sniper";
    case JOB_ICE_LIGHTNING_MAGE:
      return "Ice/Lightning Mage";
    case JOB_FIRE_POISON_MAGE:
      return "Fire/Poison Mage";
    case JOB_PRIEST:
      return "Priest";
    case JOB_HERMIT:
      return "Hermit";
    case JOB_CHIEF_BANDIT:
      return "Chief Bandit";
    case JOB_DARK_KNIGHT:
      return "Dark Knight";
    case JOB_PALADIN:
      return "Paladin";
    case JOB_HERO:
      return "Hero";
    case JOB_BOW_MASTER:
      return "Bow Master";
    case JOB_MARKSMAN:
      return "Marksman";
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
      return "Ice/Lightning Arch Mage";
    case JOB_FIRE_POISON_ARCH_MAGE:
      return "Fire/Poison Arch Mage";
    case JOB_BISHOP:
      return "Bishop";
    case JOB_NIGHT_LORD:
      return "Night Lord";
    case JOB_SHADOWER:
      return "Shadower";
    default:
      return "Unknown";
  }
}

std::string ShortJobName(Job job) {
  switch (job) {
    case JOB_ICE_LIGHTNING_WIZARD:
      return "I/L Wizard";
    case JOB_FIRE_POISON_WIZARD:
      return "F/P Wizard";
    case JOB_ICE_LIGHTNING_MAGE:
      return "I/L Mage";
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
      return "I/L Arch Mage";
    case JOB_FIRE_POISON_ARCH_MAGE:
      return "F/P Arch Mage";
    case JOB_FIRE_POISON_MAGE:
      return "F/P Mage";
    default:
      return JobName(job);
  }
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

std::string ItemNameCell(const std::string& name,
                         std::chrono::steady_clock::duration elapsed) {
  return ScrollingWindow(name, kItemNameWidth, elapsed);
}

std::string FormatItemEntry(const std::string& name, EquipSlot slot,
                            const std::string& info, int scroll_pass,
                            int scroll_left, int scroll_restore,
                            std::chrono::steady_clock::duration elapsed) {
  std::string padded_info = PadRight(info, kInfoWidth);
  std::string scrolls;
  if (scroll_pass < 0) {
    scrolls = "-";
  } else {
    scrolls = std::to_string(scroll_pass) + "/" + std::to_string(scroll_left) +
              "/" + std::to_string(scroll_restore);
  }
  return ItemNameCell(name, elapsed) + "  " +
         PadRight(FormatSlot(slot), kSlotWidth) + "  " + padded_info + "  " +
         scrolls;
}

std::string FormatItemEntry(const std::string& name, EquipSlot slot,
                            const std::string& info,
                            const EquipPrototype& proto, const Equip& state,
                            std::chrono::steady_clock::duration elapsed) {
  // An item with no slots has no ledger to show, so the column reads "-".
  // Three zeroes would look like slots standing ready to be spent. A weapon
  // that has spent all of its own still reads 8/0/0, because a Clean Slate can
  // buy one back.
  if (proto.upgrade_slots() <= 0) {
    return FormatItemEntry(name, slot, info, -1, -1, -1, elapsed);
  }
  int pass = state.scroll_successes();
  int left = state.remaining_upgrade_slots();
  return FormatItemEntry(name, slot, info, pass, left,
                         proto.upgrade_slots() - pass - left, elapsed);
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

ftxui::Element ContinueButton() {
  return ActionButton("Continue", /*focused=*/true);
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

ftxui::Element ScrollBar(int total, int first_visible, int visible) {
  if (visible <= 0 || total <= visible) {
    return ftxui::text("");
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
