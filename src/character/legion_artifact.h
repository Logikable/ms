/* The Legion Artifact and the Legion Champions, the two Legion systems GMS
 * added after the grid. Both are account-wide and kept on the Legion proto.
 *
 * The Artifact holds nine crystals of up to level 5, each raising three stats
 * by its level. A stat's level is the sum over the crystals holding it, up to
 * 10, and pays the client's per-level formula (Skill 80003516-80003531).
 *
 * A Champion is a character registered by job, ranked C to SSS. Every
 * Champion's rank pays every registered Champion an insignia: B and up all
 * stats and HP, A attack, S boss damage, SS critical damage, SSS ignored
 * defense (Skill 80003819-80003823, a level per Champion holding the rank).
 */
#ifndef MS_SRC_CHARACTER_LEGION_ARTIFACT_H_
#define MS_SRC_CHARACTER_LEGION_ARTIFACT_H_

#include "google/protobuf/repeated_ptr_field.h"
#include "src/protos/character.pb.h"
#include "src/protos/legion.pb.h"

namespace ms {

inline constexpr int kArtifactCrystals = 9;
inline constexpr int kArtifactCrystalMaxLevel = 5;
inline constexpr int kArtifactStatsPerCrystal = 3;
inline constexpr int kArtifactStatMaxLevel = 10;
inline constexpr int kChampionSlots = 4;

// The level `stat` has reached across the crystals, capped at 10.
int ArtifactStatLevel(const LegionArtifact& artifact, ArtifactStat stat);

// Whether `artifact` is one GMS allows: at most nine crystals, each of level 1
// to 5 with three different stats.
bool ArtifactAllowed(const LegionArtifact& artifact);

// What the stats this game reads add. Cooldown skip, summon duration, Final
// Attack damage and status resistance have nothing to land on yet and pay
// nothing.
struct LegionExtraTotals {
  int all_stats = 0;
  int max_hp = 0;
  int max_mp = 0;
  int attack = 0;
  double damage_pct = 0.0;
  double boss_pct = 0.0;
  double ied = 0.0;
  double buff_duration_pct = 0.0;
  double meso_pct = 0.0;
  double item_drop_pct = 0.0;
  double crit_rate = 0.0;
  double crit_dmg = 0.0;
  double exp_pct = 0.0;
};
LegionExtraTotals ArtifactTotalsFor(const LegionArtifact& artifact);

// What the insignias pay a character of `job`: nothing unless it is one of the
// first kChampionSlots Champions.
LegionExtraTotals ChampionTotalsFor(
    const google::protobuf::RepeatedPtrField<LegionChampion>& champions,
    Job job);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_LEGION_ARTIFACT_H_
