#include "src/character/hexa_stat.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <set>

#include "src/character/hyper_stats.h"
#include "src/character/sol_erda.h"
#include "src/combat/damage.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// The wiki's HEXA Matrix table, levels 1 to 10, primary then additional. The
// percentages are in hundredths of a percent so Critical Damage's 0.35% steps
// stay whole.
struct Ladder {
  int primary[kHexaStatMaxLevel];
  int additional[kHexaStatMaxLevel];
};

constexpr Ladder kCritDamage = {
    {35, 70, 105, 140, 210, 280, 350, 455, 560, 700},
    {35, 70, 105, 140, 175, 210, 245, 280, 315, 350}};
constexpr Ladder kBossOrIed = {
    {100, 200, 300, 400, 600, 800, 1000, 1300, 1600, 2000},
    {100, 200, 300, 400, 500, 600, 700, 800, 900, 1000}};
constexpr Ladder kDamage = {{75, 150, 225, 300, 450, 600, 750, 975, 1200, 1500},
                            {75, 150, 225, 300, 375, 450, 525, 600, 675, 750}};
// Whole points, not hundredths.
constexpr Ladder kAttack = {{5, 10, 15, 20, 30, 40, 50, 65, 80, 100},
                            {5, 10, 15, 20, 25, 30, 35, 40, 45, 50}};
constexpr Ladder kMainStat = {
    {100, 200, 300, 400, 600, 800, 1000, 1300, 1600, 2000},
    {100, 200, 300, 400, 500, 600, 700, 800, 900, 1000}};

const Ladder* LadderFor(HexaStat stat) {
  switch (stat) {
    case HEXA_STAT_CRIT_DAMAGE:
      return &kCritDamage;
    case HEXA_STAT_BOSS_DAMAGE:
    case HEXA_STAT_IGNORE_DEFENSE:
      return &kBossOrIed;
    case HEXA_STAT_DAMAGE:
      return &kDamage;
    case HEXA_STAT_ATTACK:
      return &kAttack;
    case HEXA_STAT_MAIN_STAT:
      return &kMainStat;
    default:
      return nullptr;
  }
}

bool IsWhole(HexaStat stat) {
  return stat == HEXA_STAT_ATTACK || stat == HEXA_STAT_MAIN_STAT;
}

void Add(HexaStat stat, int level, bool primary, HexaStatTotals& totals) {
  const double value = HexaStatValue(stat, level, primary);
  switch (stat) {
    case HEXA_STAT_CRIT_DAMAGE:
      totals.crit_dmg += value;
      break;
    case HEXA_STAT_BOSS_DAMAGE:
      totals.boss_pct += value;
      break;
    case HEXA_STAT_IGNORE_DEFENSE:
      totals.ied = CombineIgnoredDefense(totals.ied, value);
      break;
    case HEXA_STAT_DAMAGE:
      totals.damage_pct += value;
      break;
    case HEXA_STAT_ATTACK:
      totals.attack += static_cast<int>(value);
      break;
    case HEXA_STAT_MAIN_STAT:
      totals.main_stat += static_cast<int>(value);
      break;
    default:
      break;
  }
}

constexpr HexaStatPrice kOpenPrices[kHexaStatNodes] = {
    {5 * kEnergyPerSolErda, 10},
    {10 * kEnergyPerSolErda, 200},
    {15 * kEnergyPerSolErda, 350}};

// Indexed by the primary's level, 0 to 10.
constexpr int64_t kEnhancePrices[kHexaStatMaxLevel + 1] = {
    10, 10, 10, 20, 20, 20, 20, 30, 40, 50, 50};
constexpr double kPrimaryChances[kHexaStatMaxLevel + 1] = {
    0.35, 0.35, 0.35, 0.20, 0.20, 0.20, 0.20, 0.15, 0.10, 0.05, 0.0};

}  // namespace

int HexaStatNodesOpen(int level) {
  int open = 0;
  for (int unlock : kHexaStatNodeUnlockLevels) {
    open += level >= unlock ? 1 : 0;
  }
  return open;
}

