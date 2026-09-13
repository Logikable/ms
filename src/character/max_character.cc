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

// What each band of gear costs and what the climb has paid by the level it
// opens at, from //analysis:progression_sim's median branch. Every row is
// priced through analysis/star_force_curve.h over the twenty-two pieces a
// level 140 character wears, seventeen of which take stars.
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
// Only the last row costs more than the climb pays, and that is the row it
// should be: 200 is where levelling stops and the days at the cap -- 410M
// each -- go on cubes. The bill there is eleven of them, against the fourteen
// progression_sim's own endgame plays out.
//
// Two of the user's opening figures came down against this. A level 140
// weapon "maxed out" is 15 stars on a level 120 item, which is 226M on its
// own -- the whole climb, for one piece -- so the ceiling is 14, one star
// short of the cap and where the sim's own shopper parks it from 130. And
// hammers cannot start before 180: two in every piece is 340M whatever else
// is being bought.
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

// The %stat line that raises what the character fights with.
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

// The attack share, which is what a weaponry slot is cubed for.
PotentialLineType AttackShareFor(StatField primary) {
  return primary == STAT_FIELD_INT ? POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT
                                   : POTENTIAL_LINE_TYPE_ATTACK_PCT;
}

// The other one, which pays this character nothing: a warrior's weapon rolls
// M.ATT as readily as ATT. Here so a weaponry slot carries two lines worth
// having and one dead one -- //analysis:cube_sim prices two useful lines on a
// weapon at 15 cubes and three at 70, and nobody buys the third.
PotentialLineType DeadShareFor(StatField primary) {
  return primary == STAT_FIELD_INT ? POTENTIAL_LINE_TYPE_ATTACK_PCT
                                   : POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT;
}

void AddLine(Potential& potential, PotentialLineType type, PotentialRank rank) {
  PotentialLine& line = *potential.add_lines();
  line.set_type(type);
  line.set_rank(rank);
}

// The monster the preset is spent against: the toughest boss whose gate the
// character has passed for the Boss allocation, and the toughest ordinary
// monster standing at or below their level for the Farm one. Both read off
// the roster rather than picked, so a boss landing later moves this on its
// own.
//
// Null for an empty roster, which is what the rate falls back to bare combat
// power for.
const Mob* NominalTarget(const std::map<std::string, Boss>& bosses,
                         const std::map<std::string, Mob>& mobs, int level,
                         StatPreset preset) {
  const Mob* worst = nullptr;
  auto harder = [&worst](const Mob& mob) {
    if (worst == nullptr || mob.pdr() > worst->pdr()) {
      worst = &mob;
    }
  };
  if (preset == StatPreset::kFarming) {
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

// What the character is worth to the allocation: one bare swing at the
// monster ahead of them, through the whole damage chain. Rated against a real
// target rather than by combat power alone because combat power has none in
// it, and a stat that only pays against a defended monster -- Ignore Defense
// -- would otherwise price at zero and never be bought.
//
// A monster that cancels the swing outright leaves every allocation on the
// 1-damage floor together, and the preset then buys nothing. That is the
// roster saying the fight is out of reach, not a tie to be broken.
double MaxHyperRate(CharacterInstance& character,
                    const std::map<std::string, Skill>& skills,
                    StatPreset preset, const Mob* target) {
  const OffenseStats offense = CharacterOffense(character, skills, preset);
  if (target == nullptr) {
    return CombatPower(offense, preset == StatPreset::kBossing);
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
  // The prime line is what the piece was cubed for. On a weapon that is the
  // one line no amount of %ATT replaces -- defense ignored on the weapon,
  // boss damage on the secondary -- and both are Unique-rank lines, which is
  // what puts the weaponry slots a rank above the rest of the outfit.
  //
  // Two lines worth having and one dead one, and the third stays dead: a
  // weapon holding three useful lines is a chase four times as long as one
  // holding two, and nobody finishes it.
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
  for (StatPreset preset : {StatPreset::kFarming, StatPreset::kBossing}) {
    const Mob* target = NominalTarget(bosses, mobs, level, preset);
    HyperWorth worth = MeasureHyperWorth(
        character, preset, [&skills, preset, target](CharacterInstance& c) {
          return MaxHyperRate(c, skills, preset, target);
        });
    SpendHyperStats(character, preset, worth);
  }
}

AbilityPreset MaxAbilityPreset(StatPreset preset, StatField primary) {
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
  // One Legendary line and two Epic ones, which is the shape of every ability
  // a reset chase actually lands: only the top line ever carries the
  // ability's rank, and the two under it roll Epic far more often than
  // Unique. The top line is what //analysis:ability_plan measures as the best
  // of the rank -- critical rate for a fight, normal damage for a crowd.
  //
  // Attack Speed is the line GMS players chase and it is deliberately not
  // here: the Extreme Green Potion already hands a boss fight its extra
  // stage, past the cap this line is held to, so a max character is holding
  // a dead line the moment they walk through a boss door.
  const AbilityLineType top = preset == StatPreset::kBossing
                                  ? ABILITY_LINE_TYPE_CRIT_RATE
                                  : ABILITY_LINE_TYPE_NORMAL_DAMAGE;
  const AbilityLineType attack = primary == STAT_FIELD_INT
                                     ? ABILITY_LINE_TYPE_MAGIC_ATTACK
                                     : ABILITY_LINE_TYPE_ATTACK;
  const AbilityLineType second =
      preset == StatPreset::kBossing ? attack : ABILITY_LINE_TYPE_ALL_STATS;

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
