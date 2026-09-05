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
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// The narrowest the card lays out at, inside its border. Descriptions carry
// GMS's own sentences at their own length, so the card is wide enough to read
// them a clause at a time; a skill whose rows want less than this still gets
// it, and spends the slack on its value column.
constexpr int kMinContentWidth = 58;
// The border either side and the scroll bar's column, which the card holds
// open whether or not there is anything to scroll.
constexpr int kCardChrome = 3;
// Effect rows indent past the one-space border gutter, so they read as
// belonging to the "Level N" heading above them.
constexpr int kEffectIndent = 3;
// The gap between a label and its value.
constexpr int kLabelGap = 2;
// What a value keeps when a label is too wide for the card to seat them both.
// Past this the label takes a row of its own instead.
constexpr int kMinValueWidth = 8;

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
    {"Boss Damage", &SkillEffect::boss_pct_per_combo_orb, kPlus,
     " per Combo Orb"},
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
    // What the barrier takes off whatever is hitting the character. A price
    // paid by the monster, so it reads as a subtraction the way the row above
    // it does.
    {"Enemy ATT", &SkillEffect::enemy_attack_pct, kMinus, ""},
    {"Enemy ATT", &SkillEffect::enemy_attack_pct_when_scarred, kMinus,
     " while Scarred"},
    {"Damage to MP", &SkillEffect::damage_to_mp_pct, kBare, ""},
    {"Reflected", &SkillEffect::damage_reflect_pct, kBare, ""},
    // Maple Warrior's, and the only row charged against what the player spent
    // rather than against a total the game knows.
    {"Stats from AP", &SkillEffect::ap_stat_pct, kPlus, ""},
    // Maple World Goddess's Blessing's, and a share of that row rather than of
    // anything the character has -- so it names the skill it multiplies.
    {"Maple Warrior", &SkillEffect::ap_stat_bonus_pct, kPlus, ""},
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
    // The share of that chance this swing gives up, which is a price and reads
    // as one.
    {"Meso Drop Chance", &SkillEffect::meso_drop_cut, kMinus, ""},
    // Last, and the only rows here that are not about a fight -- the same
    // place they take on the stats page, for the same reason.
    {"Meso Drop Rate", &SkillEffect::meso_pct, kPlus, ""},
    {"Item Drop Rate", &SkillEffect::item_drop_pct, kPlus, ""},
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
    // What comes off that wait. Named for the clock rather than for the
    // revival, because the skill stating it does not revive anybody -- it
    // shortens the wait of the pact that does.
    {"Revive Cooldown", &SkillEffect::revive_cooldown_cut_seconds, kMinus, "s"},
};

struct FlatLever {
  const char* label;
  double (SkillEffect::*fn)() const;
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
    // Glacial Fury's pair. The magic attack is an ICE swing's alone, which the
    // row says because nothing else on the page does.
    {"Freeze Stacks", &SkillEffect::freeze_stack_cap_bonus, "", false},
    {"Ice MATT", &SkillEffect::magic_attack_per_freeze_stack,
     " per Freeze Stack", false},
    {"DEF", &SkillEffect::def_per_combo_orb, " per Combo Orb", false},
    {"STR", &SkillEffect::str, "", false},
    {"DEX", &SkillEffect::dex, "", false},
    {"INT", &SkillEffect::int_, "", false},
    {"LUK", &SkillEffect::luk, "", false},
    {"Max HP", &SkillEffect::max_hp, "", false},
    {"Max MP", &SkillEffect::max_mp, "", false},
    {"Max HP", &SkillEffect::max_hp_per_level, " per level", false},
    {"Max MP", &SkillEffect::max_mp_per_level, " per level", false},
    {"Attack Speed", &SkillEffect::attack_speed, " stage", true},
    // The same row: what the card has to say about a stage is what it is
    // worth, and the cap it answers to is the stats page's business.
    {"Attack Speed", &SkillEffect::uncapped_attack_speed, " stage", true},
};

// The value of a lever at learned level L, in the same shape the stats
// themselves are folded with.
double PercentAt(const Skill& skill, double (SkillEffect::*fn)() const,
                 int level) {
  return (skill.base().*fn)() + (skill.per_level().*fn)() * (level - 1);
}

// The same, for a lever counted in whole numbers: the ladder is walked as a
// fraction and floored where it is read, as everywhere else.
int FlatAt(const Skill& skill, double (SkillEffect::*fn)() const, int level) {
  return WholeValue(PercentAt(skill, fn, level));
}

// A fraction as a percentage, to one decimal, with a whole number left whole.
// Rounds rather than truncates: summing a lever's per-level steps lands a hair
// under the round figure (16 levels of +1% is 0.15999...), and a skill that
// reads "15.9%" at the level its data says 16% is simply wrong.
//
// A lever too small for that decimal gets a second one. Mortal Blow puts back
// a hundredth of a percent a swing at its first level, and "0%" would be a
// page saying the point bought nothing.
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

// What an empowered form upgrades, as the page names it. A form carried by a
// skill with no attack of its own and no skill named is a data error the
// catalog test catches; here it reads as the skill's own attack.
std::string EmpoweredTarget(const EmpoweredForm& form) {
  return form.skill_name().empty() ? "attack" : form.skill_name();
}

