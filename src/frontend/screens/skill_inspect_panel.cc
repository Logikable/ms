#include "src/frontend/screens/skill_inspect_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "google/protobuf/repeated_ptr_field.h"
#include "src/character/character_stats.h"
#include "src/character/v_matrix.h"
#include "src/combat/damage.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/scroll_card.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// The narrowest width the card uses. Descriptions use GMS's own sentences, so
// it is wide enough to read them a clause at a time; a skill needing less keeps
// this width and gives the extra to its value column.
constexpr int kMinContentWidth = 58;
// The border on each side and the scroll bar's column, which the card keeps
// whether or not there is anything to scroll.
constexpr int kCardChrome = 3;
// Effect rows are indented past the one-space border gutter, so they read as
// belonging to the "Level N" heading above them.
constexpr int kEffectIndent = 3;
// The gap between a label and its value.
constexpr int kLabelGap = 2;
// The minimum width a value keeps when a label is too wide for both to fit.
// Beyond this the label gets a row of its own.
constexpr int kMinValueWidth = 8;

// A percentage field and how it is displayed. The sign shows the field's
// direction, not its stored value: one that reduces damage is stored positive
// and shown as a subtraction. kSigned is for a field that can be negative as
// well as positive.
enum Sign { kPlus, kMinus, kBare, kSigned };

// Tolerance for flooring a whole-number field: a per-level step that can't be
// represented exactly lands slightly under the level it should reach.
constexpr double kWholeEpsilon = 1e-9;

struct PercentLever {
  const char* label;
  double (SkillEffect::*fn)() const;
  Sign sign;
  // What the percentage applies to when it isn't the whole effect: a per-orb
  // bonus is worth five times what its row says.
  const char* unit;
  // Whether the field only takes whole numbers, so the page floors it as the
  // game does. Only the row that needs it sets it.
  bool whole;
};

// Percentage fields in display order. Damage isn't here: an attack's own
// percentage defines it and gets its own line above these.
const PercentLever kPercentLevers[] = {
    {"Max HP", &SkillEffect::max_hp_pct, kPlus, ""},
    {"Max MP", &SkillEffect::max_mp_pct, kPlus, ""},
    {"ATT", &SkillEffect::attack_pct, kPlus, ""},
    {"Damage", &SkillEffect::damage_pct, kPlus, ""},
    {"Final Damage", &SkillEffect::final_dmg_pct, kPlus, ""},
    {"Boss Damage", &SkillEffect::boss_pct, kPlus, ""},
    {"Normal Enemy Damage", &SkillEffect::normal_pct, kPlus, ""},
    {"Critical Damage", &SkillEffect::crit_dmg_per_crit_rate, kPlus,
     " of Critical Rate"},
    {"Ignore DEF", &SkillEffect::ied_pct, kPlus, ""},
    {"Final Damage", &SkillEffect::final_dmg_pct_per_combo_orb, kPlus,
     " per Combo Orb"},
    {"Boss Damage", &SkillEffect::boss_pct_per_combo_orb, kPlus,
     " per Combo Orb"},
    {"Combo Orb Effect", &SkillEffect::combo_orb_gain_pct, kPlus, ""},
    {"Critical Damage", &SkillEffect::crit_dmg_per_freeze_stack, kPlus,
     " per Freeze Stack"},
    {"Final Damage", &SkillEffect::final_dmg_pct_per_freeze_stack, kPlus,
     " per Freeze Stack"},
    {"Final Damage", &SkillEffect::final_dmg_pct_when_afflicted, kPlus,
     " on Frozen or Burning enemies"},
    {"Final Damage", &SkillEffect::final_dmg_pct_per_dot, kPlus,
     " per burn nearby"},
    {"Final Damage", &SkillEffect::final_dmg_pct_when_scarred, kPlus,
     " on Scarred enemies"},
    {"Scar Chance", &SkillEffect::scar_chance, kPlus, " per hit"},
    {"Damage", &SkillEffect::max_hp_damage_pct, kPlus, " of Max HP per line"},
    {"Ignore DEF", &SkillEffect::ied_pct_per_freeze_stack, kPlus,
     " per Freeze Stack"},
    {"Critical Rate", &SkillEffect::crit_rate, kPlus, ""},
    {"Critical Damage", &SkillEffect::crit_dmg, kPlus, ""},
    {"Mastery", &SkillEffect::mastery, kBare, ""},
    {"Damage Taken", &SkillEffect::damage_taken_pct, kMinus, ""},
    {"Dodge Chance", &SkillEffect::dodge_chance, kPlus, ""},
    // What the barrier removes from whatever is hitting. The monster pays it,
    // so it is shown as a subtraction.
    {"Enemy ATT", &SkillEffect::enemy_attack_pct, kMinus, ""},
    {"Enemy ATT", &SkillEffect::enemy_attack_pct_when_scarred, kMinus,
     " while Scarred"},
    {"Damage to MP", &SkillEffect::damage_to_mp_pct, kBare, ""},
    {"Reflected", &SkillEffect::damage_reflect_pct, kBare, ""},
    // Maple Warrior's, and the only row based on what the player spent rather
    // than a total the game computes.
    {"Stats from AP", &SkillEffect::ap_stat_pct, kPlus, ""},
    // Maple World Goddess's Blessing's, a percentage of that row rather than of
    // anything the character has, so it names the skill it multiplies.
    {"Maple Warrior", &SkillEffect::ap_stat_bonus_pct, kPlus, ""},
    {"Heal", &SkillEffect::heal_pct, kPlus, " HP"},
    {"Heal per Attack", &SkillEffect::hp_recover_pct, kPlus, " HP"},
    {"Elemental Resist", &SkillEffect::elemental_resistance, kPlus, ""},
    // The offensive one, also paid by the monster. Named for the enemy to keep
    // it apart from the row above, which is the character's own.
    {"Enemy Elem Resist", &SkillEffect::ier_pct, kMinus, ""},
    {"Buff Duration", &SkillEffect::buff_duration_pct, kPlus, ""},
    // The one field a skill can reduce instead of grant: Reckless Hunt trades
    // DEF for damage, and a row that hid the cost would be misleading.
    {"Defense", &SkillEffect::def_pct, kSigned, ""},
    // Pick Pocket's, rolled once per line the attack lands (see the field's
    // note). Shown without a sign because it is a chance rather than a gain.
    {"Meso Drop Chance", &SkillEffect::meso_drop_chance, kBare, ""},
    // The share of that chance this attack gives up: a cost, and shown as one.
    // Its Final Attack counterpart is in FinalAttackCutRow.
    {"Meso Drop Chance", &SkillEffect::meso_drop_cut, kMinus, ""},
    // Last, and the only rows here not about fighting, the same place they have
    // on the stats page, for the same reason.
    {"Meso Drop Rate", &SkillEffect::meso_pct, kPlus, ""},
    {"Item Drop Rate", &SkillEffect::item_drop_pct, kPlus, ""},
    {"Additional EXP", &SkillEffect::exp_pct, kPlus, ""},
};

// Fields that are plain counts rather than percentages. Both are doubles: one
// for a half point granted, the other for the fraction its per-level ladder
// climbs by.
const PercentLever kNumberLevers[] = {
    {"Status Resist", &SkillEffect::status_resistance, kPlus, ""},
    // Whole levels, stored as a fraction so the ladder can step. Floored for
    // display exactly as it is floored where it is used.
    {"Skill Levels", &SkillEffect::skill_level_bonus, kPlus, "", true},
    // Named for what it gives rather than the timer, so the row also states the
    // effect, since nothing else on the page says the skill revives. It
    // shortens as the skill levels up.
    {"Revives Every", &SkillEffect::revive_cooldown_seconds, kBare, "s"},
    // Named for the timer rather than the revival: this skill doesn't revive
    // anyone; it shortens the cooldown of the pact that does.
    {"Revive Cooldown", &SkillEffect::revive_cooldown_cut_seconds, kMinus, "s"},
};

struct FlatLever {
  const char* label;
  double (SkillEffect::*fn)() const;
  // What the number counts, when it isn't the stat itself. "" for a plain
  // total; a stage or a per-character-level grant needs a unit.
  const char* unit;
  // Whether `unit` is something counted, and so takes an "s" for any number but
  // one. "per level" isn't; it says when, not how many.
  bool countable;
};

const FlatLever kFlatLevers[] = {
    {"DEF", &SkillEffect::def, "", false},
    {"ATT", &SkillEffect::attack, "", false},
    {"MATT", &SkillEffect::magic_attack, "", false},
    {"ATT", &SkillEffect::attack_per_combo_orb, " per Combo Orb", false},
    // Glacial Fury's pair. The magic attack applies only to ice attacks, which
    // the row says because nothing else on the page does.
    {"Freeze Stacks", &SkillEffect::freeze_stack_cap_bonus, "", false},
    {"Ice MATT", &SkillEffect::magic_attack_per_freeze_stack,
     " per Freeze Stack", false},
    {"DEF", &SkillEffect::def_per_combo_orb, " per Combo Orb", false},
    // Shown as orbs rather than a percentage, since the percentage depends on
    // the character's own per-orb bonus.
    {"Final Damage", &SkillEffect::final_dmg_combo_orbs, " Combo Orbs' worth",
     false},
    {"STR", &SkillEffect::str, "", false},
    {"DEX", &SkillEffect::dex, "", false},
    {"INT", &SkillEffect::int_, "", false},
    {"LUK", &SkillEffect::luk, "", false},
    {"Max HP", &SkillEffect::max_hp, "", false},
    {"Max MP", &SkillEffect::max_mp, "", false},
    {"Max HP", &SkillEffect::max_hp_per_level, " per level", false},
    {"Max MP", &SkillEffect::max_mp_per_level, " per level", false},
    {"Attack Speed", &SkillEffect::attack_speed, " stage", true},
    // The same row: the card shows what a stage is worth, and the cap it is
    // subject to belongs on the stats page.
    {"Attack Speed", &SkillEffect::uncapped_attack_speed, " stage", true},
};

// The value of a field at learned level L, in the same form the stats use.
double PercentAt(const Skill& skill, double (SkillEffect::*fn)() const,
                 int level) {
  return (skill.base().*fn)() + (skill.per_level().*fn)() * (level - 1);
}

// The same for a field counted in whole numbers: the ladder is computed as a
// fraction and floored where it is read, as everywhere else.
int FlatAt(const Skill& skill, double (SkillEffect::*fn)() const, int level) {
  return WholeValue(PercentAt(skill, fn, level));
}

// A fraction as a percentage to one decimal, with whole numbers left whole. It
// rounds rather than truncates: summed per-level steps land slightly under the
// round figure, and "15.9%" where the data says 16% is wrong. A value too small
// for one decimal gets a second, or "0%" would suggest the point gave nothing.
std::string FormatPercent(double frac) {
  int places = frac != 0.0 && std::fabs(frac) < 0.0005 ? 2 : 1;
  double scale = places == 2 ? 10000.0 : 1000.0;
  char buf[32];
  snprintf(buf, sizeof(buf), "%.*f", places,
           std::round(frac * scale) / (scale / 100.0));
  std::string s = buf;
  if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) {
    s.resize(s.size() - 2);
  }
  return s + "%";
}

