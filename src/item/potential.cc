#include "src/item/potential.h"

#include <iterator>
#include <random>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// The groups a line can roll on, as a set. Hat, gloves, armour and accessory
// are apart only for the four lines that belong to one of them, so most rows
// name kNonWeapon or kEveryGroup.
enum GroupBit {
  kWeaponryBit = 1 << 0,
  kHatBit = 1 << 1,
  kGlovesBit = 1 << 2,
  kArmorBit = 1 << 3,
  kAccessoryBit = 1 << 4,
};

constexpr int kNonWeapon = kHatBit | kGlovesBit | kArmorBit | kAccessoryBit;
constexpr int kEveryGroup = kWeaponryBit | kNonWeapon;

// One line the game offers: where it rolls and the ranks it rolls at. GMS
// puts an equipment level on its strongest lines as well; that is dropped,
// since a level 30 item never reaches the ranks they sit at anyway.
struct LineSpec {
  PotentialLineType type;
  int groups;
  PotentialRank min_rank;
  PotentialRank max_rank;
};

constexpr LineSpec kLines[] = {
    // Flat stats, Rare alone. Epic and up dropped them in GMS, which is most
    // of what a rank up buys.
    {POTENTIAL_LINE_TYPE_STR, kNonWeapon, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_RARE},
    {POTENTIAL_LINE_TYPE_DEX, kNonWeapon, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_RARE},
    {POTENTIAL_LINE_TYPE_INT, kNonWeapon, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_RARE},
    {POTENTIAL_LINE_TYPE_LUK, kNonWeapon, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_RARE},
    {POTENTIAL_LINE_TYPE_ALL_STATS, kNonWeapon, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_RARE},
    {POTENTIAL_LINE_TYPE_MAX_HP, kNonWeapon, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_RARE},

    {POTENTIAL_LINE_TYPE_STR_PCT, kEveryGroup, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_DEX_PCT, kEveryGroup, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_INT_PCT, kEveryGroup, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_LUK_PCT, kEveryGroup, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_MAX_HP_PCT, kEveryGroup, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_ALL_STATS_PCT, kEveryGroup, POTENTIAL_RANK_EPIC,
     POTENTIAL_RANK_LEGENDARY},

    // The weapon lines. A secondary and an emblem roll them too -- see
    // PotentialGroupOf.
    {POTENTIAL_LINE_TYPE_ATTACK_PCT, kWeaponryBit, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT, kWeaponryBit, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_DAMAGE_PCT, kWeaponryBit, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15, kWeaponryBit, POTENTIAL_RANK_EPIC,
     POTENTIAL_RANK_EPIC},
    {POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30, kWeaponryBit, POTENTIAL_RANK_UNIQUE,
     POTENTIAL_RANK_UNIQUE},
    {POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35, kWeaponryBit,
     POTENTIAL_RANK_LEGENDARY, POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40, kWeaponryBit,
     POTENTIAL_RANK_LEGENDARY, POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30, kWeaponryBit, POTENTIAL_RANK_UNIQUE,
     POTENTIAL_RANK_UNIQUE},
    {POTENTIAL_LINE_TYPE_BOSS_DAMAGE_35, kWeaponryBit, POTENTIAL_RANK_LEGENDARY,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40, kWeaponryBit, POTENTIAL_RANK_LEGENDARY,
     POTENTIAL_RANK_LEGENDARY},

    // The four lines one group apiece rolls.
    {POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT, kGlovesBit, POTENTIAL_RANK_LEGENDARY,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_MESO_RATE, kAccessoryBit, POTENTIAL_RANK_LEGENDARY,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_ITEM_DROP_RATE, kAccessoryBit,
     POTENTIAL_RANK_LEGENDARY, POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_COOLDOWN_1, kHatBit, POTENTIAL_RANK_LEGENDARY,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_COOLDOWN_2, kHatBit, POTENTIAL_RANK_LEGENDARY,
     POTENTIAL_RANK_LEGENDARY},
};

// One band of equipment level and what a line pays inside it. The last row of
// a table stands for every level above it.
struct Band {
  int max_level;
  int value[4];
};

constexpr int kNoCeiling = 100000;

// The percentage lines, by rank. One table serves %STR, %DEX, %INT, %LUK,
// %HP, %ATT, %MATT and %Damage -- GMS pays them all the same.
constexpr Band kPercentBands[] = {
    {30, {1, 2, 3, 6}},
    {70, {2, 4, 6, 9}},
    {150, {3, 6, 9, 12}},
    {kNoCeiling, {4, 7, 10, 13}},
};

