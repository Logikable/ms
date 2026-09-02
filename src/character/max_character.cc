#include "src/character/max_character.h"

#include <algorithm>
#include <vector>

#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/character/inner_ability.h"
#include "src/character/job_branch.h"
#include "src/character/stat_preset.h"
#include "src/item/potential.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

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

void AddLine(Potential& potential, PotentialLineType type, PotentialRank rank) {
  PotentialLine& line = *potential.add_lines();
  line.set_type(type);
  line.set_rank(rank);
}

// The stats a fight is won on. Order settles a tie between two equal offers
// and nothing else: what decides the allocation is that a level's price
// climbs with the level it reaches, so the pool spreads itself over the whole
// list rather than maxing the head of it. That is the shape
// //analysis:hyper_plan arrives at from measurement -- eight or nine stats
// around level six at the cap, not two stats at ten.
std::vector<HyperStatField> HyperStatsFor(StatPreset preset, Job job) {
  HyperStatField stat = HYPER_STAT_FIELD_STR;
  // The stat the damage chain adds to four times the primary. Worth a ninth
  // of what the primary is, and here for the tail of the pool: the last
  // twenty points buy four levels of a stat standing at zero and nothing at
  // all of one already at six.
  HyperStatField secondary = HYPER_STAT_FIELD_DEX;
  switch (BranchOf(job)) {
    case JobBranch::kArcher:
      stat = HYPER_STAT_FIELD_DEX;
      secondary = HYPER_STAT_FIELD_STR;
      break;
    case JobBranch::kMagician:
      stat = HYPER_STAT_FIELD_INT;
      secondary = HYPER_STAT_FIELD_LUK;
      break;
    case JobBranch::kRogue:
      stat = HYPER_STAT_FIELD_LUK;
      secondary = HYPER_STAT_FIELD_DEX;
      break;
    default:
      break;
  }
  return {HYPER_STAT_FIELD_ATTACK,
          HYPER_STAT_FIELD_DAMAGE,
          preset == StatPreset::kBossing ? HYPER_STAT_FIELD_BOSS_DAMAGE
                                         : HYPER_STAT_FIELD_NORMAL_DAMAGE,
          HYPER_STAT_FIELD_IED,
          HYPER_STAT_FIELD_CRIT_DAMAGE,
          HYPER_STAT_FIELD_CRIT_RATE,
          stat,
          HYPER_STAT_FIELD_MAX_HP,
          secondary};
}

// Spends one preset's pool: the cheapest level on offer, over and over, until
// nothing left is affordable. Cheapest first rather than best first because
// what a level costs is the whole of the difference here -- the fifteenth
// level of one stat is 110 points against ten for the fifth of another, and
// no stat on the list is worth eleven times another.
void SpendPreset(CharacterInstance& character, StatPreset preset) {
  character.ResetHyperStats(preset);
  const std::vector<HyperStatField> stats =
      HyperStatsFor(preset, character.proto().job());
  while (true) {
    HyperStatField cheapest = HYPER_STAT_FIELD_UNSPECIFIED;
    int price = 0;
    for (HyperStatField field : stats) {
      const int next = character.hyper_stat_level(field, preset) + 1;
      const int cost = HyperStatLevelCost(next);
      if (next > character.max_hyper_stat_level() ||
          cost > character.hyper_stat_points_left(preset)) {
        continue;
      }
      if (cheapest == HYPER_STAT_FIELD_UNSPECIFIED || cost < price) {
        cheapest = field;
        price = cost;
      }
    }
    if (cheapest == HYPER_STAT_FIELD_UNSPECIFIED ||
        !character.AllocateHyperStat(cheapest, preset)) {
      return;
    }
  }
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
  if (weaponry) {
    const PotentialLineType attack = AttackShareFor(primary);
    if (slot == EQUIP_SLOT_PRIMARY_WEAPON) {
      AddLine(potential, POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30, rank);
    } else if (slot == EQUIP_SLOT_SECONDARY) {
      AddLine(potential, POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30, rank);
    } else {
      AddLine(potential, attack, rank);
    }
    while (potential.lines_size() < kPotentialLines) {
      AddLine(potential, attack, PreviousPotentialRank(rank));
    }
    return potential;
  }
  const PotentialLineType share = StatShareFor(primary);
  AddLine(potential, share, rank);
  while (potential.lines_size() < kPotentialLines) {
    AddLine(potential, share, PreviousPotentialRank(rank));
  }
  return potential;
}

void SpendMaxHyperStats(CharacterInstance& character) {
  SpendPreset(character, StatPreset::kFarming);
  SpendPreset(character, StatPreset::kBossing);
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
