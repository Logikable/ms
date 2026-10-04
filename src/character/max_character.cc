#include "src/character/max_character.h"

#include <algorithm>
#include <cmath>
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
#include "src/character/set_pieces.h"
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

// The stars //analysis:progression_sim's sweep of 2026-10-04 wore on arriving
// at each level: the fifth of ten branches from the top over every starred
// piece. A fresh tier arrives at 10-11 whatever came before it.
struct GearBand {
  int level;
  MaxGear gear;
};

constexpr GearBand kBands[] = {
    {0, {5, 5, 0}},     {140, {8, 14, 0}},    {170, {10, 14, 0}},
    {200, {10, 14, 0}}, {230, {10, 13, 230}}, {260, {11, 14, 260}},
};

// One piece of an outfit: a catalog key every job wears, or the job's own
// piece of a set family in `slot`.
struct Pick {
  const char* item;
  SetFamily family;
  EquipSlot slot;
};

constexpr Pick Item(const char* key) {
  return {key, SetFamily::kRootAbyss, EQUIP_SLOT_UNSPECIFIED};
}
constexpr Pick Set(SetFamily family, EquipSlot slot) {
  return {nullptr, family, slot};
}

constexpr int kMaxPicks = 32;
struct OutfitBand {
  int level;
  Pick picks[kMaxPicks];
};

constexpr SetFamily kRA = SetFamily::kRootAbyss;
constexpr SetFamily kAL = SetFamily::kAbsoLab;

// What the sweep of 2026-10-04 wore in each slot on arriving at each level: the
// fifth of ten branches from the top, by set family so each job wears its own
// piece. The job's own weapon, secondary and ammunition come first, from the
// workbench's table. At 260 the top stays AbsoLab: the typical runs there
// happened to hold Root Abyss, and no player sells an AbsoLab top back.
constexpr OutfitBand kOutfits[] = {
    {100, {Item("frozen_hat"), Item("frozen_top"), Item("frozen_bottom")}},
    {140,
     {Item("frozen_hat"), Item("frozen_top"), Item("frozen_bottom"),
      Item("frozen_cape"), Item("gold_maple_leaf_emblem"),
      Item("master_adventurer"), Item("lightning_god_ring"),
      Item("condensed_power_crystal")}},
    {170,
     {Item("frozen_hat"), Item("frozen_top"), Item("frozen_bottom"),
      Item("frozen_cape"), Item("frozen_gloves"), Item("frozen_boots"),
      Item("gold_maple_leaf_emblem"), Item("master_adventurer"),
      Item("lightning_god_ring"), Item("meister_ring"),
      Item("silver_blossom_ring"), Item("condensed_power_crystal"),
      Item("aquatic_letter_eye_accessory"), Item("dea_sidus_earring"),
      Item("horntail_necklace"), Item("stone_of_eternal_life"),
      Item("royal_black_metal_shoulder"), Item("crystal_ventus_badge")}},
    {200,
     {Item("frozen_hat"),
      Item("frozen_top"),
      Item("frozen_bottom"),
      Item("frozen_cape"),
      Item("frozen_gloves"),
      Item("frozen_boots"),
      Item("gold_maple_leaf_emblem"),
      Item("master_adventurer"),
      Item("lightning_god_ring"),
      Item("meister_ring"),
      Item("silver_blossom_ring"),
      Item("condensed_power_crystal"),
      Item("black_bean_mark"),
      Item("dea_sidus_earring"),
      Item("horntail_necklace"),
      Item("pink_holy_cup"),
      Item("royal_black_metal_shoulder"),
      Item("crystal_ventus_badge"),
      Item("golden_clover_belt"),
      Item("bronze_incense_burner_totem")}},
    {230,
     {Set(kRA, EQUIP_SLOT_HAT),
      Set(kAL, EQUIP_SLOT_TOP),
      Set(kRA, EQUIP_SLOT_BOTTOM),
      Set(kAL, EQUIP_SLOT_CAPE),
      Set(kAL, EQUIP_SLOT_GLOVES),
      Item("frozen_boots"),
      Set(kAL, EQUIP_SLOT_PRIMARY_WEAPON),
      Set(SetFamily::kPrincessNo, EQUIP_SLOT_SECONDARY),
      Set(SetFamily::kCygnus, EQUIP_SLOT_SHOULDER),
      Item("gold_maple_leaf_emblem"),
      Item("master_adventurer"),
      Item("lightning_god_ring"),
      Item("meister_ring"),
      Item("silver_blossom_ring"),
      Item("kannas_treasure"),
      Item("condensed_power_crystal"),
      Item("papulatus_mark"),
      Item("dea_sidus_earring"),
      Item("horntail_necklace"),
      Item("dominator_pendant"),
      Item("pink_holy_cup"),
      Item("crystal_ventus_badge"),
      Item("golden_clover_belt"),
      Item("bronze_incense_burner_totem"),
      Item("horseback_riding_doll_totem"),
      Item("jade_kettle_totem")}},
    {260,
     {Set(kAL, EQUIP_SLOT_HAT),
      Set(kAL, EQUIP_SLOT_TOP),
      Set(kAL, EQUIP_SLOT_BOTTOM),
      Set(kAL, EQUIP_SLOT_CAPE),
      Set(kAL, EQUIP_SLOT_GLOVES),
      Set(kAL, EQUIP_SLOT_SHOES),
      Set(kAL, EQUIP_SLOT_SHOULDER),
      Set(kAL, EQUIP_SLOT_PRIMARY_WEAPON),
      Set(SetFamily::kPrincessNo, EQUIP_SLOT_SECONDARY),
      Item("gold_maple_leaf_emblem"),
      Item("master_adventurer"),
      Item("guardian_angel_ring"),
      Item("meister_ring"),
      Item("silver_blossom_ring"),
      Item("kannas_treasure"),
      Item("berserked"),
      Item("magic_eyepatch"),
      Item("estella_earrings"),
      Item("horntail_necklace"),
      Item("dominator_pendant"),
      Item("pink_holy_cup"),
      Item("black_heart"),
      Item("crystal_ventus_badge"),
      Item("golden_clover_belt"),
      Item("bronze_incense_burner_totem"),
      Item("horseback_riding_doll_totem"),
      Item("jade_kettle_totem")}},
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
    {230, {12, 10, 8, 6, 0, 0}},
    {260, {17, 16, 15, 15, 14, 13}},
};

