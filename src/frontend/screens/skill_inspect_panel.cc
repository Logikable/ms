#include "src/frontend/screens/skill_inspect_panel.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/panel_util.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// Chars inside the window border. Wide enough that no requirement wraps: the
// panel stands on its own in the middle of the screen, so the only cost of the
// room is the room itself.
constexpr int kContentWidth = 44;
// Effect rows indent past the one-space border gutter, so they read as
// belonging to the "Level N" heading above them.
constexpr int kEffectIndent = 3;
// Seats "Required Weapon", the longest label, with a gap after it.
constexpr int kEffectLabelWidth = 18;
// What is left for an effect row's value once the indent and label are paid.
constexpr int kValueWidth = kContentWidth - kEffectIndent - kEffectLabelWidth;

// A percentage lever and how it reads to the player. The sign is the lever's
// direction, not its stored value: every lever is stored positive, and one
// that cancels damage is a good thing shown as a subtraction.
enum Sign { kPlus, kMinus, kBare };

// Slack for the floor a whole-number lever takes, matching the one the stats
// use: a per-level step that cannot be written exactly lands a hair under the
// level it climbs to.
constexpr double kWholeEpsilon = 1e-9;

struct PercentLever {
  const char* label;
  double (SkillEffect::*fn)() const;
  Sign sign;
  // What the percentage is charged against, when it is not the whole of the
  // effect: a per-orb bargain is worth five times what its row says, and a row
  // that did not say so would read as the total.
  const char* unit;
  // Whether the lever only ever pays out in whole numbers, so the page floors
  // it as the game does. Left off by every row but the one that needs it.
  bool whole;
};

// Percentage levers in display order. Damage is not here -- an attack's own
// percentage is its identity and gets a line of its own, above these.
const PercentLever kPercentLevers[] = {
    {"Max HP", &SkillEffect::max_hp_pct, kPlus, ""},
    {"Max MP", &SkillEffect::max_mp_pct, kPlus, ""},
    {"ATT", &SkillEffect::attack_pct, kPlus, ""},
    {"Damage", &SkillEffect::damage_pct, kPlus, ""},
    {"Final Damage", &SkillEffect::final_dmg_pct, kPlus, ""},
    {"Ignore DEF", &SkillEffect::ied_pct, kPlus, ""},
    {"Final Damage", &SkillEffect::final_dmg_pct_per_combo_orb, kPlus,
     " per Combo Orb"},
    {"Critical Rate", &SkillEffect::crit_rate, kPlus, ""},
    {"Critical Damage", &SkillEffect::crit_dmg, kPlus, ""},
    {"Mastery", &SkillEffect::mastery, kBare, ""},
    {"Damage Taken", &SkillEffect::damage_taken_pct, kMinus, ""},
    {"Dodge Chance", &SkillEffect::dodge_chance, kPlus, ""},
    {"Damage to MP", &SkillEffect::damage_to_mp_pct, kBare, ""},
    {"Reflected", &SkillEffect::damage_reflect_pct, kBare, ""},
    {"Heal", &SkillEffect::heal_pct, kPlus, " HP"},
    {"Heal per Attack", &SkillEffect::hp_recover_pct, kPlus, " HP"},
    {"Elemental Resist", &SkillEffect::elemental_resistance, kPlus, ""},
    {"Defense", &SkillEffect::def_pct, kPlus, ""},
};

// The levers that are a plain count rather than a share of anything. Both are
// doubles for reasons of their own: abnormal status resistance for the half
// point Vessel of Light grants, and bonus skill levels for the fraction its
// ladder climbs by.
const PercentLever kNumberLevers[] = {
    {"Status Resist", &SkillEffect::status_resistance, kPlus, ""},
    // Whole levels, carried as a fraction so the ladder can step. Floored for
    // the page exactly as it is floored where it is read.
    {"Skill Levels", &SkillEffect::skill_level_bonus, kPlus, "", true},
};

