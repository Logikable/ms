#include "src/combat/damage.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "src/combat/constants.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// Bosses take half elemental damage by default; ignore-elemental-resistance
// claws it back via 0.5 * (1 + ier).
constexpr double kBossElementalBase = 0.5;

// Attack speed: delay = base * (kSpeedBase - stage) / kSpeedDivisor, then
// ceil'd to whole kTickMs ticks.
constexpr int kSpeedBase = 20;
constexpr int kSpeedDivisor = 16;

// EquipStats stores boss_damage / ignore_enemy_defense as whole percents.
constexpr double kPercentToFraction = 100.0;

struct WeaponConstantRow {
  EquipType weapon;
  double constant;
};

// Each weapon carrying the constant of the job line that owns it. The swords
// and blunts are the Paladin line's (Page -> White Knight -> Paladin), the
// axes the Hero line's (Fighter -> Crusader -> Hero), the spear and polearm
// the Dark Knight line's (Spearman -> Berserker). Neither warrior line has a
// one-handed item yet, but both name the type in their skills.
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

// Where a job's line disagrees with the weapon's own. Only the Hero line does:
// it swings a sword harder than the Paladin line the sword's default comes
// from. A first-job character is on no line yet and takes the default, so
// advancing to a Fighter is worth a tenth of a multiplier on its own.
//
// A line's every job needs its own row -- a Crusader is still on the Hero line
// and would otherwise swing a sword worse than the Fighter they were.
const JobWeaponConstantRow kJobWeaponConstants[] = {
    {JOB_FIGHTER, EQUIP_TYPE_ONE_HANDED_SWORD, 1.34},
    {JOB_FIGHTER, EQUIP_TYPE_TWO_HANDED_SWORD, 1.44},
    {JOB_CRUSADER, EQUIP_TYPE_ONE_HANDED_SWORD, 1.34},
    {JOB_CRUSADER, EQUIP_TYPE_TWO_HANDED_SWORD, 1.44},
};

// Level multiplier: 1.1 at equal level, +0.02 per level above, capped at +5.
constexpr double kEqualLevelMultiplier = 1.1;
constexpr double kAboveLevelStep = 0.02;
constexpr int kAboveLevelCap = 5;

// Level multiplier when the monster out-levels the player, indexed by the gap
// (mob level - player level), 1..39; a gap of 40+ is 0 (output floored to 1
// damage). The -37..-39 rows read 0.8/0.5/0.3 on the wiki, which breaks the
// otherwise monotone decline -- taken here as 0.08/0.05/0.03, a dropped leading
// zero. That tail is unreachable with current content (worst gap is -19).
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

// The damage the player TAKES has its own pair of level factors, unrelated to
// the one above.
//
// A, the multiplier on the whole hit: 0.85 at parity, falling 0.0075 per level
// the character is ABOVE the mob down to 0.775 at +10, and rising 0.0075 per
// five-level band BELOW it up to 0.88 at -31 and lower. A narrow band either
// way -- being under-levelled is punished through B, not here.
constexpr double kTakenParityMultiplier = 0.85;
constexpr double kTakenLevelStep = 0.0075;
constexpr int kTakenAboveCap = 10;
// The under-level bands are five levels wide and the first one that is worth
// anything starts at -16, so a gap lands in band (gap - 11) / 5.
constexpr int kTakenBandStart = 11;
constexpr int kTakenBandWidth = 5;
constexpr int kTakenBandCap = 4;

// B, how much of the character's DEF counts: all of it at or above the mob's
// level, then 1% less per level under down to -10, then 2% less per level down
// to the floor of 0.50 at -30 and beyond.
constexpr double kDefEffectivenessNearStep = 0.01;
constexpr int kDefEffectivenessNearGap = 10;
constexpr double kDefEffectivenessFarStep = 0.02;
constexpr double kDefEffectivenessFloor = 0.50;

// The mob's two rolls: the minimum swings for 85% of its attack, and DEF may
// cancel at most 68% of the attack against it; the maximum swings for all of
// it, against a cap of 80%.
constexpr double kMinRollAttack = 0.85;
constexpr double kMinRollDefCap = 0.68;
constexpr double kMaxRollDefCap = 0.80;

// What crit is worth to a swing on average: the share of swings that crit,
// times what a crit adds. Both halves carry the base every character has, so a
// character who has bought neither still crits sometimes -- and a rate can
// never pass 1, however much a skill adds to it.
double CritFactor(const OffenseStats& offense) {
  double rate = std::min(1.0, offense.crit_rate + kBaseCritRate);
  return rate * (offense.crit_dmg + kBaseCritDamage);
}

