#include "src/item/flame.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

bool IsWeapon(const EquipPrototype& proto) {
  return proto.equip_slot() == EQUIP_SLOT_PRIMARY_WEAPON;
}

// The wiki's level bands (maplestorywiki Bonus_Stats/Stat_Tables). Each
// formula stops climbing at a band GMS states outright rather than following
// its own slope.
int SingleStatStep(int level) {
  if (level >= 230) {
    return 12;
  }
  return std::min(level / 20 + 1, 11);
}

int PairStatStep(int level) {
  if (level >= 250) {
    return 7;
  }
  return std::min(level / 40 + 1, 6);
}

int MaxHpStep(int level) {
  if (level < 10) {
    return 3;
  }
  if (level < 200) {
    return level / 10 * 30;
  }
  return 600 + std::min((level - 200) / 10, 5) * 20;
}

// A flame-advantaged weapon's ATT or MATT, as a share of `base`: the band times
// the tier, compounding 10% a tier from tier 3.
int WeaponAttack(int base, int level, int tier) {
  double percent = PairStatStep(level) * tier * std::pow(1.1, tier - 3);
  // Rounded up, as GMS does. The epsilon keeps an exact product such as 18% of
  // 100 from rounding up to 19 on a floating-point crumb.
  return static_cast<int>(std::ceil(base * percent / 100.0 - 1e-9));
}

struct PairFields {
  int (EquipStats::*get)() const;
  void (EquipStats::*set)(int32_t);
};

// The fields a stat line raises; one for a single stat, two for a pair.
std::vector<PairFields> StatFields(FlameStat stat) {
  const PairFields str = {&EquipStats::str, &EquipStats::set_str};
  const PairFields dex = {&EquipStats::dex, &EquipStats::set_dex};
  const PairFields int_ = {&EquipStats::int_, &EquipStats::set_int_};
  const PairFields luk = {&EquipStats::luk, &EquipStats::set_luk};
  switch (stat) {
    case FLAME_STAT_STR:
      return {str};
    case FLAME_STAT_DEX:
      return {dex};
    case FLAME_STAT_INT:
      return {int_};
    case FLAME_STAT_LUK:
      return {luk};
    case FLAME_STAT_STR_DEX:
      return {str, dex};
    case FLAME_STAT_STR_INT:
      return {str, int_};
    case FLAME_STAT_STR_LUK:
      return {str, luk};
    case FLAME_STAT_DEX_INT:
      return {dex, int_};
    case FLAME_STAT_DEX_LUK:
      return {dex, luk};
    case FLAME_STAT_INT_LUK:
      return {int_, luk};
    case FLAME_STAT_MAX_HP:
      return {{&EquipStats::max_hp, &EquipStats::set_max_hp}};
    case FLAME_STAT_ATTACK:
      return {{&EquipStats::attack, &EquipStats::set_attack}};
    case FLAME_STAT_MAGIC_ATTACK:
      return {{&EquipStats::magic_attack, &EquipStats::set_magic_attack}};
    case FLAME_STAT_BOSS_DAMAGE:
      return {{&EquipStats::boss_damage, &EquipStats::set_boss_damage}};
    default:
      return {};
  }
}

// Two rolls are the same if they have the same lines at the same tiers, in
// any order.
bool SameLines(const FlameLines& a, const FlameLines& b) {
  auto key = [](const FlameLines& lines) {
    std::vector<std::pair<int, int>> sorted;
    for (const FlameLine& line : lines) {
      sorted.emplace_back(line.stat(), line.tier());
    }
    std::sort(sorted.begin(), sorted.end());
    return sorted;
  };
  return key(a) == key(b);
}

}  // namespace

const Flame& FlameOf(FlameType type) {
  for (const Flame& flame : kFlames) {
    if (flame.type == type) {
      return flame;
    }
  }
  LOG(FATAL) << "Flame " << static_cast<int>(type) << " isn't on the shelf";
}

bool SlotTakesFlame(EquipSlot slot) {
  switch (slot) {
    case EQUIP_SLOT_PRIMARY_WEAPON:
    case EQUIP_SLOT_HAT:
    case EQUIP_SLOT_TOP:
    case EQUIP_SLOT_BOTTOM:
    case EQUIP_SLOT_CAPE:
    case EQUIP_SLOT_GLOVES:
    case EQUIP_SLOT_SHOES:
    case EQUIP_SLOT_BELT:
    case EQUIP_SLOT_FACE_ACCESSORY:
    case EQUIP_SLOT_EYE_ACCESSORY:
    case EQUIP_SLOT_EARRINGS:
    case EQUIP_SLOT_PENDANT:
    case EQUIP_SLOT_PENDANT_2:
    case EQUIP_SLOT_POCKET:
      return true;
    default:
      return false;
  }
}

std::vector<FlameStat> FlamePool(const EquipPrototype& proto) {
  const int level = proto.required_level();
  std::vector<FlameStat> pool = {
      FLAME_STAT_STR,     FLAME_STAT_DEX,     FLAME_STAT_INT,
      FLAME_STAT_LUK,     FLAME_STAT_STR_DEX, FLAME_STAT_STR_INT,
      FLAME_STAT_STR_LUK, FLAME_STAT_DEX_INT, FLAME_STAT_DEX_LUK,
      FLAME_STAT_INT_LUK, FLAME_STAT_MAX_HP,
  };
  if (IsWeapon(proto)) {
    pool.push_back(FLAME_STAT_ATTACK);
    pool.push_back(FLAME_STAT_MAGIC_ATTACK);
    pool.push_back(FLAME_STAT_ALL_STAT);
    if (level >= 90) {
      pool.push_back(FLAME_STAT_BOSS_DAMAGE);
    }
    pool.push_back(FLAME_STAT_DAMAGE);
    return pool;
  }
  if (level >= 60) {
    pool.push_back(FLAME_STAT_ATTACK);
    pool.push_back(FLAME_STAT_MAGIC_ATTACK);
  }
  if (level >= 70) {
    pool.push_back(FLAME_STAT_ALL_STAT);
  }
  return pool;
}

