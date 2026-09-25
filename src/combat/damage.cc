#include "src/combat/damage.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "src/character/job_branch.h"
#include "src/combat/constants.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// Bosses take half elemental damage; `ier` restores it as 0.5 * (1 + ier). GMS
// multiplies rather than subtracting: 10% ignored brings the reduction to 45%,
// not 40%.
constexpr double kBossElementalBase = 0.5;

// Attack speed: delay = base * (kSpeedBase - stage) / kSpeedDivisor, rounded up
// to whole kTickMs ticks.
constexpr int kSpeedBase = 20;
constexpr int kSpeedDivisor = 16;

// EquipStats stores boss_damage and ignore_enemy_defense as whole percents.
constexpr double kPercentToFraction = 100.0;

// GMS's starting mastery by weapon type: melee (sword, axe, spear, polearm,
// dagger), ranged (bow, crossbow), and magic (wand, staff). See BaseMastery.
constexpr double kMeleeBaseMastery = 0.20;
constexpr double kRangedBaseMastery = 0.15;
constexpr double kMagicBaseMastery = 0.25;

struct WeaponConstantRow {
  EquipType weapon;
  double constant;
};

// Each weapon's constant comes from the job line that mainly uses it: swords
// and blunts from Paladin, axes from Hero, spears and polearms from Dark
// Knight.
const WeaponConstantRow kWeaponConstants[] = {
    {EQUIP_TYPE_ONE_HANDED_SWORD, 1.24},
    {EQUIP_TYPE_TWO_HANDED_SWORD, 1.34},
    {EQUIP_TYPE_ONE_HANDED_BLUNT, 1.24},
    {EQUIP_TYPE_TWO_HANDED_BLUNT, 1.34},
    {EQUIP_TYPE_ONE_HANDED_AXE, 1.34},
    {EQUIP_TYPE_TWO_HANDED_AXE, 1.44},
    {EQUIP_TYPE_SPEAR, 1.49},
    {EQUIP_TYPE_POLEARM, 1.49},
    {EQUIP_TYPE_BOW, 1.30},
    {EQUIP_TYPE_CROSSBOW, 1.35},
    {EQUIP_TYPE_STAFF, 1.20},
    {EQUIP_TYPE_DAGGER, 1.30},
    {EQUIP_TYPE_CLAW, 1.75},
};

struct JobWeaponConstantRow {
  Job job;
  EquipType weapon;
  double constant;
};

// Overrides where a job line's constant differs from the weapon's default. Only
// the Hero line does, hitting harder with swords than Paladin. Every job in the
// line needs its own row, or a Crusader would hit weaker than a Fighter.
const JobWeaponConstantRow kJobWeaponConstants[] = {
    {JOB_FIGHTER, EQUIP_TYPE_ONE_HANDED_SWORD, 1.34},
    {JOB_FIGHTER, EQUIP_TYPE_TWO_HANDED_SWORD, 1.44},
    {JOB_CRUSADER, EQUIP_TYPE_ONE_HANDED_SWORD, 1.34},
    {JOB_CRUSADER, EQUIP_TYPE_TWO_HANDED_SWORD, 1.44},
    {JOB_HERO, EQUIP_TYPE_ONE_HANDED_SWORD, 1.34},
    {JOB_HERO, EQUIP_TYPE_TWO_HANDED_SWORD, 1.44},
};

// Level multiplier: 1.1 at equal level, +0.02 per level above, up to +5.
constexpr double kEqualLevelMultiplier = 1.1;
constexpr double kAboveLevelStep = 0.02;
constexpr int kAboveLevelCap = 5;