// A hit always costs at least a point of HP, however thoroughly DEF cancelled
// the attack behind it, exactly as GMS does. The tenth of the pool a respawn
// beat hands back covers that chip many times over -- see fight.h.
constexpr double kMinimumDamage = 1.0;

// The multiplier on a whole incoming hit for the level gap (GMS's A).
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

// The share of the character's DEF that counts against a mob of this level
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

// What DEF cancels from one roll. `cap` is the most this roll allows it to,
// `effectiveness` GMS's B.
double DefenseReduction(double def, double effectiveness, double cap) {
  if (def >= cap) {
    // Enough armour to be sitting on the cap even before the under-levelling
    // penalty: GMS waives the penalty rather than charge it to a character it
    // could not have helped anyway.
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
    return 0.0;  // Caller floors output to 1 damage.
  }
  return kUnderLevelMultiplier[gap];
}

double CombineIgnoredDefense(double a, double b) {
  return 1.0 - (1.0 - a) * (1.0 - b);
}

bool DealsDamage(SkillKind kind) {
  return kind == SKILL_KIND_ATTACK || kind == SKILL_KIND_AUTO_ATTACK;
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
  // A mastery skill's first level sits below the baseline every character
  // swings at, so the better of the two wins rather than the learned one.
  offense.mastery = std::max(offense.mastery, passives.mastery);
  // Primary/secondary stat by job; unknown jobs fall through to 0, matching
  // MainStatValue in equipped_panel.
  switch (job) {
    case JOB_SWORDMAN:
    case JOB_FIGHTER:
    case JOB_PAGE:
    case JOB_SPEARMAN:
    case JOB_BERSERKER:
    case JOB_CRUSADER:
    case JOB_WHITE_KNIGHT:
    case JOB_BEGINNER:
      // STR primary, DEX secondary.
      offense.primary = allocated.str() + equipped.str();
      offense.secondary = allocated.dex() + equipped.dex();
      break;
    case JOB_ARCHER:
    case JOB_HUNTER:
    case JOB_CROSSBOWMAN:
    case JOB_RANGER:
    case JOB_SNIPER:
      // The mirror image: DEX primary, STR secondary.
      offense.primary = allocated.dex() + equipped.dex();
      offense.secondary = allocated.str() + equipped.str();
      break;
    case JOB_MAGICIAN:
    case JOB_ICE_LIGHTNING_WIZARD:
    case JOB_FIRE_POISON_WIZARD:
    case JOB_CLERIC:
    case JOB_ICE_LIGHTNING_MAGE:
    case JOB_FIRE_POISON_MAGE:
    case JOB_PRIEST:
      // INT primary, LUK secondary.
      offense.primary = allocated.int_() + equipped.int_();
      offense.secondary = allocated.luk() + equipped.luk();
      break;
    case JOB_ROGUE:
    case JOB_ASSASSIN:
    case JOB_BANDIT:
    case JOB_HERMIT:
    case JOB_CHIEF_BANDIT:
      // LUK primary, DEX secondary -- the magician's pair, swapped.
      offense.primary = allocated.luk() + equipped.luk();
      offense.secondary = allocated.dex() + equipped.dex();
      break;
    default:
      break;
  }
  // Magicians swing on magic attack; the rest of the chain treats it exactly
  // as weapon attack, so it rides the same field.
  bool magic = job == JOB_MAGICIAN || job == JOB_ICE_LIGHTNING_WIZARD ||
               job == JOB_FIRE_POISON_WIZARD || job == JOB_CLERIC ||
               job == JOB_ICE_LIGHTNING_MAGE || job == JOB_FIRE_POISON_MAGE ||
               job == JOB_PRIEST;
  offense.attack = magic ? equipped.magic_attack() : equipped.attack();
  offense.boss_pct =
      equipped.boss_damage() / kPercentToFraction + passives.boss_pct;
  offense.mirror_pct = passives.mirror_line_pct;
  offense.ied = CombineIgnoredDefense(
      equipped.ignore_enemy_defense() / kPercentToFraction, passives.ied);
  // The learned attack skill's multiplier replaces the bare 100% poke. Effect
  // at level L is base + per_level*(L-1). Passive skills fold elsewhere (their
  // levers are all defensive -- see DerivedStatsFor), so they are ignored here.
  // A skill that fires on its own clock still deals its damage the same way;
  // what differs is when it goes off, which is the fight's business.
  if (attack_skill != nullptr && DealsDamage(attack_skill->kind())) {
    offense.skill_pct =
        attack_skill->base().skill_pct() +
        attack_skill->per_level().skill_pct() * (attack_level - 1);
    offense.normal_skill_pct =
        attack_skill->base().normal_skill_pct() +
        attack_skill->per_level().normal_skill_pct() * (attack_level - 1);
    // A passive elsewhere in the book can name this skill and make it hit
    // harder. Added to the multiplier rather than beside it, so it is worth
    // its value once per line -- which is how GMS states it.
    std::map<std::string, double>::const_iterator boost =
        passives.skill_pct_bonus.find(attack_skill->name());
    if (boost != passives.skill_pct_bonus.end()) {
      offense.skill_pct += boost->second;
    }
    // A multi-hit skill strikes each target this many times per swing, so its
    // per-target damage is skill_pct once per line.
    offense.lines = std::max(1, attack_skill->lines());
  }
  // The shadow copies whatever the swing turned out to be, the bare poke's one
  // line included. Set last, so it cannot be read before lines is settled.
  offense.mirror_lines = offense.lines;
  return offense;
}

