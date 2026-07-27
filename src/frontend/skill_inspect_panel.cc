#include "src/frontend/skill_inspect_panel.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_util.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

constexpr int kContentWidth = 38;  // chars inside the window border
// Effect rows indent past the one-space border gutter, so they read as
// belonging to the "Level N" heading above them.
constexpr int kEffectIndent = 3;
constexpr int kEffectLabelWidth = 15;

// A percentage lever and how it reads to the player. The sign is the lever's
// direction, not its stored value: every lever is stored positive, and one
// that cancels damage is a good thing shown as a subtraction.
enum Sign { kPlus, kMinus, kBare };

struct PercentLever {
  const char* label;
  double (SkillEffect::*fn)() const;
  Sign sign;
};

// Percentage levers in display order. Damage is not here -- an attack's own
// percentage is its identity and gets a line of its own, above these.
const PercentLever kPercentLevers[] = {
    {"Max HP", &SkillEffect::max_hp_pct, kPlus},
    {"Max MP", &SkillEffect::max_mp_pct, kPlus},
    {"Critical Rate", &SkillEffect::crit_rate, kPlus},
    {"Damage Taken", &SkillEffect::damage_taken_pct, kMinus},
    {"Damage to MP", &SkillEffect::damage_to_mp_pct, kBare},
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

int FlatAt(const Skill& skill, int (SkillEffect::*fn)() const, int level) {
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

// The weapons an attack demands, as "Dagger" or "Dagger / Claw". Empty when
// the skill can be swung with anything, which is what most skills want.
std::string RequiredWeapons(const Skill& skill) {
  std::string result;
  for (int i = 0; i < skill.required_equip_type_size(); ++i) {
    std::string name =
        FormatEquipType(static_cast<EquipType>(skill.required_equip_type(i)));
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

// The rows that hold at every level: how far a swing reaches and what it must
// be held with. A passive has none of these and gets no block at all.
std::vector<ftxui::Element> InvariantRows(const Skill& skill) {
  std::vector<ftxui::Element> rows;
  if (skill.max_enemies() > 1) {
    rows.push_back(
        EffectRow("Enemies Hit", std::to_string(skill.max_enemies())));
  }
  std::string weapons = RequiredWeapons(skill);
  if (!weapons.empty()) {
    rows.push_back(EffectRow("Requires", weapons));
  }
  return rows;
}

// The damage line of an attack skill. A multi-line swing shows the per-strike
// percentage, how many strikes, and what the two come to against one enemy --
// the total is what the player is really comparing between skills.
std::string DamageText(const Skill& skill, int level) {
  double per_hit = PercentAt(skill, &SkillEffect::skill_pct, level);
  // An unset lines means one strike, the same reading the damage chain takes.
  int lines = skill.lines();
  if (lines <= 0) {
    lines = 1;
  }
  if (lines == 1) {
    return FormatPercent(per_hit);
  }
  return FormatPercent(per_hit) + " x" + std::to_string(lines) + " = " +
         FormatPercent(per_hit * lines);
}

// Everything the skill grants at `level`. Empty for a skill whose real effect
// is something this game has no notion of.
std::vector<ftxui::Element> EffectRows(const Skill& skill, int level) {
  std::vector<ftxui::Element> rows;
  if (IsActive(skill) && PercentAt(skill, &SkillEffect::skill_pct, level) > 0) {
    rows.push_back(EffectRow("Damage", DamageText(skill, level)));
  }
  for (const FlatLever& lever : kFlatLevers) {
    int value = FlatAt(skill, lever.fn, level);
    if (value == 0) {
      continue;
    }
    std::string text = "+" + std::to_string(value) + lever.unit;
    if (lever.countable && value != 1) {
      text += "s";
    }
    rows.push_back(EffectRow(lever.label, text));
  }
  for (const PercentLever& lever : kPercentLevers) {
    double value = PercentAt(skill, lever.fn, level);
    if (value <= 0.0) {
      continue;
    }
    std::string sign = "";
    if (lever.sign == kPlus) {
      sign = "+";
    } else if (lever.sign == kMinus) {
      sign = "-";
    }
    rows.push_back(EffectRow(lever.label, sign + FormatPercent(value)));
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
  rows.push_back(ftxui::text(skill_->name()) | ftxui::hcenter);
  rows.push_back(
      ftxui::text("Max Level: " + std::to_string(skill_->max_level())) |
      ftxui::hcenter);

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
