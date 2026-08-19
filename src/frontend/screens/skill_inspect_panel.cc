#include "src/frontend/screens/skill_inspect_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character_stats.h"
#include "src/combat/damage.h"
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
    // Maple Warrior's, and the only row charged against what the player spent
    // rather than against a total the game knows.
    {"Stats from AP", &SkillEffect::ap_stat_pct, kPlus, ""},
    {"Heal", &SkillEffect::heal_pct, kPlus, " HP"},
    {"Heal per Attack", &SkillEffect::hp_recover_pct, kPlus, " HP"},
    {"Elemental Resist", &SkillEffect::elemental_resistance, kPlus, ""},
    {"Buff Duration", &SkillEffect::buff_duration_pct, kPlus, ""},
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
    // The wait between one revival and the next. Named for what it buys
    // rather than for the clock, so the row states the effect too: nothing
    // else on the page says the skill revives at all. No sign, and it
    // SHORTENS as the skill is levelled.
    {"Revives Every", &SkillEffect::revive_cooldown_seconds, kBare, "s"},
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
// What an empowered form upgrades, as the page names it. A form carried by a
// skill with no attack of its own and no skill named is a data error the
// catalog test catches; here it reads as the skill's own attack.
std::string EmpoweredTarget(const Skill& skill, const EmpoweredForm& form) {
  if (!form.skill_name().empty()) {
    return form.skill_name();
  }
  return skill.boosts_skill_name().empty() ? "attack"
                                           : skill.boosts_skill_name();
}

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
std::string FormatNumber(double value, int decimals = 1) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.*f", decimals, value);
  std::string s = buf;
  // A whole number is written whole, and a shorter fraction keeps only the
  // digits it needs: 0.35 stays 0.35 where 70.0 is just 70.
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') {
      s.pop_back();
    }
    if (s.back() == '.') {
      s.pop_back();
    }
  }
  return s;
}

// How far one attack reaches and how often it comes. The skill's own swing and
// each of its own-clock halves say it in these same words, so two halves that
// reach differently can be told apart at a glance.
std::string ReachText(int enemies, double clock) {
  return std::to_string(enemies) +
         (enemies == 1 ? " enemy every " : " enemies every ") +
         FormatNumber(clock, 2) + "s";
}

// The wait before a skill can be swung again, at `level`. What a landed hit
// takes off that wait rides the same row: it is the same clock, and a row of
// its own read as a second one.
std::string CooldownText(const Skill& skill, int level) {
  std::string wait = FormatNumber(CooldownAt(skill, level)) + "s";
  if (skill.buff().cooldown_reduction_seconds() > 0.0) {
    wait += ", -" + FormatNumber(skill.buff().cooldown_reduction_seconds(), 2) +
            "s per hit";
  }
  return wait;
}