// Level multiplier when the monster is higher level, indexed by the gap 1 to
// 39; 40 or more is 0. The wiki lists -37 to -39 as 0.8/0.5/0.3, which breaks
// the steady decline, so we read them as 0.08/0.05/0.03 (a dropped leading
// zero).
constexpr double kUnderLevelMultiplier[] = {
    0.0,                           // gap 0 unused (see LevelMultiplier)
    1.0584, 1.007, 0.9672, 0.918,  // -1..-4
    0.88,   0.85,  0.83,   0.8,    // -5..-8
    0.78,   0.75,  0.73,   0.7,    // -9..-12
    0.68,   0.65,  0.63,   0.6,    // -13..-16
    0.58,   0.55,  0.53,   0.5,    // -17..-20
    0.48,   0.45,  0.43,   0.4,    // -21..-24
    0.38,   0.35,  0.33,   0.3,    // -25..-28
    0.28,   0.25,  0.23,   0.2,    // -29..-32
    0.18,   0.15,  0.13,   0.1,    // -33..-36
    0.08,   0.05,  0.03,           // -37..-39
};
constexpr int kMaxUnderLevelGap = 39;

// Damage taken has its own two level factors. A multiplies the whole hit: 0.85
// at equal level, down 0.0075 per level above the mob to 0.775 at +10, and up
// 0.0075 per five-level band below it to 0.88. The range is narrow; being
// underleveled is mostly punished through B.
constexpr double kTakenParityMultiplier = 0.85;
constexpr double kTakenLevelStep = 0.0075;
constexpr int kTakenAboveCap = 10;
// The under-level bands are five levels wide and the first starts at -16, so a
// gap falls in band (gap - 11) / 5.
constexpr int kTakenBandStart = 11;
constexpr int kTakenBandWidth = 5;
constexpr int kTakenBandCap = 4;

// B, the fraction of the character's DEF that counts: all of it at or above the
// mob's level, then 1% less per level below down to -10, then 2% less per level
// down to a floor of 0.50 at -30 and below.
constexpr double kDefEffectivenessNearStep = 0.01;
constexpr int kDefEffectivenessNearGap = 10;
constexpr double kDefEffectivenessFarStep = 0.02;
constexpr double kDefEffectivenessFloor = 0.50;

// The mob's two rolls: minimum is 85% of its attack with DEF capped at 68%;
// maximum is its full attack with DEF capped at 80%.
constexpr double kMinRollAttack = 0.85;
constexpr double kMinRollDefCap = 0.68;
constexpr double kMaxRollDefCap = 0.80;

// Average crit bonus: crit chance times crit damage. Both include every
// character's base values, and chance is capped at 1.
double CritFactor(const OffenseStats& offense) {
  double rate = std::min(1.0, offense.crit_rate + kBaseCritRate);
  return rate * (offense.crit_dmg + kBaseCritDamage);
}

// Every hit costs at least 1 HP, as in GMS. The HP restored each respawn covers
// this many times over.
constexpr double kMinimumDamage = 1.0;

// The multiplier on an incoming hit for the level gap (GMS's A).
double TakenLevelMultiplier(int player_level, int mob_level) {
  int diff = player_level - mob_level;
  if (diff >= 0) {
    return kTakenParityMultiplier -
           kTakenLevelStep * std::min(diff, kTakenAboveCap);
  }
  int band =
      std::clamp((-diff - kTakenBandStart) / kTakenBandWidth, 0, kTakenBandCap);
  return kTakenParityMultiplier + kTakenLevelStep * band;
}

// The fraction of the character's DEF that counts against a mob of this level
// (GMS's B).
double DefEffectiveness(int player_level, int mob_level) {
  int gap = mob_level - player_level;
  if (gap <= 0) {
    return 1.0;
  }
  if (gap <= kDefEffectivenessNearGap) {
    return 1.0 - kDefEffectivenessNearStep * gap;
  }
  double near = 1.0 - kDefEffectivenessNearStep * kDefEffectivenessNearGap;
  return std::max(
      kDefEffectivenessFloor,
      near - kDefEffectivenessFarStep * (gap - kDefEffectivenessNearGap));
}

