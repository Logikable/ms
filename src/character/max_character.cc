#include "src/character/max_character.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/hyper_plan.h"
#include "src/character/hyper_stats.h"
#include "src/character/inner_ability.h"
#include "src/character/job_branch.h"
#include "src/character/stat_preset.h"
#include "src/combat/damage.h"
#include "src/item/potential.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

namespace {

// What each gear band costs and what leveling has paid by the level where it
// opens, from //analysis:progression_sim's median branch. Every row is priced
// through analysis/star_force_curve.h over the twenty-two items a level 140
// character wears, seventeen of which take stars.
//
//   level   income   gear                                      bill
//   -----   ------   ---------------------------------------   ----
//    130     165M    10*, weapon 14*, scrolled                  135M
//    140     226M    the same, every slot filled                229M
//    170     620M    the same, plus the Wealth potion           435M
//    180     820M    hammered on top of it                      675M
//    190    1.25B    11*, weapon 15*, both potions             1.13B
//    200    1.70B    12*, weapon 15*, potentials, potions      ~6.3B
//
// Only the last row costs more than leveling pays, which is intended: when this
// was priced, 200 was the level cap, and the days spent there (410M each) went
// on cubes. The bill there is eleven cubes, against the fourteen
// progression_sim's own endgame uses.
//
// Two of the original targets were lowered to fit. A "maxed out" level 140
// weapon is 15 stars on a level 120 item, which costs 226M on its own (the
// whole climb for one item), so the ceiling is 14, one star short of the cap
// and where the sim's own shopper stops from 130. And hammers can't start
// before 180: two in every item cost 340M on top of everything else.
struct GearBand {
  int level;
  MaxGear gear;
};

constexpr GearBand kBands[] = {
    {0,
     {false, 10, 12, POTENTIAL_RANK_UNSPECIFIED, POTENTIAL_RANK_UNSPECIFIED}},
    {130,
     {false, 10, 14, POTENTIAL_RANK_UNSPECIFIED, POTENTIAL_RANK_UNSPECIFIED}},
    {180,
     {true, 10, 14, POTENTIAL_RANK_UNSPECIFIED, POTENTIAL_RANK_UNSPECIFIED}},
    {190,
     {true, 11, 15, POTENTIAL_RANK_UNSPECIFIED, POTENTIAL_RANK_UNSPECIFIED}},
    {200, {true, 12, 15, POTENTIAL_RANK_EPIC, POTENTIAL_RANK_UNIQUE}},
};

// The %stat line for the stat the character fights with.
PotentialLineType StatShareFor(StatField primary) {
  switch (primary) {
    case STAT_FIELD_DEX:
      return POTENTIAL_LINE_TYPE_DEX_PCT;
    case STAT_FIELD_INT:
      return POTENTIAL_LINE_TYPE_INT_PCT;
    case STAT_FIELD_LUK:
      return POTENTIAL_LINE_TYPE_LUK_PCT;
    default:
      return POTENTIAL_LINE_TYPE_STR_PCT;
  }
}

// The attack line, which is what a weapon slot is cubed for.
PotentialLineType AttackShareFor(StatField primary) {
  return primary == STAT_FIELD_INT ? POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT
                                   : POTENTIAL_LINE_TYPE_ATTACK_PCT;
}

// The other attack line, which does nothing for this character: a warrior's
// weapon rolls M.ATT as easily as ATT. It's here so a weapon slot has two
// useful lines and one useless one. //analysis:cube_sim puts two useful lines
// on a weapon at 15 cubes and three at 70, and nobody pays for the third.
PotentialLineType DeadShareFor(StatField primary) {
  return primary == STAT_FIELD_INT ? POTENTIAL_LINE_TYPE_ATTACK_PCT
                                   : POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT;
}

void AddLine(Potential& potential, PotentialLineType type, PotentialRank rank) {
  PotentialLine& line = *potential.add_lines();
  line.set_type(type);
  line.set_rank(rank);
}

// The monster the preset is spent against: for the Boss allocation, the
// toughest boss the character's level allows; for the Farm one, the toughest
// normal monster at or below their level. Both are read from the catalogs, not
// chosen, so a new boss changes this automatically.
//
// Null for empty catalogs, in which case the rating falls back to plain combat
// power.
const Mob* NominalTarget(const std::map<std::string, Boss>& bosses,
                         const std::map<std::string, Mob>& mobs, int level,
                         Activity preset) {
  const Mob* worst = nullptr;
  auto harder = [&worst](const Mob& mob) {
    if (worst == nullptr || mob.pdr() > worst->pdr()) {
      worst = &mob;
    }
  };
  if (preset == Activity::kFarming) {
    for (const std::pair<const std::string, Mob>& entry : mobs) {
      if (!entry.second.boss() && entry.second.level() <= level) {
        harder(entry.second);
      }
    }
    return worst;
  }
  for (const std::pair<const std::string, Boss>& entry : bosses) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (difficulty.coming_soon() || difficulty.unlock_level() > level) {
        continue;
      }
      for (const BossPhase& phase : difficulty.phases()) {
        for (const Spawn& spawn : phase.spawns()) {
          auto found = mobs.find(spawn.mob());
          if (found != mobs.end()) {
            harder(found->second);
          }
        }
      }
    }
  }
  return worst;
}

