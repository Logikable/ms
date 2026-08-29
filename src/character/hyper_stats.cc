#include "src/character/hyper_stats.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "src/protos/character.pb.h"

namespace ms {
namespace {

// Points to raise a stat one level, from level 1 up. The step doubles, then
// settles, then widens again at 11 -- which is what makes the last five
// levels the expensive half of the stat.
constexpr int kLevelCosts[] = {1,  2,  4,  8,  10, 15, 20, 25,
                               30, 35, 50, 65, 80, 95, 110};

// Levels the pool pays for at once. A level's points are floor(level / 10)
// less this, so the band a character is in is what decides the rate.
constexpr int kPointsPerLevelOffset = 11;
constexpr int kLevelsPerPointBand = 10;

// floor(log_base(value)), for value >= 1. GMS widens two of the ladders below
// once a stat passes a power of the base, which is what this counts.
int FloorLog(int base, int value) {
  int steps = 0;
  int reached = base;
  while (reached <= value) {
    reached *= base;
    ++steps;
  }
  return steps;
}

}  // namespace

const HyperStatPreset& PresetOf(const HyperStats& stats, HyperPreset preset) {
  if (preset == HyperPreset::kBossing) {
    return stats.bossing();
  }
  return stats.farming();
}

HyperStatPreset& PresetOf(HyperStats& stats, HyperPreset preset) {
  if (preset == HyperPreset::kBossing) {
    return *stats.mutable_bossing();
  }
  return *stats.mutable_farming();
}

int HyperStatPointsAtLevel(int level) {
  if (level < kHyperStatUnlockLevel) {
    return 0;
  }
  return level / kLevelsPerPointBand - kPointsPerLevelOffset;
}

int TotalHyperStatPoints(int level) {
  int total = 0;
  for (int at = kHyperStatUnlockLevel; at <= level; ++at) {
    total += HyperStatPointsAtLevel(at);
  }
  return total;
}

int HyperStatLevelCost(int level) {
  if (level < 1 || level > kMaxHyperStatLevel) {
    return 0;
  }
  return kLevelCosts[level - 1];
}

int HyperStatTotalCost(int level) {
  int total = 0;
  for (int at = 1; at <= level; ++at) {
    total += HyperStatLevelCost(at);
  }
  return total;
}

int MaxHyperStatLevel(int job_stage) {
  if (job_stage >= kFifthJobStage) {
    return kMaxHyperStatLevel;
  }
  return kMaxHyperStatLevel - kHyperStatLevelsBeforeFifthJob;
}

bool HyperStatUnlocked(HyperStatField field, int character_level) {
  if (character_level < kHyperStatUnlockLevel) {
    return false;
  }
  if (field == HYPER_STAT_FIELD_ARCANE_FORCE) {
    return character_level >= kArcaneForceHyperLevel;
  }
  return field != HYPER_STAT_FIELD_UNSPECIFIED;
}

double HyperStatBonus(HyperStatField field, int level) {
  static_assert(HyperStatField_ARRAYSIZE == 16,
                "a new Hyper Stat needs its own ladder here");
  if (level < 1) {
    return 0.0;
  }
  int rung = std::min(level, kMaxHyperStatLevel);
  double at = rung;
  switch (field) {
    case HYPER_STAT_FIELD_STR:
    case HYPER_STAT_FIELD_DEX:
    case HYPER_STAT_FIELD_INT:
    case HYPER_STAT_FIELD_LUK:
      return 30.0 * at;
    case HYPER_STAT_FIELD_MAX_HP:
    case HYPER_STAT_FIELD_MAX_MP:
      return 2.0 * at;
    // Critical Rate and the two damage ladders take a second helping of every
    // level past the fifth, which is GMS's log5 term.
    case HYPER_STAT_FIELD_CRIT_RATE:
      return at + FloorLog(5, rung) * (at - 5.0);
    case HYPER_STAT_FIELD_CRIT_DAMAGE:
      return at;
    case HYPER_STAT_FIELD_IED:
    case HYPER_STAT_FIELD_DAMAGE:
    case HYPER_STAT_FIELD_ATTACK:
      return 3.0 * at;
    case HYPER_STAT_FIELD_BOSS_DAMAGE:
    case HYPER_STAT_FIELD_NORMAL_DAMAGE:
      return 3.0 * at + FloorLog(5, rung) * (at - 5.0);
    // These two widen at the tenth level rather than the fifth.
    case HYPER_STAT_FIELD_EXP:
      return 0.5 * at + FloorLog(10, rung) * 0.5 * (at - 10.0);
    case HYPER_STAT_FIELD_ARCANE_FORCE:
      return 5.0 * at + FloorLog(10, rung) * 5.0 * (at - 10.0);
    default:
      return 0.0;
  }
}

int HyperStatLevel(const HyperStatPreset& preset, HyperStatField field) {
  google::protobuf::Map<int32_t, int32_t>::const_iterator it =
      preset.levels().find(field);
  if (it == preset.levels().end()) {
    return 0;
  }
  return it->second;
}

int HyperStatPointsSpent(const HyperStatPreset& preset) {
  int spent = 0;
  for (const std::pair<const int, int>& entry : preset.levels()) {
    spent += HyperStatTotalCost(entry.second);
  }
  return spent;
}

}  // namespace ms