// How much DEF removes from one roll. `cap` is this roll's maximum and
// `effectiveness` is GMS's B.
double DefenseReduction(double def, double effectiveness, double cap) {
  if (def >= cap) {
    // If DEF reaches the cap even before the level penalty, GMS skips the
    // penalty, since more DEF couldn't have helped.
    return cap;
  }
  return effectiveness * def;
}

}  // namespace

double LevelMultiplier(int player_level, int mob_level) {
  int diff = player_level - mob_level;
  if (diff >= 0) {
    // At or above the monster's level: a bonus that stops growing past +5.
    return kEqualLevelMultiplier +
           kAboveLevelStep * std::min(diff, kAboveLevelCap);
  }
  int gap = -diff;
  if (gap > kMaxUnderLevelGap) {
    return 0.0;  // the caller floors this to 1 damage
  }
  return kUnderLevelMultiplier[gap];
}

SwingRolls RollsFor(const OffenseStats& offense) {
  SwingRolls rolls;
  rolls.lines = std::max(1, offense.lines);
  rolls.mirror_lines = offense.mirror_lines;
  rolls.mirror_pct = offense.mirror_pct;
  rolls.mastery = offense.mastery;
  rolls.crit_rate = std::min(1.0, offense.crit_rate + kBaseCritRate);
  rolls.crit_dmg = offense.crit_dmg + kBaseCritDamage;
  return rolls;
}

double RollFactor(const SwingRolls& rolls, std::mt19937& rng,
                  std::vector<LineRoll>* lines) {
  if (lines != nullptr) {
    lines->clear();
  }
  double effective_lines = rolls.lines + rolls.mirror_lines * rolls.mirror_pct;
  double mean =
      (1.0 + rolls.mastery) / 2.0 * (1.0 + rolls.crit_rate * rolls.crit_dmg);
  if (effective_lines <= 0.0 || mean <= 0.0) {
    // Nothing is random, so the hit is one line carrying all the damage.
    if (lines != nullptr) {
      lines->push_back({1.0, false});
    }
    return 1.0;
  }
  std::uniform_real_distribution<double> spread(rolls.mastery, 1.0);
  std::bernoulli_distribution crits(rolls.crit_rate);
  double scale = 1.0 / (effective_lines * mean);
  double total = 0.0;
  // A zero-damage line isn't drawn but is still rolled. Every character has
  // shadow copies (possibly worth 0), and they must keep consuming random
  // numbers or fights would play out differently for everyone.
  for (int i = 0; i < rolls.lines; ++i) {
    bool crit = crits(rng);
    double line = spread(rng) * (crit ? 1.0 + rolls.crit_dmg : 1.0);
    total += line;
    if (lines != nullptr && line > 0.0) {
      lines->push_back({line * scale, crit});
    }
  }
  // Shadow copies roll separately from the attack's hits: they are extra hits,
  // and GMS rolls every hit.
  for (int i = 0; i < rolls.mirror_lines; ++i) {
    bool crit = crits(rng);
    double line =
        rolls.mirror_pct * spread(rng) * (crit ? 1.0 + rolls.crit_dmg : 1.0);
    total += line;
    if (lines != nullptr && line > 0.0) {
      lines->push_back({line * scale, crit});
    }
  }
  return total * scale;
}

double CombineIgnoredDefense(double a, double b) {
  return 1.0 - (1.0 - a) * (1.0 - b);
}

bool DealsDamage(SkillKind kind) {
  return kind == SKILL_KIND_ATTACK || kind == SKILL_KIND_AUTO_ATTACK;
}

int WholeValue(double value) {
  constexpr double kLadderEpsilon = 1e-9;
  return static_cast<int>(std::floor(value + kLadderEpsilon));
}

