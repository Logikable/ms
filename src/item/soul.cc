#include "src/item/soul.h"

#include <algorithm>
#include <map>
#include <random>
#include <string>

#include "absl/log/log.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// One tier's line values, from the wiki's <Boss>'s_Soul_Shard pages. ATT and
// MATT share a value.
struct TierValues {
  SoulTier tier;
  int attack;
  int all_stats;
  int max_hp;
  int crit_rate;
  int ignore_defense;
  int boss_damage;
};

constexpr TierValues kTiers[] = {
    {SOUL_TIER_C, 6, 12, 1100, 6, 3, 3},  {SOUL_TIER_B, 7, 15, 1200, 7, 4, 4},
    {SOUL_TIER_A, 8, 17, 1300, 8, 4, 4},  {SOUL_TIER_S, 10, 20, 1500, 10, 5, 5},
    {SOUL_TIER_SS, 3, 5, 2000, 12, 7, 7},
};

constexpr SoulLine kLines[] = {
    SOUL_LINE_ATTACK,      SOUL_LINE_MAGIC_ATTACK, SOUL_LINE_ALL_STATS,
    SOUL_LINE_MAX_HP,      SOUL_LINE_CRIT_RATE,    SOUL_LINE_IGNORE_DEFENSE,
    SOUL_LINE_BOSS_DAMAGE,
};

const TierValues& ValuesOf(SoulTier tier) {
  for (const TierValues& values : kTiers) {
    if (values.tier == tier) {
      return values;
    }
  }
  LOG(FATAL) << "Soul tier " << static_cast<int>(tier) << " has no values";
}

}  // namespace

bool TakesSoul(const EquipPrototype& proto) {
  return proto.equip_slot() == EQUIP_SLOT_PRIMARY_WEAPON;
}

int SoulLineValue(SoulTier tier, SoulLine line) {
  const TierValues& values = ValuesOf(tier);
  switch (line) {
    case SOUL_LINE_ATTACK:
    case SOUL_LINE_MAGIC_ATTACK:
      return values.attack;
    case SOUL_LINE_ALL_STATS:
      return values.all_stats;
    case SOUL_LINE_MAX_HP:
      return values.max_hp;
    case SOUL_LINE_CRIT_RATE:
      return values.crit_rate;
    case SOUL_LINE_IGNORE_DEFENSE:
      return values.ignore_defense;
    case SOUL_LINE_BOSS_DAMAGE:
      return values.boss_damage;
    default:
      return 0;
  }
}

bool SoulLineIsPercent(SoulTier tier, SoulLine line) {
  switch (line) {
    case SOUL_LINE_CRIT_RATE:
    case SOUL_LINE_IGNORE_DEFENSE:
    case SOUL_LINE_BOSS_DAMAGE:
      return true;
    case SOUL_LINE_ATTACK:
    case SOUL_LINE_MAGIC_ATTACK:
    case SOUL_LINE_ALL_STATS:
      return tier == SOUL_TIER_SS;
    default:
      return false;
  }
}

Soul RollSoul(const ItemPrototype& shard, std::mt19937& rng) {
  std::uniform_int_distribution<int> pick(0, std::size(kLines) - 1);
  Soul soul;
  soul.set_boss(shard.short_name());
  soul.set_tier(shard.soul_tier());
  soul.set_line(kLines[pick(rng)]);
  return soul;
}

EquipStats SoulStats(const Soul& soul) {
  EquipStats stats;
  if (soul.line() == SOUL_LINE_UNSPECIFIED) {
    return stats;
  }
  stats.set_attack(kSoulGaugeAttack);
  stats.set_magic_attack(kSoulGaugeAttack);
  if (SoulLineIsPercent(soul.tier(), soul.line())) {
    return stats;
  }
  const int value = SoulLineValue(soul.tier(), soul.line());
  switch (soul.line()) {
    case SOUL_LINE_ATTACK:
      stats.set_attack(stats.attack() + value);
      break;
    case SOUL_LINE_MAGIC_ATTACK:
      stats.set_magic_attack(stats.magic_attack() + value);
      break;
    case SOUL_LINE_ALL_STATS:
      stats.set_str(value);
      stats.set_dex(value);
      stats.set_int_(value);
      stats.set_luk(value);
      break;
    case SOUL_LINE_MAX_HP:
      stats.set_max_hp(value);
      break;
    default:
      break;
  }
  return stats;
}

void AddSoul(const Soul& soul, PotentialTotals& totals) {
  if (soul.line() == SOUL_LINE_UNSPECIFIED ||
      !SoulLineIsPercent(soul.tier(), soul.line())) {
    return;
  }
  const double share = SoulLineValue(soul.tier(), soul.line()) / 100.0;
  switch (soul.line()) {
    case SOUL_LINE_ATTACK:
      totals.attack_pct += share;
      break;
    case SOUL_LINE_MAGIC_ATTACK:
      totals.magic_attack_pct += share;
      break;
    case SOUL_LINE_ALL_STATS:
      totals.str_pct += share;
      totals.dex_pct += share;
      totals.int_pct += share;
      totals.luk_pct += share;
      break;
    case SOUL_LINE_CRIT_RATE:
      totals.crit_rate += share;
      break;
    case SOUL_LINE_IGNORE_DEFENSE:
      totals.ied = 1.0 - (1.0 - totals.ied) * (1.0 - share);
      break;
    case SOUL_LINE_BOSS_DAMAGE:
      totals.boss_pct += share;
      break;
    default:
      break;
  }
}

void FillShardLevels(const std::map<std::string, Boss>& bosses,
                     std::map<std::string, ItemPrototype>& items) {
  for (const std::pair<const std::string, Boss>& entry : bosses) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      for (const MobDrop& drop : difficulty.drops()) {
        auto found = items.find(drop.item());
        if (found == items.end() ||
            found->second.kind() != ITEM_KIND_SOUL_SHARD) {
          continue;
        }
        ItemPrototype& shard = found->second;
        if (shard.currency_level() == 0 ||
            difficulty.unlock_level() < shard.currency_level()) {
          shard.set_currency_level(difficulty.unlock_level());
        }
      }
    }
  }
}

}  // namespace ms