double ExpectedAttackDamage(const OffenseStats& offense, const Mob& mob) {
  double mob_pdr = mob.pdr() / kPercentToFraction;
  bool is_boss = mob.boss();

  double stat_value = 4.0 * offense.primary + offense.secondary;
  // The weapon constant is GMS's second factor, right behind the leading 0.01
  // that the /100 is.
  double max_base =
      stat_value * offense.attack / 100.0 * offense.weapon_constant;
  double damage = max_base * (1.0 + offense.mastery) / 2.0;

  // A swing that hits normal monsters harder adds its bonus to the swing
  // itself, so it is worth its value once per line rather than once per swing.
  // The shadow's lines land beside the real ones rather than multiplying them:
  // same damage either way, but this is the shape the swing really has.
  double lines = offense.lines + offense.mirror_lines * offense.mirror_pct;
  damage *=
      lines * (offense.skill_pct + (is_boss ? 0.0 : offense.normal_skill_pct));
  damage *= 1.0 + offense.damage_pct + (is_boss ? offense.boss_pct : 0.0);
  damage *= 1.0 + CritFactor(offense);
  damage *= 1.0 + offense.final_dmg_pct;
  damage *= 1.0 - mob_pdr * (1.0 - offense.ied);
  if (is_boss) {
    damage *= kBossElementalBase * (1.0 + offense.ier);
  }
  double level_mult = LevelMultiplier(offense.level, mob.level());
  if (level_mult <= 0.0) {
    return 1.0;  // 40+ levels under the mob: output is floored to 1 damage.
  }
  return damage * level_mult;
}

double ExpectedDamageTaken(const DefenseStats& defense, const Mob& mob) {
  double attack = mob.attack();
  double def = defense.def;
  double effectiveness = DefEffectiveness(defense.level, mob.level());
  double max_hit =
      attack - DefenseReduction(def, effectiveness, kMaxRollDefCap * attack);
  double min_hit =
      kMinRollAttack * attack -
      DefenseReduction(def, effectiveness, kMinRollDefCap * attack);
  double damage = TakenLevelMultiplier(defense.level, mob.level()) *
                  ((min_hit + max_hit) / 2.0);
  // Reduction from skills and gear lands after the whole defense formula, and
  // multiplies rather than adds -- see DerivedStats::damage_taken_pct.
  damage *= 1.0 - defense.damage_taken_pct;
  // A dodge takes the whole hit away rather than a share of it, so it lands
  // outside the floor: a character who dodges everything takes nothing, where
  // reduction alone always leaves the 1 damage GMS insists on.
  return std::max(kMinimumDamage, damage) * (1.0 - defense.dodge_chance);
}

int CombatPower(const OffenseStats& offense) {
  // The same opening as ExpectedAttackDamage: the /100 here is GMS's leading
  // 0.01, which turns out to be the very same constant.
  double stat_value = 4.0 * offense.primary + offense.secondary;
  double power = stat_value * offense.attack / 100.0 * offense.weapon_constant;
  power *= (1.0 + offense.mastery) / 2.0;
  power *= 1.0 + offense.damage_pct + offense.boss_pct;
  // Lines are stripped out of this number, so the shadow's share of one has to
  // be put back by hand -- it is a fact about the character, not the swing.
  power *= 1.0 + offense.mirror_pct;
  power *= 1.0 + CritFactor(offense);
  power *= 1.0 + offense.final_dmg_pct;
  return static_cast<int>(std::floor(power));
}

double SwingIntervalSeconds(int base_delay_ms, int attack_speed_stage) {
  double raw_ms = base_delay_ms * (kSpeedBase - attack_speed_stage) /
                  static_cast<double>(kSpeedDivisor);
  double ticks = std::ceil(raw_ms / kTickMs);
  return ticks * kTickMs / 1000.0;
}

}  // namespace ms
