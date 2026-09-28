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

// What //analysis:progression_sim's 75-day sweep (2026-09-27, one seed, all
// ten branches) wore on arriving at each level, read toward its better half as
// kPotentialBands is. Every piece stands at its own star limit through 140.
// From 170 whatever can go further sits at 11-13 stars, and from 230 most
// pieces are at 17, the run the 16th star opens. The weapon stops at 15: a
// boom needs a spare copy to recover from, and a weapon rarely has one.
struct GearBand {
  int level;
  MaxGear gear;
};

constexpr GearBand kBands[] = {
    {0, {false, 10, 12, 0}},    {130, {false, 10, 14, 0}},
    {170, {true, 12, 14, 0}},   {200, {true, 12, 14, 200}},
    {230, {true, 17, 14, 230}}, {260, {true, 17, 15, 260}},
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

// The same on a non-weapon's bonus potential, which has its own %stat lines.
PotentialLineType BonusStatShareFor(StatField primary) {
  switch (primary) {
    case STAT_FIELD_DEX:
      return POTENTIAL_LINE_TYPE_BONUS_DEX_PCT;
    case STAT_FIELD_INT:
      return POTENTIAL_LINE_TYPE_BONUS_INT_PCT;
    case STAT_FIELD_LUK:
      return POTENTIAL_LINE_TYPE_BONUS_LUK_PCT;
    default:
      return POTENTIAL_LINE_TYPE_BONUS_STR_PCT;
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

// What a line does for the character, turned into a line type by LineTypeFor.
enum class Share {
  kStat,       // %stat of the stat the character fights with
  kAttack,     // %ATT, or %M.ATT for a magician
  kOffAttack,  // the other one, which does nothing: a weapon's dead line
  kMaxHp,      // %Max HP, an armour piece's dead line
  kIed,
  kBoss,
  kCritDamage,
};

// The kinds of item that wear different lines.
enum class SlotKind {
  kWeapon,
  kSecondary,
  kEmblem,
  kHat,
  kGloves,
  kArmour,
  kAccessory,
};
constexpr int kSlotKinds = 7;

SlotKind KindOf(EquipSlot slot) {
  switch (slot) {
    case EQUIP_SLOT_PRIMARY_WEAPON:
      return SlotKind::kWeapon;
    case EQUIP_SLOT_SECONDARY:
      return SlotKind::kSecondary;
    case EQUIP_SLOT_EMBLEM:
      return SlotKind::kEmblem;
    case EQUIP_SLOT_HAT:
      return SlotKind::kHat;
    case EQUIP_SLOT_GLOVES:
      return SlotKind::kGloves;
    default:
      return PotentialGroupOf(slot) == PotentialGroup::kAccessory
                 ? SlotKind::kAccessory
                 : SlotKind::kArmour;
  }
}

// One kind of item's potential: its rank and its three lines, prime first.
struct Recipe {
  PotentialRank rank = POTENTIAL_RANK_UNSPECIFIED;
  Share lines[kPotentialLines] = {};
};

// Every kind's potential on both tracks, in SlotKind order.
struct PotentialBand {
  int level;
  Recipe main[kSlotKinds];
  Recipe bonus[kSlotKinds];
};

constexpr PotentialRank R = POTENTIAL_RANK_RARE;
constexpr PotentialRank E = POTENTIAL_RANK_EPIC;
constexpr PotentialRank U = POTENTIAL_RANK_UNIQUE;
constexpr PotentialRank L = POTENTIAL_RANK_LEGENDARY;
constexpr Share S = Share::kStat;
constexpr Share A = Share::kAttack;
constexpr Share X = Share::kOffAttack;
constexpr Share H = Share::kMaxHp;

// What //analysis:progression_sim's 75-day sweep (2026-09-26, one seed, all
// ten branches) wore on arriving at each level, read toward its better half:
// a max character spent well, not luckily. The weapon stays Unique at 260
// because it is replaced too often for the 2.4% step to Legendary to pay;
// the secondary and emblem are kept, and get there. Bonus potential opens at
// 230, so that level has none yet.
constexpr PotentialBand kPotentialBands[] = {
    {200,
     {{E, {A, A, X}},
      {E, {A, A, X}},
      {E, {A, A, X}},
      {R, {S, H, H}},
      {R, {S, H, H}},
      {R, {S, H, H}},
      {R, {S, H, H}}},
     {}},
    {230,
     {{U, {Share::kIed, A, X}},
      {E, {A, A, X}},
      {L, {Share::kBoss, Share::kBoss, Share::kIed}},
      {E, {S, S, H}},
      {E, {S, S, H}},
      {E, {S, S, H}},
      {R, {S, S, H}}},
     {}},
    {260,
     {{U, {Share::kIed, A, A}},
      {L, {Share::kBoss, Share::kBoss, Share::kIed}},
      {L, {Share::kBoss, Share::kBoss, Share::kIed}},
      {U, {S, S, H}},
      {L, {Share::kCritDamage, S, S}},
      {U, {S, S, S}},
      {E, {S, S, H}}},
     {{U, {A, A, X}},
      {U, {A, A, X}},
      {U, {A, A, X}},
      {E, {S, S, H}},
      {E, {S, S, H}},
      {E, {S, S, H}},
      {E, {S, S, H}}}},
};

// The band `level` names, or null for none.
const PotentialBand* BandFor(int level) {
  for (const PotentialBand& band : kPotentialBands) {
    if (band.level == level) {
      return &band;
    }
  }
  return nullptr;
}

// The line type `share` is on `track` at `rank`. Bonus weaponry reuses the
// main %stat and %attack types; the rest of bonus has its own. Ignored defence
// and boss damage come in one size per rank.
PotentialLineType LineTypeFor(Share share, PotentialRank rank,
                              PotentialTrack track, bool weaponry,
                              StatField primary) {
  const bool own_bonus = track == PotentialTrack::kBonus && !weaponry;
  switch (share) {
    case Share::kStat:
      return own_bonus ? BonusStatShareFor(primary) : StatShareFor(primary);
    case Share::kAttack:
      return AttackShareFor(primary);
    case Share::kOffAttack:
      return DeadShareFor(primary);
    case Share::kMaxHp:
      return track == PotentialTrack::kBonus
                 ? POTENTIAL_LINE_TYPE_BONUS_MAX_HP_PCT
                 : POTENTIAL_LINE_TYPE_MAX_HP_PCT;
    case Share::kIed:
      return rank == L   ? POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40
             : rank == U ? POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30
                         : POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15;
    case Share::kBoss:
      return rank == L ? POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40
                       : POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30;
    case Share::kCritDamage:
      return POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT;
  }
  return POTENTIAL_LINE_TYPE_UNSPECIFIED;
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
                          StatField primary, PotentialTrack track) {
  Potential potential;
  const PotentialGroup group = PotentialGroupOf(slot);
  const PotentialBand* band = BandFor(gear.potential_level);
  if (group == PotentialGroup::kNone || band == nullptr) {
    return potential;
  }
  const Recipe& recipe = (track == PotentialTrack::kMain
                              ? band->main
                              : band->bonus)[static_cast<int>(KindOf(slot))];
  if (recipe.rank == POTENTIAL_RANK_UNSPECIFIED) {
    return potential;
  }
  potential.set_rank(recipe.rank);
  const bool weaponry = group == PotentialGroup::kWeaponry;
  for (int i = 0; i < kPotentialLines; ++i) {
    // Only the first line is prime, as a player keeps what the cube offers.
    const PotentialRank rank =
        i == 0 ? recipe.rank : PreviousPotentialRank(recipe.rank);
    AddLine(potential,
            LineTypeFor(recipe.lines[i], rank, track, weaponry, primary), rank);
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
