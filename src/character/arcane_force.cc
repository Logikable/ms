#include "src/character/arcane_force.h"

#include <algorithm>
#include <cstdint>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// One row of GMS's table: the percent of the requirement where it starts, and
// the factors from there up to the next row.
struct ForceRow {
  int met_pct;
  double dealt;
  double taken;
};

// Read from the bottom up, so a character with nothing lands on the first row
// and one with 1.5x the requirement on the last. The top row's 0 taken is GMS's
// "monster hits for 1", which the damage floor provides.
constexpr ForceRow kForceTable[] = {
    {0, 0.10, 2.8},   {10, 0.30, 2.4},  {30, 0.60, 1.8},
    {50, 0.70, 1.6},  {70, 0.80, 1.4},  {100, 1.00, 1.0},
    {110, 1.10, 0.8}, {130, 1.30, 0.4}, {150, 1.50, 0.0},
};

// Sets the one stat a symbol grants in `stats`. Which stat depends on who wears
// it.
void SetStat(EquipStats& stats, StatField primary, int amount) {
  switch (primary) {
    case STAT_FIELD_STR:
      stats.set_str(amount);
      return;
    case STAT_FIELD_DEX:
      stats.set_dex(amount);
      return;
    case STAT_FIELD_INT:
      stats.set_int_(amount);
      return;
    case STAT_FIELD_LUK:
      stats.set_luk(amount);
      return;
    // Jobs whose damage is built on HP instead of a stat, and characters with
    // no job. Neither can get a symbol: the first has no primary stat to grant,
    // and the second can't reach level 200.
    case STAT_FIELD_HP:
    case STAT_FIELD_MP:
    case STAT_FIELD_UNSPECIFIED:
      return;
  }
}

}  // namespace

bool IsArcaneSymbol(const EquipPrototype& proto) {
  return proto.has_arcane_symbol();
}

int SymbolLevel(const Equip& item) {
  return std::max(1, item.symbol_level());
}

int SymbolExpToNextLevel(int level) {
  if (level >= kMaxSymbolLevel) {
    return 0;
  }
  return level * level + 11;
}

int64_t SymbolLevelUpCost(const EquipPrototype& proto, int level) {
  int duplicates = SymbolExpToNextLevel(level);
  if (duplicates == 0) {
    return 0;
  }
  // GMS writes the multiplier as base + 0.1 x level. Kept in tenths so the
  // floor lands where the math says, not where rounding a tenth puts it.
  int64_t tenths = 10LL * proto.arcane_symbol().meso_cost_base() + level;
  return 10000LL * (tenths * duplicates / 10);
}

int SymbolArcaneForce(int level) {
  return 10 * level + 20;
}

int SymbolWorth(const Equip& item) {
  int worth = 1 + item.symbol_exp();
  for (int level = 1; level < SymbolLevel(item); ++level) {
    worth += SymbolExpToNextLevel(level);
  }
  return worth;
}

bool SymbolCanLevelUp(const Equip& item) {
  int needed = SymbolExpToNextLevel(SymbolLevel(item));
  return needed > 0 && item.symbol_exp() >= needed;
}

void LevelUpSymbol(Equip& item) {
  if (!SymbolCanLevelUp(item)) {
    return;
  }
  int level = SymbolLevel(item);
  item.set_symbol_exp(item.symbol_exp() - SymbolExpToNextLevel(level));
  item.set_symbol_level(level + 1);
}

EquipStats SymbolStatsFor(StatField primary, int level) {
  EquipStats stats;
  SetStat(stats, primary, 10 * SymbolArcaneForce(level));
  return stats;
}

ForceFactors ArcaneFactorsFor(int owned, int required) {
  if (required <= 0) {
    return ForceFactors();
  }
  int met_pct = std::max(0, owned) * 100 / required;
  ForceFactors factors;
  for (const ForceRow& row : kForceTable) {
    if (met_pct >= row.met_pct) {
      factors.damage_dealt = row.dealt;
      factors.damage_taken = row.taken;
    }
  }
  return factors;
}

}  // namespace ms