// What the character is worth to the allocation: one plain swing at their
// target, through the full damage chain. It uses a real target instead of just
// combat power because combat power has no target in it, so a stat that only
// helps against defence (Ignore Defense) would be valued at zero and never
// bought.
//
// A monster that cancels the swing entirely leaves every allocation at the
// 1-damage floor, and the preset then buys nothing. That means the fight is out
// of reach, not that the choices are tied.
double MaxHyperRate(CharacterInstance& character,
                    const std::map<std::string, Skill>& skills, Activity preset,
                    const Mob* target) {
  const OffenseStats offense = CharacterOffense(character, skills, preset);
  if (target == nullptr) {
    return CombatPower(offense, preset == Activity::kBossing);
  }
  return ExpectedAttackDamage(offense, *target);
}

}  // namespace

MaxGear MaxGearForLevel(int level) {
  MaxGear gear = kBands[0].gear;
  for (const GearBand& band : kBands) {
    if (level >= band.level) {
      gear = band.gear;
    }
  }
  return gear;
}

Potential MaxPotentialFor(EquipSlot slot, const MaxGear& gear,
                          StatField primary) {
  const PotentialGroup group = PotentialGroupOf(slot);
  const bool weaponry = group == PotentialGroup::kWeaponry;
  const PotentialRank rank =
      weaponry ? gear.weaponry_potential : gear.armour_potential;
  Potential potential;
  if (group == PotentialGroup::kNone || rank == POTENTIAL_RANK_UNSPECIFIED) {
    return potential;
  }
  potential.set_rank(rank);
  // The prime line is what the item was cubed for. On a weapon that is the one
  // line no amount of %ATT can replace (ignored defence on the weapon, boss
  // damage on the secondary), and both are Unique-rank lines, which puts the
  // weapon slots a rank above the rest of the gear.
  //
  // Two useful lines and one useless one, and the third stays useless: a weapon
  // with three useful lines takes four times as long to roll as one with two,
  // and nobody finishes it.
  if (weaponry) {
    if (slot == EQUIP_SLOT_PRIMARY_WEAPON) {
      AddLine(potential, POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30, rank);
    } else if (slot == EQUIP_SLOT_SECONDARY) {
      AddLine(potential, POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30, rank);
    } else {
      AddLine(potential, AttackShareFor(primary), rank);
    }
    AddLine(potential, AttackShareFor(primary), PreviousPotentialRank(rank));
    AddLine(potential, DeadShareFor(primary), PreviousPotentialRank(rank));
    return potential;
  }
  const PotentialLineType share = StatShareFor(primary);
  AddLine(potential, share, rank);
  while (potential.lines_size() < kPotentialLines) {
    AddLine(potential, share, PreviousPotentialRank(rank));
  }
  return potential;
}

void SpendMaxHyperStats(CharacterInstance& character,
                        const std::map<std::string, Skill>& skills,
                        const std::map<std::string, Boss>& bosses,
                        const std::map<std::string, Mob>& mobs) {
  const int level = character.proto().level();
  for (Activity activity : {Activity::kFarming, Activity::kBossing}) {
    const Mob* target = NominalTarget(bosses, mobs, level, activity);
    const StatPreset slot = AutoswapSlotFor(activity);
    HyperWorth worth = MeasureHyperWorth(
        character, slot, [&skills, activity, target](CharacterInstance& c) {
          return MaxHyperRate(c, skills, activity, target);
        });
    SpendHyperStats(character, slot, worth);
  }
}

AbilityPreset MaxAbilityPreset(Activity preset, StatField primary) {
  AbilityLineType stat = ABILITY_LINE_TYPE_STR;
  switch (primary) {
    case STAT_FIELD_DEX:
      stat = ABILITY_LINE_TYPE_DEX;
      break;
    case STAT_FIELD_INT:
      stat = ABILITY_LINE_TYPE_INT;
      break;
    case STAT_FIELD_LUK:
      stat = ABILITY_LINE_TYPE_LUK;
      break;
    default:
      break;
  }
  // One Legendary line and two Epic ones, which is what resetting realistically
  // lands on: only the top line has the ability's rank, and the two below roll
  // Epic far more often than Unique. The top line is what
  // //analysis:ability_plan finds best for the rank: critical rate for bosses,
  // normal damage for crowds.
  //
  // Attack Speed is the line GMS players chase, and it's deliberately left out:
  // the Extreme Green Potion already gives boss fights an extra stage past the
  // cap this line is limited by, so the line would be useless in every boss
  // fight.
  const AbilityLineType top = preset == Activity::kBossing
                                  ? ABILITY_LINE_TYPE_CRIT_RATE
                                  : ABILITY_LINE_TYPE_NORMAL_DAMAGE;
  const AbilityLineType attack = primary == STAT_FIELD_INT
                                     ? ABILITY_LINE_TYPE_MAGIC_ATTACK
                                     : ABILITY_LINE_TYPE_ATTACK;
  const AbilityLineType second =
      preset == Activity::kBossing ? attack : ABILITY_LINE_TYPE_ALL_STATS;

  AbilityPreset built;
  built.set_rank(ABILITY_RANK_LEGENDARY);
  const AbilityLineType types[] = {top, second, stat};
  const AbilityRank ranks[] = {ABILITY_RANK_LEGENDARY, ABILITY_RANK_EPIC,
                               ABILITY_RANK_EPIC};
  for (int i = 0; i < kAbilityLines; ++i) {
    AbilityLine& line = *built.add_lines();
    line.set_type(types[i]);
    line.set_rank(ranks[i]);
  }
  return built;
}

}  // namespace ms