// The rows that hold at every level: what the skill asks for and how far a
// swing reaches. A skill with none of them gets no block at all.
//
// No row here counts seconds. The pacing band stretches every duration in the
// game alike, so a figure the player could hold a stopwatch to would not be
// the one printed -- and none of them is a choice they make anyway.
std::vector<ftxui::Element> InvariantRows(const Skill& skill) {
  std::vector<ftxui::Element> rows = RequirementRows(skill);
  // A skill with a clock of its own states it where it states its reach: the
  // two together are the shape of it, and a row holding only a number of
  // seconds is a row spent on bookkeeping.
  //
  // Only a clock the weapon cannot hurry is stated -- a skill on its own
  // interval, or a key-down one whose delay is fixed. An ordinary swing's
  // delay is scaled by the weapon's speed stage, so a single figure here
  // would be wrong for half the weapons that can swing it.
  int enemies = std::max(1, skill.max_enemies());
  double clock = skill.cast_interval_seconds();
  if (clock <= 0.0 && skill.fixed_delay() && skill.base_delay_ms() > 0) {
    clock = skill.base_delay_ms() / 1000.0;
  }
  if (clock > 0.0) {
    rows.push_back(EffectRow("Attacks", ReachText(enemies, clock)));
  } else if (skill.max_enemies() > 1) {
    // An arrow that gains as it travels states the gain beside the reach, the
    // two being one fact: the reach is how far the gain compounds.
    std::string reach = std::to_string(skill.max_enemies());
    if (skill.pierce_gain_pct() > 0.0) {
      reach += ", +" + FormatPercent(skill.pierce_gain_pct()) + " each";
    }
    rows.push_back(EffectRow("Enemies Hit", reach));
  }
  // Each own-clock half states its own reach beside the swing's. Revenge of the
  // Evil Eye is why: its auras land 20 strikes on 3 enemies where the volley
  // fired with them reaches 10, and the damage rows alone would read as one
  // number simply being twice the other.
  for (const AutoMode& mode : skill.auto_mode()) {
    if (mode.cast_interval_seconds() <= 0.0) {
      continue;
    }
    rows.push_back(
        EffectRow(mode.label(), ReachText(std::max(1, mode.max_enemies()),
                                          mode.cast_interval_seconds())));
  }
  // A ring that never grows is stated once here. One that does is a thing a
  // point buys, so it reads at the level instead -- the same split the
  // cooldown takes, and for the same reason.
  if (skill.combo_orbs() > 0 && skill.combo_orbs_per_level() <= 0.0) {
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
  for (const EmpoweredForm& form : skill.empowered_form()) {
    if (form.casts_per_trigger() <= 0) {
      continue;
    }
    for (ftxui::Element& row : WrappedEffectRows(
             "Empowers", "Every " + Ordinal(form.casts_per_trigger()) + " " +
                             EmpoweredTarget(skill, form))) {
      rows.push_back(std::move(row));
    }
    // A mark on each enemy is a different promise from a count on the swing --
    // five that one enemy took, rather than five swings -- and only a row of
    // its own says which of the two the number above is.
    if (form.brands_each_enemy()) {
      rows.push_back(EffectRow("Marks", "Each Enemy Hit"));
    }
    // Only when it differs from the reach stated above: the Sniper's form is
    // wider than the swing it stands in for, and leaving that out would
    // understate the upgrade -- but Creeping Toxin detonates exactly as far as
    // it spread, and a row repeating the one above it is noise.
    if (!form.brands_each_enemy() && form.max_enemies() > 1 &&
        form.max_enemies() != skill.max_enemies()) {
      rows.push_back(
          EffectRow("Empowered Enemies", std::to_string(form.max_enemies())));
    }
  }
  // How long the player swings something else for afterwards, which is what a
  // skill this much better than the usual swing costs. A wait that shortens as
  // the skill is taught is not invariant, so it waits for the level block.
  if (skill.cooldown_seconds() > 0.0 &&
      skill.cooldown_seconds_per_level() == 0.0) {
    rows.push_back(EffectRow("Cooldown", CooldownText(skill, 1)));
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
                   SkillLinesAt(skill, level));
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
                   SkillLinesAt(skill, level));
}