struct FlatLever {
  const char* label;
  int (SkillEffect::*fn)() const;
  // What the number counts, when it is not the stat itself. "" for a plain
  // total; a stage or a per-character-level grant needs saying.
  const char* unit;
  // Whether `unit` is a thing being counted, and so takes an "s" for any
  // number but one. "per level" is not -- it says when, not how many.
  bool countable;
};

const FlatLever kFlatLevers[] = {
    {"DEF", &SkillEffect::def, "", false},
    {"ATT", &SkillEffect::attack, "", false},
    {"MATT", &SkillEffect::magic_attack, "", false},
    {"ATT", &SkillEffect::attack_per_combo_orb, " per Combo Orb", false},
    {"STR", &SkillEffect::str, "", false},
    {"DEX", &SkillEffect::dex, "", false},
    {"INT", &SkillEffect::int_, "", false},
    {"LUK", &SkillEffect::luk, "", false},
    {"Max HP", &SkillEffect::max_hp_per_level, " per level", false},
    {"Max MP", &SkillEffect::max_mp_per_level, " per level", false},
    {"Attack Speed", &SkillEffect::attack_speed, " stage", true},
};

// The value of a lever at learned level L, in the same shape the stats
// themselves are folded with.
double PercentAt(const Skill& skill, double (SkillEffect::*fn)() const,
                 int level) {
  return (skill.base().*fn)() + (skill.per_level().*fn)() * (level - 1);
}

// A fraction as a percentage, to one decimal, with a whole number left whole.
// Rounds rather than truncates: summing a lever's per-level steps lands a hair
// under the round figure (16 levels of +1% is 0.15999...), and a skill that
// reads "15.9%" at the level its data says 16% is simply wrong.
std::string FormatPercent(double frac) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f", std::round(frac * 1000.0) / 10.0);
  std::string s = buf;
  if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) {
    s.resize(s.size() - 2);
  }
  return s + "%";
}

// A count of seconds, without a trailing ".0" on the whole ones.
std::string FormatSeconds(double seconds) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f", seconds);
  std::string s = buf;
  if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) {
    s.resize(s.size() - 2);
  }
  return s;
}

// One "  label      value" row of an effect block.
ftxui::Element EffectRow(const std::string& label, const std::string& value) {
  return ftxui::text(std::string(kEffectIndent, ' ') +
                     PadRight(label, kEffectLabelWidth) + value);
}

// Breaks `text` into lines that fit `width`, splitting only between words. A
// word longer than the column is left whole and allowed to overhang rather
// than being cut in half, which no description here comes close to needing.
std::vector<std::string> WrapText(const std::string& text, int width) {
  std::vector<std::string> lines;
  std::string line;
  size_t i = 0;
  while (i < text.size()) {
    size_t end = text.find(' ', i);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string word = text.substr(i, end - i);
    if (!line.empty() &&
        static_cast<int>(line.size() + 1 + word.size()) > width) {
      lines.push_back(line);
      line.clear();
    }
    if (!line.empty()) {
      line += " ";
    }
    line += word;
    i = end + 1;
  }
  if (!line.empty()) {
    lines.push_back(line);
  }
  return lines;
}

// A label/value row whose value is allowed not to fit: it continues on the
// next line with the label left blank, rather than being cut mid-word. An
// empty value writes no row at all.
std::vector<ftxui::Element> WrappedEffectRows(const std::string& label,
                                              const std::string& value) {
  std::vector<ftxui::Element> rows;
  std::string current = label;
  for (const std::string& line : WrapText(value, kValueWidth)) {
    rows.push_back(EffectRow(current, line));
    current.clear();
  }
  return rows;
}

// A weapon that comes in both hands' versions. Demanding the two of them is
// how the data says "any sword", but "One-Handed Sword / Two-Handed Sword" is
// neither how the description writes it nor narrow enough for the column.
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