SkillEffect EffectAt(const SkillEffect& base, const SkillEffect& per_level,
                     int level) {
  SkillEffect at = base;
  if (level <= 1) {
    return at;
  }
  // Only the fields per_level sets: most skills scale one or two of the ninety,
  // and the rest keep `base`'s value.
  std::vector<const google::protobuf::FieldDescriptor*> climbing;
  const google::protobuf::Reflection* reflect = per_level.GetReflection();
  reflect->ListFields(per_level, &climbing);
  for (const google::protobuf::FieldDescriptor* field : climbing) {
    switch (field->cpp_type()) {
      case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
        reflect->SetDouble(
            &at, field,
            reflect->GetDouble(base, field) +
                reflect->GetDouble(per_level, field) * (level - 1));
        break;
      case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
        reflect->SetBool(&at, field, true);
        break;
      default:
        break;
    }
  }
  return at;
}

namespace {

// The cap for one field of one lever: the cap it names, or else the caster's
// own value split across the party, in whole percentage points.
double CasterIntCap(const AllyIntLever& lever, const SkillEffect& own,
                    const google::protobuf::FieldDescriptor* field,
                    int party_size) {
  const google::protobuf::Reflection* reflect = own.GetReflection();
  if (!lever.cap_is_party_share()) {
    double cap = lever.cap().GetReflection()->GetDouble(lever.cap(), field);
    return cap > 0.0 ? cap : std::numeric_limits<double>::infinity();
  }
  double share = reflect->GetDouble(own, field) / std::max(1, party_size);
  return std::round(share * 100.0) / 100.0;
}

}  // namespace

SkillEffect GrownByCasterInt(const Buff& buff, const SkillEffect& half,
                             const SkillEffect& own, int caster_int,
                             int party_size) {
  SkillEffect grown = half;
  const google::protobuf::Reflection* reflect = grown.GetReflection();
  for (const AllyIntLever& lever : buff.ally_int_lever()) {
    if (lever.int_step() <= 0.0) {
      continue;
    }
    double steps = std::floor(caster_int / lever.int_step());
    // Only the fields the lever grows, like EffectAt.
    std::vector<const google::protobuf::FieldDescriptor*> growing;
    const google::protobuf::Reflection* per = lever.effect().GetReflection();
    per->ListFields(lever.effect(), &growing);
    for (const google::protobuf::FieldDescriptor* field : growing) {
      if (field->cpp_type() !=
          google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE) {
        continue;
      }
      double grew = reflect->GetDouble(grown, field) +
                    per->GetDouble(lever.effect(), field) * steps;
      reflect->SetDouble(
          &grown, field,
          std::min(grew, CasterIntCap(lever, own, field, party_size)));
    }
  }
  return grown;
}

int SkillLinesAt(const Skill& skill, int level) {
  int lines = std::max(1, skill.lines());
  if (skill.lines_per_level() <= 0.0 || level <= 1) {
    return lines;
  }
  return lines + WholeValue(skill.lines_per_level() * (level - 1));
}

int SkillCasts(const Skill& skill) {
  return std::max(1, skill.casts());
}

int SwingHitCasts(const SwingHit& hit) {
  return std::max(1, hit.casts());
}

std::string EmpoweredSkillName(const std::string& target) {
  return "Empowered " + target;
}

std::vector<std::string> BoostTargetNames(const SkillBoost& boost) {
  std::vector<std::string> names;
  if (boost.reach() != BOOST_REACH_EMPOWERED) {
    names.push_back(boost.skill_name());
  }
  if (boost.reach() != BOOST_REACH_ORDINARY) {
    names.push_back(EmpoweredSkillName(boost.skill_name()));
  }
  return names;
}

int ComboOrbsAt(const Skill& skill, int level) {
  // Same epsilon as WholeValue, for the same reason.
  constexpr double kOrbEpsilon = 1e-9;
  if (skill.combo_orbs_per_level() <= 0.0 || level <= 1) {
    return skill.combo_orbs();
  }
  return skill.combo_orbs() +
         static_cast<int>(std::floor(
             skill.combo_orbs_per_level() * (level - 1) + kOrbEpsilon));
}

