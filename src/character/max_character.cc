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
#include "src/character/noblesse_plan.h"
#include "src/character/stat_preset.h"
#include "src/combat/damage.h"
#include "src/item/flame.h"
#include "src/item/item.h"
#include "src/item/potential.h"
#include "src/item/soul.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

namespace {

// What //analysis:progression_sim's sweep wore on arriving at each level, read
// toward its better half as kPotentialBands is; 260 was re-read off the
// 120-day sweep of 2026-10-01 and held. Every piece stands at its own star
// limit through 140. From 170 whatever can go further sits at 11-12 stars, and
// at 260 most pieces are at 17, the run the 16th star opens. The weapon stops
// at 15: a boom needs a spare copy to recover from, and a weapon rarely has
// one.
struct GearBand {
  int level;
  MaxGear gear;
};

constexpr GearBand kBands[] = {
    {0, {false, 10, 12, 0}},    {130, {false, 10, 14, 0}},
    {170, {true, 12, 14, 0}},   {230, {true, 12, 14, 230}},
    {260, {true, 17, 15, 260}},
};

// Each Arcane Symbol's level on arrival, read off the same sweep as kBands, in
// EQUIP_SLOT_SYMBOL_* order. At 200 it is the level reward alone; Morass waits
// for the first daily claim after 230. No climb held a Sacred Symbol at 260:
// they come only from drops.
struct SymbolBand {
  int level;
  int arcane[6];
};

constexpr SymbolBand kSymbolBands[] = {
    {200, {1, 0, 0, 0, 0, 0}},
    {230, {9, 9, 7, 5, 0, 0}},
    {260, {14, 14, 13, 13, 12, 11}},
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

void AddLine(Potential& potential, PotentialLineType type, PotentialRank rank) {
  PotentialLine& line = *potential.add_lines();
  line.set_type(type);
  line.set_rank(rank);
}

// What a line does for the character, turned into a line type by LineTypeFor.
enum class Share {
  kStat,     // %stat of the stat the character fights with
  kAttack,   // %ATT, or %M.ATT for a magician
  kAllStat,  // %all stats, a smaller line of the same kind
  kMaxHp,    // %Max HP, an armour piece's dead line
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
// Only `prime` leading lines are at the rank, as a player keeps what the cube
// offers; more than one is for a line only the rank itself rolls.
struct Recipe {
  PotentialRank rank = POTENTIAL_RANK_UNSPECIFIED;
  Share lines[kPotentialLines] = {};
  int prime = 1;
};

// Every kind's potential on both tracks, in SlotKind order.
struct PotentialBand {
  int level;
  Recipe main[kSlotKinds];
  Recipe bonus[kSlotKinds];
};

constexpr PotentialRank E = POTENTIAL_RANK_EPIC;
constexpr PotentialRank U = POTENTIAL_RANK_UNIQUE;
constexpr PotentialRank L = POTENTIAL_RANK_LEGENDARY;
constexpr Share S = Share::kStat;
constexpr Share A = Share::kAttack;
constexpr Share H = Share::kMaxHp;
constexpr Share T = Share::kAllStat;
constexpr Share B = Share::kBoss;
constexpr Share I = Share::kIed;
constexpr Share C = Share::kCritDamage;

// What //analysis:progression_sim's sweep (two seeds, all ten branches) wore
// on arriving at each level, read toward its better half: a max character
// spent well, not luckily. 230 is the 75-day sweep of 2026-09-30; 260 the
// 120-day one of 2026-10-01. At 200 most pieces have not been
// cubed yet, so there is no band for it. Bonus potential opens at 230, so that
// level has none yet.
constexpr PotentialBand kPotentialBands[] = {
    {230,
     {{L, {A, B, I}},
      {L, {A, B, I}},
      {L, {B, B, I}},
      {U, {S, S, T}},
      {L, {C, S, S}},
      {L, {S, S, T}},
      {L, {S, T, H}}},
     {}},
    {260,
     {{L, {B, B, I}},
      {L, {B, B, I}},
      {L, {B, B, I}},
      {L, {S, S, T}},
      {L, {C, C, S}, 2},
      {L, {S, S, T}},
      {L, {S, S, T}}},
     {{L, {A, A, A}},
      {L, {A, A, A}},
      {L, {A, A, A}},
      {U, {S, S, S}},
      {U, {S, S, T}},
      {U, {S, S, T}},
      {E, {S, S, S}}}},
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
    case Share::kAllStat:
      return own_bonus ? POTENTIAL_LINE_TYPE_BONUS_ALL_STATS_PCT
                       : POTENTIAL_LINE_TYPE_ALL_STATS_PCT;
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

// What a flame line does for the character, turned into a FlameStat by
// FlameStatFor.
enum class FlameShare {
  kPrimary,
  kPrimaryPair,  // the primary stat with the secondary
  kOtherPair,    // the primary stat with one of the other two
  kAttack,       // ATT, or MATT for a magician
  kAllStat,
  kBoss,
  kDamage,
};

struct FlameRecipeLine {
  FlameShare share;
  int tier;
};

// What //analysis:progression_sim's 120-day sweep (2026-10-01, two seeds, all
// ten branches) wore on arriving at 260, at the upper quartile of the tiers on
// lines the job uses: weapon 24, armour 21, accessories 20. The sweep shops
// Burning almost always, so nothing reaches tier 7.
constexpr int kFlameBandLevel = 260;
constexpr FlameRecipeLine kWeaponFlame[kFlameLines] = {
    {FlameShare::kAttack, 6},
    {FlameShare::kBoss, 6},
    {FlameShare::kDamage, 6},
    {FlameShare::kPrimary, 6}};
constexpr FlameRecipeLine kArmourFlame[kFlameLines] = {
    {FlameShare::kPrimary, 6},
    {FlameShare::kAttack, 5},
    {FlameShare::kAllStat, 5},
    {FlameShare::kOtherPair, 5}};
constexpr FlameRecipeLine kAccessoryFlame[kFlameLines] = {
    {FlameShare::kPrimary, 5},
    {FlameShare::kPrimaryPair, 5},
    {FlameShare::kAllStat, 5},
    {FlameShare::kOtherPair, 5}};

FlameStat SingleStat(StatField stat) {
  switch (stat) {
    case STAT_FIELD_DEX:
      return FLAME_STAT_DEX;
    case STAT_FIELD_INT:
      return FLAME_STAT_INT;
    case STAT_FIELD_LUK:
      return FLAME_STAT_LUK;
    default:
      return FLAME_STAT_STR;
  }
}

// The pair line holding both stats, in either order.
FlameStat PairOf(StatField a, StatField b) {
  auto has = [&](StatField x, StatField y) {
    return (a == x && b == y) || (a == y && b == x);
  };
  if (has(STAT_FIELD_STR, STAT_FIELD_DEX)) {
    return FLAME_STAT_STR_DEX;
  }
  if (has(STAT_FIELD_STR, STAT_FIELD_INT)) {
    return FLAME_STAT_STR_INT;
  }
  if (has(STAT_FIELD_STR, STAT_FIELD_LUK)) {
    return FLAME_STAT_STR_LUK;
  }
  if (has(STAT_FIELD_DEX, STAT_FIELD_INT)) {
    return FLAME_STAT_DEX_INT;
  }
  if (has(STAT_FIELD_DEX, STAT_FIELD_LUK)) {
    return FLAME_STAT_DEX_LUK;
  }
  return FLAME_STAT_INT_LUK;
}

// The primary stat's pair with the first stat that is neither it nor the
// secondary, in STR DEX INT LUK order.
FlameStat OtherPairOf(StatField primary, StatField secondary) {
  for (StatField other :
       {STAT_FIELD_STR, STAT_FIELD_DEX, STAT_FIELD_INT, STAT_FIELD_LUK}) {
    if (other != primary && other != secondary) {
      return PairOf(primary, other);
    }
  }
  return PairOf(primary, secondary);
}

FlameStat FlameStatFor(FlameShare share, StatField primary,
                       StatField secondary) {
  switch (share) {
    case FlameShare::kPrimary:
      return SingleStat(primary);
    case FlameShare::kPrimaryPair:
      return PairOf(primary, secondary);
    case FlameShare::kOtherPair:
      return OtherPairOf(primary, secondary);
    case FlameShare::kAttack:
      return primary == STAT_FIELD_INT ? FLAME_STAT_MAGIC_ATTACK
                                       : FLAME_STAT_ATTACK;
    case FlameShare::kAllStat:
      return FLAME_STAT_ALL_STAT;
    case FlameShare::kBoss:
      return FLAME_STAT_BOSS_DAMAGE;
    case FlameShare::kDamage:
      return FLAME_STAT_DAMAGE;
  }
  return FLAME_STAT_UNSPECIFIED;
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

int MaxSymbolLevel(EquipSlot slot, int level) {
  const int area = slot - EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY;
  if (area < 0 || area >= 6) {
    return 0;
  }
  int symbol_level = 0;
  for (const SymbolBand& band : kSymbolBands) {
    if (level >= band.level) {
      symbol_level = band.arcane[area];
    }
  }
  return symbol_level;
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
    const PotentialRank rank =
        i < recipe.prime ? recipe.rank : PreviousPotentialRank(recipe.rank);
    AddLine(potential,
            LineTypeFor(recipe.lines[i], rank, track, weaponry, primary), rank);
  }
  return potential;
}

FlameLines MaxFlameFor(const EquipPrototype& proto, int level,
                       StatField primary, StatField secondary) {
  FlameLines lines;
  if (level < kFlameBandLevel || !SlotTakesFlame(proto.equip_slot()) ||
      !Supports(proto, UPGRADE_FLAME)) {
    return lines;
  }
  const FlameRecipeLine* recipe =
      proto.equip_slot() == EQUIP_SLOT_PRIMARY_WEAPON ? kWeaponFlame
      : PotentialGroupOf(proto.equip_slot()) == PotentialGroup::kAccessory
          ? kAccessoryFlame
          : kArmourFlame;
  // A line the item's pool lacks, such as ATT below level 60, is left off
  // rather than replaced.
  const std::vector<FlameStat> pool = FlamePool(proto);
  for (int i = 0; i < kFlameLines; ++i) {
    const FlameStat stat = FlameStatFor(recipe[i].share, primary, secondary);
    if (std::find(pool.begin(), pool.end(), stat) == pool.end()) {
      continue;
    }
    FlameLine& line = *lines.Add();
    line.set_stat(stat);
    line.set_tier(recipe[i].tier);
  }
  return lines;
}

void SpendMaxNoblesse(CharacterInstance& character,
                      const std::map<std::string, Skill>& skills,
                      const std::map<std::string, Boss>& bosses,
                      const std::map<std::string, Mob>& mobs) {
  const Mob* target = NominalTarget(bosses, mobs, character.proto().level(),
                                    Activity::kBossing);
  RefundNoblesseSp(character, skills);
  SpendNoblesseSp(character, skills, [&skills, target](CharacterInstance& c) {
    return MaxHyperRate(c, skills, Activity::kBossing, target);
  });
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

void WearMaxSoul(CharacterInstance& character,
                 const std::map<std::string, Skill>& skills,
                 const std::map<std::string, Boss>& bosses,
                 const std::map<std::string, Mob>& mobs,
                 const std::map<std::string, ItemPrototype>& items) {
  const int level = character.proto().level();
  if (level < kSoulUnlockLevel) {
    return;
  }
  // The latest boss of the best tier, so the name is the one a player at this
  // level would be farming.
  const ItemPrototype* best = nullptr;
  int best_unlock = 0;
  for (const std::pair<const std::string, Boss>& entry : bosses) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (difficulty.coming_soon() || difficulty.unlock_level() > level) {
        continue;
      }
      for (const MobDrop& drop : difficulty.drops()) {
        auto found = items.find(drop.item());
        if (found == items.end() ||
            found->second.soul_tier() == SOUL_TIER_UNSPECIFIED) {
          continue;
        }
        const ItemPrototype& shard = found->second;
        if (best == nullptr || shard.soul_tier() > best->soul_tier() ||
            (shard.soul_tier() == best->soul_tier() &&
             difficulty.unlock_level() > best_unlock)) {
          best = &shard;
          best_unlock = difficulty.unlock_level();
        }
      }
    }
  }
  if (best == nullptr) {
    return;
  }
  const Mob* target = NominalTarget(bosses, mobs, level, Activity::kBossing);
  Soul soul;
  soul.set_boss(best->short_name());
  soul.set_tier(best->soul_tier());
  SoulLine best_line = SOUL_LINE_UNSPECIFIED;
  double best_rate = -1.0;
  for (int line = SOUL_LINE_ATTACK; line <= SOUL_LINE_BOSS_DAMAGE; ++line) {
    soul.set_line(static_cast<SoulLine>(line));
    if (!character.TakeSoul(EQUIP_SLOT_PRIMARY_WEAPON, soul)) {
      return;
    }
    const double rate =
        MaxHyperRate(character, skills, Activity::kBossing, target);
    if (rate > best_rate) {
      best_rate = rate;
      best_line = soul.line();
    }
  }
  soul.set_line(best_line);
  character.TakeSoul(EQUIP_SLOT_PRIMARY_WEAPON, soul);
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