// The pair `type` belongs to, if the skill demands the whole of it. Null when
// only one hand's version is asked for, which stays the weapon it names.
const WeaponPair* WholePairFor(const std::set<EquipType>& demanded,
                               EquipType type) {
  for (const WeaponPair& pair : kWeaponPairs) {
    if ((type == pair.one_handed || type == pair.two_handed) &&
        demanded.count(pair.one_handed) > 0 &&
        demanded.count(pair.two_handed) > 0) {
      return &pair;
    }
  }
  return nullptr;
}

// The weapons a skill demands, as "Dagger" or "Sword / Axe". Empty when it can
// be swung with anything, which is what most skills want.
std::string RequiredWeapons(const google::protobuf::RepeatedField<int>& types) {
  std::set<EquipType> demanded;
  for (int type : types) {
    demanded.insert(static_cast<EquipType>(type));
  }

  // Walked in the order they are listed, so a collapsed pair lands where its
  // first half was named.
  std::string result;
  std::set<EquipType> written;
  for (int listed : types) {
    EquipType type = static_cast<EquipType>(listed);
    if (written.count(type) > 0) {
      continue;
    }
    written.insert(type);
    std::string name = FormatEquipType(type);
    const WeaponPair* pair = WholePairFor(demanded, type);
    if (pair != nullptr) {
      name = pair->name;
      written.insert(pair->one_handed);
      written.insert(pair->two_handed);
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

// What the skill asks for before it can be swung at all. The two read as a
// pair: a weapon in hand and a skill already learned are the same kind of
// condition, so they are labelled and laid out alike instead of one sitting up
// beside the description and the other down among the facts.
std::vector<ftxui::Element> RequirementRows(const Skill& skill) {
  std::vector<ftxui::Element> rows;
  for (ftxui::Element& row : WrappedEffectRows(
           "Required Weapon", RequiredWeapons(skill.required_equip_type()))) {
    rows.push_back(std::move(row));
  }
  if (!skill.has_required_skill()) {
    return rows;
  }
  // Built from the requirement rather than typed beside it, so the sentence
  // and the rule the skills tab enforces cannot drift apart.
  std::string required = skill.required_skill().skill_name() + " Lv. " +
                         std::to_string(skill.required_skill().level()) + "+";
  for (ftxui::Element& row : WrappedEffectRows("Required Skill", required)) {
    rows.push_back(std::move(row));
  }
  return rows;
}

// A plain number, to one decimal, with a whole number left whole. The same
// rounding FormatPercent does and for the same reason.
std::string FormatNumber(double value) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f", std::round(value * 10.0) / 10.0);
  std::string s = buf;
  if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) {
    s.resize(s.size() - 2);
  }
  return s;
}

// The rows that hold at every level: what the skill asks for, how far a swing
// reaches, and how often it goes off on its own. A skill with none of them
// gets no block at all.
std::vector<ftxui::Element> InvariantRows(const Skill& skill) {
  std::vector<ftxui::Element> rows = RequirementRows(skill);
  if (skill.max_enemies() > 1) {
    rows.push_back(
        EffectRow("Enemies Hit", std::to_string(skill.max_enemies())));
  }
  if (skill.combo_orbs() > 0) {
    rows.push_back(EffectRow("Combo Orbs", std::to_string(skill.combo_orbs())));
  }
  // How often a skill that fights on its own goes off, which is most of what
  // it is worth. Stated in GMS seconds, as the data holds it -- the pacing
  // band stretches this and every other duration alike, so a figure the
  // player could hold a stopwatch to would say less than the ratio does.
  if (skill.cast_interval_seconds() > 0.0) {
    rows.push_back(EffectRow(
        "Fires Every", FormatSeconds(skill.cast_interval_seconds()) + "s"));
  }
  // How long the player swings something else for afterwards, which is what a
  // skill this much better than the usual swing costs.
  if (skill.cooldown_seconds() > 0.0) {
    rows.push_back(
        EffectRow("Cooldown", FormatSeconds(skill.cooldown_seconds()) + "s"));
  }
  return rows;
}

// A swing's damage: the per-strike percentage, how many strikes, and what the
// two come to against one enemy -- the total is what the player is really
// comparing between skills. One strike states the one figure and stops.
std::string SwingText(double per_hit, int lines) {
  // An unset lines means one strike, the same reading the damage chain takes.
  if (lines <= 0) {
    lines = 1;
  }
  if (lines == 1) {
    return FormatPercent(per_hit);
  }
  return FormatPercent(per_hit) + " x" + std::to_string(lines) + " = " +
         FormatPercent(per_hit * lines);
}

std::string DamageText(const Skill& skill, int level) {
  return SwingText(PercentAt(skill, &SkillEffect::skill_pct, level),
                   skill.lines());
}

// What the same swing lands on anything that is not a boss, for a skill
// carrying normal_skill_pct. Stated as the whole swing rather than as the
// bonus, because the bonus adds to the damage above per LINE and a row saying
// "+180%" beside a 900% swing reads as 1080% when it is twice that.
std::string NormalMonsterText(const Skill& skill, int level) {
  double bonus = PercentAt(skill, &SkillEffect::normal_skill_pct, level);
  if (bonus <= 0.0) {
    return "";
  }
  return SwingText(PercentAt(skill, &SkillEffect::skill_pct, level) + bonus,
                   skill.lines());
}

// The opening hit's line, or "" for a swing that has none. It lands on top of
// the swing's own damage, on one enemy of the several the swing reaches, so
// the row has to say which enemy or the two numbers read as alternatives.
std::string LeadText(const Skill& skill, int level) {
  double per_hit = PercentAt(skill, &SkillEffect::lead_pct, level);
  if (per_hit <= 0.0) {
    return "";
  }
  int lines = std::max(1, skill.lead_lines());
  std::string damage = FormatPercent(per_hit);
  if (lines > 1) {
    damage +=
        " x" + std::to_string(lines) + " = " + FormatPercent(per_hit * lines);
  }
  return damage + " (one enemy)";
}

// The plain lever rows of one effect, at `level`. `suffix` goes after every
// value: a weapon bonus uses it to name what has to be in hand, and the
// skill's own levers pass "" because the Requires row above says it once.
std::vector<ftxui::Element> LeverRows(const SkillEffect& base,
                                      const SkillEffect& per, int level,
                                      const std::string& suffix) {
  std::vector<ftxui::Element> rows;
  for (const FlatLever& lever : kFlatLevers) {
    int value = (base.*lever.fn)() + (per.*lever.fn)() * (level - 1);
    if (value == 0) {
      continue;
    }
    std::string text = "+" + std::to_string(value) + lever.unit;
    if (lever.countable && value != 1) {
      text += "s";
    }
    rows.push_back(EffectRow(lever.label, text + suffix));
  }
  for (const PercentLever& lever : kPercentLevers) {
    double value = (base.*lever.fn)() + (per.*lever.fn)() * (level - 1);
    if (value <= 0.0) {
      continue;
    }
    std::string sign = "";
    if (lever.sign == kPlus) {
      sign = "+";
    } else if (lever.sign == kMinus) {
      sign = "-";
    }
    rows.push_back(EffectRow(
        lever.label, sign + FormatPercent(value) + lever.unit + suffix));
  }
  for (const PercentLever& lever : kNumberLevers) {
    double value = (base.*lever.fn)() + (per.*lever.fn)() * (level - 1);
    if (lever.whole) {
      value = std::floor(value + kWholeEpsilon);
    }
    if (value <= 0.0) {
      continue;
    }
    rows.push_back(EffectRow(lever.label,
                             "+" + FormatNumber(value) + lever.unit + suffix));
  }
  return rows;
}

// Everything the skill grants at `level`. Empty for a skill whose real effect
// is something this game has no notion of.
std::vector<ftxui::Element> EffectRows(const Skill& skill, int level) {
  std::vector<ftxui::Element> rows;
  if (IsActive(skill) && PercentAt(skill, &SkillEffect::skill_pct, level) > 0) {
    rows.push_back(EffectRow("Damage", DamageText(skill, level)));
  }
  // Straight under the damage it is the other reading of, so the two totals
  // stand one over the other.
  std::string normal = NormalMonsterText(skill, level);
  if (!normal.empty()) {
    rows.push_back(EffectRow("Normal Monsters", normal));
  }
  // Under the swing's own damage, because it is the extra the swing opens with
  // rather than a second attack.
  for (ftxui::Element& row :
       WrappedEffectRows("Opening Hit", LeadText(skill, level))) {
    rows.push_back(std::move(row));
  }
  // Final Attack's chance and its damage are one fact, not two levers: neither
  // half says anything on its own, so they share a line.
  double proc = PercentAt(skill, &SkillEffect::final_attack_chance, level);
  if (proc > 0.0) {
    rows.push_back(
        EffectRow("Final Attack",
                  FormatPercent(proc) + " for " +
                      FormatPercent(PercentAt(
                          skill, &SkillEffect::final_attack_pct, level))));
  }
  for (ftxui::Element& row :
       LeverRows(skill.base(), skill.per_level(), level, "")) {
    rows.push_back(std::move(row));
  }
  // A weapon bonus reads as the lever it grants with the weapons it needs in
  // brackets: "Damage  +5% (Axe)". Flat, so it is read at level 1.
  for (const WeaponBonus& bonus : skill.weapon_bonus()) {
    std::string suffix =
        " (" + RequiredWeapons(bonus.required_equip_type()) + ")";
    for (ftxui::Element& row : LeverRows(
             bonus.effect(), SkillEffect::default_instance(), 1, suffix)) {
      rows.push_back(std::move(row));
    }
  }
  return rows;
}

// One "Level N" heading and the effects under it.
std::vector<ftxui::Element> LevelBlock(const Skill& skill, int level) {
  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(" Level " + std::to_string(level)));
  std::vector<ftxui::Element> effects = EffectRows(skill, level);
  if (effects.empty()) {
    // A skill whose whole effect is unmodelled still has levels to spend on,
    // and saying so is better than a heading standing over nothing.
    rows.push_back(EmptyState("no effect", kEffectIndent));
  }
  for (ftxui::Element& row : effects) {
    rows.push_back(std::move(row));
  }
  return rows;
}

}  // namespace