double CooldownAt(const Skill& skill, int level) {
  if (skill.cooldown_seconds() <= 0.0) {
    return 0.0;
  }
  double wait = skill.cooldown_seconds() +
                skill.cooldown_seconds_per_level() * (level - 1);
  return std::max(0.0, wait);
}

bool Pulses(const BuffPulse& pulse) {
  return pulse.cast_interval_seconds() > 0.0 ||
         !pulse.paced_by_skill_name().empty();
}

double LongestBuffDuration(const Buff& buff) {
  double longest = buff.duration_seconds();
  for (const Stance& stance : buff.stance()) {
    longest = std::max(longest, stance.duration_seconds());
  }
  return longest;
}

double ReducedCooldown(double wait, double reduction_seconds) {
  constexpr double kUntouchedBelow = 5.0;
  constexpr double kHalvedBelow = 10.0;
  // For cooldowns of 5 to 10 seconds, each second of reduction removes 5% of
  // the cooldown instead, so -2 seconds removes 10%.
  constexpr double kSharePerSecond = 0.05;
  if (wait < kUntouchedBelow || reduction_seconds <= 0.0) {
    return wait;
  }
  if (wait <= kHalvedBelow) {
    return std::max(kUntouchedBelow,
                    wait * (1.0 - kSharePerSecond * reduction_seconds));
  }
  const double left = wait - reduction_seconds;
  if (left >= kHalvedBelow) {
    return left;
  }
  // Only half of the reduction below ten seconds applies.
  return kHalvedBelow - (kHalvedBelow - left) / 2.0;
}

int ShieldHitsAt(const Shield& shield, int level) {
  double hits = shield.hits() + shield.hits_per_level() * (level - 1);
  return static_cast<int>(std::max(0.0, std::floor(hits)));
}

double WeaponConstant(Job job, EquipType weapon) {
  for (const JobWeaponConstantRow& row : kJobWeaponConstants) {
    if (row.job == job && row.weapon == weapon) {
      return row.constant;
    }
  }
  for (const WeaponConstantRow& row : kWeaponConstants) {
    if (row.weapon == weapon) {
      return row.constant;
    }
  }
  return 1.0;
}

double BaseMastery(Job job) {
  switch (BranchOf(job)) {
    case JobBranch::kArcher:
      return kRangedBaseMastery;
    case JobBranch::kMagician:
      return kMagicBaseMastery;
    default:
      // Warriors, thieves and beginners all use weapons GMS counts as melee,
      // including daggers.
      return kMeleeBaseMastery;
  }
}