// "4th", for the attack an upgrade applies on. The teens are handled anyway, in
// case one of these ever reaches eleven.
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

// What an empowered form upgrades, as the page names it. A form naming nothing
// on a skill with no attack is a data error the catalog test catches.
std::string EmpoweredTarget(const EmpoweredForm& form) {
  return form.skill_name().empty() ? "attack" : form.skill_name();
}

// A card row before the card knows its width: a label and its value, laid out
// once the widest of each is known.
struct Row {
  enum Kind {
    kEffect,  // label and value, in the card's two columns
    kProse,   // one paragraph, wrapped to the card and indented a space
    kWhole,   // an element that is a whole row: a heading or an empty state
    kRule,    // a section divider; the bar crosses one it scrolls beside
  };
  Kind kind = kEffect;
  std::string label;
  std::string value;
  ftxui::Element element;
};

// An effect row. An empty value writes no row, since there is nothing to say
// about a field the skill doesn't have.
Row EffectRow(std::string label, std::string value) {
  return {Row::kEffect, std::move(label), std::move(value), nullptr};
}

Row WholeRow(ftxui::Element element) {
  return {Row::kWhole, "", "", std::move(element)};
}

// Breaks a " / " list across lines without splitting an entry: "One-Handed
// Sword / Two-Handed Axe" broken between words would name the wrong weapon.
std::vector<std::string> WrapList(const std::string& text, int width) {
  const std::string kSeparator = " / ";
  std::vector<std::string> lines;
  std::string line;
  size_t i = 0;
  while (i <= text.size()) {
    size_t end = text.find(kSeparator, i);
    std::string entry =
        text.substr(i, end == std::string::npos ? std::string::npos : end - i);
    bool last = end == std::string::npos;
    std::string piece = last ? entry : entry + " /";
    if (!line.empty() &&
        static_cast<int>(line.size() + 1 + piece.size()) > width) {
      lines.push_back(line);
      line.clear();
    }
    if (!line.empty()) {
      line += " ";
    }
    line += piece;
    if (last) {
      break;
    }
    i = end + kSeparator.size();
  }
  if (!line.empty()) {
    lines.push_back(line);
  }
  return lines;
}