void SkillInspectPanel::SetSkill(const Skill* skill, int level) {
  skill_ = skill;
  level_ = level;
}

ftxui::Element SkillInspectPanel::Render() const {
  if (skill_ == nullptr) {
    return ThemedWindow(" Skill ", EmptyState("no skill"));
  }

  std::vector<ftxui::Element> rows;
  rows.push_back(CenteredRow(skill_->name()));
  rows.push_back(
      CenteredRow("Max Level: " + std::to_string(skill_->max_level())));

  rows.push_back(ThemedSeparator());
  for (const std::string& line :
       WrapText(skill_->description(), kContentWidth - 2)) {
    rows.push_back(ftxui::text(" " + line));
  }

  std::vector<ftxui::Element> invariant = InvariantRows(*skill_);
  if (!invariant.empty()) {
    rows.push_back(ThemedSeparator());
    for (ftxui::Element& row : invariant) {
      rows.push_back(std::move(row));
    }
  }

  // The level the skill is at, then what one more point would buy. An
  // unlearned skill has only the second; a maxed one has only the first.
  if (level_ > 0) {
    rows.push_back(ThemedSeparator());
    for (ftxui::Element& row : LevelBlock(*skill_, level_)) {
      rows.push_back(std::move(row));
    }
  }
  if (level_ < skill_->max_level()) {
    rows.push_back(ThemedSeparator());
    for (ftxui::Element& row : LevelBlock(*skill_, level_ + 1)) {
      rows.push_back(std::move(row));
    }
  }

  std::string title = " Passive ";
  if (IsActive(*skill_)) {
    title = " Active ";
  }
  return ThemedWindow(
      title, ftxui::vbox(std::move(rows)) |
                 ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth));
}

}  // namespace ms