namespace {

// Sets primary and secondary stat by branch. Unknown jobs stay at 0, matching
// the Equipped panel's main-stat column.
void AddStatsByBranch(Job job, const AllocatedStats& allocated,
                      const EquipStats& equipped, OffenseStats& offense) {
  switch (BranchOf(job)) {
    // Beginners use STR, like warriors.
    case JobBranch::kBeginner:
    case JobBranch::kWarrior:
      offense.primary = allocated.str() + equipped.str();
      offense.secondary = allocated.dex() + equipped.dex();
      break;
    case JobBranch::kArcher:
      // The reverse of warriors: DEX primary, STR secondary.
      offense.primary = allocated.dex() + equipped.dex();
      offense.secondary = allocated.str() + equipped.str();
      break;
    case JobBranch::kMagician:
      offense.primary = allocated.int_() + equipped.int_();
      offense.secondary = allocated.luk() + equipped.luk();
      break;
    case JobBranch::kRogue:
      // The reverse of magicians' pair.
      offense.primary = allocated.luk() + equipped.luk();
      offense.secondary = allocated.dex() + equipped.dex();
      break;
    case JobBranch::kNone:
      break;
  }
}

// Applies the job book's bonuses for this skill. Each combines with the
// attack's own values the usual way; skill damage adds to the multiplier, so it
// applies once per line, as GMS states it.
void AddNamedBoost(const Skill& attack_skill, const PassiveOffense& passives,
                   OffenseStats& offense) {
  std::map<std::string, SkillBonus>::const_iterator boost =
      passives.skill_bonus.find(attack_skill.name());
  if (boost == passives.skill_bonus.end()) {
    return;
  }
  const SkillBonus& bonus = boost->second;
  offense.skill_pct += bonus.skill_pct;
  offense.damage_pct += bonus.damage_pct;
  offense.boss_pct += bonus.boss_pct;
  offense.normal_pct += bonus.normal_pct;
  offense.ied = CombineIgnoredDefense(offense.ied, bonus.ied);
  offense.crit_rate += bonus.crit_rate;
  offense.final_dmg_pct =
      (1.0 + offense.final_dmg_pct) * (1.0 + bonus.final_dmg_pct) - 1.0;
}

// Ignore defense, boss damage, damage, crit chance and final damage listed on
// an attack skill apply to that attack only. GMS lists them on the skill (e.g.
// Gungnir's Descent ignores 30%, Snipe always crits). Passives that grant them
// apply to the character instead.
void AddSwingLevers(const Skill& attack_skill, int attack_level,
                    OffenseStats& offense) {
  offense.ied = CombineIgnoredDefense(
      offense.ied, attack_skill.base().ied_pct() +
                       attack_skill.per_level().ied_pct() * (attack_level - 1));
  offense.ier += attack_skill.base().ier_pct() +
                 attack_skill.per_level().ier_pct() * (attack_level - 1);
  offense.boss_pct += attack_skill.base().boss_pct() +
                      attack_skill.per_level().boss_pct() * (attack_level - 1);
  offense.damage_pct +=
      attack_skill.base().damage_pct() +
      attack_skill.per_level().damage_pct() * (attack_level - 1);
  offense.normal_pct +=
      attack_skill.base().normal_pct() +
      attack_skill.per_level().normal_pct() * (attack_level - 1);
  // Added to the character's crit chance, so 1.00 guarantees a crit no matter
  // what else they have.
  offense.crit_rate +=
      attack_skill.base().crit_rate() +
      attack_skill.per_level().crit_rate() * (attack_level - 1);
  // Multiplied with the character's final damage, like all final damage
  // sources. See SkillEffect::final_dmg_pct.
  double swing_fd =
      attack_skill.base().final_dmg_pct() +
      attack_skill.per_level().final_dmg_pct() * (attack_level - 1);
  offense.final_dmg_pct =
      (1.0 + offense.final_dmg_pct) * (1.0 + swing_fd) - 1.0;
}

// The attack skill's multiplier, replacing the basic attack's 100%. At level L
// it is base + per_level * (L - 1). Passives are applied elsewhere. Skills on
// their own timer compute damage the same way; the fight decides when they
// fire.
void AddAttackSkill(const Skill& attack_skill, int attack_level,
                    const PassiveOffense& passives, OffenseStats& offense) {
  offense.skill_pct = attack_skill.base().skill_pct() +
                      attack_skill.per_level().skill_pct() * (attack_level - 1);
  offense.normal_skill_pct =
      attack_skill.base().normal_skill_pct() +
      attack_skill.per_level().normal_skill_pct() * (attack_level - 1);
  AddNamedBoost(attack_skill, passives, offense);
  if (attack_skill.kind() == SKILL_KIND_ATTACK) {
    AddSwingLevers(attack_skill, attack_level, offense);
  }
  // A multi-hit skill hits each target this many times per attack, so
  // per-target damage is skill_pct once per line.
  offense.lines = SkillLinesAt(attack_skill, attack_level);
  // Bolt Surplus's extra hit applies to attacks that already hit more than
  // once, not to skills on their own timer. See
  // SkillEffect::bonus_attack_lines.
  if (offense.lines > 1 && attack_skill.kind() == SKILL_KIND_ATTACK) {
    offense.lines += passives.bonus_attack_lines;
  }
}

}  // namespace