// Breaks `text` into lines that fit `width`, splitting only between words. A
// word longer than the column overflows rather than being cut. Lists break by
// entry instead (see WrapList).
std::vector<std::string> WrapText(const std::string& text, int width) {
  if (text.find(" / ") != std::string::npos) {
    return WrapList(text, width);
  }
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

// The weapons a skill requires, as "Dagger" or "Sword / Axe". Empty when any
// weapon works, which is true of most skills.
std::string RequiredWeapons(const google::protobuf::RepeatedField<int>& types) {
  std::vector<EquipType> demanded;
  for (int type : types) {
    demanded.push_back(static_cast<EquipType>(type));
  }
  return FormatWeaponList(demanded);
}

// What the skill requires before it can be used. The two read as a pair: a
// weapon in hand and a skill already learned are the same kind of condition, so
// they are laid out alike.
std::vector<Row> RequirementRows(const Skill& skill) {
  std::vector<Row> rows;
  rows.push_back(EffectRow("Required Weapon",
                           RequiredWeapons(skill.required_equip_type())));
  // The level a Hyper Skill unlocks at, which is its requirement instead of a
  // prerequisite skill.
  if (skill.required_level() > 0) {
    rows.push_back(
        EffectRow("Required Level", std::to_string(skill.required_level())));
  }
  if (!skill.has_required_skill()) {
    return rows;
  }
  // Built from the requirement rather than typed separately, so the text and
  // the rule the skills tab enforces can't drift apart.
  std::string required = skill.required_skill().skill_name() + " Lv. " +
                         std::to_string(skill.required_skill().level()) + "+";
  rows.push_back(EffectRow("Required Skill", required));
  return rows;
}

// What this skill replaces, for skills that include all of an earlier skill.
// Without this row the two would look like they stack.
std::vector<Row> ReplacesRows(const Skill& skill) {
  if (skill.supersedes_skill_name().empty()) {
    return {};
  }
  return {EffectRow("Replaces", skill.supersedes_skill_name())};
}

// The group whose effects this skill doesn't stack with. A weaker warning than
// the row above: these stack with everything else and only override each other.
// A group is named after the skill the others stand in for, so that skill gets
// no row, since "Sharp Eyes does not stack with Sharp Eyes" says nothing.
std::vector<Row> ExclusiveGroupRows(const Skill& skill) {
  if (skill.exclusive_group().empty() ||
      skill.exclusive_group() == skill.name()) {
    return {};
  }
  return {EffectRow("Does Not Stack With", skill.exclusive_group())};
}

// A plain number to one decimal, with whole numbers left whole. The same
// rounding as FormatPercent, for the same reason.
std::string FormatNumber(double value, int decimals = 1) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.*f", decimals, value);
  std::string s = buf;
  // A whole number is shown whole, and a shorter fraction keeps only the digits
  // it needs: 0.35 stays 0.35 while 70.0 becomes 70.
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

// How many enemies one attack hits. The attack and each own-clock half use the
// same wording, so two with different reach can be told apart.
std::string ReachText(int enemies) {
  return std::to_string(enemies) + (enemies == 1 ? " enemy" : " enemies");
}

std::string ReachText(int enemies, double clock) {
  return ReachText(enemies) + " every " + FormatNumber(clock, 2) + "s";
}

// How often an own-clock half triggers: its own interval in seconds, or a count
// of the character's own attacks. It names one or the other.
std::string ModeClockText(const AutoMode& mode) {
  if (mode.attacks_per_cast() > 0) {
    return " every " + std::to_string(mode.attacks_per_cast()) + " attacks";
  }
  return " every " + FormatNumber(mode.cast_interval_seconds(), 2) + "s";
}

// Whether a half triggers at all, which is whether it names a clock. One naming
// neither is treated as not triggering, as the fight treats it.
bool ModeFires(const AutoMode& mode) {
  return mode.cast_interval_seconds() > 0.0 || mode.attacks_per_cast() > 0;
}

// How often a pulse triggers, as the end of its row: its own interval, or the
// attack it follows.
std::string PulseClockText(const BuffPulse& pulse) {
  if (!pulse.paced_by_skill_name().empty()) {
    return " with every " + pulse.paced_by_skill_name();
  }
  return " every " + FormatNumber(pulse.cast_interval_seconds(), 2) + "s";
}

// The wait before a skill can be used again. What a landed hit reduces it by is
// on the same row, since it is one timer and a second row would look like two.
std::string CooldownText(const Skill& skill, int level) {
  std::string wait = FormatNumber(CooldownAt(skill, level)) + "s";
  if (skill.buff().cooldown_reduction_seconds() > 0.0) {
    wait += ", -" + FormatNumber(skill.buff().cooldown_reduction_seconds(), 2) +
            "s per hit";
  }
  return wait;
}

// Appends `from` to `into`. The row builders all return vectors, and a row is
// moved rather than copied.
void Append(std::vector<Row> from, std::vector<Row>& into) {
  for (Row& row : from) {
    into.push_back(std::move(row));
  }
}

// `rows` without the empty ones. Done here rather than at layout, so a block of
// only empty rows counts as empty and gets no divider or heading.
std::vector<Row> Speaking(std::vector<Row> rows) {
  std::vector<Row> kept;
  for (Row& row : rows) {
    if (row.kind == Row::kEffect && row.value.empty()) {
      continue;
    }
    kept.push_back(std::move(row));
  }
  return kept;
}

// What a scattered attack throws. A count that grows with the burns applied
// shows its range and what increases it, so a reader knows where a fight lands.
std::string ScatterText(const Scatter& scatter) {
  std::string text = std::to_string(scatter.hits());
  if (scatter.hits_per_dot() > 0.0) {
    text += "-" + std::to_string(scatter.max_hits());
  }
  text += " strikes";
  if (scatter.hits_per_dot() > 0.0) {
    text += ", +" + FormatNumber(scatter.hits_per_dot(), 2) + " per DoT stack";
  }
  if (scatter.repeat_final_dmg_pct() != 0.0) {
    text += ", repeats at " + FormatPercent(scatter.repeat_final_dmg_pct()) +
            " Final Damage";
  }
  if (scatter.max_hits_per_enemy() > 0) {
    text += ", up to " + std::to_string(scatter.max_hits_per_enemy()) +
            " per enemy";
  }
  return text;
}

std::vector<Row> ReachRows(const Skill& skill) {
  std::vector<Row> rows;
  // A skill with its own timer shows it alongside its reach, since together
  // they describe its shape. Only a timer attack speed can't shorten is shown:
  // an ordinary attack is scaled by the speed stage, so one number would be
  // wrong for half the weapons using it.
  int enemies = std::max(1, skill.max_enemies());
  double clock = skill.cast_interval_seconds();
  if (clock <= 0.0 && skill.fixed_delay() && skill.base_delay_ms() > 0) {
    clock = skill.base_delay_ms() / 1000.0;
  }
  if (clock > 0.0) {
    rows.push_back(EffectRow("Attacks", ReachText(enemies, clock)));
  } else if (skill.max_enemies() > 1) {
    // An arrow that gains damage as it travels shows the gain beside its reach,
    // since the two are one fact: the reach is how far the gain compounds.
    std::string reach = std::to_string(skill.max_enemies());
    if (skill.pierce_gain_pct() > 0.0) {
      reach += ", +" + FormatPercent(skill.pierce_gain_pct()) + " each";
    }
    rows.push_back(EffectRow("Enemies Hit", reach));
  }
  // A scattered attack gets its own row: the reach above is how far it spreads
  // before hitting targets twice, and the reduction is what hitting twice
  // costs.
  if (skill.scatter().hits() > 0) {
    rows.push_back(EffectRow("Scattered", ScatterText(skill.scatter())));
  }
  // Each own-clock half shows its reach beside the attack's. Otherwise an aura
  // hitting 3 enemies 20 times beside a volley reaching 10 would look like one
  // number being twice the other.
  for (const AutoMode& mode : skill.auto_mode()) {
    if (!ModeFires(mode)) {
      continue;
    }
    rows.push_back(EffectRow(
        mode.label(),
        ReachText(std::max(1, mode.max_enemies())) + ModeClockText(mode)));
  }
  return rows;
}

// Which attack this skill upgrades, how often, and how far the upgraded attack
// reaches when that differs from the attack it replaces.
std::vector<Row> EmpoweredRows(const Skill& skill) {
  std::vector<Row> rows;
  // A skill upgrading an attack says which and how often. An empty name means
  // the skill upgrades its own attack, which has no separate name.
  for (const EmpoweredForm& form : skill.empowered_form()) {
    if (form.casts_per_trigger() <= 0) {
      continue;
    }
    // An upgrade with no rate is unconditional; "Empowers" already says so, and
    // the rate is what distinguishes the other case.
    std::string how = form.casts_per_trigger() == 1
                          ? ""
                          : "Every " + Ordinal(form.casts_per_trigger()) + " ";
    rows.push_back(EffectRow("Empowers", how + EmpoweredTarget(form)));
    // A mark on each enemy is different from a count on the attack: five hits
    // on one enemy, not five attacks.
    if (form.brands_each_enemy()) {
      rows.push_back(EffectRow("Marks", "Each Enemy Hit"));
    }
    // Only a form with its own reach gets the row. One that states none reaches
    // as far as what it replaces, and repeating that is noise.
    if (!form.brands_each_enemy() && form.max_enemies() > 1) {
      rows.push_back(
          EffectRow("Empowered Enemies", std::to_string(form.max_enemies())));
    }
  }
  return rows;
}

// The element a tag names, for a row that has to say which attacks something
// affects. Only the two the page already shows can appear here.
std::string TagName(SkillTag tag) {
  switch (tag) {
    case SKILL_TAG_ICE:
      return "Ice";
    case SKILL_TAG_HOLY:
      return "Holy";
    default:
      return "Lightning";
  }
}

// The attack's element, where the page names one. Every other tag marks a
// category nothing here uses yet and has no element to show.
SkillTag ElementOf(const Skill& skill) {
  for (int i = 0; i < skill.tags_size(); ++i) {
    if (skill.tags(i) == SKILL_TAG_ICE ||
        skill.tags(i) == SKILL_TAG_LIGHTNING ||
        skill.tags(i) == SKILL_TAG_HOLY) {
      return skill.tags(i);
    }
  }
  return SKILL_TAG_UNSPECIFIED;
}

// Everything about a skill that is the same at every level.
//
// That an attack freezes, stuns or marks is for the description to say: rows
// here answer "how much", and the game's speed scaling stretches every status
// duration. What a mark is worth to the rest of the skill book does have a
// number.
std::vector<Row> ElementRows(const Skill& skill) {
  std::vector<Row> rows;
  if (ElementOf(skill) != SKILL_TAG_UNSPECIFIED) {
    rows.push_back(EffectRow("Element", TagName(ElementOf(skill))));
  }
  // What a stun gives the attacks that consume it. The row states the tag,
  // since a bonus with no tag is consumed by nothing and gets no row.
  if (skill.stun().final_dmg_pct() > 0.0 &&
      skill.stun().lifted_tag() != SKILL_TAG_UNSPECIFIED) {
    rows.push_back(EffectRow("Stunned Enemies",
                             "+" + FormatPercent(skill.stun().final_dmg_pct()) +
                                 " Final Damage from " +
                                 TagName(skill.stun().lifted_tag())));
  }
  // What consuming a mark gives the line that consumes it. Only one line gets
  // it, so the row says so; read as applying to the whole attack, it would be
  // ten times the real value.
  if (skill.mark().final_dmg_pct() > 0.0 &&
      skill.mark().lifted_tag() != SKILL_TAG_UNSPECIFIED) {
    rows.push_back(EffectRow("Marked Enemies",
                             "+" + FormatPercent(skill.mark().final_dmg_pct()) +
                                 " Final Damage to one " +
                                 TagName(skill.mark().lifted_tag()) + " hit"));
  }
  return rows;
}

// Rows that apply at every level: what the skill requires and how far an attack
// reaches. No row here shows seconds, since the game's speed scaling stretches
// every duration, so a number a player could time wouldn't match the one shown.
std::vector<Row> InvariantRows(const Skill& skill) {
  std::vector<Row> rows = RequirementRows(skill);
  Append(ReplacesRows(skill), rows);
  Append(ExclusiveGroupRows(skill), rows);
  Append(ElementRows(skill), rows);
  Append(ReachRows(skill), rows);
  // The maximum stack count. Shown here rather than at the level because the
  // cap never increases; what a point buys is what one stack is worth.
  if (skill.freeze_stack_cap() > 0) {
    rows.push_back(EffectRow(
        "Freeze Stacks", "Up to " + std::to_string(skill.freeze_stack_cap())));
  }
  // The same for the burns the drains count, whose cap never increases either.
  if (skill.dot_count_cap() > 0) {
    rows.push_back(EffectRow("Burns Counted",
                             "Up to " + std::to_string(skill.dot_count_cap())));
  }
  // A fixed orb count is shown once here; one that grows is what a point buys,
  // so it goes in the level block, split the same way as the cooldown.
  if (skill.combo_orbs() > 0 && skill.combo_orbs_per_level() <= 0.0) {
    rows.push_back(EffectRow("Combo Orbs", std::to_string(skill.combo_orbs())));
  }
  // The one timer the player can feel, because they control it: their own
  // attacks. It is a count of attacks rather than a duration.
  if (skill.attacks_per_cast() > 0) {
    rows.push_back(EffectRow(
        "Fires Every", std::to_string(skill.attacks_per_cast()) + " Attacks"));
  }
  // The other counted timer: how many monsters the player kills.
  if (skill.kills_per_cast() > 0) {
    rows.push_back(EffectRow(
        "Fires Every", std::to_string(skill.kills_per_cast()) + " Defeats"));
  }
  Append(EmpoweredRows(skill), rows);
  // How long the player uses other attacks afterwards, which is the cost of a
  // skill this strong. A cooldown that shortens with level isn't invariant and
  // goes in the level block.
  if (skill.cooldown_seconds() > 0.0 &&
      skill.cooldown_seconds_per_level() == 0.0) {
    rows.push_back(EffectRow("Cooldown", CooldownText(skill, 1)));
  }
  return Speaking(std::move(rows));
}

// An attack's damage: the per-line percentage, the lines, and the total on one
// enemy, since skills are compared by the total.
std::string SwingText(double per_hit, int lines, int casts = 1) {
  // An unset line count means one line, as the damage formula reads it.
  if (lines <= 0) {
    lines = 1;
  }
  if (casts <= 0) {
    casts = 1;
  }
  if (lines == 1 && casts == 1) {
    return FormatPercent(per_hit);
  }
  // In GMS's order (damage, lines, then strikes), because the total alone hides
  // which of the three changed.
  std::string text = FormatPercent(per_hit) + " x" + std::to_string(lines);
  if (casts > 1) {
    text += " x" + std::to_string(casts);
  }
  return text + " = " + FormatPercent(per_hit * lines * casts);
}

std::string DamageText(const Skill& skill, int level) {
  return SwingText(PercentAt(skill, &SkillEffect::skill_pct, level),
                   SkillLinesAt(skill, level), SkillCasts(skill));
}

// What the same attack deals to anything that isn't a boss. Shown as the whole
// attack: the bonus is added per line, so "+180%" beside a 900% attack would
// read as 1080% when it is twice that.
std::string NormalMonsterText(const Skill& skill, int level) {
  double bonus = PercentAt(skill, &SkillEffect::normal_skill_pct, level);
  double damage = PercentAt(skill, &SkillEffect::skill_pct, level);
  // Nothing to compare against. Meso Explosion shows its points on a thrown
  // coin rather than an attack, and OwnEffectRows prints that pair.
  if (bonus <= 0.0 || damage <= 0.0) {
    return "";
  }
  return SwingText(damage + bonus, SkillLinesAt(skill, level),
                   SkillCasts(skill));
}

// Adds one clause to a comma-separated list, since a boost granting several
// things has to name each.
void AppendGain(const std::string& text, std::string& gains) {
  if (text.empty()) {
    return;
  }
  if (!gains.empty()) {
    gains += ", ";
  }
  gains += text;
}

// What a boost adds to the shape of the attack it names: lines, reach, cooldown
// and timer.
std::string StructureBoostText(const SkillBoost& boost, int level) {
  // Computed exactly as the line ladder is, so the level where a skill gains
  // reach is the level its data says.
  constexpr double kEnemyEpsilon = 1e-9;
  std::string gains;
  if (boost.lines() > 0) {
    gains = "+" + std::to_string(boost.lines()) +
            (boost.lines() == 1 ? " Strike" : " Strikes");
  }
  // Separate from the line above because these are different strikes: "+1
  // Strike" alone would be read as the attack's own.
  if (boost.extra_hit_lines() > 0) {
    AppendGain("+" + std::to_string(boost.extra_hit_lines()) +
                   (boost.extra_hit_lines() == 1 ? " Strike" : " Strikes") +
                   " to its extra hits",
               gains);
  }
  int enemies =
      boost.max_enemies() +
      static_cast<int>(std::floor(boost.max_enemies_per_level() * (level - 1) +
                                  kEnemyEpsilon));
  if (enemies > 0) {
    AppendGain(
        "+" + std::to_string(enemies) + (enemies == 1 ? " Enemy" : " Enemies"),
        gains);
  }
  // The cooldown reduction as a percentage, as GMS states it. The seconds it
  // amounts to are on the target skill's own page, which shows its full ladder.
  if (boost.cooldown_pct() > 0.0) {
    AppendGain("-" + FormatPercent(boost.cooldown_pct()) + " Cooldown", gains);
  }
  // The new timer rather than the change: a replacement can't be read as a
  // difference, and the target's own page shows the same figure the same way.
  if (boost.attacks_per_cast() > 0) {
    AppendGain("every " + std::to_string(boost.attacks_per_cast()) + " attacks",
               gains);
  }
  // The hits it gives that attack, shown like the attack's own. Named, since
  // what it adds is a separate strike rather than more of the attack.
  for (const SwingHit& hit : boost.extra_hit()) {
    std::string landed = SwingText(
        hit.base().skill_pct() + hit.per_level().skill_pct() * (level - 1),
        hit.lines(), SwingHitCasts(hit));
    if (hit.max_enemies() > 0) {
      landed += " on " + ReachText(hit.max_enemies());
    }
    AppendGain(hit.label().empty() ? landed : hit.label() + " " + landed,
               gains);
  }
  return gains;
}

// What a boost adds to the mark the named skill leaves and the buff it acts as.
// Separate from the field table, since neither is the attack and each has a
// timer.
std::string MarkBoostText(const SkillBoost& boost, int level) {
  std::string gains;
  double dot_pct =
      boost.dot_skill_pct() + boost.dot_skill_pct_per_level() * (level - 1);
  if (dot_pct != 0.0) {
    AppendGain((dot_pct > 0.0 ? "+" : "") + FormatPercent(dot_pct) +
                   " DoT Damage per Tick",
               gains);
  }
  // Seconds rather than a percentage, which is how GMS states it and the only
  // readable way: the target's page shows the whole ladder.
  double dot_seconds = boost.dot_duration_seconds() +
                       boost.dot_duration_seconds_per_level() * (level - 1);
  if (dot_seconds != 0.0) {
    AppendGain((dot_seconds > 0.0 ? "+" : "") + FormatNumber(dot_seconds) +
                   "s DoT Duration",
               gains);
  }
  if (boost.buff_duration_seconds() != 0.0) {
    AppendGain((boost.buff_duration_seconds() > 0.0 ? "+" : "") +
                   FormatNumber(boost.buff_duration_seconds()) + "s Duration",
               gains);
  }
  if (boost.shield_hits() != 0.0) {
    AppendGain((boost.shield_hits() > 0.0 ? "+" : "") +
                   FormatNumber(boost.shield_hits()) + " Blocked Attacks",
               gains);
  }
  if (boost.shield_boss_damage_taken_pct() != 0.0) {
    AppendGain((boost.shield_boss_damage_taken_pct() > 0.0 ? "+" : "") +
                   FormatPercent(boost.shield_boss_damage_taken_pct()) +
                   " Boss Damage Reduction",
               gains);
  }
  return gains;
}

// What one skill gives another that isn't damage: strikes, reach, a shorter
// cooldown, a longer buff, or fields only it has.
std::string BoostText(const SkillBoost& boost, int level) {
  std::string gains = StructureBoostText(boost, level);
  AppendGain(MarkBoostText(boost, level), gains);
  // The fields the boost gives only that skill, each named, since a sentence
  // granting two can't leave either out. In the effect rows' order.
  struct BoostLever {
    const char* label;
    double (SkillEffect::*fn)() const;
  };
  const BoostLever kBoostLevers[] = {
      // Points on the skill's own multiplier, which every strike uses, so the
      // row says per strike. The row below is the other kind of bonus: a share
      // of the character's damage that only this attack gets.
      {"Damage per Strike", &SkillEffect::skill_pct},
      {"Damage", &SkillEffect::damage_pct},
      {"Final Damage", &SkillEffect::final_dmg_pct},
      {"Boss Damage", &SkillEffect::boss_pct},
      {"Normal Enemy Damage", &SkillEffect::normal_pct},
      {"Ignore DEF", &SkillEffect::ied_pct},
      {"Critical Rate", &SkillEffect::crit_rate},
      {"Final Attack Rate", &SkillEffect::final_attack_chance},
  };
  // Shown as the multiplier GMS uses, not the points it amounts to: the row
  // means the target's own rate doubles, whatever that rate is.
  if (boost.final_attack_chance_mult() > 0.0) {
    AppendGain("x" + FormatNumber(boost.final_attack_chance_mult()) +
                   " Final Attack Rate",
               gains);
  }
  for (const BoostLever& lever : kBoostLevers) {
    double value = (boost.effect().*lever.fn)() +
                   (boost.effect_per_level().*lever.fn)() * (level - 1);
    if (value == 0.0) {
      continue;
    }
    // A field can be granted negative, and FormatPercent writes the minus
    // itself, so only the plus has to be added back.
    AppendGain(
        (value > 0.0 ? "+" : "") + FormatPercent(value) + " " + lever.label,
        gains);
  }
  return gains;
}

// The opening hit's line. It lands on top of the attack's damage, on one of the
// enemies it reaches, so the row says which; otherwise the two would look like
// alternatives.
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
  // How many of the attack's enemies it hits: one for the opening hit every
  // rogue lands, more for an arrow's fragment, which bounces onto several.
  int enemies = std::max(1, skill.lead_enemies());
  if (enemies == 1) {
    return damage + " (one enemy)";
  }
  return damage + " (" + std::to_string(enemies) + " enemies)";
}

