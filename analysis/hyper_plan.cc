#include "analysis/hyper_plan.h"

#include "src/character/character.h"
#include "src/character/hyper_plan.h"
#include "src/character/stat_preset.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"

namespace ms {

HyperWorth MeasureHyperWorth(GameState& state, StatPreset preset,
                             const MeasuredHyperRate& rate) {
  // ToProto, not proto(): the live containers hold the character's items, and
  // the backing message they were taken out of has none of them. See
  // MeasureAbilityWorth, which was written the other way round once.
  Character before = state.character.ToProto();
  HyperWorth worth = MeasureHyperWorth(
      state.character, preset,
      [&state, &rate](CharacterInstance&) { return rate(state); });
  state.character.RestoreFrom(before, state.equips, state.items);
  return worth;
}

int SpendHyperStats(GameState& state, StatPreset preset,
                    const HyperWorth& worth) {
  return SpendHyperStats(state.character, preset, worth);
}

}  // namespace ms