OffenseStats OffenseStatsFor(Job job, int level,
                             const AllocatedStats& allocated,
                             const EquipStats& equipped, EquipType weapon,
                             const Skill* attack_skill, int attack_level,
                             const PassiveOffense& passives) {
  OffenseStats offense;
  offense.level = level;
  offense.weapon_constant = WeaponConstant(job, weapon);
  offense.crit_rate = passives.crit_rate;
  offense.crit_dmg = passives.crit_dmg;
  offense.damage_pct = passives.damage_pct;
  offense.final_dmg_pct = passives.final_dmg_pct;
  offense.force_pct = passives.force_pct;
  offense.ier = passives.ier;
  // The job line's base mastery plus the best mastery skill's bonus.
  offense.mastery = BaseMastery(job) + passives.mastery;
  AddStatsByBranch(job, allocated, equipped, offense);
  // Magicians use magic attack; the formula treats it exactly like weapon
  // attack, so it goes in the same field.
  offense.attack =
      SwingsOnMagic(job) ? equipped.magic_attack() : equipped.attack();
  offense.boss_pct =
      equipped.boss_damage() / kPercentToFraction + passives.boss_pct;
  offense.normal_pct = passives.normal_pct;
  offense.mirror_pct = passives.mirror_line_pct;
  // Every character's base ignore defense combines with gear and skills
  // multiplicatively, like any other source.
  offense.ied = CombineIgnoredDefense(
      kBaseIgnoreDefense,
      CombineIgnoredDefense(
          equipped.ignore_enemy_defense() / kPercentToFraction, passives.ied));
  if (attack_skill != nullptr && DealsDamage(attack_skill->kind())) {
    AddAttackSkill(*attack_skill, attack_level, passives, offense);
  }
  // Shadow copies match the attack's final line count, unless the skill opts
  // out. Set last, after `lines` is final.
  bool shadowed = attack_skill == nullptr || !attack_skill->skips_mirror();
  offense.mirror_lines = shadowed ? offense.lines : 0;
  return offense;
}

double ExpectedAttackDamage(const OffenseStats& offense, const Mob& mob) {
  double mob_pdr = mob.pdr() / kPercentToFraction;
  bool is_boss = mob.boss();

  double stat_value = 4.0 * offense.primary + offense.secondary;
  // The weapon constant is GMS's second factor, after the leading 0.01 (the
  // /100).
  double max_base =
      stat_value * offense.attack / 100.0 * offense.weapon_constant;
  double damage = max_base * (1.0 + offense.mastery) / 2.0;

  // Normal-monster skill damage adds to the attack's multiplier, so it applies
  // once per line. Shadow lines are added alongside the real ones rather than
  // multiplying them.
  double lines = offense.lines + offense.mirror_lines * offense.mirror_pct;
  damage *=
      lines * (offense.skill_pct + (is_boss ? 0.0 : offense.normal_skill_pct));
  damage *= 1.0 + offense.damage_pct +
            (is_boss ? offense.boss_pct : offense.normal_pct);
  damage *= 1.0 + CritFactor(offense);
  damage *= 1.0 + offense.final_dmg_pct;
  // Defense over 100% would make this negative if ignore defense can't bring it
  // below 100%. Clamp at 0 and let the 1-damage floor apply.
  damage *= std::max(0.0, 1.0 - mob_pdr * (1.0 - offense.ied));
  if (is_boss) {
    // Every character's base ignore elemental resistance plus what their skills
    // add, like the crit base above.
    damage *= kBossElementalBase *
              (1.0 + kBaseIgnoreElementalResistance + offense.ier);
  }
  double level_mult = LevelMultiplier(offense.level, mob.level());
  if (level_mult <= 0.0) {
    return 1.0;  // 40 or more levels below the mob
  }
  // Damage is at least 1, whatever reduced it: a 40-level gap, or unignored
  // defense.
  return std::max(1.0, damage * level_mult * offense.force_pct);
}

