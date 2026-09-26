#include "src/character/symbol.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// A Sacred Symbol's level-1 stat, and what each level after adds.
constexpr int kSacredBaseStat = 500;
constexpr int kSacredStatPerLevel = 200;

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

bool IsSymbol(const EquipPrototype& proto) {
  return proto.symbol_case() != EquipPrototype::SYMBOL_NOT_SET;
}

bool IsArcaneSymbol(const EquipPrototype& proto) {
  return proto.has_arcane_symbol();
}

bool IsSacredSymbol(const EquipPrototype& proto) {
  return proto.has_sacred_symbol();
}

int SymbolMaxLevel(const EquipPrototype& proto) {
  switch (proto.symbol_case()) {
    case EquipPrototype::kArcaneSymbol:
      return kMaxArcaneSymbolLevel;
    case EquipPrototype::kSacredSymbol:
      return kMaxSacredSymbolLevel;
    case EquipPrototype::SYMBOL_NOT_SET:
      return 0;
  }
  return 0;
}

int SymbolLevel(const Equip& item) {
  return std::max(1, item.symbol_level());
}

bool SymbolMaxed(const EquipPrototype& proto, const Equip& item) {
  return IsSymbol(proto) && SymbolLevel(item) >= SymbolMaxLevel(proto);
}

int SymbolExpToNextLevel(const EquipPrototype& proto, int level) {
  if (level >= SymbolMaxLevel(proto)) {
    return 0;
  }
  if (IsSacredSymbol(proto)) {
    return 9 * level * level + 20 * level;
  }
  return level * level + 11;
}

int64_t SymbolLevelUpCost(const EquipPrototype& proto, int level) {
  int64_t duplicates = SymbolExpToNextLevel(proto, level);
  if (duplicates == 0) {
    return 0;
  }
  // Both multipliers are kept in tenths so the floor lands where the math
  // says, not where rounding a tenth puts it.
  if (IsSacredSymbol(proto)) {
    int64_t tenths =
        std::llround(10 * proto.sacred_symbol().meso_cost_base()) - 6 * level;
    return 100000LL * (tenths * duplicates / 10);
  }
  int64_t tenths = 10LL * proto.arcane_symbol().meso_cost_base() + level;
  return 10000LL * (tenths * duplicates / 10);
}

int SymbolForce(const EquipPrototype& proto, int level) {
  if (IsSacredSymbol(proto)) {
    return 10 * level;
  }
  return 10 * level + 20;
}

int SymbolWorth(const EquipPrototype& proto, const Equip& item) {
  int worth = 1 + item.symbol_exp();
  for (int level = 1; level < SymbolLevel(item); ++level) {
    worth += SymbolExpToNextLevel(proto, level);
  }
  return worth;
}

bool SymbolCanLevelUp(const EquipPrototype& proto, const Equip& item) {
  int needed = SymbolExpToNextLevel(proto, SymbolLevel(item));
  return needed > 0 && item.symbol_exp() >= needed;
}

void LevelUpSymbol(const EquipPrototype& proto, Equip& item) {
  if (!SymbolCanLevelUp(proto, item)) {
    return;
  }
  int level = SymbolLevel(item);
  item.set_symbol_exp(item.symbol_exp() - SymbolExpToNextLevel(proto, level));
  item.set_symbol_level(level + 1);
}

EquipStats SymbolStatsFor(const EquipPrototype& proto, StatField primary,
                          int level) {
  EquipStats stats;
  if (IsSacredSymbol(proto)) {
    SetStat(stats, primary,
            kSacredBaseStat + kSacredStatPerLevel * (level - 1));
  } else {
    SetStat(stats, primary, 10 * SymbolForce(proto, level));
  }
  return stats;
}

}  // namespace ms