// The level each fight's first solo clear came at in the same sweep. Hard
// Damien, Hard Lotus and Darknell wait until 255, long after they open.
struct Clear {
  const char* boss;
  const char* difficulty;
  int level;
};

constexpr Clear kClears[] = {
    {"zakum", "Normal", 110},
    {"hilla", "Normal", 130},
    {"horntail", "Normal", 150},
    {"magnus", "Normal", 160},
    {"pink_bean", "Normal", 170},
    {"arkarium", "Normal", 193},
    {"horntail", "Chaos", 200},
    {"hilla", "Hard", 201},
    {"cygnus", "Normal", 203},
    {"pierre", "Chaos", 204},
    {"von_bon", "Chaos", 206},
    {"crimson_queen", "Chaos", 209},
    {"zakum", "Chaos", 209},
    {"pink_bean", "Chaos", 211},
    {"magnus", "Hard", 211},
    {"vellum", "Chaos", 212},
    {"papulatus", "Chaos", 219},
    {"princess_no", "Normal", 219},
    {"damien", "Normal", 224},
    {"lotus", "Normal", 225},
    {"guardian_angel_slime", "Normal", 231},
    {"lucid", "Normal", 243},
    {"will", "Normal", 250},
    {"gloom", "Normal", 253},
    {"damien", "Hard", 255},
    {"lotus", "Hard", 255},
    {"darknell", "Normal", 255},
    // Not yet read off a sweep: the cap until one is.
    {"guardian_angel_slime", "Chaos", 260},
    {"lucid", "Hard", 260},
    {"will", "Hard", 260},
    {"gloom", "Chaos", 260},
    {"darknell", "Hard", 260},
};

// The alts the same sweep had levelled by each level: how many, to what
// level, and which lines, commonest first. A line the main is on is skipped
// and the next one taken.
struct AltBand {
  int level;
  int count;
  int alt_level;
  Job lines[7];
};

constexpr AltBand kAltBands[] = {
    {230,
     3,
     70,
     {JOB_CROSSBOWMAN, JOB_HUNTER, JOB_ICE_LIGHTNING_WIZARD,
      JOB_FIRE_POISON_WIZARD, JOB_BANDIT}},
    {260,
     5,
     120,
     {JOB_CROSSBOWMAN, JOB_HUNTER, JOB_FIRE_POISON_WIZARD,
      JOB_ICE_LIGHTNING_WIZARD, JOB_BANDIT, JOB_CLERIC, JOB_ASSASSIN}},
};

// The share of each node kind's levels the same sweep had bought. The sweep
// spends on the nodes it measures best and maxes them; spreading the same
// share over every node of the kind keeps the points without choosing for
// each job.
struct MatrixBand {
  int level;
  double job;
  double boost;
  double archetype;
  double common;
};

constexpr MatrixBand kMatrixBands[] = {
    {200, 0.0, 0.0, 0.0, 0.0},
    {230, 2.0 / 3, 2.0 / 3, 0.4, 0.2},
    {260, 1.0, 1.0, 1.0, 1.0},
};