// Flat STR, DEX, INT and LUK.
constexpr Band kFlatStatBands[] = {
    {20, {2, 0, 0, 0}},          {40, {4, 0, 0, 0}},  {50, {6, 0, 0, 0}},
    {70, {8, 0, 0, 0}},          {90, {10, 0, 0, 0}}, {150, {12, 0, 0, 0}},
    {kNoCeiling, {13, 0, 0, 0}},
};

constexpr Band kFlatAllStatsBands[] = {
    {20, {1, 0, 0, 0}}, {40, {2, 0, 0, 0}},  {60, {3, 0, 0, 0}},
    {80, {4, 0, 0, 0}}, {150, {5, 0, 0, 0}}, {kNoCeiling, {6, 0, 0, 0}},
};

// Flat Max HP: ten per ten levels of equipment, levelling off past 110.
constexpr Band kFlatMaxHpBands[] = {
    {10, {10, 0, 0, 0}},          {20, {20, 0, 0, 0}},   {30, {30, 0, 0, 0}},
    {40, {40, 0, 0, 0}},          {50, {50, 0, 0, 0}},   {60, {60, 0, 0, 0}},
    {70, {70, 0, 0, 0}},          {80, {80, 0, 0, 0}},   {90, {90, 0, 0, 0}},
    {100, {100, 0, 0, 0}},        {110, {110, 0, 0, 0}}, {150, {120, 0, 0, 0}},
    {kNoCeiling, {125, 0, 0, 0}},
};

// Gloves' critical damage, which nothing under level 50 rolls.
constexpr Band kCritDamageBands[] = {
    {60, {0, 0, 0, 5}},
    {80, {0, 0, 0, 6}},
    {kNoCeiling, {0, 0, 0, 8}},
};

// An accessory's %meso and %drop, which are Legendary alone.
constexpr Band kRewardRateBands[] = {
    {30, {0, 0, 0, 10}},
    {70, {0, 0, 0, 15}},
    {kNoCeiling, {0, 0, 0, 20}},
};

// Chance a cube carries a potential up a rank, by the rank it is leaving.
constexpr double kRedRankUp[4] = {1.0 / 7.0, 0.06, 0.024, 0.0};

// Chance the 2nd and 3rd lines come out prime.
constexpr double kRedPrime[kPotentialLines] = {1.0, 0.10, 0.01};

int RankIndex(PotentialRank rank) {
  return rank - POTENTIAL_RANK_RARE;
}

int GroupBitOf(PotentialGroup group) {
  switch (group) {
    case PotentialGroup::kWeaponry:
      return kWeaponryBit;
    case PotentialGroup::kHat:
      return kHatBit;
    case PotentialGroup::kGloves:
      return kGlovesBit;
    case PotentialGroup::kArmor:
      return kArmorBit;
    case PotentialGroup::kAccessory:
      return kAccessoryBit;
    case PotentialGroup::kNone:
      return 0;
  }
  return 0;
}

int BandValue(const Band* bands, int count, int item_level, int rank_index) {
  for (int i = 0; i < count; ++i) {
    if (item_level <= bands[i].max_level) {
      return bands[i].value[rank_index];
    }
  }
  return bands[count - 1].value[rank_index];
}

const LineSpec* SpecFor(PotentialLineType type) {
  for (const LineSpec& spec : kLines) {
    if (spec.type == type) {
      return &spec;
    }
  }
  return nullptr;
}

}  // namespace

PotentialGroup PotentialGroupOf(EquipSlot slot) {
  switch (slot) {
    case EQUIP_SLOT_PRIMARY_WEAPON:
    case EQUIP_SLOT_SECONDARY:
    case EQUIP_SLOT_EMBLEM:
      return PotentialGroup::kWeaponry;
    case EQUIP_SLOT_HAT:
      return PotentialGroup::kHat;
    case EQUIP_SLOT_GLOVES:
      return PotentialGroup::kGloves;
    case EQUIP_SLOT_TOP:
    case EQUIP_SLOT_BOTTOM:
    case EQUIP_SLOT_CAPE:
    case EQUIP_SLOT_SHOES:
    case EQUIP_SLOT_SHOULDER:
    case EQUIP_SLOT_BELT:
    case EQUIP_SLOT_HEART:
      return PotentialGroup::kArmor;
    case EQUIP_SLOT_FACE_ACCESSORY:
    case EQUIP_SLOT_EYE_ACCESSORY:
    case EQUIP_SLOT_EARRINGS:
    case EQUIP_SLOT_RING:
    case EQUIP_SLOT_RING_2:
    case EQUIP_SLOT_RING_3:
    case EQUIP_SLOT_RING_4:
    case EQUIP_SLOT_PENDANT:
    case EQUIP_SLOT_PENDANT_2:
      return PotentialGroup::kAccessory;
    default:
      // The projectile, the six symbols, the badge, the medal and the pocket.
      return PotentialGroup::kNone;
  }
}

