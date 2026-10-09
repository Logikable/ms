#include "src/character/legion_plan.h"

#include "src/character/legion.h"
#include "src/protos/legion.pb.h"

namespace ms {

int SpendLegion(CharacterInstance& character, StatPreset slot,
                const LegionRate& rate) {
  if (!character.legion_unlocked()) {
    return 0;
  }
  const LegionSummary summary = character.legion_summary();
  Legion legion = character.legion();
  ResetLegionPreset(legion, slot);
  character.set_legion(legion);
  int spent = 0;
  while (spent < summary.points) {
    const double before = rate(character);
    LegionStat best = LEGION_STAT_UNSPECIFIED;
    double best_gain = 0.0;
    for (int i = LegionStat_MIN + 1; i <= LegionStat_MAX; ++i) {
      const LegionStat stat = static_cast<LegionStat>(i);
      Legion trial = legion;
      if (SpendLegionPoints(trial, slot, stat, 1, summary) == 0) {
        continue;
      }
      character.set_legion(trial);
      const double gain = rate(character) - before;
      if (best == LEGION_STAT_UNSPECIFIED || gain > best_gain) {
        best = stat;
        best_gain = gain;
      }
    }
    if (best == LEGION_STAT_UNSPECIFIED) {
      break;
    }
    SpendLegionPoints(legion, slot, best, 1, summary);
    character.set_legion(legion);
    ++spent;
  }
  character.set_legion(legion);
  return spent;
}

}  // namespace ms