int FlameLineValue(const FlameLine& line, const EquipPrototype& proto) {
  const int level = proto.required_level();
  const int tier = line.tier();
  switch (line.stat()) {
    case FLAME_STAT_STR:
    case FLAME_STAT_DEX:
    case FLAME_STAT_INT:
    case FLAME_STAT_LUK:
      return SingleStatStep(level) * tier;
    case FLAME_STAT_STR_DEX:
    case FLAME_STAT_STR_INT:
    case FLAME_STAT_STR_LUK:
    case FLAME_STAT_DEX_INT:
    case FLAME_STAT_DEX_LUK:
    case FLAME_STAT_INT_LUK:
      return PairStatStep(level) * tier;
    case FLAME_STAT_MAX_HP:
      return MaxHpStep(level) * tier;
    case FLAME_STAT_ATTACK:
      return IsWeapon(proto)
                 ? WeaponAttack(proto.base_stats().attack(), level, tier)
                 : tier;
    case FLAME_STAT_MAGIC_ATTACK:
      return IsWeapon(proto)
                 ? WeaponAttack(proto.base_stats().magic_attack(), level, tier)
                 : tier;
    case FLAME_STAT_ALL_STAT:
    case FLAME_STAT_DAMAGE:
      return tier;
    case FLAME_STAT_BOSS_DAMAGE:
      return 2 * tier;
    default:
      return 0;
  }
}

EquipStats FlameStats(const FlameLines& lines, const EquipPrototype& proto) {
  EquipStats stats;
  for (const FlameLine& line : lines) {
    const int value = FlameLineValue(line, proto);
    for (const PairFields& field : StatFields(line.stat())) {
      (stats.*field.set)((stats.*field.get)() + value);
    }
  }
  return stats;
}

FlamePercents FlamePercentsOf(const FlameLines& lines,
                              const EquipPrototype& proto) {
  FlamePercents percents;
  for (const FlameLine& line : lines) {
    if (line.stat() == FLAME_STAT_ALL_STAT) {
      percents.all_stat += FlameLineValue(line, proto);
    } else if (line.stat() == FLAME_STAT_DAMAGE) {
      percents.damage += FlameLineValue(line, proto);
    }
  }
  return percents;
}

FlameLines RollFlame(FlameType flame, const EquipPrototype& proto,
                     const FlameLines& current, std::mt19937& rng) {
  const Flame& shelf = FlameOf(flame);
  std::vector<FlameStat> pool = FlamePool(proto);
  std::uniform_int_distribution<int> tier(shelf.min_tier,
                                          shelf.min_tier + kFlameTiers - 1);
  FlameLines lines;
  do {
    lines.Clear();
    std::shuffle(pool.begin(), pool.end(), rng);
    for (int i = 0; i < kFlameLines; ++i) {
      FlameLine* line = lines.Add();
      line->set_stat(pool[i]);
      line->set_tier(tier(rng));
    }
  } while (SameLines(lines, current));
  return lines;
}

std::string FlameName(FlameType type) {
  switch (type) {
    case FlameType::kBurning:
      return "Burning Rebirth Flame";
    case FlameType::kBlack:
      return "Black Rebirth Flame";
  }
  return "";
}

std::string FlameStatName(FlameStat stat) {
  switch (stat) {
    case FLAME_STAT_STR:
      return "STR";
    case FLAME_STAT_DEX:
      return "DEX";
    case FLAME_STAT_INT:
      return "INT";
    case FLAME_STAT_LUK:
      return "LUK";
    case FLAME_STAT_STR_DEX:
      return "STR & DEX";
    case FLAME_STAT_STR_INT:
      return "STR & INT";
    case FLAME_STAT_STR_LUK:
      return "STR & LUK";
    case FLAME_STAT_DEX_INT:
      return "DEX & INT";
    case FLAME_STAT_DEX_LUK:
      return "DEX & LUK";
    case FLAME_STAT_INT_LUK:
      return "INT & LUK";
    case FLAME_STAT_MAX_HP:
      return "Max HP";
    case FLAME_STAT_ATTACK:
      return "ATT";
    case FLAME_STAT_MAGIC_ATTACK:
      return "MATT";
    case FLAME_STAT_ALL_STAT:
      return "All Stats";
    case FLAME_STAT_BOSS_DAMAGE:
      return "Boss Damage";
    case FLAME_STAT_DAMAGE:
      return "Damage";
    default:
      return "";
  }
}

std::string FlameLineValueText(const FlameLine& line,
                               const EquipPrototype& proto) {
  const std::string value = "+" + std::to_string(FlameLineValue(line, proto));
  switch (line.stat()) {
    case FLAME_STAT_ALL_STAT:
    case FLAME_STAT_BOSS_DAMAGE:
    case FLAME_STAT_DAMAGE:
      return value + "%";
    default:
      return value;
  }
}

}  // namespace ms