double HexaStatValue(HexaStat stat, int level, bool primary) {
  const Ladder* ladder = LadderFor(stat);
  if (ladder == nullptr || level <= 0) {
    return 0.0;
  }
  const int index = std::min(level, kHexaStatMaxLevel) - 1;
  const int raw = primary ? ladder->primary[index] : ladder->additional[index];
  return IsWhole(stat) ? raw : raw / 10000.0;
}

bool HexaStatNodeValid(const HexaStatNode& node) {
  const std::set<HexaStat> distinct = {node.primary(), node.first(),
                                       node.second()};
  if (distinct.size() != 3 || distinct.count(HEXA_STAT_UNSPECIFIED) > 0) {
    return false;
  }
  for (int level :
       {node.primary_level(), node.first_level(), node.second_level()}) {
    if (level < 0 || level > kHexaStatMaxLevel) {
      return false;
    }
  }
  return node.primary_level() + node.first_level() + node.second_level() <=
         kHexaStatNodeLevels;
}

bool HexaStatsAllowed(const HexaStats& stats) {
  if (stats.nodes_size() > kHexaStatNodes) {
    return false;
  }
  std::set<HexaStat> primaries;
  std::map<HexaStat, int> additional;
  for (const HexaStatNode& node : stats.nodes()) {
    if (!HexaStatNodeValid(node) || !primaries.insert(node.primary()).second) {
      return false;
    }
    if (++additional[node.first()] > 2 || ++additional[node.second()] > 2) {
      return false;
    }
  }
  return true;
}

HexaStatTotals HexaStatTotalsFor(const HexaStats& stats, int level) {
  HexaStatTotals totals;
  const int open = std::min(HexaStatNodesOpen(level), stats.nodes_size());
  for (int i = 0; i < open; ++i) {
    const HexaStatNode& node = stats.nodes(i);
    if (!HexaStatNodeValid(node)) {
      continue;
    }
    Add(node.primary(), node.primary_level(), /*primary=*/true, totals);
    Add(node.first(), node.first_level(), /*primary=*/false, totals);
    Add(node.second(), node.second_level(), /*primary=*/false, totals);
  }
  return totals;
}

HexaStatPrice HexaStatOpenPrice(int index) {
  if (index < 0 || index >= kHexaStatNodes) {
    return {};
  }
  return kOpenPrices[index];
}

bool HexaStatNextNodeOpens(const HexaStats& stats, int level, int job_stage) {
  const int index = stats.nodes_size();
  if (job_stage < kSixthJobStage || index >= kHexaStatNodes ||
      level < kHexaStatNodeUnlockLevels[index]) {
    return false;
  }
  return index == 0 ||
         HexaStatNodeLevels(stats.nodes(index - 1)) >= kHexaStatNodeLevels;
}

int HexaStatNodeLevels(const HexaStatNode& node) {
  return node.primary_level() + node.first_level() + node.second_level();
}

int64_t HexaStatEnhancePrice(const HexaStatNode& node) {
  if (HexaStatNodeLevels(node) >= kHexaStatNodeLevels) {
    return 0;
  }
  return kEnhancePrices[std::clamp(node.primary_level(), 0, kHexaStatMaxLevel)];
}

double HexaStatPrimaryChance(int primary_level) {
  return kPrimaryChances[std::clamp(primary_level, 0, kHexaStatMaxLevel)];
}

bool RollHexaStatLevel(HexaStatNode& node, std::mt19937& rng) {
  if (HexaStatNodeLevels(node) >= kHexaStatNodeLevels) {
    return false;
  }
  std::uniform_real_distribution<double> roll(0.0, 1.0);
  if (roll(rng) < HexaStatPrimaryChance(node.primary_level())) {
    node.set_primary_level(node.primary_level() + 1);
    return true;
  }
  // Short of 20 levels, at least one additional stat has room.
  const bool first_room = node.first_level() < kHexaStatMaxLevel;
  const bool second_room = node.second_level() < kHexaStatMaxLevel;
  const bool first = first_room && (!second_room || roll(rng) < 0.5);
  if (first) {
    node.set_first_level(node.first_level() + 1);
  } else {
    node.set_second_level(node.second_level() + 1);
  }
  return true;
}

}  // namespace ms