// A row of the card before the card knows how wide it is. An effect row is
// its label and what that label is worth, laid out once the widest of each is
// known; everything else is already whole.
struct Row {
  enum Kind {
    kEffect,  // label and value, in the card's two columns
    kProse,   // one paragraph, wrapped to the card and indented a space
    kWhole,   // an element that is its own row: a heading, an empty state
    kRule,    // a section divider, drawn across the scroll bar as well
  };
  Kind kind = kEffect;
  std::string label;
  std::string value;
  ftxui::Element element;
};

// An effect row. A value with nothing in it writes no row at all -- there is
// nothing to say about a lever the skill does not carry.
Row EffectRow(std::string label, std::string value) {
  return {Row::kEffect, std::move(label), std::move(value), nullptr};
}

Row TextRow(ftxui::Element element) {
  return {Row::kWhole, "", "", std::move(element)};
}

// Breaks a " / " separated list across lines without splitting an entry: the
// separator ends the line it falls on. "One-Handed Sword / Two-Handed Axe"
// broken between the words says the wrong weapon, so the list is packed by
// entry rather than by word.
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
// word longer than the column is left whole and allowed to overhang rather
// than being cut in half, which no description here comes close to needing.
// A list breaks by entry instead; see WrapList.
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
std::vector<Row> RequirementRows(const Skill& skill) {
  std::vector<Row> rows;
  rows.push_back(EffectRow("Required Weapon",
                           RequiredWeapons(skill.required_equip_type())));
  // The level a Hyper Skill opens at, which is the gate it carries in place of
  // a skill below it.
  if (skill.required_level() > 0) {
    rows.push_back(
        EffectRow("Required Level", std::to_string(skill.required_level())));
  }
  if (!skill.has_required_skill()) {
    return rows;
  }
  // Built from the requirement rather than typed beside it, so the sentence
  // and the rule the skills tab enforces cannot drift apart.
  std::string required = skill.required_skill().skill_name() + " Lv. " +
                         std::to_string(skill.required_skill().level()) + "+";
  rows.push_back(EffectRow("Required Skill", required));
  return rows;
}

// What this skill takes over from, for the handful that state the whole of an
// earlier skill rather than a delta over it. Without the row the two read as
// though they stack, and a player would count the older one twice.
std::vector<Row> ReplacesRows(const Skill& skill) {
  if (skill.supersedes_skill_name().empty()) {
    return {};
  }
  return {EffectRow("Replaces", skill.supersedes_skill_name())};
}