// How many times an attack carrying these hits lands them: once per strike it
// is split into, since each of those counts as a whole attack in the fight. 1
// for an attack whose strikes land together. See Skill.cast_interval_ms.
int SequencedCasts(const Skill& skill) {
  return skill.cast_interval_ms() > 0 ? SkillCasts(skill) : 1;
}

// The damage rows for hits an attack lands alongside its own, each on its own
// line with its normal-monster value below it. Shared with empowered forms.
// `swing_casts` is how many times the attack repeats, which these repeat with.
std::vector<Row> SwingHitRows(
    const google::protobuf::RepeatedPtrField<SwingHit>& hits, int level,
    int swing_casts = 1) {
  std::vector<Row> rows;
  for (const SwingHit& hit : hits) {
    double per_hit =
        hit.base().skill_pct() + hit.per_level().skill_pct() * (level - 1);
    // A hit with a higher crit rate says so on its own damage row, since it is
    // a fact about this damage rather than a character stat. Wrapped, since the
    // note is the one thing that can push a damage row past its column.
    std::string text =
        SwingText(per_hit, hit.lines(), SwingHitCasts(hit) * swing_casts);
    // A half that picks its own targets says so, since reading it against the
    // attack's reach would be wrong whichever way the two differ.
    if (hit.max_enemies() > 0) {
      text += ", " + ReachText(hit.max_enemies());
    }
    double crit =
        hit.base().crit_rate() + hit.per_level().crit_rate() * (level - 1);
    if (crit >= 1.0) {
      text += " (crit)";
    } else if (crit > 0.0) {
      text += " (" + FormatPercent(crit) + " crit)";
    }
    // Final damage only this half has, on its own damage row for the same
    // reason as the crit rate: the attack's row shows the character's stat.
    double lifted = hit.base().final_dmg_pct() +
                    hit.per_level().final_dmg_pct() * (level - 1);
    if (lifted > 0.0) {
      text += " (+" + FormatPercent(lifted) + " final)";
    }
    rows.push_back(EffectRow(hit.label(), text));
    double bonus = hit.base().normal_skill_pct() +
                   hit.per_level().normal_skill_pct() * (level - 1);
    if (bonus > 0.0) {
      rows.push_back(EffectRow(hit.label() + " Normal",
                               SwingText(per_hit + bonus, hit.lines(),
                                         SwingHitCasts(hit) * swing_casts)));
    }
    // A hit that heals pays per line of every strike, so its row reads like the
    // damage row: the total is what matters.
    double heal = hit.base().hp_recover_pct() +
                  hit.per_level().hp_recover_pct() * (level - 1);
    if (heal > 0.0) {
      rows.push_back(EffectRow(
          hit.label() + " Heal",
          "+" + SwingText(heal, hit.lines(), SwingHitCasts(hit) * swing_casts) +
              " HP"));
    }
  }
  return rows;
}

std::vector<Row> LeverRows(const SkillEffect& base, const SkillEffect& per,
                           int level, const std::string& suffix) {
  std::vector<Row> rows;
  for (const FlatLever& lever : kFlatLevers) {
    // WholeValue rather than a cast, so the page floors a fractional ladder by
    // the same rule the character gets it by instead of restating the rule.
    int value =
        WholeValue((base.*lever.fn)() + (per.*lever.fn)() * (level - 1));
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
    // A signed field writes a row for any value except zero; every other field
    // is treated as unset when it isn't positive.
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
    std::string sign = "+";
    if (lever.sign == kBare) {
      sign = "";
    } else if (lever.sign == kMinus) {
      sign = "-";
    }
    rows.push_back(EffectRow(lever.label,
                             sign + FormatNumber(value) + lever.unit + suffix));
  }
  return rows;
}

// A fountain shows both halves: the pulse grows and the interval shortens
// together, so showing only the pulse understates every point after the first.
std::vector<Row> RegenRows(const Skill& skill, int level) {
  double regen = PercentAt(skill, &SkillEffect::regen_pct, level);
  int regen_hp = FlatAt(skill, &SkillEffect::regen_hp, level);
  double interval =
      PercentAt(skill, &SkillEffect::regen_interval_seconds, level);
  if ((regen <= 0.0 && regen_hp <= 0) || interval <= 0.0) {
    return {};
  }
  // A fountain that grants both shows both, flat amount first, since that is
  // the one the player can compare with their pool.
  std::string poured;
  if (regen_hp > 0) {
    poured = std::to_string(regen_hp) + " HP";
  }
  if (regen > 0.0) {
    poured += (poured.empty() ? "" : " and ") + FormatPercent(regen);
  }
  std::string text = poured + " every " + FormatNumber(interval) + "s";
  // One more pulse per step of INT, most of what a Bishop's points buy. Shown
  // with the pulse rather than alone, where it would look like a percentage.
  double step = PercentAt(skill, &SkillEffect::regen_int_step, level);
  if (step > 0.0) {
    text +=
        ", +" + FormatPercent(regen) + " per " + FormatNumber(step) + " INT";
  }
  return {EffectRow("HP Recovered", text)};
}

// The heal that triggers when nearly dead, which needs all four of its numbers
// to mean anything: how much it heals, for how long, at what threshold, and how
// often.
std::vector<Row> EmergencyHealRows(const Skill& skill, int level) {
  double pct = PercentAt(skill, &SkillEffect::emergency_heal_pct, level);
  if (pct <= 0.0) {
    return {};
  }
  double seconds =
      PercentAt(skill, &SkillEffect::emergency_heal_seconds, level);
  double threshold =
      PercentAt(skill, &SkillEffect::emergency_heal_hp_threshold, level);
  double cooldown =
      PercentAt(skill, &SkillEffect::emergency_heal_cooldown_seconds, level);
  return {EffectRow("Below " + FormatPercent(threshold) + " HP",
                    FormatPercent(pct) + " a second for " +
                        FormatNumber(seconds) + "s"),
          EffectRow("Recovery Cooldown", FormatNumber(cooldown) + "s")};
}

// What a hold does beyond its own pulse: the final strike, the pulse it grows
// into, and the share of a hit it blocks.
std::vector<Row> ChannelFinishRows(const Skill& skill, int level) {
  google::protobuf::RepeatedPtrField<SwingHit> hits;
  if (skill.channel().has_finish()) {
    *hits.Add() = skill.channel().finish();
  }
  std::vector<Row> rows = SwingHitRows(hits, level);
  if (skill.channel().damage_taken_pct() > 0.0) {
    rows.push_back(
        EffectRow("Damage Taken",
                  "-" + FormatPercent(skill.channel().damage_taken_pct())));
  }
  return rows;
}

// What a hold is worth and what it costs: where it grows, how many pulses one
// press gives, and the pool it draws from instead of a cooldown.
std::vector<Row> ChannelRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  // Below the opening pulse, since a pulse count means nothing until the reader
  // sees what each pulse does.
  if (skill.channel().has_grown()) {
    google::protobuf::RepeatedPtrField<SwingHit> grown;
    *grown.Add() = skill.channel().grown();
    Append(SwingHitRows(grown, level), rows);
    rows.push_back(
        EffectRow("Grows After",
                  std::to_string(skill.channel().small_pulses()) + " Pulses"));
  }
  // A count rather than a timer, and a maximum rather than a guarantee: the
  // player lets go when it stops paying off.
  std::string pulses = "Up to " + std::to_string(skill.channel().max_pulses());
  if (skill.channel().pulses_per_charge() > 0) {
    pulses += ", " + std::to_string(skill.channel().pulses_per_charge()) +
              " per Charge";
  }
  rows.push_back(EffectRow("Pulses", pulses));
  if (skill.channel().charge_seconds() > 0.0) {
    rows.push_back(EffectRow(
        "Charges", "1 per " + FormatNumber(skill.channel().charge_seconds()) +
                       "s, up to " +
                       std::to_string(skill.channel().max_charges())));
  }
  return rows;
}

