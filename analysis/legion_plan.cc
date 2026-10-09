#include "analysis/legion_plan.h"

#include <algorithm>
#include <vector>

#include "src/character/character.h"
#include "src/character/legion.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/legion.pb.h"

namespace ms {

LegionWorth MeasureLegionWorth(GameState& state, StatPreset preset,
                               const std::function<double(GameState&)>& rate) {
  LegionWorth worth;
  CharacterInstance& character = state.character;
  if (!character.legion_unlocked()) {
    return worth;
  }
  // ToProto, not proto(): the live containers hold the items.
  const Character before = character.ToProto();
  const Legion kept = character.legion();
  const LegionSummary summary = character.legion_summary();
  Legion empty = kept;
  ResetLegionPreset(empty, preset);
  character.set_legion(empty);
  const double base = rate(state);
  for (int i = LegionStat_MIN + 1; i <= LegionStat_MAX; ++i) {
    const LegionStat stat = static_cast<LegionStat>(i);
    Legion trial = empty;
    const int points =
        SpendLegionPoints(trial, preset, stat, summary.points, summary);
    if (points <= 0) {
      continue;
    }
    character.set_legion(trial);
    worth.per_point[i] = (rate(state) - base) / points;
    character.RestoreFrom(before, state.equips, state.items);
  }
  character.RestoreFrom(before, state.equips, state.items);
  character.set_legion(kept);
  return worth;
}

int SpendLegionByWorth(CharacterInstance& character, StatPreset preset,
                       const LegionWorth& worth) {
  if (!character.legion_unlocked()) {
    return 0;
  }
  const LegionSummary summary = character.legion_summary();
  std::vector<LegionStat> order;
  for (int i = LegionStat_MIN + 1; i <= LegionStat_MAX; ++i) {
    order.push_back(static_cast<LegionStat>(i));
  }
  std::stable_sort(order.begin(), order.end(),
                   [&worth](LegionStat a, LegionStat b) {
                     return worth.per_point[a] > worth.per_point[b];
                   });
  Legion legion = character.legion();
  ResetLegionPreset(legion, preset);
  int spent = 0;
  for (LegionStat stat : order) {
    spent += SpendLegionPoints(legion, preset, stat, summary.points, summary);
  }
  character.set_legion(legion);
  return spent;
}

}  // namespace ms