bool SlotTakesPotential(EquipSlot slot) {
  return PotentialGroupOf(slot) != PotentialGroup::kNone;
}

PotentialRank NextPotentialRank(PotentialRank rank) {
  if (rank >= POTENTIAL_RANK_LEGENDARY) {
    return POTENTIAL_RANK_LEGENDARY;
  }
  return static_cast<PotentialRank>(rank + 1);
}

PotentialRank PreviousPotentialRank(PotentialRank rank) {
  if (rank <= POTENTIAL_RANK_RARE) {
    return POTENTIAL_RANK_RARE;
  }
  return static_cast<PotentialRank>(rank - 1);
}

double PotentialRankUpChance(CubeType cube, PotentialRank rank) {
  if (rank < POTENTIAL_RANK_RARE || rank > POTENTIAL_RANK_LEGENDARY) {
    return 0.0;
  }
  switch (cube) {
    case CubeType::kRed:
      return kRedRankUp[RankIndex(rank)];
  }
  return 0.0;
}

double PotentialPrimeChance(CubeType cube, int index) {
  if (index < 0 || index >= kPotentialLines) {
    return 0.0;
  }
  switch (cube) {
    case CubeType::kRed:
      return kRedPrime[index];
  }
  return 0.0;
}

int PotentialLineValue(PotentialLineType type, PotentialRank rank,
                       int item_level) {
  const LineSpec* spec = SpecFor(type);
  if (spec == nullptr || rank < spec->min_rank || rank > spec->max_rank) {
    return 0;
  }
  const int index = RankIndex(rank);
  switch (type) {
    case POTENTIAL_LINE_TYPE_STR:
    case POTENTIAL_LINE_TYPE_DEX:
    case POTENTIAL_LINE_TYPE_INT:
    case POTENTIAL_LINE_TYPE_LUK:
      return BandValue(kFlatStatBands, std::size(kFlatStatBands), item_level,
                       index);
    case POTENTIAL_LINE_TYPE_ALL_STATS:
      return BandValue(kFlatAllStatsBands, std::size(kFlatAllStatsBands),
                       item_level, index);
    case POTENTIAL_LINE_TYPE_MAX_HP:
      return BandValue(kFlatMaxHpBands, std::size(kFlatMaxHpBands), item_level,
                       index);
    case POTENTIAL_LINE_TYPE_STR_PCT:
    case POTENTIAL_LINE_TYPE_DEX_PCT:
    case POTENTIAL_LINE_TYPE_INT_PCT:
    case POTENTIAL_LINE_TYPE_LUK_PCT:
    case POTENTIAL_LINE_TYPE_MAX_HP_PCT:
    case POTENTIAL_LINE_TYPE_ATTACK_PCT:
    case POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT:
    case POTENTIAL_LINE_TYPE_DAMAGE_PCT:
      return BandValue(kPercentBands, std::size(kPercentBands), item_level,
                       index);
    // Worth a rank less than a single stat's share, which is what pays for it
    // covering all four.
    case POTENTIAL_LINE_TYPE_ALL_STATS_PCT:
      return BandValue(kPercentBands, std::size(kPercentBands), item_level,
                       RankIndex(PreviousPotentialRank(rank)));
    case POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT:
      return BandValue(kCritDamageBands, std::size(kCritDamageBands),
                       item_level, index);
    case POTENTIAL_LINE_TYPE_MESO_RATE:
    case POTENTIAL_LINE_TYPE_ITEM_DROP_RATE:
      return BandValue(kRewardRateBands, std::size(kRewardRateBands),
                       item_level, index);
    // The lines GMS states outright rather than per level band. Their size is
    // in their name, since two of them share a pool.
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15:
      return 15;
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30:
      return 30;
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_35:
      return 35;
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40:
      return 40;
    case POTENTIAL_LINE_TYPE_COOLDOWN_1:
      return 1;
    case POTENTIAL_LINE_TYPE_COOLDOWN_2:
      return 2;
    case POTENTIAL_LINE_TYPE_UNSPECIFIED:
      return 0;
  }
  return 0;
}

std::vector<PotentialLineType> PotentialPool(PotentialGroup group,
                                             PotentialRank rank) {
  std::vector<PotentialLineType> pool;
  const int bit = GroupBitOf(group);
  if (bit == 0) {
    return pool;
  }
  for (const LineSpec& spec : kLines) {
    if ((spec.groups & bit) == 0 || rank < spec.min_rank ||
        rank > spec.max_rank) {
      continue;
    }
    pool.push_back(spec.type);
  }
  return pool;
}