// What the skill itself does when it triggers: its damage, its healing, and the
// effects a plain field row can't express.
std::vector<Row> OwnEffectRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  bool held = skill.channel().max_pulses() > 0;
  if (IsActive(skill) && PercentAt(skill, &SkillEffect::skill_pct, level) > 0) {
    // A hold's damage row is one pulse, so it says so; read as the whole hold,
    // it would be off by the pulse count.
    rows.push_back(EffectRow(held ? "Damage per Pulse" : "Damage",
                             DamageText(skill, level)));
  }
  if (held) {
    Append(ChannelRows(skill, level), rows);
  }
  Append(RegenRows(skill, level), rows);
  Append(EmergencyHealRows(skill, level), rows);
  // What one meso is worth thrown back, shown like every other attack here: per
  // line, times the count. Meso Mastery's points each add a line.
  double meso_hit = PercentAt(skill, &SkillEffect::meso_hit_pct, level);
  if (meso_hit > 0.0) {
    rows.push_back(EffectRow(
        "Damage per Meso",
        SwingText(meso_hit, SkillLinesAt(skill, level), SkillCasts(skill))));
    // The other reading of the row above, directly below it: GMS gives a thrown
    // coin extra points, which only make sense as their total.
    double normal = PercentAt(skill, &SkillEffect::normal_skill_pct, level);
    if (normal > 0.0) {
      rows.push_back(
          EffectRow("Normal Monsters",
                    SwingText(meso_hit + normal, SkillLinesAt(skill, level),
                              SkillCasts(skill))));
    }
  }
  // A share of what the copied hit dealt: a 70% shadow behind a 210% line deals
  // 147%. The row says "of each hit", since a bare percentage looks like a flat
  // figure to everyone.
  double mirror = PercentAt(skill, &SkillEffect::mirror_line_pct, level);
  if (mirror > 0.0) {
    rows.push_back(
        EffectRow("Shadow Damage", FormatPercent(mirror) + " of each hit"));
  }
  // An extra strike on every attack the character already lands more than once.
  int strikes = FlatAt(skill, &SkillEffect::bonus_attack_lines, level);
  if (strikes > 0) {
    rows.push_back(EffectRow("Extra Strike", "+" + std::to_string(strikes) +
                                                 " on every multi-hit skill"));
  }
  // Dispel's whole effect, shown even though nothing in the game inflicts
  // conditions yet. Flat, since more points don't add anything.
  if (skill.base().cures_conditions()) {
    rows.push_back(EffectRow("Cures", "All Conditions"));
  }
  // Lets the barrier work on bosses. Flat for the same reason as Dispel's row:
  // it is an on/off switch, and no level turns it further.
  if (skill.base().enemy_attack_reaches_boss()) {
    rows.push_back(EffectRow("Enemy ATT", "Also Reduced on Bosses"));
  }
  // Directly under the damage it is the other reading of, so the two totals
  // line up.
  std::string normal = NormalMonsterText(skill, level);
  if (!normal.empty()) {
    rows.push_back(EffectRow("Normal Monsters", normal));
  }
  // The other hit the same attack lands, with its normal-monster value below
  // it, like the attack's own two rows above.
  Append(SwingHitRows(skill.extra_hit(), level, SequencedCasts(skill)), rows);
  if (held) {
    Append(ChannelFinishRows(skill, level), rows);
  }
  // Below the attack's own damage, because it is the extra hit the attack opens
  // with rather than a second attack.
  rows.push_back(EffectRow("Opening Hit", LeadText(skill, level)));
  return rows;
}

// The burn an attack leaves, as one row: the tick damage, its interval and its
// duration, since none of the three means anything alone.
std::string DotText(const Dot& dot, int level) {
  double per_tick =
      dot.base().skill_pct() + dot.per_level().skill_pct() * (level - 1);
  double burns_for =
      dot.duration_seconds() + dot.duration_seconds_per_level() * (level - 1);
  std::string text = SwingText(per_tick, dot.lines()) + " every " +
                     FormatNumber(dot.interval_seconds(), 2) + "s for " +
                     FormatNumber(burns_for) + "s";
  // What a carried poison has that an attack's burn doesn't: it is rolled for,
  // and it stacks.
  double chance = dot.chance() + dot.chance_per_level() * (level - 1);
  if (chance > 0.0) {
    text = FormatPercent(std::min(1.0, chance)) + " chance of " + text;
  }
  int stacks = static_cast<int>(dot.max_stacks() +
                                dot.max_stacks_per_level() * (level - 1) +
                                kWholeEpsilon);
  if (stacks > 1) {
    text += ", stacks " + std::to_string(stacks) + " times";
  }
  return text;
}

// The chance every attack has to hit one enemy harder. Chance and damage are
// one fact and share a line, like a Final Attack's. The recovery gets its own
// row, since it is what the player gets rather than what the enemy takes.
std::vector<Row> ProcRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  const Proc& proc = skill.proc();
  double chance = proc.chance() + proc.chance_per_level() * (level - 1);
  if (chance <= 0.0) {
    return rows;
  }
  double damage =
      proc.base().damage_pct() + proc.per_level().damage_pct() * (level - 1);
  rows.push_back(EffectRow("Chance to Crush", FormatPercent(chance) + " for +" +
                                                  FormatPercent(damage) +
                                                  " Damage, one enemy"));
  double heal = proc.base().hp_recover_pct() +
                proc.per_level().hp_recover_pct() * (level - 1);
  if (heal > 0.0) {
    rows.push_back(
        EffectRow("Heal on Crush", "+" + FormatPercent(heal) + " HP"));
  }
  return rows;
}

// Chance and damage are one fact, not two fields, so they share a line. Taken
// as a pair of effects rather than from the skill, because a buff can grant one
// while active and state its ladder there.
std::vector<Row> FinalAttackRows(const SkillEffect& base,
                                 const SkillEffect& per, int level,
                                 int max_enemies, const std::string& label) {
  double proc =
      base.final_attack_chance() + per.final_attack_chance() * (level - 1);
  if (proc <= 0.0) {
    return {};
  }
  // A separate target count has to be stated, or a player comparing it with
  // another would think it worth several times more or less. Cut at a comma so
  // the note doesn't push the row past its column.
  std::string reach;
  if (max_enemies == 1) {
    reach = ", one enemy";
  } else if (max_enemies > 1) {
    reach = ", " + ReachText(max_enemies);
  }
  int strikes = static_cast<int>(base.final_attack_lines() +
                                 per.final_attack_lines() * (level - 1));
  double damage =
      base.final_attack_pct() + per.final_attack_pct() * (level - 1);
  return {EffectRow(
      label.empty() ? "Final Attack" : label,
      FormatPercent(proc) + " for " + SwingText(damage, strikes) + reach)};
}

// The burn the attack leaves, and the side strike it triggers.
std::vector<Row> SwingRiderRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  // Below the attack's own damage, since it is what that attack left behind.
  if (skill.dot().interval_seconds() > 0.0) {
    rows.push_back(EffectRow("DoT", DotText(skill.dot(), level)));
  }
  Append(FinalAttackRows(skill.base(), skill.per_level(), level,
                         skill.final_attack_max_enemies(),
                         skill.final_attack_label()),
         rows);
  // The side strike the attack triggers. Its cooldown goes on the damage row,
  // and its reach is shown only where it differs from the attack's.
  if (skill.has_side_strike()) {
    const SideStrike& side = skill.side_strike();
    int casts = std::max(1, side.casts());
    double per_hit =
        side.base().skill_pct() + side.per_level().skill_pct() * (level - 1);
    std::string text = SwingText(per_hit, side.lines(), casts);
    if (side.max_enemies() > 0 && side.max_enemies() != skill.max_enemies()) {
      text += ", " + std::to_string(side.max_enemies()) + " enemies";
    }
    // Only when the strike really has a cooldown. One without triggers on every
    // attack, and "every 0s" tells nobody anything.
    if (side.cooldown_seconds() > 0.0) {
      text += " every " + FormatNumber(side.cooldown_seconds()) + "s";
    }
    rows.push_back(EffectRow(side.label(), text));
    double normal = side.base().normal_skill_pct() +
                    side.per_level().normal_skill_pct() * (level - 1);
    if (normal > 0.0) {
      rows.push_back(
          EffectRow(side.label() + " Normal",
                    SwingText(per_hit + normal, side.lines(), casts)));
    }
    // A side strike that scatters gets its own row, for the same reason the
    // attack does, and under its own name: the reach above is how far it
    // spreads.
    if (side.scatter().hits() > 0) {
      rows.push_back(
          EffectRow(side.label() + " Scattered", ScatterText(side.scatter())));
    }
  }
  return rows;
}

// The damage of every half with its own timer, and of every attack this skill
// substitutes for another.
std::vector<Row> OwnClockRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  // Each own-clock half's damage, below the attack's own, so they read as one
  // skill with several ways of dealing damage, each under the name its reach
  // row above uses.
  for (const AutoMode& mode : skill.auto_mode()) {
    if (!ModeFires(mode)) {
      continue;
    }
    rows.push_back(EffectRow(
        mode.label(), SwingText(mode.base().skill_pct() +
                                    mode.per_level().skill_pct() * (level - 1),
                                mode.lines(), mode.casts())));
  }
  // The upgraded attack's damage next to the permanent bonus below it: one
  // skill strengthening another in two ways, so both are read together. Its
  // normal-monster value follows, as with the ordinary attack.
  for (const EmpoweredForm& form : skill.empowered_form()) {
    if (form.casts_per_trigger() <= 0) {
      continue;
    }
    // A single form is "Empowered Damage" and needs nothing more; several have
    // to say which attack each belongs to, so they use the upgraded skill's
    // name.
    std::string label = skill.empowered_form_size() > 1
                            ? EmpoweredTarget(form)
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
    // What the upgraded attack lands alongside itself, under the attack it
    // belongs to: the explosion at the end of an arrow's flight, or the mark it
    // consumes.
    Append(SwingHitRows(form.extra_hit(), level), rows);
  }
  return rows;
}

// A heading over one half of a skill that has two. Orange and green are the
// skill list's tags for active and passive, so the halves use the colours the
// player already knows. Only the heading is coloured.
Row SectionRow(const std::string& label, ftxui::Color color) {
  return WholeRow(ftxui::text(" " + label) | ftxui::color(color));
}

// What one list of boosts gives other skills, one sentence per grant.
// `own_card` puts a heading above them instead of naming the skill in each row,
// which suits a boost node.
std::vector<Row> BoostRows(
    const google::protobuf::RepeatedPtrField<SkillBoost>& boosts, int level,
    bool own_card) {
  std::vector<Row> rows;
  // Label for what, value for how much, like every other row here. One row per
  // skill however many times it is named: a node's damage, the extra enemy at
  // level 20 and the defence ignored at 40 are three grants on one row.
  std::map<std::string, std::string> gained;
  std::vector<std::string> named;
  for (const SkillBoost& granted : boosts) {
    if (level < granted.min_level()) {
      continue;
    }
    std::string gains = BoostText(granted, level);
    if (gains.empty()) {
      continue;
    }
    // A grant for the empowered form only gets its own row, since merged into
    // the parent's row it would look like the parent gets it.
    std::string target = granted.reach() == BOOST_REACH_EMPOWERED
                             ? EmpoweredSkillName(granted.skill_name())
                             : granted.skill_name();
    if (gained.find(target) == gained.end()) {
      named.push_back(target);
    }
    AppendGain(gains, gained[target]);
  }
  // A boost node is nothing but its boosts, so it gets the heading however many
  // skills it names; saying "Boosts" once saves the seven columns on every row.
  // A Hyper Skill names one skill and says so in the row.
  if (named.size() > 1 || own_card) {
    rows.push_back(SectionRow("Boosts", kGreen));
    for (const std::string& name : named) {
      rows.push_back(EffectRow(name, gained[name]));
    }
    return rows;
  }
  for (const std::string& name : named) {
    rows.push_back(EffectRow("Boosts " + name, gained[name]));
  }
  return rows;
}