// The group a skill shares its levers with, where it is in one. The same
// warning as the row above and a weaker one: these do stack with everything
// else, and only outbid each other. See Skill.exclusive_group.
//
// A group is usually named for the skill the rest of it stands in for, so the
// row is left off that one: "Sharp Eyes does not stack with Sharp Eyes" says
// nothing, and what it does not stack with says so on its own card.
std::vector<Row> ExclusiveGroupRows(const Skill& skill) {
  if (skill.exclusive_group().empty() ||
      skill.exclusive_group() == skill.name()) {
    return {};
  }
  return {EffectRow("Does Not Stack With", skill.exclusive_group())};
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

// Appends `from` to `into`. The row builders here all return vectors, and a
// row is a move rather than a copy.
void Append(std::vector<Row> from, std::vector<Row>& into) {
  for (Row& row : from) {
    into.push_back(std::move(row));
  }
}

// `rows` with the silent ones dropped: an effect row with an empty value says
// nothing about a lever the skill does not carry. Dropped here rather than at
// layout so that a block of nothing but those still reads as empty, and takes
// neither a divider nor a heading.
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

// How far the swing reaches and how often it lands, its own-clock halves
// included. Reach and rate share a row where a skill has both: the two
// together are the shape of it.
std::vector<Row> ReachRows(const Skill& skill) {
  std::vector<Row> rows;
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
  // A swing thrown as scattered strikes says so on its own row: the reach above
  // is how far it can spread before it starts doubling up, and the cut is what
  // doubling up costs.
  if (skill.scatter().hits() > 0) {
    std::string text = std::to_string(skill.scatter().hits()) + " strikes";
    if (skill.scatter().repeat_final_dmg_pct() != 0.0) {
      text += ", repeats at " +
              FormatPercent(skill.scatter().repeat_final_dmg_pct()) +
              " Final Damage";
    }
    rows.push_back(EffectRow("Scattered", text));
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
  return rows;
}

// Which attack this skill upgrades, how often, and how far the upgraded swing
// carries when that is not the swing it stands in for.
std::vector<Row> EmpoweredRows(const Skill& skill) {
  std::vector<Row> rows;
  // A skill that upgrades an attack says which attack and how often. Its reach
  // is stated too, unlike a turret's: this one is wider than the attack it
  // stands in for, so leaving it out would understate the upgrade. An empty
  // name is the skill upgrading its own attack, which has no name to give.
  for (const EmpoweredForm& form : skill.empowered_form()) {
    if (form.casts_per_trigger() <= 0) {
      continue;
    }
    // An upgrade with no rate on it is unconditional -- "Empowers" already
    // says so, and a rate is what tells the other shape apart from it.
    std::string how = form.casts_per_trigger() == 1
                          ? ""
                          : "Every " + Ordinal(form.casts_per_trigger()) + " ";
    rows.push_back(EffectRow("Empowers", how + EmpoweredTarget(form)));
    // A mark on each enemy is a different promise from a count on the swing --
    // five that one enemy took, rather than five swings -- and only a row of
    // its own says which of the two the number above is.
    if (form.brands_each_enemy()) {
      rows.push_back(EffectRow("Marks", "Each Enemy Hit"));
    }
    // Only a form that states a reach of its own gets the row: the Sniper's is
    // wider than the swing it stands in for, and leaving that out would
    // understate the upgrade. One that says nothing goes exactly as far as
    // what it replaces, and a row repeating that is noise.
    if (!form.brands_each_enemy() && form.max_enemies() > 1) {
      rows.push_back(
          EffectRow("Empowered Enemies", std::to_string(form.max_enemies())));
    }
  }
  return rows;
}

// Everything about a skill that reads the same at every level.
// Which element a swing is, for the pair a Freezing Crush wizard alternates
// between. Only these two tags reach the page: the rest mark a family nothing
// in this game reads yet.
// The element row, and the freeze riding it. How LONG the ice holds is not
// printed for the reason no other clock is -- the pacing band stretches it --
// but which swings freeze at all is a real difference between them, and the
// one Frozen Orb is on the wrong side of. Frostprey freezes and carries no
// element, so the freeze takes a row of its own when nothing tags the skill.
std::vector<Row> ElementRows(const Skill& skill) {
  bool freezes = skill.freeze_seconds() > 0.0;
  for (int i = 0; i < skill.tags_size(); ++i) {
    if (skill.tags(i) == SKILL_TAG_ICE) {
      return {EffectRow("Element", freezes ? "Ice, Freezes" : "Ice")};
    }
    if (skill.tags(i) == SKILL_TAG_LIGHTNING) {
      return {EffectRow("Element", "Lightning")};
    }
  }
  if (freezes) {
    return {EffectRow("Freezes", "What it hits")};
  }
  return {};
}

// The rows that hold at every level: what the skill asks for and how far a
// swing reaches. A skill with none of them gets no block at all.
//
// No row here counts seconds. The pacing band stretches every duration in the
// game alike, so a figure the player could hold a stopwatch to would not be
// the one printed -- and none of them is a choice they make anyway.
std::vector<Row> InvariantRows(const Skill& skill) {
  std::vector<Row> rows = RequirementRows(skill);
  Append(ReplacesRows(skill), rows);
  Append(ExclusiveGroupRows(skill), rows);
  Append(ElementRows(skill), rows);
  Append(ReachRows(skill), rows);
  // How deep the pile of Freeze Stacks the skill grants goes. Stated here
  // rather than at the level because the cap never climbs -- what a point buys
  // is what one stack is worth.
  if (skill.freeze_stack_cap() > 0) {
    rows.push_back(EffectRow(
        "Freeze Stacks", "Up to " + std::to_string(skill.freeze_stack_cap())));
  }
  // The same, for the burns the drains count: the cap never climbs either.
  if (skill.dot_count_cap() > 0) {
    rows.push_back(EffectRow("Burns Counted",
                             "Up to " + std::to_string(skill.dot_count_cap())));
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
  // The other counted clock: what the player leaves dead behind them.
  if (skill.kills_per_cast() > 0) {
    rows.push_back(EffectRow(
        "Fires Every", std::to_string(skill.kills_per_cast()) + " Defeats"));
  }
  Append(EmpoweredRows(skill), rows);
  // How long the player swings something else for afterwards, which is what a
  // skill this much better than the usual swing costs. A wait that shortens as
  // the skill is taught is not invariant, so it waits for the level block.
  if (skill.cooldown_seconds() > 0.0 &&
      skill.cooldown_seconds_per_level() == 0.0) {
    rows.push_back(EffectRow("Cooldown", CooldownText(skill, 1)));
  }
  return Speaking(std::move(rows));
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

// One clause onto the list, comma-separated. A boost granting several has to
// name each of them, and a bare "+1 Strike" beside a bare "+5s" would say
// neither.
void AppendGain(const std::string& text, std::string& gains) {
  if (text.empty()) {
    return;
  }
  if (!gains.empty()) {
    gains += ", ";
  }
  gains += text;
}

// What a boost adds to the shape of the swing it names: strikes, reach, the
// wait, the clock.
std::string StructureBoostText(const SkillBoost& boost, int level) {
  // Read exactly as the line ladder is, so the level a skill widens at is the
  // level its data names.
  constexpr double kEnemyEpsilon = 1e-9;
  std::string gains;
  if (boost.lines() > 0) {
    gains = "+" + std::to_string(boost.lines()) +
            (boost.lines() == 1 ? " Strike" : " Strikes");
  }
  // Told apart from the line above because they are different strikes: what
  // the swing lands beside itself is priced on its own, so a row saying only
  // "+1 Strike" would be read as the swing's.
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
  // The share off the wait, as GMS writes it -- the seconds it comes to are
  // on the target's own page, which states its whole ladder.
  if (boost.cooldown_pct() > 0.0) {
    AppendGain("-" + FormatPercent(boost.cooldown_pct()) + " Cooldown", gains);
  }
  // The new clock rather than the change to it: what replaces cannot be read
  // as a delta, and the target's own page states the same figure the same way.
  if (boost.attacks_per_cast() > 0) {
    AppendGain("every " + std::to_string(boost.attacks_per_cast()) + " attacks",
               gains);
  }
  return gains;
}

// What a boost adds to the mark the named skill leaves, and to the buff it
// stands as. Both are apart from the lever table below for the same reason:
// neither is the swing, and each states a clock of its own.
std::string MarkBoostText(const SkillBoost& boost, int level) {
  std::string gains;
  double dot_pct =
      boost.dot_skill_pct() + boost.dot_skill_pct_per_level() * (level - 1);
  if (dot_pct != 0.0) {
    AppendGain((dot_pct > 0.0 ? "+" : "") + FormatPercent(dot_pct) +
                   " DoT Damage per Tick",
               gains);
  }
  // Seconds rather than a share, which is how GMS states it and the only way
  // the row could be read: the target's page states the whole ladder.
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

// What one skill hands another that is not damage, at `level`: strikes on
// every swing, enemies on its reach, a shorter wait, a longer buff, the levers
// it alone carries, or several. "" when it grants none of them.
std::string BoostText(const SkillBoost& boost, int level) {
  std::string gains = StructureBoostText(boost, level);
  AppendGain(MarkBoostText(boost, level), gains);
  // The levers the boost hands that skill alone, named because a sentence
  // granting two of them cannot leave either unsaid. In the order the effect
  // rows above use.
  struct BoostLever {
    const char* label;
    double (SkillEffect::*fn)() const;
  };
  const BoostLever kBoostLevers[] = {
      // Points on the skill's own multiplier, which every strike of it lands
      // -- so the row says per strike. The row below is the other bargain: a
      // share of the character's damage that only this swing collects, which
      // is what the stats page and GMS both call Damage. See
      // SkillBoost::effect.
      {"Damage per Strike", &SkillEffect::skill_pct},
      {"Damage", &SkillEffect::damage_pct},
      {"Final Damage", &SkillEffect::final_dmg_pct},
      {"Boss Damage", &SkillEffect::boss_pct},
      {"Ignore DEF", &SkillEffect::ied_pct},
      {"Critical Rate", &SkillEffect::crit_rate},
      {"Final Attack Rate", &SkillEffect::final_attack_chance},
  };
  for (const BoostLever& lever : kBoostLevers) {
    double value = (boost.effect().*lever.fn)() +
                   (boost.effect_per_level().*lever.fn)() * (level - 1);
    if (value == 0.0) {
      continue;
    }
    // A lever can be handed out backwards -- Hurricane - Split Attack pays a
    // second arrow for a quarter off what each one lands -- and FormatPercent
    // writes the minus itself, so only the plus has to be put back.
    AppendGain(
        (value > 0.0 ? "+" : "") + FormatPercent(value) + " " + lever.label,
        gains);
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
  // How many of the swing's enemies it lands on: one for the opening hit every
  // rogue swings, more for an arrow's fragment, which bounces onto several.
  int enemies = std::max(1, skill.lead_enemies());
  if (enemies == 1) {
    return damage + " (one enemy)";
  }
  return damage + " (" + std::to_string(enemies) + " enemies)";
}

// The plain lever rows of one effect, at `level`. `suffix` goes after every
// value: a weapon bonus uses it to name what has to be in hand, and the
// skill's own levers pass "" because the Requires row above says it once.
// The damage rows for hits a swing lands beside its own: each on its own line,
// with what it is worth against an ordinary monster under it where that
// differs. Shared, because an empowered form lands them too.
std::vector<Row> SwingHitRows(
    const google::protobuf::RepeatedPtrField<SwingHit>& hits, int level) {
  std::vector<Row> rows;
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
    rows.push_back(EffectRow(hit.label(), text));
    double bonus = hit.base().normal_skill_pct() +
                   hit.per_level().normal_skill_pct() * (level - 1);
    if (bonus > 0.0) {
      rows.push_back(EffectRow(hit.label() + " Normal",
                               SwingText(per_hit + bonus, hit.lines())));
    }
  }
  return rows;
}

std::vector<Row> LeverRows(const SkillEffect& base, const SkillEffect& per,
                           int level, const std::string& suffix) {
  std::vector<Row> rows;
  for (const FlatLever& lever : kFlatLevers) {
    // WholeValue rather than a cast, so the page floors a fractional ladder
    // by the same rule the character is granted by rather than restating it.
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

// A fountain states both halves. Neither alone says what a point bought: the
// pulse grows and the wait between pulses shortens together, so a page showing
// only the pulse would understate every point after the first.
std::vector<Row> RegenRows(const Skill& skill, int level) {
  double regen = PercentAt(skill, &SkillEffect::regen_pct, level);
  int regen_hp = FlatAt(skill, &SkillEffect::regen_hp, level);
  double interval =
      PercentAt(skill, &SkillEffect::regen_interval_seconds, level);
  if ((regen <= 0.0 && regen_hp <= 0) || interval <= 0.0) {
    return {};
  }
  // A fountain pouring both states both, the flat half first: it is the one
  // the player can hold against their pool.
  std::string poured;
  if (regen_hp > 0) {
    poured = std::to_string(regen_hp) + " HP";
  }
  if (regen > 0.0) {
    poured += (poured.empty() ? "" : " and ") + FormatPercent(regen);
  }
  std::string text = poured + " every " + FormatNumber(interval) + "s";
  // Holy Water pours one more helping per step of INT, which is most of what a
  // Bishop's points buy. Stated with the pulse rather than as a row of its
  // own: alone it would read as a share of the pool.
  double step = PercentAt(skill, &SkillEffect::regen_int_step, level);
  if (step > 0.0) {
    text +=
        ", +" + FormatPercent(regen) + " per " + FormatNumber(step) + " INT";
  }
  return {EffectRow("HP Recovered", text)};
}

// The strike a hold ends on, which reads exactly as any other second hit since
// that is what it is -- one landed once however long the hold ran.
std::vector<Row> ChannelFinishRows(const Skill& skill, int level) {
  google::protobuf::RepeatedPtrField<SwingHit> finish;
  *finish.Add() = skill.channel().finish();
  std::vector<Row> rows = SwingHitRows(finish, level);
  if (skill.channel().damage_taken_pct() > 0.0) {
    rows.push_back(
        EffectRow("Damage Taken",
                  "-" + FormatPercent(skill.channel().damage_taken_pct())));
  }
  return rows;
}

// What the skill itself does when it goes off: its damage, its healing, and
// the shapes a plain lever row cannot state.
std::vector<Row> OwnEffectRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  bool held = skill.channel().max_pulses() > 0;
  if (IsActive(skill) && PercentAt(skill, &SkillEffect::skill_pct, level) > 0) {
    // A hold's damage row is ONE pulse of it, so it says so: the swing lands
    // that over and over, and a row reading as the whole thing would be out by
    // its pulse count.
    rows.push_back(EffectRow(held ? "Damage per Pulse" : "Damage",
                             DamageText(skill, level)));
  }
  // How many pulses one hold is worth. A count rather than a clock, which is
  // the half of the hold the player can act on -- they let go when it stops
  // paying.
  if (held) {
    rows.push_back(EffectRow(
        "Pulses", "Up to " + std::to_string(skill.channel().max_pulses())));
  }
  Append(RegenRows(skill, level), rows);
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
  int strikes = FlatAt(skill, &SkillEffect::bonus_attack_lines, level);
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
  // The barrier opened onto bosses. Flat for the same reason Dispel's row is:
  // the switch is thrown once and no level throws it further.
  if (skill.base().enemy_attack_reaches_boss()) {
    rows.push_back(EffectRow("Enemy ATT", "Also Reduced on Bosses"));
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
  Append(SwingHitRows(skill.extra_hit(), level), rows);
  if (held) {
    Append(ChannelFinishRows(skill, level), rows);
  }
  // Under the swing's own damage, because it is the extra the swing opens with
  // rather than a second attack.
  rows.push_back(EffectRow("Opening Hit", LeadText(skill, level)));
  return rows;
}

// The burn a swing leaves, as one row: what a tick is worth, how often it
// comes and how long it lasts. All three on the damage row, because none of
// them says anything on its own and a clock never gets a row of its own here.
std::string DotText(const Dot& dot, int level) {
  double per_tick =
      dot.base().skill_pct() + dot.per_level().skill_pct() * (level - 1);
  double burns_for =
      dot.duration_seconds() + dot.duration_seconds_per_level() * (level - 1);
  std::string text = SwingText(per_tick, dot.lines()) + " every " +
                     FormatNumber(dot.interval_seconds(), 2) + "s for " +
                     FormatNumber(burns_for) + "s";
  // What a poison the character carries has and a burn a swing leaves does
  // not: it is rolled for, and it piles up. Both are silent on the burns that
  // simply take hold once.
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

// The chance every swing has to land harder on one enemy. Its chance and what
// it pays are one fact, exactly as a Final Attack's are, so they share a line
// -- and the recovery it hands back takes a line of its own, being what the
// player gets rather than what the enemy takes.
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

// The burn the swing leaves, and the strike it sets off beside itself.
std::vector<Row> SwingRiderRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  // Under the swing's own damage, since it is what that swing left behind.
  if (skill.dot().interval_seconds() > 0.0) {
    rows.push_back(EffectRow("DoT", DotText(skill.dot(), level)));
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
    int strikes = FlatAt(skill, &SkillEffect::final_attack_lines, level);
    std::string text =
        FormatPercent(proc) + " for " +
        SwingText(PercentAt(skill, &SkillEffect::final_attack_pct, level),
                  strikes) +
        reach;
    rows.push_back(EffectRow("Final Attack", text));
  }
  // The strike the swing sets off beside itself. Its wait rides the damage row
  // rather than taking one of its own, and its reach is stated only where it
  // is not the swing's -- see the Enemies Hit row above.
  if (skill.has_side_strike()) {
    const SideStrike& side = skill.side_strike();
    double per_hit =
        side.base().skill_pct() + side.per_level().skill_pct() * (level - 1);
    std::string text = SwingText(per_hit, side.lines());
    if (side.max_enemies() > 0 && side.max_enemies() != skill.max_enemies()) {
      text += ", " + std::to_string(side.max_enemies()) + " enemies";
    }
    text += " every " + FormatNumber(side.cooldown_seconds()) + "s";
    rows.push_back(EffectRow(side.label(), text));
    double normal = side.base().normal_skill_pct() +
                    side.per_level().normal_skill_pct() * (level - 1);
    if (normal > 0.0) {
      rows.push_back(EffectRow(side.label() + " Normal",
                               SwingText(per_hit + normal, side.lines())));
    }
    // A side strike scatters on its own row for the same reason the swing's
    // does, and under its own name: the reach above is how far it spreads.
    if (side.scatter().hits() > 0) {
      std::string scattered =
          std::to_string(side.scatter().hits()) + " strikes";
      if (side.scatter().repeat_final_dmg_pct() != 0.0) {
        scattered += ", repeats at " +
                     FormatPercent(side.scatter().repeat_final_dmg_pct()) +
                     " Final Damage";
      }
      rows.push_back(EffectRow(side.label() + " Scattered", scattered));
    }
  }
  return rows;
}

// The damage of every half that fights on a clock of its own, and of every
// swing this skill puts in the place of another.
std::vector<Row> OwnClockRows(const Skill& skill, int level) {
  std::vector<Row> rows;
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
    // What the upgraded swing lands beside itself, under the swing it belongs
    // to -- the explosion at the end of an arrow's flight, the mark it spends.
    Append(SwingHitRows(form.extra_hit(), level), rows);
  }
  return rows;
}

// What this skill hands to another skill in the book, one sentence a grant.
std::vector<Row> BoostRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  // The skill it boosts belongs in the label: the label says what is boosted
  // and the value by how much, which is the shape every other row here has.
  // One row per skill named, because two skills granted different things
  // cannot share a row.
  for (const SkillBoost& granted : skill.boost()) {
    std::string gains = BoostText(granted, level);
    if (gains.empty()) {
      continue;
    }
    rows.push_back(EffectRow("Boosts " + granted.skill_name(), gains));
  }
  return rows;
}

// Everything that lands beside the swing rather than as part of it, and what
// the skill hands to another skill in the book.
std::vector<Row> ExtraAttackRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  Append(SwingRiderRows(skill, level), rows);
  Append(OwnClockRows(skill, level), rows);
  Append(BoostRows(skill, level), rows);
  return rows;
}

// A heading over one half of a skill that has two. Orange and green are the
// skill list's own tags for active and passive (game_names' TagFor), so the
// two halves are told apart here by the colours the player learned there.
// Only the heading is coloured: a value that is always coloured says nothing.
Row SectionRow(const std::string& label, ftxui::Color color) {
  return TextRow(ftxui::text(" " + label) | ftxui::color(color));
}

// The shell a buff stands as: the hits it swallows whole, and what it does
// about the ones it cannot. Empty for every buff that is not one.
std::vector<Row> ShieldRows(const Shield& shield, int level) {
  std::vector<Row> rows;
  int hits = ShieldHitsAt(shield, level);
  if (hits <= 0) {
    return rows;
  }
  rows.push_back(EffectRow("Blocks", std::to_string(hits) + " attacks"));
  if (shield.boss_damage_taken_pct() > 0.0) {
    // Named for what it covers rather than for what it is: a player reading
    // this wants to know it is the boss hits the shell cannot swallow.
    rows.push_back(
        EffectRow("Damage Taken (Boss)",
                  "-" + FormatPercent(shield.boss_damage_taken_pct())));
  }
  return rows;
}

// What a timed buff grants, headed by how long it stands. The wait for the
// next one is the skill's own Cooldown row, above. No row here says "while
// up" -- the heading says it once for all of them.
// What the buff bleeds. A pulse borrowing the swing's reach says its damage and
// its clock on one row -- the two are one fact, and the enemies are the ones
// the swing above already states. One reaching enemies of its own says so where
// every other own-clock half does, in an Attacks row, and keeps the damage row
// for the damage.
std::vector<Row> PulseRows(const BuffPulse& pulse, int level) {
  if (pulse.cast_interval_seconds() <= 0.0) {
    return {};
  }
  double per_hit =
      pulse.base().skill_pct() + pulse.per_level().skill_pct() * (level - 1);
  int lines = std::max(1, pulse.lines());
  int strikes = std::max(1, pulse.casts());
  std::string damage = SwingText(per_hit, pulse.lines());
  if (strikes > 1) {
    // Written in GMS's own order -- so much damage, so many times, so many
    // strikes -- because the total alone hides which of the three moved.
    damage = FormatPercent(per_hit) + " x" + std::to_string(lines) + " x" +
             std::to_string(strikes) + " = " +
             FormatPercent(per_hit * lines * strikes);
  }
  if (pulse.max_pulses() > 0) {
    damage += ", " + std::to_string(pulse.max_pulses()) + " times";
  }
  if (pulse.max_enemies() == 0) {
    return {EffectRow(pulse.label(),
                      damage + " every " +
                          FormatNumber(pulse.cast_interval_seconds(), 2) +
                          "s")};
  }
  return {EffectRow(pulse.label(), damage),
          EffectRow("Attacks", ReachText(pulse.max_enemies(),
                                         pulse.cast_interval_seconds()))};
}

// What everybody else in the party gets while the buff stands, in the colour
// the party screens are drawn in. Under the buff's own heading rather than at
// the foot of the card, because these lapse with it. See Buff.ally_base.
std::vector<Row> AllyBuffRows(const Buff& buff, int level) {
  SkillEffect base = buff.ally_base();
  SkillEffect per = buff.ally_per_level();
  double heal = base.heal_pct() + per.heal_pct() * (level - 1);
  base.clear_heal_pct();
  per.clear_heal_pct();
  std::vector<Row> levers = LeverRows(base, per, level, "");
  // A party shell stands over everybody as one, so what it blocks is stated
  // for them exactly as it is for the caster. One that is not says nothing
  // here -- see Shield.party.
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
  Append(std::move(shield), rows);
  return rows;
}

std::vector<Row> BuffRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  const Buff& buff = skill.buff();
  if (buff.duration_seconds() <= 0.0) {
    return rows;
  }
  // A buff bought with landed hits says so in its heading: that count is the
  // whole of what it costs, and there is no Cooldown row to carry it.
  std::string charge =
      buff.charge_lines() > 0
          ? " every " + std::to_string(buff.charge_lines()) + " hits"
          : "";
  // A shared buff says so here rather than in a Your Party section: it grants
  // the party nothing of its own -- everyone raises the same one in turn.
  std::string shared = buff.party_shared() ? ", shared with your party" : "";
  rows.push_back(SectionRow(
      "Active for " +
          FormatNumber(buff.duration_seconds() +
                       buff.duration_seconds_per_level() * (level - 1)) +
          "s" + charge + shared,
      kGold));
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
  Append(LeverRows(base, per, level, ""), rows);
  Append(ShieldRows(buff.shield(), level), rows);
  Append(PulseRows(buff.pulse(), level), rows);
  Append(AllyBuffRows(buff, level), rows);
  return rows;
}

// What the skill simply keeps, however it chose to write it: an attack states
// its own half apart from the levers riding its swing, and a passive states
// everything here. What a chance pays lands here too -- nothing about it
// lapses, and a passive that only ever fires sometimes is a passive still.
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

// A weapon bonus reads as the lever it grants with the weapons it needs in
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

// Everything the skill grants at `level`. Empty for a skill whose real effect
// is something this game has no notion of.
std::vector<Row> EffectRows(const Skill& skill, int level) {
  std::vector<Row> rows;
  Append(OwnEffectRows(skill, level), rows);
  Append(ExtraAttackRows(skill, level), rows);
  // A skill can grant three different things, and only one of them is the
  // character's for good: what rides the swing it states them on, what stands
  // for as long as a buff is up, and what it simply keeps. Each is headed
  // where another is present, because "Ignore DEF" is otherwise the same row
  // meaning three things. A skill with one half alone needs no heading and
  // gets none.
  std::vector<Row> swing =
      skill.kind() == SKILL_KIND_ATTACK
          ? LeverRows(SwingLeversOf(skill.base()),
                      SwingLeversOf(skill.per_level()), level, "")
          : std::vector<Row>();
  std::vector<Row> permanent = PermanentRows(skill, level);
  std::vector<Row> buff = BuffRows(skill, level);
  // Read before either is handed over, since Append takes its rows by value.
  bool has_swing = !swing.empty();
  bool has_buff = !buff.empty();
  if (has_swing) {
    rows.push_back(SectionRow("This Attack Only", kGold));
    Append(std::move(swing), rows);
  }
  Append(std::move(buff), rows);
  // A skill paid only for shielding somebody heads its own half whatever else
  // is on the page: a player maxing it alone and seeing nothing move has to
  // be able to read why here.
  std::vector<Row> ally =
      LeverRows(skill.ally_base(), skill.ally_per_level(), level, "");
  if (!permanent.empty() &&
      (has_buff || has_swing || skill.requires_party() || !ally.empty())) {
    rows.push_back(SectionRow(
        skill.requires_party() ? "Passive, in a Party" : "Passive", kGreen));
  }
  Append(std::move(permanent), rows);
  // What everybody else in the party gets, in the colour the party screens
  // are drawn in. Its own section: these are not the reader's numbers.
  if (!ally.empty()) {
    rows.push_back(SectionRow("Your Party", kTheme));
    Append(std::move(ally), rows);
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
  Append(WeaponBonusRows(skill), rows);
  return Speaking(std::move(rows));
}

// One "Level N" heading and the effects under it.
std::vector<Row> LevelBlock(const Skill& skill, int level) {
  std::vector<Row> rows;
  rows.push_back(TextRow(ftxui::text(" Level " + std::to_string(level))));
  std::vector<Row> effects = EffectRows(skill, level);
  if (effects.empty()) {
    // A skill whose whole effect is unmodelled still has levels to spend on,
    // and saying so is better than a heading standing over nothing.
    rows.push_back(TextRow(EmptyState("no effect", kEffectIndent)));
  }
  for (Row& row : effects) {
    rows.push_back(std::move(row));
  }
  return rows;
}

// One laid-out row of the card, ready to draw. `separator` rules off a
// section, and is drawn across the scroll bar's column as well as the text --
// a rule that stops a column short of the border reads as a gap in the card.
struct Line {
  ftxui::Element element;
  bool separator = false;
};

// Every row of the card, in order, borders aside and columns not yet decided.
std::vector<Row> CardRows(const Skill& skill, int level, int bonus,
                          SkillInspectPanel::Levels levels) {
  std::vector<Row> rows;
  auto rule = [&rows]() {
    rows.push_back({Row::kRule, "", "", ThemedSeparator()});
  };
  rows.push_back(TextRow(CenteredRow(skill.name())));
  rows.push_back(
      TextRow(CenteredRow("Max Level: " + std::to_string(skill.max_level()))));

  rule();
  rows.push_back({Row::kProse, "", skill.description(), nullptr});

  std::vector<Row> invariant = InvariantRows(skill);
  if (!invariant.empty()) {
    rule();
    Append(std::move(invariant), rows);
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
  int second = skill.max_level();
  bool has_second = second > first;
  if (levels == SkillInspectPanel::kLearned) {
    first = LevelWithBonus(skill, level, bonus);
    second = LevelWithBonus(skill, level + 1, bonus);
    // A point that buys nothing gets no block: the lent levels can already
    // have carried the skill to the ceiling the next one would reach.
    has_second = level < skill.max_level() && second > first;
  }
  if (first > 0) {
    rule();
    Append(LevelBlock(skill, first), rows);
  }
  if (has_second) {
    rule();
    Append(LevelBlock(skill, second), rows);
  }
  return rows;
}

// The widest label the rows carry, which is what the label column is cut to.
int WidestLabel(const std::vector<Row>& rows) {
  int widest = 0;
  for (const Row& row : rows) {
    if (row.kind == Row::kEffect) {
      widest = std::max(widest, TextColumns(row.label));
    }
  }
  return widest;
}

// The columns the card asks for: its indent, the widest label with a gap
// after it, and the widest value, held to the floor a description reads at.
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

// The columns inside the border, from what the rows ask for and what the
// screen holding the card allows. Both bounds are whole-card widths, and zero
// means unbounded.
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

// The card's rows drawn into `content` columns. A value too long for its
// column continues on the next line with the label left blank, rather than
// being cut mid-word.
std::vector<Line> LayOut(std::vector<Row> rows, int content) {
  const std::string indent(kEffectIndent, ' ');
  // Just enough for the widest label, unless the card is too narrow to seat
  // that and a value worth reading -- then the long labels take their own row.
  int label_width =
      std::min(WidestLabel(rows) + kLabelGap,
               std::max(1, content - kEffectIndent - kMinValueWidth));
  int value_width = std::max(1, content - kEffectIndent - label_width);
  std::vector<Line> lines;
  for (Row& row : rows) {
    if (row.kind == Row::kRule) {
      lines.push_back({std::move(row.element), /*separator=*/true});
      continue;
    }
    if (row.kind == Row::kWhole) {
      lines.push_back({std::move(row.element), /*separator=*/false});
      continue;
    }
    if (row.kind == Row::kProse) {
      for (const std::string& line : WrapText(row.value, content - 2)) {
        lines.push_back({ftxui::text(" " + line), /*separator=*/false});
      }
      continue;
    }
    std::string head = row.label;
    // A label too wide for its column takes a row to itself rather than being
    // cut: what it names is a skill, and half a skill's name is not one. The
    // value then reads under it, in the column it always sits in.
    if (TextColumns(head) > label_width) {
      lines.push_back({ftxui::text(indent + head), /*separator=*/false});
      head.clear();
    }
    for (const std::string& line : WrapText(row.value, value_width)) {
      lines.push_back({ftxui::text(indent + PadRight(head, label_width) + line),
                       /*separator=*/false});
      head.clear();
    }
  }
  return lines;
}

}  // namespace

void SkillInspectPanel::SetSkill(const Skill* skill, int learned, int bonus,
                                 Levels levels) {
  skill_ = skill;
  level_ = learned;
  bonus_ = bonus;
  levels_ = levels;
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
  std::vector<Row> rows = CardRows(*skill_, level_, bonus_, levels_);
  int content = ContentWidth(rows, min_width_, max_width_);
  int total = static_cast<int>(LayOut(std::move(rows), content).size());
  int last = total - VisibleRows(total);
  offset_ = std::max(0, std::min(offset_ + delta, last));
}

ftxui::Element SkillInspectPanel::Render() const {
  if (skill_ == nullptr) {
    return ThemedWindow(" Skill ", EmptyState("no skill"));
  }

  std::vector<Row> card = CardRows(*skill_, level_, bonus_, levels_);
  int content = ContentWidth(card, min_width_, max_width_);
  std::vector<Line> rows = LayOut(std::move(card), content);
  int total = static_cast<int>(rows.size());
  int visible = VisibleRows(total);
  // Clamped here as well as in ScrollBy: the terminal can be made taller under
  // a card already scrolled to its foot, which leaves the old offset too far
  // down for the window it now has.
  int offset = std::max(0, std::min(offset_, total - visible));

  // Laid out a row at a time rather than as a column of text beside a column
  // of bar. A separator is then a child of the card's own vbox and stretches
  // the whole way across it; nested in the narrower text column it stopped a
  // column short of the border and read as a notch.
  //
  // The bar's column is held open whether or not there is anything to scroll,
  // so a card does not change width the moment it outgrows the terminal.
  std::vector<ftxui::Element> cells = ScrollBarCells(total, offset, visible);
  std::vector<ftxui::Element> lines;
  for (int row = 0; row < visible; ++row) {
    Line& card_row = rows[offset + row];
    if (card_row.separator) {
      lines.push_back(std::move(card_row.element) |
                      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, content));
      continue;
    }
    ftxui::Element cell =
        cells.empty() ? ftxui::text(" ") : std::move(cells[row]);
    lines.push_back(ftxui::hbox({
        std::move(card_row.element) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, content),
        std::move(cell) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1),
    }));
  }

  // What the skill is, which is the first thing worth knowing about it.
  std::string title = IsActive(*skill_) ? " Active " : " Passive ";
  return ThemedWindow(title, ftxui::vbox(std::move(lines)));
}

PreviewCardSize LargestPreviewCard(const std::vector<const Skill*>& skills,
                                   int max_columns) {
  PreviewCardSize size;
  SkillInspectPanel panel;
  // Two passes, because a card's height is a fact about the width it was laid
  // out at: the widest card in the book sets the width, and every card is then
  // measured again at that width to find the tallest.
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
