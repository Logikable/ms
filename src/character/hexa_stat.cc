#include "src/character/hexa_stat.h"

#include <algorithm>
#include <map>
#include <set>

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

}  // namespace ms