// What one skill hands another that is not damage, at `level`: strikes on
// every swing, enemies on its reach, a faster clock, or several. "" when it
// grants none of them.
std::string BoostText(const SkillBoost& boost, int level) {
  // Read exactly as the line ladder is, so the level a skill widens at is the
  // level its data names.
  constexpr double kEnemyEpsilon = 1e-9;
  std::string gains;
  if (boost.lines() > 0) {
    gains = "+" + std::to_string(boost.lines()) +
            (boost.lines() == 1 ? " Strike" : " Strikes");
  }
  int enemies =
      boost.max_enemies() +
      static_cast<int>(std::floor(boost.max_enemies_per_level() * (level - 1) +
                                  kEnemyEpsilon));
  if (enemies > 0) {
    if (!gains.empty()) {
      gains += ", ";
    }
    gains +=
        "+" + std::to_string(enemies) + (enemies == 1 ? " Enemy" : " Enemies");
  }
  // The new clock rather than the change to it: what replaces cannot be read
  // as a delta, and the target's own page states the same figure the same way.
  if (boost.attacks_per_cast() > 0) {
    if (!gains.empty()) {
      gains += ", ";
    }
    gains += "every " + std::to_string(boost.attacks_per_cast()) + " attacks";
  }
  return gains;
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
// The damage rows for hits a swing lands beside its own: each on its own line,
// with what it is worth against an ordinary monster under it where that
// differs. Shared, because an empowered form lands them too.
std::vector<ftxui::Element> SwingHitRows(
    const google::protobuf::RepeatedPtrField<SwingHit>& hits, int level) {
  std::vector<ftxui::Element> rows;
  for (const SwingHit& hit : hits) {
    double per_hit =
        hit.base().skill_pct() + hit.per_level().skill_pct() * (level - 1);
    // A hit that crits more often says so on its own damage row: it is a fact
    // about this damage rather than a lever of the character's, and a row of
    // its own would read as one. Wrapped, since the note is the one thing that
    // can push a damage row past its column.
    std::string text = SwingText(per_hit, hit.lines());
    double crit =
        hit.base().crit_rate() + hit.per_level().crit_rate() * (level - 1);
    if (crit >= 1.0) {
      text += " (crit)";
    } else if (crit > 0.0) {
      text += " (" + FormatPercent(crit) + " crit)";
    }
    for (ftxui::Element& row : WrappedEffectRows(hit.label(), text)) {
      rows.push_back(std::move(row));
    }
    double bonus = hit.base().normal_skill_pct() +
                   hit.per_level().normal_skill_pct() * (level - 1);
    if (bonus > 0.0) {
      rows.push_back(EffectRow(hit.label() + " Normal",
                               SwingText(per_hit + bonus, hit.lines())));
    }
  }
  return rows;
}

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
    std::string sign = lever.sign == kBare ? "" : "+";
    rows.push_back(EffectRow(lever.label,
                             sign + FormatNumber(value) + lever.unit + suffix));
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
    rows.push_back(EffectRow(
        "HP Recovered",
        FormatPercent(regen) + " every " + FormatNumber(regen_interval) + "s"));
  }
  // What one meso is worth thrown back, read the way every other swing on this
  // page is read: per line, times the count. Meso Mastery's points land on a
  // line apiece, so the per-line figure is the one that has to be shown.
  double meso_hit = PercentAt(skill, &SkillEffect::meso_hit_pct, level);
  if (meso_hit > 0.0) {
    rows.push_back(EffectRow("Damage per Meso",
                             SwingText(meso_hit, SkillLinesAt(skill, level))));
  }
  // A share of what the hit it copies dealt, not a share of a bare swing: a
  // 70% shadow behind a 210% line lands 147%. The row says "of each hit"
  // because the bare percentage reads as the flat figure to everyone.
  double mirror = PercentAt(skill, &SkillEffect::mirror_line_pct, level);
  if (mirror > 0.0) {
    rows.push_back(
        EffectRow("Shadow Damage", FormatPercent(mirror) + " of each hit"));
  }
  // A strike on every swing the character already lands more than once. Worth
  // most on the shortest of them, which the row cannot say and the player will
  // work out the first time they read a nine-line skill.
  int strikes = skill.base().bonus_attack_lines() +
                skill.per_level().bonus_attack_lines() * (level - 1);
  if (strikes > 0) {
    rows.push_back(EffectRow("Extra Strike", "+" + std::to_string(strikes) +
                                                 " on every multi-hit skill"));
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
  // The other hit the same swing lands, and its own reading against an
  // ordinary monster under it -- the pair reads exactly as the swing's own two
  // rows above, because that is what it is.
  for (ftxui::Element& row : SwingHitRows(skill.extra_hit(), level)) {
    rows.push_back(std::move(row));
  }
  // Under the swing's own damage, because it is the extra the swing opens with
  // rather than a second attack.
  for (ftxui::Element& row :
       WrappedEffectRows("Opening Hit", LeadText(skill, level))) {
    rows.push_back(std::move(row));
  }
  return rows;
}