// The level from which the sweep's bossing Inner Ability had reached
// Legendary. Farming never left Rare.
constexpr int kLegendaryAbilityLevel = 170;

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

constexpr PotentialRank R = POTENTIAL_RANK_RARE;
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

// What //analysis:progression_sim's sweeps wore on arriving at each level, the
// fifth of ten branches from the top: a max character spent well, not
// luckily. Re-read off the sweep of 2026-10-04, which moved 230's gloves and
// armour to Unique and 260's bonus potentials down a rank. At 200 most pieces
// have not been cubed yet, so there is no band for it. Bonus potential opens
// at 230, so that level has none yet.
constexpr PotentialBand kPotentialBands[] = {
    {230,
     {{L, {A, B, I}},
      {L, {A, B, I}},
      {L, {B, B, I}},
      {U, {S, S, T}},
      {U, {S, S, T}},
      {U, {S, S, T}},
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
      {E, {S, S, S}},
      {E, {S, S, S}},
      {E, {S, S, S}},
      {R, {S, S, H}, 3}}},
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

std::vector<std::string> MaxOutfit(Job job, int level) {
  const OutfitBand* band = nullptr;
  for (const OutfitBand& candidate : kOutfits) {
    if (level >= candidate.level) {
      band = &candidate;
    }
  }
  std::vector<std::string> keys;
  if (band == nullptr) {
    return keys;
  }
  for (const Pick& pick : band->picks) {
    std::string key = pick.item != nullptr ? pick.item
                      : pick.slot != EQUIP_SLOT_UNSPECIFIED
                          ? SetPieceFor(pick.family, pick.slot, job)
                          : "";
    if (!key.empty()) {
      keys.push_back(std::move(key));
    }
  }
  return keys;
}

int MaxClearLevel(const std::string& boss, const std::string& difficulty) {
  for (const Clear& clear : kClears) {
    if (boss == clear.boss && difficulty == clear.difficulty) {
      return clear.level;
    }
  }
  return 0;
}

std::vector<MaxAlt> MaxAlts(Job played_line, int level) {
  std::vector<MaxAlt> alts;
  const AltBand* band = nullptr;
  for (const AltBand& candidate : kAltBands) {
    if (level >= candidate.level) {
      band = &candidate;
    }
  }
  if (band == nullptr) {
    return alts;
  }
  for (Job line : band->lines) {
    if (static_cast<int>(alts.size()) == band->count) {
      break;
    }
    if (line != JOB_UNSPECIFIED && line != played_line) {
      alts.push_back({line, band->alt_level});
    }
  }
  return alts;
}

int MaxMatrixLevel(const Skill& node, int level) {
  double share = 0.0;
  for (const MatrixBand& band : kMatrixBands) {
    if (level < band.level) {
      continue;
    }
    switch (node.v_node()) {
      case V_NODE_KIND_JOB:
        share = band.job;
        break;
      case V_NODE_KIND_BOOST:
        share = band.boost;
        break;
      case V_NODE_KIND_ARCHETYPE:
        share = band.archetype;
        break;
      default:
        share = band.common;
        break;
    }
  }
  return static_cast<int>(std::lround(share * SkillMaxLevel(node)));
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

AbilityPreset MaxAbilityPreset(Activity preset, StatField primary, int level) {
  AbilityPreset built;
  // The rank a character starts at. Three All Stat lines are what the sweep's
  // farming preset sat on for good: the honor all went to bossing.
  if (preset == Activity::kFarming || level < kLegendaryAbilityLevel) {
    built.set_rank(ABILITY_RANK_RARE);
    for (int i = 0; i < kAbilityLines; ++i) {
      AbilityLine& line = *built.add_lines();
      line.set_type(ABILITY_LINE_TYPE_ALL_STATS);
      line.set_rank(ABILITY_RANK_RARE);
    }
    return built;
  }
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
  // Epic far more often than Unique. Critical rate on top is what
  // //analysis:ability_plan finds best for bosses.
  //
  // Attack Speed is the line GMS players chase, and it's deliberately left out:
  // the Extreme Green Potion already gives boss fights an extra stage past the
  // cap this line is limited by, so the line would be useless in every boss
  // fight.
  const AbilityLineType attack = primary == STAT_FIELD_INT
                                     ? ABILITY_LINE_TYPE_MAGIC_ATTACK
                                     : ABILITY_LINE_TYPE_ATTACK;
  built.set_rank(ABILITY_RANK_LEGENDARY);
  const AbilityLineType types[] = {ABILITY_LINE_TYPE_CRIT_RATE, attack, stat};
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