// The wound a skill leaves and the stronger attack it enables. Two headings,
// both needed: the player has to learn that the attack has a second form, what
// causes the wound that enables it, and that the form has a longer cooldown.
std::vector<Row> WoundRows(const Skill& skill, int level) {
  const Wound& wound = skill.wound();
  std::vector<Row> rows;
  if (wound.max_stacks() <= 0) {
    return rows;
  }
  std::string left_by;
  for (const Wound::Source& source : wound.source()) {
    if (!left_by.empty()) {
      left_by += ", ";
    }
    left_by += source.skill_name() + " " + std::to_string(source.stacks());
  }
  rows.push_back(SectionRow(
      "Wound, " + std::to_string(wound.max_stacks()) + " deep on one enemy",
      kGold));
  if (!left_by.empty()) {
    rows.push_back(EffectRow("Left By", left_by));
  }
  rows.push_back(
      EffectRow("Lasts", FormatNumber(wound.duration_seconds()) + "s"));
  if (!wound.has_form()) {
    return rows;
  }
  const WoundForm& form = wound.form();
  rows.push_back(SectionRow("Against a Full Wound", kGold));
  double per_hit =
      form.base().skill_pct() + form.per_level().skill_pct() * (level - 1);
  rows.push_back(EffectRow(
      "Damage", SwingText(per_hit, form.lines(), std::max(1, form.casts()))));
  rows.push_back(
      EffectRow("Attacks", ReachText(std::max(1, form.max_enemies()))));
  if (form.cooldown_seconds() > 0.0) {
    rows.push_back(
        EffectRow("Cooldown", FormatNumber(form.cooldown_seconds()) + "s"));
  }
  // Everything but the damage, which the row above already shows.
  SkillEffect base = form.base();
  SkillEffect per = form.per_level();
  base.clear_skill_pct();
  per.clear_skill_pct();
  Append(LeverRows(base, per, level, ""), rows);
  return rows;
}

std::vector<Row> ExtraAttackRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  Append(SwingRiderRows(skill, level), rows);
  Append(WoundRows(skill, level), rows);
  Append(OwnClockRows(skill, level), rows);
  Append(BoostRows(skill.boost(), level, skill.v_node() == V_NODE_KIND_BOOST),
         rows);
  return rows;
}

// The shield a buff provides: the hits it fully absorbs, and what it does about
// the ones it can't. Empty for every buff that isn't a shield.
std::vector<Row> ShieldRows(const Shield& shield, int level) {
  std::vector<Row> rows;
  int hits = ShieldHitsAt(shield, level);
  if (hits <= 0) {
    return rows;
  }
  rows.push_back(EffectRow("Blocks", std::to_string(hits) + " attacks"));
  if (shield.boss_damage_taken_pct() > 0.0) {
    // Named for what it covers rather than what it is: a player reading this
    // wants to know it applies to the boss hits the shield can't absorb.
    rows.push_back(
        EffectRow("Damage Taken (Boss)",
                  "-" + FormatPercent(shield.boss_damage_taken_pct())));
  }
  return rows;
}

// The final strike a pulse ends with: its own damage and reach, and nothing
// about the timer above, since it lands once, only when that timer ends. The
// fields belong on the pulse's row, which covers everything it does.
std::vector<Row> FinalStrikeRows(const BuffPulse& pulse, int level) {
  if (!pulse.has_final_strike()) {
    return {};
  }
  const SwingHit& burst = pulse.final_strike();
  double per_hit =
      burst.base().skill_pct() + burst.per_level().skill_pct() * (level - 1);
  std::string text = SwingText(per_hit, burst.lines(), SwingHitCasts(burst));
  if (burst.max_enemies() > 0) {
    text += " on " + ReachText(burst.max_enemies());
  }
  std::vector<Row> rows = {EffectRow(burst.label(), text)};
  // What it adds beyond the pulse's own, which it already keeps.
  SkillEffect base = burst.base();
  SkillEffect per = burst.per_level();
  base.clear_skill_pct();
  per.clear_skill_pct();
  Append(LeverRows(base, per, level, ""), rows);
  return rows;
}

// What the buff deals over time. A pulse using the attack's reach shows its
// damage and interval on one row, since they are one fact; one with its own
// targets shows them in an Attacks row and keeps the damage row for damage.
std::vector<Row> PulseRows(const BuffPulse& pulse, int level) {
  if (!Pulses(pulse)) {
    return {};
  }
  double per_hit =
      pulse.base().skill_pct() + pulse.per_level().skill_pct() * (level - 1);
  std::string damage =
      SwingText(per_hit, pulse.lines(), std::max(1, pulse.casts()));
  if (pulse.max_pulses() > 0) {
    damage += ", " + std::to_string(pulse.max_pulses()) + " times";
    // The final strike isn't part of the count, so it isn't folded in: a player
    // reading nine wants the tenth stated.
    if (pulse.final_repeat_strike()) {
      damage += ", then once more";
    }
  }
  // Anything else the pulse carries applies to its own strikes, not the
  // character: Burning Soul Blade's sword crits half again as often as its
  // owner.
  SkillEffect base = pulse.base();
  SkillEffect per = pulse.per_level();
  base.clear_skill_pct();
  per.clear_skill_pct();
  std::vector<Row> levers = LeverRows(base, per, level, "");
  std::vector<Row> rows;
  if (pulse.max_enemies() == 0) {
    rows.push_back(EffectRow(pulse.label(), damage + PulseClockText(pulse)));
  } else {
    rows.push_back(EffectRow(pulse.label(), damage));
    rows.push_back(EffectRow(
        "Attacks", ReachText(pulse.max_enemies()) + PulseClockText(pulse)));
  }
  // What each pulse that lands adds to the next. Shown as the damage it builds
  // up to, since the top of the ramp is what a boss takes for most of a cast.
  double step = pulse.skill_pct_per_repeat() +
                pulse.skill_pct_per_repeat_per_level() * (level - 1);
  if (step > 0.0 && pulse.max_repeats() > 0) {
    rows.push_back(EffectRow(
        "Per Stack", "+" + FormatPercent(step) + ", up to " +
                         FormatPercent(per_hit + step * pulse.max_repeats())));
  }
  // The strikes a tick lands whatever the number of enemies. Shown as the
  // total, since that is what a lone boss takes, and a crowd only spreads it.
  if (pulse.fixed_strikes().hits() > 0) {
    rows.push_back(EffectRow(
        "Plus", SwingText(per_hit, pulse.fixed_strikes().hits()) + ", spread"));
  }
  // What extra enemies add to the rain, whose strikes grow with the attack. The
  // cap goes on the row, or the ladder would look unlimited.
  if (pulse.lines_per_extra_enemy() > 0 && pulse.max_extra_lines() > 0) {
    rows.push_back(EffectRow(
        "Per Extra Enemy",
        "+" + std::to_string(pulse.lines_per_extra_enemy()) +
            (pulse.lines_per_extra_enemy() == 1 ? " Strike" : " Strikes") +
            ", up to +" + std::to_string(pulse.max_extra_lines())));
  }
  Append(FinalStrikeRows(pulse, level), rows);
  // Last, because they apply to everything above: the fields a pulse states are
  // the turret's, including its final strike.
  Append(std::move(levers), rows);
  return rows;
}

// The party's healing, shown like the character's own. It has its own builder
// rather than a field row, since neither half means anything alone.
std::vector<Row> PartyRegenRows(const SkillEffect& half, const Buff& buff) {
  if (half.regen_pct() <= 0.0 || half.regen_interval_seconds() <= 0.0) {
    return {};
  }
  std::string text = FormatPercent(half.regen_pct()) + " every " +
                     FormatNumber(half.regen_interval_seconds()) + "s";
  for (const AllyIntLever& lever : buff.ally_int_lever()) {
    if (lever.effect().regen_pct() <= 0.0 || lever.int_step() <= 0.0) {
      continue;
    }
    text += ", +" + FormatPercent(lever.effect().regen_pct()) + " per " +
            FormatWithCommas(static_cast<int64_t>(lever.int_step())) + " INT";
    if (lever.cap().regen_pct() > 0.0) {
      text += " up to " + FormatPercent(lever.cap().regen_pct());
    }
  }
  return {EffectRow("HP Recovered", text)};
}

// The value a field table writes for each field, keyed by its label, so a cap
// can be shown in the same units as its field.
std::map<std::string, std::string> LeverValuesByLabel(const SkillEffect& at) {
  std::map<std::string, std::string> values;
  for (const Row& row : LeverRows(at, SkillEffect(), 1, "")) {
    // The sign belongs to a grant, not to a quoted cap.
    values[row.label] = row.value.empty() || row.value.front() != '+'
                            ? row.value
                            : row.value.substr(1);
  }
  return values;
}

// What the caster's INT adds to the party's share, one row per field: the rate
// and the cap. Shown at a single step, so the row states a rate; the total
// depends on the caster's own INT, which is the point.
std::vector<Row> AllyIntLeverRows(const Buff& buff, int level) {
  std::map<std::string, std::string> own =
      LeverValuesByLabel(EffectAt(buff.base(), buff.per_level(), level));
  std::map<std::string, std::string> ceiling;
  std::vector<Row> rows;
  for (const AllyIntLever& lever : buff.ally_int_lever()) {
    // The fountain shows its own growth beside its pulse, above.
    if (lever.int_step() <= 0.0 || lever.effect().regen_pct() > 0.0) {
      continue;
    }
    if (!lever.cap_is_party_share()) {
      ceiling = LeverValuesByLabel(lever.cap());
    }
    std::string per = " per " +
                      FormatWithCommas(static_cast<int64_t>(lever.int_step())) +
                      " INT";
    for (Row& row : LeverRows(lever.effect(), SkillEffect(), 1, per)) {
      const std::map<std::string, std::string>& against =
          lever.cap_is_party_share() ? own : ceiling;
      std::map<std::string, std::string>::const_iterator it =
          against.find(row.label);
      if (it != against.end()) {
        row.value +=
            lever.cap_is_party_share()
                ? ", up to your own " + it->second + " split between the party"
                : ", up to " + it->second;
      }
      rows.push_back(std::move(row));
    }
  }
  return rows;
}

// What the rest of the party gets while the buff is active, in the party
// screens' colour. Under the buff's heading rather than at the bottom of the
// card, because these end with it.
std::vector<Row> AllyBuffRows(const Buff& buff, int level) {
  SkillEffect base = buff.ally_base();
  SkillEffect per = buff.ally_per_level();
  double heal = base.heal_pct() + per.heal_pct() * (level - 1);
  base.clear_heal_pct();
  per.clear_heal_pct();
  std::vector<Row> levers = LeverRows(base, per, level, "");
  // A party shield covers everyone as one, so what it blocks is shown for them
  // as for the caster. A non-party shield shows nothing here.
  std::vector<Row> shield = buff.shield().party()
                                ? ShieldRows(buff.shield(), level)
                                : std::vector<Row>();
  if (heal <= 0.0 && levers.empty() && shield.empty()) {
    return {};
  }
  std::vector<Row> rows = {SectionRow("Your Party", kTheme)};
  if (heal > 0.0) {
    rows.push_back(
        EffectRow("Heal on Cast", "+" + FormatPercent(heal) + " HP"));
  }
  Append(std::move(levers), rows);
  Append(AllyIntLeverRows(buff, level), rows);
  // Last of the three, since it is the part of the grant that isn't a field.
  Append(PartyRegenRows(
             EffectAt(buff.ally_base(), buff.ally_per_level(), level), buff),
         rows);
  Append(std::move(shield), rows);
  return rows;
}