// The burn a swing leaves, as one row: what a tick is worth, how often it
// comes and how long it lasts. All three on the damage row, because none of
// them says anything on its own and a clock never gets a row of its own here.
std::string DotText(const Dot& dot, int level) {
  double per_tick =
      dot.base().skill_pct() + dot.per_level().skill_pct() * (level - 1);
  return SwingText(per_tick, dot.lines()) + " every " +
         FormatNumber(dot.interval_seconds(), 2) + "s for " +
         FormatNumber(dot.duration_seconds()) + "s";
}

// What lands beside the swing rather than as part of it, and what the skill
// hands to another skill in the book.
std::vector<ftxui::Element> ExtraAttackRows(const Skill& skill, int level) {
  std::vector<ftxui::Element> rows;
  // Under the swing's own damage, since it is what that swing left behind.
  if (skill.dot().interval_seconds() > 0.0) {
    std::string label =
        skill.dot().label().empty() ? std::string("Burn") : skill.dot().label();
    for (ftxui::Element& row :
         WrappedEffectRows(label, DotText(skill.dot(), level))) {
      rows.push_back(std::move(row));
    }
  }
  // Final Attack's chance and its damage are one fact, not two levers: neither
  // half says anything on its own, so they share a line.
  double proc = PercentAt(skill, &SkillEffect::final_attack_chance, level);
  if (proc > 0.0) {
    // Where it falls on one enemy the row has to say so: a player comparing it
    // with the warriors' would otherwise read it as worth several times more.
    // Wrapped, because that note is the one thing long enough to push the row
    // past its column, and clipped to a comma so Blizzard's own fits on a line.
    std::string reach = skill.final_attack_single_enemy() ? ", one enemy" : "";
    std::string text =
        FormatPercent(proc) + " for " +
        FormatPercent(PercentAt(skill, &SkillEffect::final_attack_pct, level)) +
        reach;
    for (ftxui::Element& row : WrappedEffectRows("Final Attack", text)) {
      rows.push_back(std::move(row));
    }
  }
  // Each own-clock half's damage, under the swing's own so they read as one
  // skill with several ways of hurting things. Every one names itself, under
  // the same name its reach row above carries.
  for (const AutoMode& mode : skill.auto_mode()) {
    if (mode.cast_interval_seconds() <= 0.0) {
      continue;
    }
    rows.push_back(EffectRow(
        mode.label(), SwingText(mode.base().skill_pct() +
                                    mode.per_level().skill_pct() * (level - 1),
                                mode.lines())));
  }
  // The upgraded attack's damage, beside the permanent bonus below it: one
  // skill that strengthens another twice over, so both halves read together.
  // Its own normal-monster reading follows, for the same reason the ordinary
  // attack's does -- two numbers for the swing above, two for this one.
  for (const EmpoweredForm& form : skill.empowered_form()) {
    if (form.casts_per_trigger() <= 0) {
      continue;
    }
    // One form is "Empowered Damage" and needs no more; several have to say
    // which swing each belongs to, so they take the upgraded skill's name.
    std::string label = skill.empowered_form_size() > 1
                            ? EmpoweredTarget(skill, form)
                            : std::string("Empowered Damage");
    double per_hit =
        form.base().skill_pct() + form.per_level().skill_pct() * (level - 1);
    rows.push_back(EffectRow(label, SwingText(per_hit, form.lines())));
    double normal = form.base().normal_skill_pct() +
                    form.per_level().normal_skill_pct() * (level - 1);
    if (normal > 0.0) {
      rows.push_back(EffectRow("Empowered Normal",
                               SwingText(per_hit + normal, form.lines())));
    }
    // What the upgraded swing lands beside itself, under the swing it belongs
    // to -- the explosion at the end of an arrow's flight, the mark it spends.
    for (ftxui::Element& row : SwingHitRows(form.extra_hit(), level)) {
      rows.push_back(std::move(row));
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
  // The same sentence for what it hands another skill that is not damage: a
  // strike on every swing, a wider reach, or both. One row per skill named,
  // because two skills granted different things cannot share a row.
  for (const SkillBoost& granted : skill.boost()) {
    std::string gains = BoostText(granted, level);
    if (gains.empty()) {
      continue;
    }
    for (ftxui::Element& row :
         WrappedEffectRows("Boosts", granted.skill_name() + " " + gains)) {
      rows.push_back(std::move(row));
    }
  }
  return rows;
}

// A heading over one half of a skill that has two. Orange and green are the
// skill list's own tags for active and passive (panel_util's TagFor), so the
// two halves are told apart here by the colours the player learned there.
// Only the heading is coloured: a value that is always coloured says nothing.
ftxui::Element SectionRow(const std::string& label, ftxui::Color color) {
  return ftxui::text(" " + label) | ftxui::color(color);
}

// What a timed buff grants, headed by how long it stands. The wait for the
// next one is the skill's own Cooldown row, above. No row here says "while
// up" -- the heading says it once for all of them.
std::vector<ftxui::Element> BuffRows(const Skill& skill, int level) {
  std::vector<ftxui::Element> rows;
  const Buff& buff = skill.buff();
  if (buff.duration_seconds() <= 0.0) {
    return rows;
  }
  rows.push_back(SectionRow(
      "Active for " +
          FormatNumber(buff.duration_seconds() +
                       buff.duration_seconds_per_level() * (level - 1)) +
          "s",
      kOrange));
  // The heal is handed over once, when the buff goes up -- so it is stated on
  // its own rather than among the levers that hold for as long as it stands.
  SkillEffect base = buff.base();
  SkillEffect per = buff.per_level();
  double heal = base.heal_pct() + per.heal_pct() * (level - 1);
  if (heal > 0.0) {
    rows.push_back(
        EffectRow("Heal on Cast", "+" + FormatPercent(heal) + " HP"));
  }
  base.clear_heal_pct();
  per.clear_heal_pct();
  for (ftxui::Element& row : LeverRows(base, per, level, "")) {
    rows.push_back(std::move(row));
  }
  // What the buff bleeds, on one row: its damage and its clock are one fact,
  // and the enemies it reaches are the ones the swing above already states.
  const BuffPulse& pulse = buff.pulse();
  if (pulse.cast_interval_seconds() > 0.0) {
    rows.push_back(EffectRow(
        pulse.label(),
        SwingText(pulse.base().skill_pct() +
                      pulse.per_level().skill_pct() * (level - 1),
                  pulse.lines()) +
            " every " + FormatNumber(pulse.cast_interval_seconds(), 2) + "s"));
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
  // A skill with a timed buff grants two different things, and one of them
  // lapses: what it is holding up goes first, then what it keeps. Both are
  // headed, because "Ignore Defense" is otherwise the same row twice. A skill
  // with only one half needs neither heading and gets neither.
  std::vector<ftxui::Element> permanent =
      LeverRows(skill.base(), skill.per_level(), level, "");
  std::vector<ftxui::Element> buff = BuffRows(skill, level);
  for (ftxui::Element& row : buff) {
    rows.push_back(std::move(row));
  }
  if (!buff.empty() && !permanent.empty()) {
    rows.push_back(SectionRow("Passive", kGreen));
  }
  for (ftxui::Element& row : permanent) {
    rows.push_back(std::move(row));
  }
  // A wait that shortens as the skill is taught is one of the things a point
  // buys, so it is read at the level like the rest. The waits that never move
  // are stated once, above the divider. A growing ring of Combo Orbs splits
  // the same way.
  if (skill.cooldown_seconds() > 0.0 &&
      skill.cooldown_seconds_per_level() != 0.0) {
    rows.push_back(EffectRow("Cooldown", CooldownText(skill, level)));
  }
  if (skill.combo_orbs_per_level() > 0.0) {
    rows.push_back(
        EffectRow("Combo Orbs", std::to_string(ComboOrbsAt(skill, level))));
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

void SkillInspectPanel::SetSkill(const Skill* skill, int learned, int bonus,
                                 Levels levels) {
  skill_ = skill;
  level_ = learned;
  bonus_ = bonus;
  levels_ = levels;
}

std::vector<ftxui::Element> SkillInspectPanel::BuildRows() const {
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

  // Two blocks, and which two is the whole of the difference between the
  // modes: the level the skill is at and what one more point would buy, or
  // the first level and the last. A skill with one level has one block either
  // way, and so does a maxed or an unlearned one.
  //
  // Both levels are the lent ones -- what the skill is actually worth now,
  // and what it would be worth with one more point in it. Whether there is a
  // point left to spend is the LEARNED level's business, which is why the two
  // are asked separately.
  int first = 1;
  int second = skill_->max_level();
  bool has_second = second > first;
  if (levels_ == kLearned) {
    first = LevelWithBonus(*skill_, level_, bonus_);
    second = LevelWithBonus(*skill_, level_ + 1, bonus_);
    // A point that buys nothing gets no block: the lent levels can already
    // have carried the skill to the ceiling the next one would reach.
    has_second = level_ < skill_->max_level() && second > first;
  }
  if (first > 0) {
    rows.push_back(ThemedSeparator());
    for (ftxui::Element& row : LevelBlock(*skill_, first)) {
      rows.push_back(std::move(row));
    }
  }
  if (has_second) {
    rows.push_back(ThemedSeparator());
    for (ftxui::Element& row : LevelBlock(*skill_, second)) {
      rows.push_back(std::move(row));
    }
  }
  return rows;
}

int SkillInspectPanel::VisibleRows(int total) const {
  if (max_rows_ <= 0) {
    return total;
  }
  // The two border rows are paid first, and at least one row is drawn however
  // small the budget: a card cut to nothing says less than a card cut short.
  return std::max(1, std::min(total, max_rows_ - 2));
}

void SkillInspectPanel::ScrollBy(int delta) {
  if (skill_ == nullptr) {
    return;
  }
  int total = static_cast<int>(BuildRows().size());
  int last = total - VisibleRows(total);
  offset_ = std::max(0, std::min(offset_ + delta, last));
}

ftxui::Element SkillInspectPanel::Render() const {
  if (skill_ == nullptr) {
    return ThemedWindow(" Skill ", EmptyState("no skill"));
  }

  std::vector<ftxui::Element> rows = BuildRows();
  int total = static_cast<int>(rows.size());
  int visible = VisibleRows(total);
  // Clamped here as well as in ScrollBy: the terminal can be made taller under
  // a card already scrolled to its foot, which leaves the old offset too far
  // down for the window it now has.
  int offset = std::max(0, std::min(offset_, total - visible));
  std::vector<ftxui::Element> shown(rows.begin() + offset,
                                    rows.begin() + offset + visible);

  std::string title = " Passive ";
  if (IsActive(*skill_)) {
    title = " Active ";
  }
  // The bar's column is held open whether or not there is anything to scroll,
  // so a card does not change width the moment it outgrows the terminal.
  return ThemedWindow(
      title, ftxui::hbox({
                 ftxui::vbox(std::move(shown)) |
                     ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth),
                 ScrollBar(total, offset, visible) |
                     ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1),
             }));
}

int TallestPreviewCardRows(const std::vector<const Skill*>& skills) {
  int rows = 0;
  SkillInspectPanel panel;
  for (const Skill* skill : skills) {
    if (skill == nullptr) {
      continue;
    }
    panel.SetSkill(skill, 0, 0, SkillInspectPanel::kPreview);
    ftxui::Element card = panel.Render();
    card->ComputeRequirement();
    rows = std::max(rows, card->requirement().min_y);
  }
  return rows;
}

}  // namespace ms