Potential RollPotential(CubeType cube, PotentialGroup group, PotentialRank rank,
                        std::mt19937& rng) {
  Potential potential;
  potential.set_rank(rank);
  for (int i = 0; i < kPotentialLines; ++i) {
    std::bernoulli_distribution prime(PotentialPrimeChance(cube, i));
    const PotentialRank line_rank =
        prime(rng) ? rank : PreviousPotentialRank(rank);
    const std::vector<PotentialLineType> pool = PotentialPool(group, line_rank);
    // Never empty: the four %stat lines roll for every group, at every rank,
    // on an item of any level.
    CHECK(!pool.empty());
    std::uniform_int_distribution<int> pick(0, pool.size() - 1);
    PotentialLine* line = potential.add_lines();
    line->set_type(pool[pick(rng)]);
    line->set_rank(line_rank);
  }
  return potential;
}

void AddPotential(const Potential& potential, int item_level,
                  PotentialTotals& totals) {
  static_assert(PotentialLineType_ARRAYSIZE == 28,
                "a new potential line needs somewhere to land");
  for (const PotentialLine& line : potential.lines()) {
    const int value = PotentialLineValue(line.type(), line.rank(), item_level);
    if (value == 0) {
      continue;
    }
    const double share = value / 100.0;
    EquipStats& flat = totals.flat;
    switch (line.type()) {
      case POTENTIAL_LINE_TYPE_STR:
        flat.set_str(flat.str() + value);
        break;
      case POTENTIAL_LINE_TYPE_DEX:
        flat.set_dex(flat.dex() + value);
        break;
      case POTENTIAL_LINE_TYPE_INT:
        flat.set_int_(flat.int_() + value);
        break;
      case POTENTIAL_LINE_TYPE_LUK:
        flat.set_luk(flat.luk() + value);
        break;
      case POTENTIAL_LINE_TYPE_ALL_STATS:
        flat.set_str(flat.str() + value);
        flat.set_dex(flat.dex() + value);
        flat.set_int_(flat.int_() + value);
        flat.set_luk(flat.luk() + value);
        break;
      case POTENTIAL_LINE_TYPE_MAX_HP:
        flat.set_max_hp(flat.max_hp() + value);
        break;
      case POTENTIAL_LINE_TYPE_STR_PCT:
        totals.str_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_DEX_PCT:
        totals.dex_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_INT_PCT:
        totals.int_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_LUK_PCT:
        totals.luk_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_ALL_STATS_PCT:
        totals.str_pct += share;
        totals.dex_pct += share;
        totals.int_pct += share;
        totals.luk_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_MAX_HP_PCT:
        totals.max_hp_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_ATTACK_PCT:
        totals.attack_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT:
        totals.magic_attack_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_DAMAGE_PCT:
        totals.damage_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15:
      case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30:
      case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35:
      case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40:
        totals.ied = 1.0 - (1.0 - totals.ied) * (1.0 - share);
        break;
      case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30:
      case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_35:
      case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40:
        totals.boss_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT:
        totals.crit_dmg += share;
        break;
      case POTENTIAL_LINE_TYPE_MESO_RATE:
        totals.meso_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_ITEM_DROP_RATE:
        totals.item_drop_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_COOLDOWN_1:
      case POTENTIAL_LINE_TYPE_COOLDOWN_2:
        totals.cooldown_seconds += value;
        break;
      default:
        break;
    }
  }
}

const Cube& CubeOf(CubeType type) {
  for (const Cube& cube : kCubes) {
    if (cube.type == type) {
      return cube;
    }
  }
  LOG(FATAL) << "Cube " << static_cast<int>(type) << " is not on the shelf";
}

Potential CubePotential(const Potential& current, CubeType cube,
                        PotentialGroup group, std::mt19937& rng) {
  // The first cube into an item always hands over a Rare potential. GMS sells
  // a scroll for that step and rolls the rank with it; here the cube does it,
  // and only what it finds decides whether a rank is rolled at all.
  if (current.rank() == POTENTIAL_RANK_UNSPECIFIED) {
    return RollPotential(cube, group, POTENTIAL_RANK_RARE, rng);
  }
  PotentialRank rank = current.rank();
  std::bernoulli_distribution ranks_up(PotentialRankUpChance(cube, rank));
  if (ranks_up(rng)) {
    rank = NextPotentialRank(rank);
  }
  return RollPotential(cube, group, rank, rng);
}

}  // namespace ms