// The forms a buff can take, each with a heading and a pulse. Both are shown:
// the player never chooses between them (the fight does), so the card shows the
// pair side by side.
std::vector<Row> StanceRows(const Buff& buff, int level) {
  std::vector<Row> rows;
  for (const Stance& stance : buff.stance()) {
    rows.push_back(SectionRow(
        stance.label() + " for " +
            FormatNumber(stance.duration_seconds() +
                         stance.duration_seconds_per_level() * (level - 1)) +
            "s",
        kGold));
    Append(PulseRows(stance.pulse(), level), rows);
  }
  return rows;
}

// The attack a buff loads, headed by its name and what one activation provides.
// It gets its own block because it is a separate press with its own damage,
// reach and fields, and the count is what the player reads first. A load that
// another skill uses names the skill that fires it.
std::vector<Row> MagazineRows(const Magazine& magazine, int level) {
  if (magazine.charges() <= 0) {
    return {};
  }
  std::vector<Row> rows;
  std::string spent = std::to_string(magazine.charges()) +
                      (magazine.charges() == 1 ? " charge" : " charges");
  if (!magazine.spent_by_skill_name().empty()) {
    spent = "on " + magazine.spent_by_skill_name();
  } else if (!magazine.spent_by_every_swing()) {
    spent = std::to_string(magazine.charges()) +
            (magazine.charges() == 1 ? " shot" : " shots");
  }
  rows.push_back(SectionRow(magazine.label() + ", " + spent, kGold));
  double per_hit = magazine.base().skill_pct() +
                   magazine.per_level().skill_pct() * (level - 1);
  rows.push_back(EffectRow("Damage", SwingText(per_hit, magazine.lines(),
                                               std::max(1, magazine.casts()))));
  rows.push_back(
      EffectRow("Attacks", ReachText(std::max(1, magazine.max_enemies()))));
  if (magazine.scatter().hits() > 0) {
    rows.push_back(EffectRow("Scattered", ScatterText(magazine.scatter())));
  }
  if (magazine.charges_per_swing() > 1) {
    rows.push_back(EffectRow(
        "Spends",
        std::to_string(magazine.charges_per_swing()) + " per attack"));
  }
  // What the magazine refills to with no buff active: the passive half, shown
  // here because it applies whether or not the buff is ever used.
  if (magazine.recharge_seconds() > 0.0 && magazine.recharge_max() > 0) {
    rows.push_back(EffectRow(
        "Prepared", std::to_string(magazine.recharge_max()) + " every " +
                        FormatNumber(magazine.recharge_seconds()) +
                        "s, passively"));
  }
  // Anything else it carries applies to its own strikes, not the character: the
  // cartridge always crits, while its owner doesn't.
  SkillEffect base = magazine.base();
  SkillEffect per = magazine.per_level();
  base.clear_skill_pct();
  per.clear_skill_pct();
  Append(LeverRows(base, per, level, ""), rows);
  return rows;
}

// How long the buff lasts, for its heading. A duration the burns extend shows
// its range and the rule that changes it, as ScatterText does.
std::string BuffWindowText(const Buff& buff, int level) {
  double seconds =
      buff.duration_seconds() + buff.duration_seconds_per_level() * (level - 1);
  if (buff.duration_seconds_per_dot() <= 0.0 || buff.dot_count_cap() <= 0) {
    return FormatNumber(seconds) + "s";
  }
  double longest =
      seconds + buff.duration_seconds_per_dot() * buff.dot_count_cap();
  return FormatNumber(seconds) + "-" + FormatNumber(longest) + "s, +" +
         FormatNumber(buff.duration_seconds_per_dot()) + "s per DoT";
}

// The buff's heading: how long it lasts, and what triggers it when no Cooldown
// row can say so.
std::string BuffHeading(const Buff& buff, int level) {
  // A buff paid for with landed hits says so in its heading, since that count
  // is its whole cost and there is no Cooldown row for it.
  std::string charge =
      buff.charge_lines() > 0
          ? " every " + std::to_string(buff.charge_lines()) + " hits"
          : "";
  // A shared buff says so here rather than in a Your Party section, since it
  // gives the party nothing of its own; everyone activates the same one in
  // turn.
  std::string shared = buff.party_shared() ? ", shared with your party" : "";
  // A buff only the wound form triggers says which attack triggers it, or the
  // heading would suggest both do.
  std::string form = buff.needs_wound_form() ? ", after the wounded form" : "";
  // A buff an attack rolls for states the roll and what it needs, as a charged
  // buff states its count, since there is no Cooldown row on one that waits for
  // nothing else.
  double chance =
      buff.raise_chance() + buff.raise_chance_per_level() * (level - 1);
  if (chance > 0.0) {
    charge += ", " + FormatPercent(std::min(chance, 1.0)) + " a hit";
  }
  if (buff.needs_afflicted_target()) {
    charge += ", on a suffering enemy";
  }
  return "Active for " + BuffWindowText(buff, level) + charge + shared + form;
}

// What a timed buff grants, headed by its duration. The wait for the next one
// is the skill's own Cooldown row above. No row here says "while active"; the
// heading says it once for all of them.
std::vector<Row> BuffRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  const Buff& buff = skill.buff();
  if (LongestBuffDuration(buff) <= 0.0) {
    return rows;
  }
  // A buff with forms has no duration of its own: each form heads its own block
  // below, and one heading for both would be wrong about one of them.
  if (buff.stance().empty()) {
    rows.push_back(SectionRow(BuffHeading(buff, level), kGold));
  }
  // The heal is given once, when the buff is activated, so it is shown
  // separately from the fields that apply for as long as it lasts.
  SkillEffect base = buff.base();
  SkillEffect per = buff.per_level();
  double heal = base.heal_pct() + per.heal_pct() * (level - 1);
  if (heal > 0.0) {
    rows.push_back(
        EffectRow("Heal on Cast", "+" + FormatPercent(heal) + " HP"));
  }
  base.clear_heal_pct();
  per.clear_heal_pct();
  // A buff gathered in charges grants its fields once per charge, so the count
  // must be on the card or the rows below would look like the whole grant.
  std::string per_stage = "";
  if (buff.stacks() > 1) {
    rows.push_back(EffectRow(
        "Stacks", std::to_string(buff.stacks()) + ", each on its own clock"));
    per_stage = " each";
  }
  // A buff that loses stages grants its fields once per remaining stage, so the
  // count must be on the card or the rows below would look like the whole
  // grant.
  if (buff.stages() > 1) {
    rows.push_back(EffectRow(
        "Stages", std::to_string(buff.stages()) + ", one lost every " +
                      FormatNumber(buff.stage_interval_seconds()) + "s"));
    per_stage = " each";
  }
  // The summon this buff dismisses while active. A statement rather than a
  // number, like the Element row, since there is no figure to show.
  if (!buff.silences_skill_name().empty()) {
    rows.push_back(EffectRow("Dismisses", buff.silences_skill_name()));
  }
  // A buff that grants in bursts says so, or every row below would look like it
  // applies for the whole duration the heading states.
  if (buff.duty_seconds() > 0.0 && buff.duty_interval_seconds() > 0.0) {
    rows.push_back(EffectRow(
        "Granted", FormatNumber(buff.duty_seconds()) + "s of every " +
                       FormatNumber(buff.duty_interval_seconds()) + "s"));
  }
  Append(LeverRows(base, per, level, per_stage), rows);
  // The part granted only in a party, named in the row rather than under a
  // heading, since it is one line of a buff that otherwise works alone.
  Append(LeverRows(buff.with_party_base(), buff.with_party_per_level(), level,
                   " in a party"),
         rows);
  // A Final Attack the buff grants while active. Under the buff's own heading,
  // which already says how long that is.
  Append(FinalAttackRows(base, per, level, skill.final_attack_max_enemies(),
                         skill.final_attack_label()),
         rows);
  Append(ShieldRows(buff.shield(), level), rows);
  // Named in the row rather than under a heading: these are under the buff's
  // own heading, which already says they last only as long as it does.
  Append(BoostRows(buff.boost(), level, false), rows);
  Append(PulseRows(buff.pulse(), level), rows);
  Append(StanceRows(buff, level), rows);
  Append(MagazineRows(buff.magazine(), level), rows);
  Append(AllyBuffRows(buff, level), rows);
  return rows;
}

// What the skill grants permanently. An attack shows this separately from the
// fields that apply to its attacks, and a passive shows everything here. What a
// chance pays also goes here, since none of it ends.
std::vector<Row> PermanentRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  if (skill.kind() == SKILL_KIND_ATTACK) {
    rows = LeverRows(WithoutSwingLevers(skill.base()),
                     WithoutSwingLevers(skill.per_level()), level, "");
    Append(LeverRows(skill.passive(), skill.passive_per_level(), level, ""),
           rows);
  } else {
    rows = LeverRows(skill.base(), skill.per_level(), level, "");
  }
  Append(ProcRows(skill, level), rows);
  return rows;
}

// A weapon bonus shows the field it grants with the required weapons in
// brackets: "Damage  +5% (Axe)". Flat, so it is read at level 1.
std::vector<Row> WeaponBonusRows(const Skill& skill) {
  std::vector<Row> rows;
  for (const WeaponBonus& bonus : skill.weapon_bonus()) {
    std::string suffix =
        " (" + RequiredWeapons(bonus.required_equip_type()) + ")";
    Append(
        LeverRows(bonus.effect(), SkillEffect::default_instance(), 1, suffix),
        rows);
  }
  return rows;
}

// The share of the character's Final Attack this attack gives up. Separate from
// the field table because it has to name what it reduces: "Final Attack Rate
// -60%" tells a Night Lord nothing. See Skill::final_attack_label.
std::vector<Row> FinalAttackCutRow(const Skill& skill, int level) {
  double cut = PercentAt(skill, &SkillEffect::final_attack_chance_cut, level);
  if (cut <= 0.0) {
    return {};
  }
  const std::string& label = skill.final_attack_label();
  return {EffectRow((label.empty() ? "Final Attack" : label) + " Rate",
                    "-" + FormatPercent(cut))};
}

