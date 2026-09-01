#include "analysis/hyper_plan.h"

#include <utility>

#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/character/stat_preset.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// Every stat the pool can be spent on, which is every field the enum names
// bar the placeholder. Walked as ints because the enum has a hole in it.
bool IsHyperStatField(int field) {
  return field != HYPER_STAT_FIELD_UNSPECIFIED && HyperStatField_IsValid(field);
}

// Puts `field` alone at `level` and asks what the character takes off what is
// in front of them. Everything else is cleared: the table is what ONE stat
// pays, and two of them raised together would price each other's.
double RateWith(GameState& state, StatPreset preset, HyperStatField field,
                int level, const HyperRate& rate) {
  state.character.ResetHyperStats(preset);
  if (level > 0 && !state.character.AllocateHyperStat(field, preset, level)) {
    return 0.0;  // the pool cannot reach it yet
  }
  return rate(state);
}

}  // namespace

HyperWorth MeasureHyperWorth(GameState& state, StatPreset preset,
                             const HyperRate& rate) {
  HyperWorth worth;
  // ToProto, not proto(): the live containers hold the character's items, and
  // the backing message they were taken out of has none of them. See
  // MeasureAbilityWorth, which was written the other way round once.
  Character before = state.character.ToProto();
  double bare = RateWith(state, preset, HYPER_STAT_FIELD_UNSPECIFIED, 0, rate);
  int ceiling = state.character.max_hyper_stat_level();
  for (int field = 0; field < HyperStatField_ARRAYSIZE; ++field) {
    if (!IsHyperStatField(field) ||
        !HyperStatUnlocked(static_cast<HyperStatField>(field),
                           state.character.proto().level())) {
      continue;
    }
    for (int level = 1; level <= ceiling; ++level) {
      double paid = RateWith(state, preset, static_cast<HyperStatField>(field),
                             level, rate);
      if (paid <= 0.0) {
        break;  // out of reach, and every level above it is too
      }
      worth.rate[field][level] = paid - bare;
    }
  }
  state.character.RestoreFrom(before, state.equips, state.items);
  return worth;
}

int SpendHyperStats(GameState& state, StatPreset preset,
                    const HyperWorth& worth) {
  state.character.ResetHyperStats(preset);
  int spent = 0;
  while (true) {
    // The next level of each stat, priced against what it adds over the level
    // below it. Cross-multiplied rather than divided, so two a rounding apart
    // are still ordered by what they are worth.
    HyperStatField best = HYPER_STAT_FIELD_UNSPECIFIED;
    double best_gain = 0.0;
    int best_cost = 0;
    for (int field = 0; field < HyperStatField_ARRAYSIZE; ++field) {
      if (!IsHyperStatField(field)) {
        continue;
      }
      HyperStatField named = static_cast<HyperStatField>(field);
      int at = state.character.hyper_stat_level(named, preset);
      if (at >= state.character.max_hyper_stat_level()) {
        continue;
      }
      double gain = worth.rate[field][at + 1] - worth.rate[field][at];
      int cost = HyperStatLevelCost(at + 1);
      if (gain <= 0.0 || cost <= 0 ||
          cost > state.character.hyper_stat_points_left(preset)) {
        continue;
      }
      if (best == HYPER_STAT_FIELD_UNSPECIFIED ||
          gain * best_cost > best_gain * cost) {
        best = named;
        best_gain = gain;
        best_cost = cost;
      }
    }
    if (best == HYPER_STAT_FIELD_UNSPECIFIED ||
        !state.character.AllocateHyperStat(best, preset, 1)) {
      return spent;
    }
    spent += best_cost;
  }
}

}  // namespace ms
