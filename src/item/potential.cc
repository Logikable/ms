#include "src/item/potential.h"

#include <array>
#include <iterator>
#include <random>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/types/span.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// The groups a line can roll on, as a bit set. Hat, gloves, armour and
// accessory differ only for the four lines specific to one of them, so most
// rows use kNonWeapon or kEveryGroup.
enum GroupBit {
  kWeaponryBit = 1 << 0,
  kHatBit = 1 << 1,
  kGlovesBit = 1 << 2,
  kArmorBit = 1 << 3,
  kAccessoryBit = 1 << 4,
};

constexpr int kNonWeapon = kHatBit | kGlovesBit | kArmorBit | kAccessoryBit;
constexpr int kEveryGroup = kWeaponryBit | kNonWeapon;

// One line the game offers: which groups roll it and at which ranks. GMS also
// puts an equipment level on its strongest lines; that's dropped, since a level
// 30 item never reaches those ranks anyway.
struct LineSpec {
  PotentialLineType type;
  int groups;
  PotentialRank min_rank;
  PotentialRank max_rank;
};

constexpr LineSpec kMainLines[] = {
    // Flat stats, Rare only. GMS dropped them from Epic up, which is much of
    // what a rank-up gains.
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
    // Off weaponry, as GMS's client marks it at every rank but Epic; one
    // universal rank would be the odd one out.
    {POTENTIAL_LINE_TYPE_MAX_HP_PCT, kNonWeapon, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_ALL_STATS_PCT, kEveryGroup, POTENTIAL_RANK_EPIC,
     POTENTIAL_RANK_LEGENDARY},

    // The weapon lines. Secondaries and emblems roll them too; see
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

    // The four lines each unique to one group.
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

// Bonus potential: GMS's own pools, less its flat stat, flat Max HP and flat
// attack lines on every group and its per-9-levels lines on weaponry. A line
// listed here and above must roll at the same ranks on both tracks, since
// PotentialLineValue doesn't know which track it's on.
constexpr LineSpec kBonusLines[] = {
    {POTENTIAL_LINE_TYPE_STR_PCT, kWeaponryBit, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_DEX_PCT, kWeaponryBit, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_INT_PCT, kWeaponryBit, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_LUK_PCT, kWeaponryBit, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_ALL_STATS_PCT, kWeaponryBit, POTENTIAL_RANK_EPIC,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_ATTACK_PCT, kWeaponryBit, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT, kWeaponryBit, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_DAMAGE_PCT, kWeaponryBit, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_BONUS_BOSS_DAMAGE, kWeaponryBit, POTENTIAL_RANK_UNIQUE,
     POTENTIAL_RANK_LEGENDARY},

    {POTENTIAL_LINE_TYPE_BONUS_STR_PCT, kNonWeapon, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_BONUS_DEX_PCT, kNonWeapon, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_BONUS_INT_PCT, kNonWeapon, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_BONUS_LUK_PCT, kNonWeapon, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_BONUS_MAX_HP_PCT, kNonWeapon, POTENTIAL_RANK_RARE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_BONUS_ALL_STATS_PCT, kNonWeapon, POTENTIAL_RANK_EPIC,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_STR_PER_9_LEVELS, kNonWeapon, POTENTIAL_RANK_UNIQUE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_DEX_PER_9_LEVELS, kNonWeapon, POTENTIAL_RANK_UNIQUE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_INT_PER_9_LEVELS, kNonWeapon, POTENTIAL_RANK_UNIQUE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_LUK_PER_9_LEVELS, kNonWeapon, POTENTIAL_RANK_UNIQUE,
     POTENTIAL_RANK_LEGENDARY},
    {POTENTIAL_LINE_TYPE_COOLDOWN_1, kHatBit, POTENTIAL_RANK_LEGENDARY,
     POTENTIAL_RANK_LEGENDARY},
};

// One band of equipment level and a line's value within it. A table's last row
// covers every level above it.
struct Band {
  int max_level;
  int value[4];
};

constexpr int kNoCeiling = 100000;

// The percentage lines, by rank. One table covers %STR, %DEX, %INT, %LUK, %HP,
// %ATT, %MATT and %Damage, since GMS gives them all the same values.
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

// Flat Max HP: ten per ten equipment levels, levelling off past 110.
constexpr Band kFlatMaxHpBands[] = {
    {10, {10, 0, 0, 0}},          {20, {20, 0, 0, 0}},   {30, {30, 0, 0, 0}},
    {40, {40, 0, 0, 0}},          {50, {50, 0, 0, 0}},   {60, {60, 0, 0, 0}},
    {70, {70, 0, 0, 0}},          {80, {80, 0, 0, 0}},   {90, {90, 0, 0, 0}},
    {100, {100, 0, 0, 0}},        {110, {110, 0, 0, 0}}, {150, {120, 0, 0, 0}},
    {kNoCeiling, {125, 0, 0, 0}},
};

// Gloves' critical damage, which nothing below level 50 rolls.
constexpr Band kCritDamageBands[] = {
    {60, {0, 0, 0, 5}},
    {80, {0, 0, 0, 6}},
    {kNoCeiling, {0, 0, 0, 8}},
};

// An accessory's %meso and %drop, which are Legendary only.
constexpr Band kRewardRateBands[] = {
    {30, {0, 0, 0, 10}},
    {70, {0, 0, 0, 15}},
    {kNoCeiling, {0, 0, 0, 20}},
};

// Bonus potential's %STR, %DEX, %INT and %LUK off weaponry, from GMS's
// ItemOption.img.
constexpr Band kBonusStatBands[] = {
    {20, {1, 1, 2, 3}},  {50, {1, 2, 3, 4}},  {90, {1, 3, 4, 5}},
    {150, {2, 4, 6, 8}}, {200, {3, 5, 6, 8}}, {kNoCeiling, {3, 5, 7, 9}},
};

// Not a rank below the single stats, as the main track's is.
constexpr Band kBonusAllStatsBands[] = {
    {20, {0, 1, 1, 2}},  {50, {0, 1, 2, 3}},  {90, {0, 1, 3, 4}},
    {150, {0, 2, 5, 6}}, {200, {0, 3, 5, 6}}, {kNoCeiling, {0, 3, 6, 7}},
};

constexpr Band kBonusMaxHpBands[] = {
    {20, {1, 1, 2, 3}},   {50, {1, 2, 3, 5}},   {90, {1, 3, 5, 7}},
    {150, {2, 5, 8, 11}}, {200, {3, 6, 8, 11}}, {kNoCeiling, {3, 6, 9, 12}},
};

constexpr Band kBonusBossBands[] = {
    {200, {0, 0, 12, 18}},
    {kNoCeiling, {0, 0, 14, 20}},
};

// What the main track's ignored-defence and boss lines gain on an item above
// level 200.
constexpr int kAbove200Step = 5;

int Above200Step(int item_level) {
  return item_level > 200 ? kAbove200Step : 0;
}

// Chance a cube raises a potential's rank, by its current rank. The green cube
// shares them: GMS has never published its bonus cube's odds, and no
// community sample of them was found.
constexpr double kRedRankUp[4] = {1.0 / 7.0, 0.06, 0.024, 0.0};

// Chance the 2nd and 3rd lines are prime. Shared with the green cube too.
constexpr double kRedPrime[kPotentialLines] = {1.0, 0.10, 0.01};

// The black cube's, from the GMS community sample StrategyWiki's Potential
// System page tabulates. The white cube shares them, as green shares red's.
constexpr double kBlackRankUp[4] = {0.16, 0.11, 0.047, 0.0};
constexpr double kBlackPrime[kPotentialLines] = {1.0, 0.20, 0.05};

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

absl::Span<const LineSpec> LinesOf(PotentialTrack track) {
  switch (track) {
    case PotentialTrack::kMain:
      return kMainLines;
    case PotentialTrack::kBonus:
      return kBonusLines;
  }
  return {};
}

const LineSpec* SpecFor(PotentialLineType type) {
  for (PotentialTrack track : {PotentialTrack::kMain, PotentialTrack::kBonus}) {
    for (const LineSpec& spec : LinesOf(track)) {
      if (spec.type == type) {
        return &spec;
      }
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
      // The projectile, the symbols, the badge, the medal, the pocket and the
      // totems.
      return PotentialGroup::kNone;
  }
}

const Potential& PotentialOf(const Equip& equip, PotentialTrack track) {
  return track == PotentialTrack::kBonus ? equip.bonus_potential()
                                         : equip.main_potential();
}

Potential* MutablePotentialOf(Equip& equip, PotentialTrack track) {
  return track == PotentialTrack::kBonus ? equip.mutable_bonus_potential()
                                         : equip.mutable_main_potential();
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
    case CubeType::kGreen:
      return kRedRankUp[RankIndex(rank)];
    case CubeType::kBlack:
    case CubeType::kWhite:
      return kBlackRankUp[RankIndex(rank)];
  }
  return 0.0;
}

double PotentialPrimeChance(CubeType cube, int index) {
  if (index < 0 || index >= kPotentialLines) {
    return 0.0;
  }
  switch (cube) {
    case CubeType::kRed:
    case CubeType::kGreen:
      return kRedPrime[index];
    case CubeType::kBlack:
    case CubeType::kWhite:
      return kBlackPrime[index];
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
    // Worth one rank less than a single stat's share, to balance covering all
    // four.
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
    // Several sizes share a pool, so each is its own type, named for its value
    // up to item level 200. GMS adds kAbove200Step past that.
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15:
      return 15 + Above200Step(item_level);
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30:
      return 30 + Above200Step(item_level);
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_35:
      return 35 + Above200Step(item_level);
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40:
      return 40 + Above200Step(item_level);
    case POTENTIAL_LINE_TYPE_COOLDOWN_1:
      return 1;
    case POTENTIAL_LINE_TYPE_COOLDOWN_2:
      return 2;
    case POTENTIAL_LINE_TYPE_BONUS_STR_PCT:
    case POTENTIAL_LINE_TYPE_BONUS_DEX_PCT:
    case POTENTIAL_LINE_TYPE_BONUS_INT_PCT:
    case POTENTIAL_LINE_TYPE_BONUS_LUK_PCT:
      return BandValue(kBonusStatBands, std::size(kBonusStatBands), item_level,
                       index);
    case POTENTIAL_LINE_TYPE_BONUS_ALL_STATS_PCT:
      return BandValue(kBonusAllStatsBands, std::size(kBonusAllStatsBands),
                       item_level, index);
    case POTENTIAL_LINE_TYPE_BONUS_MAX_HP_PCT:
      return BandValue(kBonusMaxHpBands, std::size(kBonusMaxHpBands),
                       item_level, index);
    case POTENTIAL_LINE_TYPE_BONUS_BOSS_DAMAGE:
      return BandValue(kBonusBossBands, std::size(kBonusBossBands), item_level,
                       index);
    // One point per step at Unique, two at Legendary, at any item level.
    case POTENTIAL_LINE_TYPE_STR_PER_9_LEVELS:
    case POTENTIAL_LINE_TYPE_DEX_PER_9_LEVELS:
    case POTENTIAL_LINE_TYPE_INT_PER_9_LEVELS:
    case POTENTIAL_LINE_TYPE_LUK_PER_9_LEVELS:
      return rank == POTENTIAL_RANK_LEGENDARY ? 2 : 1;
    case POTENTIAL_LINE_TYPE_UNSPECIFIED:
      return 0;
  }
  return 0;
}

std::vector<PotentialLineType> PotentialPool(PotentialTrack track,
                                             PotentialGroup group,
                                             PotentialRank rank) {
  std::vector<PotentialLineType> pool;
  const int bit = GroupBitOf(group);
  if (bit == 0) {
    return pool;
  }
  for (const LineSpec& spec : LinesOf(track)) {
    if ((spec.groups & bit) == 0 || rank < spec.min_rank ||
        rank > spec.max_rank) {
      continue;
    }
    pool.push_back(spec.type);
  }
  return pool;
}

namespace {

// Every pool, built once: a roll draws three lines, and the sims roll millions.
const std::vector<PotentialLineType>& CachedPool(PotentialTrack track,
                                                 PotentialGroup group,
                                                 PotentialRank rank) {
  constexpr int kTracks = 2;
  constexpr int kGroups = static_cast<int>(PotentialGroup::kAccessory) + 1;
  using Table = std::array<std::array<std::array<std::vector<PotentialLineType>,
                                                 PotentialRank_ARRAYSIZE>,
                                      kGroups>,
                           kTracks>;
  static const Table* const table = [] {
    auto* built = new Table;
    for (int t = 0; t < kTracks; ++t) {
      for (int g = 0; g < kGroups; ++g) {
        for (int r = 0; r < PotentialRank_ARRAYSIZE; ++r) {
          (*built)[t][g][r] = PotentialPool(static_cast<PotentialTrack>(t),
                                            static_cast<PotentialGroup>(g),
                                            static_cast<PotentialRank>(r));
        }
      }
    }
    return built;
  }();
  return (*table)[static_cast<int>(track)][static_cast<int>(group)][rank];
}

}  // namespace

Potential RollPotential(CubeType cube, PotentialGroup group, PotentialRank rank,
                        std::mt19937& rng) {
  Potential potential;
  potential.set_rank(rank);
  for (int i = 0; i < kPotentialLines; ++i) {
    std::bernoulli_distribution prime(PotentialPrimeChance(cube, i));
    const PotentialRank line_rank =
        prime(rng) ? rank : PreviousPotentialRank(rank);
    const std::vector<PotentialLineType>& pool =
        CachedPool(CubeOf(cube).track, group, line_rank);
    // Never empty: four %stat lines roll for every group, at every rank, on
    // both tracks.
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
  static_assert(PotentialLineType_ARRAYSIZE == 39,
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
      case POTENTIAL_LINE_TYPE_BONUS_STR_PCT:
        totals.str_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_DEX_PCT:
      case POTENTIAL_LINE_TYPE_BONUS_DEX_PCT:
        totals.dex_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_INT_PCT:
      case POTENTIAL_LINE_TYPE_BONUS_INT_PCT:
        totals.int_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_LUK_PCT:
      case POTENTIAL_LINE_TYPE_BONUS_LUK_PCT:
        totals.luk_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_ALL_STATS_PCT:
      case POTENTIAL_LINE_TYPE_BONUS_ALL_STATS_PCT:
        totals.str_pct += share;
        totals.dex_pct += share;
        totals.int_pct += share;
        totals.luk_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_MAX_HP_PCT:
      case POTENTIAL_LINE_TYPE_BONUS_MAX_HP_PCT:
        totals.max_hp_pct += share;
        break;
      case POTENTIAL_LINE_TYPE_STR_PER_9_LEVELS:
        totals.per_9_levels.set_str(totals.per_9_levels.str() + value);
        break;
      case POTENTIAL_LINE_TYPE_DEX_PER_9_LEVELS:
        totals.per_9_levels.set_dex(totals.per_9_levels.dex() + value);
        break;
      case POTENTIAL_LINE_TYPE_INT_PER_9_LEVELS:
        totals.per_9_levels.set_int_(totals.per_9_levels.int_() + value);
        break;
      case POTENTIAL_LINE_TYPE_LUK_PER_9_LEVELS:
        totals.per_9_levels.set_luk(totals.per_9_levels.luk() + value);
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
      case POTENTIAL_LINE_TYPE_BONUS_BOSS_DAMAGE:
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
  // The first cube on an item always gives a Rare potential. GMS sells a
  // separate scroll for that step and rolls the rank with it; here the cube
  // does it, and only an existing potential gets a rank roll.
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