// Everything the skill grants at `level`. Empty for a skill whose actual effect
// the game doesn't model.
std::vector<Row> EffectRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  Append(OwnEffectRows(skill, level), rows);
  Append(ExtraAttackRows(skill, level), rows);
  // A skill grants three kinds of things, and only one is permanent: what
  // applies to its attack, what applies while a buff is active, and what it
  // always keeps. Each gets a heading when another is present, since otherwise
  // "Ignore DEF" would be one row meaning three things.
  std::vector<Row> swing =
      skill.kind() == SKILL_KIND_ATTACK
          ? LeverRows(SwingLeversOf(skill.base()),
                      SwingLeversOf(skill.per_level()), level, "")
          : std::vector<Row>();
  Append(FinalAttackCutRow(skill, level), swing);
  std::vector<Row> permanent = PermanentRows(skill, level);
  std::vector<Row> buff = BuffRows(skill, level);
  // Checked before either is appended, since Append takes its rows by value.
  bool has_swing = !swing.empty();
  bool has_buff = !buff.empty();
  if (has_swing) {
    rows.push_back(SectionRow("This Attack Only", kGold));
    Append(std::move(swing), rows);
  }
  Append(std::move(buff), rows);
  // A skill whose only value is shielding others gets its own section whatever
  // else is on the page, so a player maxing it and seeing nothing change can
  // see why.
  std::vector<Row> ally =
      LeverRows(skill.ally_base(), skill.ally_per_level(), level, "");
  if (!permanent.empty() &&
      (has_buff || has_swing || skill.requires_party() || !ally.empty())) {
    rows.push_back(SectionRow(
        skill.requires_party() ? "Passive, in a Party" : "Passive", kGreen));
  }
  Append(std::move(permanent), rows);
  // What everyone else in the party gets, in the party screens' colour. Its own
  // section, since these aren't the reader's numbers.
  if (!ally.empty()) {
    rows.push_back(SectionRow("Your Party", kTheme));
    Append(std::move(ally), rows);
  }
  // A cooldown that shortens as the skill levels is what a point buys, so it
  // goes in the level block; one that never changes is shown once above the
  // divider.
  if (skill.cooldown_seconds() > 0.0 &&
      skill.cooldown_seconds_per_level() != 0.0) {
    rows.push_back(EffectRow("Cooldown", CooldownText(skill, level)));
  }
  if (skill.combo_orbs_per_level() > 0.0) {
    rows.push_back(
        EffectRow("Combo Orbs", std::to_string(ComboOrbsAt(skill, level))));
  }
  Append(WeaponBonusRows(skill), rows);
  return Speaking(std::move(rows));
}

// One "Level N" heading and the effects below it. `cost` is that level's price
// in V Points, or 0 for a level with no price shown: one already paid for, and
// every SP level, where a level costs one point.
std::vector<Row> LevelBlock(const Skill& skill, int level, int cost = 0) {
  std::vector<Row> rows;
  std::string heading = " Level " + std::to_string(level);
  if (cost > 0) {
    heading += " - " + std::to_string(cost) + " VP";
  }
  rows.push_back(WholeRow(ftxui::text(heading)));
  std::vector<Row> effects = EffectRows(skill, level);
  if (effects.empty()) {
    // A skill whose whole effect isn't modelled still has levels to spend on,
    // and saying so is better than a heading over nothing.
    rows.push_back(WholeRow(EmptyState("no effect", kEffectIndent)));
  }
  for (Row& row : effects) {
    rows.push_back(std::move(row));
  }
  return rows;
}

// The card's rows in its two groups, without borders and before the columns are
// decided. The top part (name, description, and facts that apply at every
// level) stays on screen; the level blocks below it scroll.
struct SkillRows {
  std::vector<Row> head;
  std::vector<Row> body;
};

SkillRows CardRowsFor(const Skill& skill, int level, int bonus,
                      SkillInspectPanel::Levels levels) {
  SkillRows card;
  std::vector<Row>& rows = card.head;
  auto rule = [](std::vector<Row>& to) {
    to.push_back({Row::kRule, "", "", ThemedSeparator()});
  };
  rows.push_back(WholeRow(CenteredRow(skill.name())));
  rows.push_back(WholeRow(
      CenteredRow("Max Level: " + std::to_string(SkillMaxLevel(skill)))));

  rule(rows);
  rows.push_back({Row::kProse, "", skill.description(), nullptr});

  std::vector<Row> invariant = InvariantRows(skill);
  if (!invariant.empty()) {
    rule(rows);
    Append(std::move(invariant), rows);
  }

  // Two blocks, and which two is the only difference between the modes: the
  // current level and what one more point gives, or the first level and the
  // last. Both use the effective levels, including bonuses; whether a point is
  // left to spend depends on the learned level, which is why the two are
  // checked separately.
  int first = 1;
  int second = SkillMaxLevel(skill);
  bool has_second = second > first;
  if (levels == SkillInspectPanel::kLearned) {
    first = LevelWithBonus(skill, level, bonus);
    second = LevelWithBonus(skill, level + 1, bonus);
    // A point that gives nothing gets no block: bonus levels may already have
    // taken the skill to the maximum the next point would reach.
    has_second = level < SkillMaxLevel(skill) && second > first;
  }
  // The rule above the first block ends the top part. The rule between the
  // blocks scrolls with them, and the bar crosses it.
  if (first > 0 || has_second) {
    rule(card.head);
  }
  if (first > 0) {
    Append(LevelBlock(skill, first), card.body);
  }
  if (has_second) {
    if (first > 0) {
      rule(card.body);
    }
    // A node's next level shows its price, since its levels don't cost one
    // point each. Priced from the learned level, since the ladder charges for
    // the level being bought, whatever the heading shows.
    int cost = levels == SkillInspectPanel::kLearned
                   ? VNodeStepCost(skill.v_node(), level + 1)
                   : 0;
    Append(LevelBlock(skill, second, cost), card.body);
  }
  return card;
}

// The widest label among the rows, which sets the label column's width.
int WidestLabel(const std::vector<Row>& rows) {
  int widest = 0;
  for (const Row& row : rows) {
    if (row.kind == Row::kEffect) {
      widest = std::max(widest, TextColumns(row.label));
    }
  }
  return widest;
}

// The width the card asks for: its indent, the widest label with a gap after
// it, and the widest value, but at least the minimum a description needs.
int NaturalContentWidth(const std::vector<Row>& rows) {
  int value = 0;
  for (const Row& row : rows) {
    if (row.kind == Row::kEffect) {
      value = std::max(value, TextColumns(row.value));
    }
  }
  return std::max(kMinContentWidth,
                  kEffectIndent + WidestLabel(rows) + kLabelGap + value);
}

// The width inside the border, from what the rows need and what the screen
// allows. Both bounds are whole-card widths, and zero means no limit.
int ContentWidth(const std::vector<Row>& rows, int min_card, int max_card) {
  int content = NaturalContentWidth(rows);
  if (min_card > 0) {
    content = std::max(content, min_card - kCardChrome);
  }
  if (max_card > 0) {
    content = std::min(content, max_card - kCardChrome);
  }
  return std::max(content, kEffectIndent + kMinValueWidth);
}

// Every row of both groups, for measurements across the whole card: the label
// column is sized once, so the top part's facts and the level blocks below line
// up.
std::vector<Row> AllRows(const SkillRows& card) {
  std::vector<Row> rows = card.head;
  rows.insert(rows.end(), card.body.begin(), card.body.end());
  return rows;
}

// `rows` drawn into `content` columns, with labels cut to `widest_label`. A
// value too long continues on the next line with the label blank, instead of
// being cut mid-word.
std::vector<CardRow> LayOut(std::vector<Row> rows, int content,
                            int widest_label) {
  const std::string indent(kEffectIndent, ' ');
  // Just wide enough for the widest label, unless the card is too narrow to fit
  // that plus a readable value; then long labels get their own row.
  int label_width =
      std::min(widest_label + kLabelGap,
               std::max(1, content - kEffectIndent - kMinValueWidth));
  int value_width = std::max(1, content - kEffectIndent - label_width);
  std::vector<CardRow> lines;
  for (Row& row : rows) {
    if (row.kind == Row::kRule) {
      lines.push_back(RuleRow(std::move(row.element)));
      continue;
    }
    if (row.kind == Row::kWhole) {
      lines.push_back(TextRow(std::move(row.element)));
      continue;
    }
    if (row.kind == Row::kProse) {
      for (const std::string& line : WrapText(row.value, content - 2)) {
        lines.push_back(TextRow(ftxui::text(" " + line)));
      }
      continue;
    }
    std::string head = row.label;
    // A label that is too wide gets its own row rather than being cut, since
    // half a skill name isn't a name, and the value goes below it.
    if (TextColumns(head) > label_width) {
      lines.push_back(TextRow(ftxui::text(indent + head)));
      head.clear();
    }
    for (const std::string& line : WrapText(row.value, value_width)) {
      lines.push_back(
          TextRow(ftxui::text(indent + PadRight(head, label_width) + line)));
      head.clear();
    }
  }
  return lines;
}

// The card's rows, laid out and grouped for ScrollCard to draw.
CardRows Laid(const SkillRows& card, int content) {
  int widest = WidestLabel(AllRows(card));
  CardRows rows;
  rows.head = LayOut(card.head, content, widest);
  rows.body = LayOut(card.body, content, widest);
  return rows;
}

// A row budget no card reaches.
constexpr int kUnboundedRows = 1 << 20;

}  // namespace

ScrollCard UnboundedCard() {
  ScrollCard card;
  card.SetMaxRows(kUnboundedRows);
  return card;
}

void SkillInspectPanel::SetMaxRows(int rows) {
  card_.SetMaxRows(rows > 0 ? rows : kUnboundedRows);
}

void SkillInspectPanel::SetSkill(const Skill* skill, int learned, int bonus,
                                 Levels levels) {
  skill_ = skill;
  level_ = learned;
  bonus_ = bonus;
  levels_ = levels;
}

void SkillInspectPanel::ScrollBy(int delta) {
  // Laid out first, since the card reports from its last render and the budget
  // or the skill may have changed since.
  Render();
  card_.ScrollBy(delta);
}

ftxui::Element SkillInspectPanel::Render() const {
  if (skill_ == nullptr) {
    return ThemedWindow(" Skill ", EmptyState("no skill"));
  }
  SkillRows card = CardRowsFor(*skill_, level_, bonus_, levels_);
  int content = ContentWidth(AllRows(card), min_width_, max_width_);
  // What kind of skill it is, the first thing worth knowing about it.
  std::string title = IsActive(*skill_) ? " Active " : " Passive ";
  return card_.Render(title, Laid(card, content), content);
}

std::vector<SkillEffectLine> SkillEffectsAt(const Skill& skill, int level) {
  std::vector<SkillEffectLine> lines;
  for (const Row& row : EffectRows(skill, level)) {
    if (row.kind == Row::kEffect) {
      lines.push_back({row.label, row.value});
    }
  }
  return lines;
}

PreviewCardSize LargestPreviewCard(const std::vector<const Skill*>& skills,
                                   int max_columns) {
  PreviewCardSize size;
  SkillInspectPanel panel;
  // Two passes: a card's height depends on the width it was laid out at, so the
  // widest card sets the width and then every card is measured again.
  panel.SetWidthBounds(0, max_columns);
  for (int pass = 0; pass < 2; ++pass) {
    for (const Skill* skill : skills) {
      if (skill == nullptr) {
        continue;
      }
      panel.SetSkill(skill, 0, 0, SkillInspectPanel::kPreview);
      ftxui::Element card = panel.Render();
      card->ComputeRequirement();
      if (pass == 0) {
        size.columns = std::max(size.columns, card->requirement().min_x);
      } else {
        size.rows = std::max(size.rows, card->requirement().min_y);
      }
    }
    panel.SetWidthBounds(size.columns, size.columns);
  }
  return size;
}

}  // namespace ms
