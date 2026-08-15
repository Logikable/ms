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

// A percentage lever and how it reads to the player. Usually the sign is the
// lever's direction rather than its stored value: a lever is stored positive,
// and one that cancels damage is a good thing shown as a subtraction. kSigned
// is for the lever that can be spent as well as bought.
enum Sign { kPlus, kMinus, kBare, kSigned };

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
    {"Boss Damage", &SkillEffect::boss_pct, kPlus, ""},
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
    // The one lever a skill can take away instead of grant: Reckless Hunt
    // sells DEF for damage, and a row that hid the price would be a lie.
    {"Defense", &SkillEffect::def_pct, kSigned, ""},
    // Pick Pocket's, rolled once per line the swing lands -- see the note on
    // the field. Bare, because it is a chance rather than a gain.
    {"Meso Drop Chance", &SkillEffect::meso_drop_chance, kBare, ""},
    // Last, and the only rows here that are not about a fight -- the same
    // place they take on the stats page, for the same reason.
    {"Meso Drop Rate", &SkillEffect::meso_pct, kPlus, ""},
    {"Additional EXP", &SkillEffect::exp_pct, kPlus, ""},
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

// "4th", for the swing an upgrade lands on. Only ever a small number here, but
// the teens are written out anyway rather than left as a trap for the day one
// of these runs to eleven.
std::string Ordinal(int n) {
  std::string suffix = "th";
  if (n % 100 < 11 || n % 100 > 13) {
    if (n % 10 == 1) {
      suffix = "st";
    } else if (n % 10 == 2) {
      suffix = "nd";
    } else if (n % 10 == 3) {
      suffix = "rd";
    }
  }
  return std::to_string(n) + suffix;
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

// The weapons a skill demands, as "Dagger" or "Sword / Axe". Empty when it can
// be swung with anything, which is what most skills want.
std::string RequiredWeapons(const google::protobuf::RepeatedField<int>& types) {
  std::vector<EquipType> demanded;
  for (int type : types) {
    demanded.push_back(static_cast<EquipType>(type));
  }
  return FormatWeaponList(demanded);
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

// The rows that hold at every level: what the skill asks for and how far a
// swing reaches. A skill with none of them gets no block at all.
//
// No row here counts seconds. The pacing band stretches every duration in the
// game alike, so a figure the player could hold a stopwatch to would not be
// the one printed -- and none of them is a choice they make anyway.
std::vector<ftxui::Element> InvariantRows(const Skill& skill) {
  std::vector<ftxui::Element> rows = RequirementRows(skill);
  if (skill.max_enemies() > 1) {
    rows.push_back(
        EffectRow("Enemies Hit", std::to_string(skill.max_enemies())));
  }
  if (skill.combo_orbs() > 0) {
    rows.push_back(EffectRow("Combo Orbs", std::to_string(skill.combo_orbs())));
  }
  // The one clock the player can feel, because they set it: their own
  // attacking. It is a count of swings rather than a duration.
  if (skill.attacks_per_cast() > 0) {
    rows.push_back(EffectRow(
        "Fires Every", std::to_string(skill.attacks_per_cast()) + " Attacks"));
  }
  // A skill that upgrades an attack says which attack and how often. Its reach
  // is stated too, unlike a turret's: this one is wider than the attack it
  // stands in for, so leaving it out would understate the upgrade. An empty
  // name is the skill upgrading its own attack, which has no name to give.
  if (skill.empowered_form().casts_per_trigger() > 0) {
    std::string upgraded = skill.boosts_skill_name().empty()
                               ? "attack"
                               : skill.boosts_skill_name();
    for (ftxui::Element& row : WrappedEffectRows(
             "Empowers",
             "Every " + Ordinal(skill.empowered_form().casts_per_trigger()) +
                 " " + upgraded)) {
      rows.push_back(std::move(row));
    }
    // Only when it differs from the reach stated above: the Sniper's form is
    // wider than the swing it stands in for, and leaving that out would
    // understate the upgrade -- but Creeping Toxin detonates exactly as far as
    // it spread, and a row repeating the one above it is noise.
    if (skill.empowered_form().max_enemies() > 1 &&
        skill.empowered_form().max_enemies() != skill.max_enemies()) {
      rows.push_back(
          EffectRow("Empowered Enemies",
                    std::to_string(skill.empowered_form().max_enemies())));
    }
  }
  // How long the player swings something else for afterwards, which is what a
  // skill this much better than the usual swing costs.
  if (skill.cooldown_seconds() > 0.0) {
    rows.push_back(
        EffectRow("Cooldown", FormatNumber(skill.cooldown_seconds()) + "s"));
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
    // A signed lever writes a row for anything but nothing at all; every other
    // one is unset when it is not positive.
    bool unset =
        lever.sign == kSigned ? std::abs(value) < kWholeEpsilon : value <= 0.0;
    if (unset) {
      continue;
    }
    std::string sign = "";
    if (lever.sign == kPlus) {
      sign = "+";
    } else if (lever.sign == kMinus) {
      sign = "-";
    } else if (lever.sign == kSigned) {
      sign = value > 0.0 ? "+" : "-";
    }
    rows.push_back(
        EffectRow(lever.label,
                  sign + FormatPercent(std::abs(value)) + lever.unit + suffix));
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

// What the skill itself does when it goes off: its damage, its healing, and
// the shapes a plain lever row cannot state.
std::vector<ftxui::Element> OwnEffectRows(const Skill& skill, int level) {
  std::vector<ftxui::Element> rows;
  if (IsActive(skill) && PercentAt(skill, &SkillEffect::skill_pct, level) > 0) {
    rows.push_back(EffectRow("Damage", DamageText(skill, level)));
  }
  // A fountain states both halves. Neither alone says what a point bought:
  // the pulse grows and the wait between pulses shortens together, so a page
  // showing only the pulse would understate every point after the first.
  double regen = PercentAt(skill, &SkillEffect::regen_pct, level);
  double regen_interval =
      PercentAt(skill, &SkillEffect::regen_interval_seconds, level);
  if (regen > 0.0 && regen_interval > 0.0) {
    rows.push_back(EffectRow("HP Recovered", FormatPercent(regen)));
    rows.push_back(
        EffectRow("Heals Every", FormatNumber(regen_interval) + "s"));
  }
  // What one meso is worth thrown back, read the way every other swing on this
  // page is read: per line, times the count. Meso Mastery's points land on a
  // line apiece, so the per-line figure is the one that has to be shown.
  double meso_hit = PercentAt(skill, &SkillEffect::meso_hit_pct, level);
  if (meso_hit > 0.0) {
    rows.push_back(
        EffectRow("Damage per Meso", SwingText(meso_hit, skill.lines())));
  }
  // A share of what the hit it copies dealt, not a share of a bare swing: a
  // 70% shadow behind a 210% line lands 147%. The row says "of each hit"
  // because the bare percentage reads as the flat figure to everyone.
  double mirror = PercentAt(skill, &SkillEffect::mirror_line_pct, level);
  if (mirror > 0.0) {
    rows.push_back(
        EffectRow("Shadow Damage", FormatPercent(mirror) + " of each hit"));
  }
  // Dispel's whole effect, and a promise the skill really keeps -- there is
  // just nothing in the game yet that inflicts what it lifts. Stated flatly,
  // because it is the same at every level: the points buy nothing more.
  if (skill.base().cures_conditions()) {
    rows.push_back(EffectRow("Cures", "All Conditions"));
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
  return rows;
}

// What lands beside the swing rather than as part of it, and what the skill
// hands to another skill in the book.
std::vector<ftxui::Element> ExtraAttackRows(const Skill& skill, int level) {
  std::vector<ftxui::Element> rows;
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
  // The own-clock half's damage, under the swing's own so the two read as one
  // skill with two ways of hurting things.
  if (skill.auto_mode().cast_interval_seconds() > 0.0) {
    rows.push_back(EffectRow(
        "Turret Damage",
        SwingText(skill.auto_mode().base().skill_pct() +
                      skill.auto_mode().per_level().skill_pct() * (level - 1),
                  skill.auto_mode().lines())));
  }
  // The upgraded attack's damage, beside the permanent bonus below it: one
  // skill that strengthens another twice over, so both halves read together.
  // Its own normal-monster reading follows, for the same reason the ordinary
  // attack's does -- two numbers for the swing above, two for this one.
  if (skill.empowered_form().casts_per_trigger() > 0) {
    const EmpoweredForm& form = skill.empowered_form();
    double per_hit =
        form.base().skill_pct() + form.per_level().skill_pct() * (level - 1);
    rows.push_back(
        EffectRow("Empowered Damage", SwingText(per_hit, form.lines())));
    double normal = form.base().normal_skill_pct() +
                    form.per_level().normal_skill_pct() * (level - 1);
    if (normal > 0.0) {
      rows.push_back(EffectRow("Empowered Normal",
                               SwingText(per_hit + normal, form.lines())));
    }
  }
  // What this skill hands another one. Named in the value rather than used as
  // the label, so a long skill name wraps instead of being cut to the label
  // column -- and so the row reads as a sentence about somewhere else.
  double boost = PercentAt(skill, &SkillEffect::boosted_skill_pct, level);
  if (boost > 0.0 && !skill.boosts_skill_name().empty()) {
    for (ftxui::Element& row :
         WrappedEffectRows("Boosts", skill.boosts_skill_name() + " +" +
                                         FormatPercent(boost))) {
      rows.push_back(std::move(row));
    }
  }
  return rows;
}

// Everything the skill grants at `level`. Empty for a skill whose real effect
// is something this game has no notion of.
std::vector<ftxui::Element> EffectRows(const Skill& skill, int level) {
  std::vector<ftxui::Element> rows;
  for (ftxui::Element& row : OwnEffectRows(skill, level)) {
    rows.push_back(std::move(row));
  }
  for (ftxui::Element& row : ExtraAttackRows(skill, level)) {
    rows.push_back(std::move(row));
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