double DefenseShare(const Mob& mob, double ied) {
  double cut = mob.pdr() / kPercentToFraction * (1.0 - ied);
  if (cut <= 0.0 || cut >= 1.0) {
    return 0.0;
  }
  return cut / (1.0 - cut);
}

double ExpectedDamageTaken(const DefenseStats& defense, const Mob& mob) {
  double attack = mob.attack();
  // Weakening reduces the monster's attack, so it applies before DEF: DEF then
  // cancels a share of a smaller attack.
  if (!mob.boss() || defense.enemy_attack_reaches_boss) {
    attack *= std::max(0.0, 1.0 - defense.enemy_attack_pct);
  }
  double def = defense.def;
  double effectiveness = DefEffectiveness(defense.level, mob.level());
  double max_hit =
      attack - DefenseReduction(def, effectiveness, kMaxRollDefCap * attack);
  double min_hit =
      kMinRollAttack * attack -
      DefenseReduction(def, effectiveness, kMinRollDefCap * attack);
  double damage = TakenLevelMultiplier(defense.level, mob.level()) *
                  ((min_hit + max_hit) / 2.0);
  // Damage reduction from skills and gear applies after the defense formula,
  // and multiplies rather than adds. See DerivedStats::damage_taken_pct.
  damage *= 1.0 - defense.damage_taken_pct;
  // Applied before the floor, so a character with 1.5x the requirement takes
  // GMS's 1 damage rather than 0.
  damage *= defense.force_taken;
  // A dodge avoids the whole hit, so it applies after the floor; reduction
  // alone always leaves GMS's 1 damage.
  return std::max(kMinimumDamage, damage) * (1.0 - defense.dodge_chance);
}

int CombatPower(const OffenseStats& offense, bool vs_boss) {
  // Same start as ExpectedAttackDamage; /100 is GMS's leading 0.01.
  double stat_value = 4.0 * offense.primary + offense.secondary;
  double power = stat_value * offense.attack / 100.0 * offense.weapon_constant;
  power *= (1.0 + offense.mastery) / 2.0;
  // Only one of boss or normal damage, as in ExpectedAttackDamage; counting
  // both would overstate any real attack.
  power *= 1.0 + offense.damage_pct +
           (vs_boss ? offense.boss_pct : offense.normal_pct);
  // Line counts are left out of this number, so add back the shadow's share by
  // hand: it belongs to the character, not the attack.
  power *= 1.0 + offense.mirror_pct;
  power *= 1.0 + CritFactor(offense);
  power *= 1.0 + offense.final_dmg_pct;
  return static_cast<int>(std::floor(power));
}

bool SwingsOnMagic(Job job) {
  return BranchOf(job) == JobBranch::kMagician;
}

int BaseAttackSpeedStage(Job job, int weapon_stage) {
  // In GMS a mage's weapon doesn't affect cast speed: every spell starts at the
  // neutral stage and only boosts change it. This also applies to the basic
  // attack, since mages learn a spell at level 1 and never use it again.
  return SwingsOnMagic(job) ? kUnscaledAttackSpeedStage : weapon_stage;
}

int AttackSpeedStage(int base, int bonus, int uncapped) {
  int stage = std::min(kAttackSpeedSoftCap, base + bonus);
  return std::min(static_cast<int>(ATTACK_SPEED_FASTEST_3), stage + uncapped);
}

double SwingIntervalSeconds(int base_delay_ms, int attack_speed_stage) {
  double raw_ms = base_delay_ms * (kSpeedBase - attack_speed_stage) /
                  static_cast<double>(kSpeedDivisor);
  double ticks = std::ceil(raw_ms / kTickMs);
  return ticks * kTickMs / 1000.0;
}

}  // namespace ms
