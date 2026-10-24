#include "src/character/legion_artifact.h"

#include <algorithm>
#include <set>

#include "src/combat/damage.h"
#include "src/protos/character.pb.h"
#include "src/protos/legion.pb.h"

namespace ms {

int ArtifactStatLevel(const LegionArtifact& artifact, ArtifactStat stat) {
  int level = 0;
  for (const ArtifactCrystal& crystal : artifact.crystals()) {
    for (int held : crystal.stats()) {
      if (held == stat) {
        level += std::clamp(crystal.level(), 0, kArtifactCrystalMaxLevel);
      }
    }
  }
  return std::min(level, kArtifactStatMaxLevel);
}

bool ArtifactAllowed(const LegionArtifact& artifact) {
  if (artifact.crystals_size() > kArtifactCrystals) {
    return false;
  }
  for (const ArtifactCrystal& crystal : artifact.crystals()) {
    const std::set<int> distinct(crystal.stats().begin(),
                                 crystal.stats().end());
    if (crystal.level() < 1 || crystal.level() > kArtifactCrystalMaxLevel ||
        crystal.stats_size() != kArtifactStatsPerCrystal ||
        distinct.size() != kArtifactStatsPerCrystal ||
        distinct.count(ARTIFACT_STAT_UNSPECIFIED) > 0) {
      return false;
    }
  }
  return true;
}

LegionExtraTotals ArtifactTotalsFor(const LegionArtifact& artifact) {
  auto level = [&artifact](ArtifactStat stat) {
    return ArtifactStatLevel(artifact, stat);
  };
  // EXP, meso and drop rate pay a level plus a point every fifth level.
  auto stepped = [](int x) { return (x + x / 5) / 100.0; };
  LegionExtraTotals totals;
  totals.all_stats = 15 * level(ARTIFACT_STAT_ALL_STATS);
  totals.max_hp = 750 * level(ARTIFACT_STAT_MAX_HP_MP);
  totals.max_mp = totals.max_hp;
  totals.attack = 3 * level(ARTIFACT_STAT_ATTACK);
  totals.damage_pct = 0.015 * level(ARTIFACT_STAT_DAMAGE);
  totals.boss_pct = 0.015 * level(ARTIFACT_STAT_BOSS_DAMAGE);
  totals.ied = 0.02 * level(ARTIFACT_STAT_IGNORE_DEFENSE);
  totals.buff_duration_pct = 0.02 * level(ARTIFACT_STAT_BUFF_DURATION);
  totals.meso_pct = stepped(level(ARTIFACT_STAT_MESO));
  totals.item_drop_pct = stepped(level(ARTIFACT_STAT_ITEM_DROP));
  totals.crit_rate = 0.02 * level(ARTIFACT_STAT_CRIT_RATE);
  totals.crit_dmg = 0.004 * level(ARTIFACT_STAT_CRIT_DAMAGE);
  totals.exp_pct = stepped(level(ARTIFACT_STAT_EXP));
  return totals;
}

LegionExtraTotals ChampionTotalsFor(
    const google::protobuf::RepeatedPtrField<LegionChampion>& champions,
    Job job) {
  LegionExtraTotals totals;
  const int registered = std::min(champions.size(), kChampionSlots);
  bool champion = false;
  int at_least[CHAMPION_RANK_SSS + 1] = {};
  for (int i = 0; i < registered; ++i) {
    const LegionChampion& entry = champions[i];
    champion = champion || entry.job() == job;
    for (int rank = CHAMPION_RANK_B; rank <= entry.rank(); ++rank) {
      ++at_least[rank];
    }
  }
  if (!champion) {
    return totals;
  }
  totals.all_stats = 20 * at_least[CHAMPION_RANK_B];
  totals.max_hp = 1000 * at_least[CHAMPION_RANK_B];
  totals.max_mp = totals.max_hp;
  totals.attack = 10 * at_least[CHAMPION_RANK_A];
  totals.boss_pct = 0.05 * at_least[CHAMPION_RANK_S];
  totals.crit_dmg = 0.03 * at_least[CHAMPION_RANK_SS];
  totals.ied = 0.05 * at_least[CHAMPION_RANK_SSS];
  return totals;
}

}  // namespace ms
