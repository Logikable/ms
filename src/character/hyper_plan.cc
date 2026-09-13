#include "src/character/hyper_plan.h"

#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// Every stat the pool can be spent on, which is every field the enum names
// bar the placeholder. Walked as ints because the enum has a hole in it.
bool IsHyperStatField(int field) {
  return field != HYPER_STAT_FIELD_UNSPECIFIED && HyperStatField_IsValid(field);
}

// Puts `field` alone at `level` and asks what the character is worth.
double RateWith(CharacterInstance& character, StatPreset preset,
                HyperStatField field, int level, const HyperRate& rate) {
  character.ResetHyperStats(preset);
  if (level > 0 && !character.AllocateHyperStat(field, preset, level)) {
    return 0.0;  // the pool cannot reach it yet
  }
  return rate(character);
}

}  // namespace

HyperWorth MeasureHyperWorth(CharacterInstance& character, StatPreset preset,
                             const HyperRate& rate) {
  HyperWorth worth;
  double bare =
      RateWith(character, preset, HYPER_STAT_FIELD_UNSPECIFIED, 0, rate);
  int ceiling = character.max_hyper_stat_level();
  for (int field = 0; field < HyperStatField_ARRAYSIZE; ++field) {
    if (!IsHyperStatField(field) ||
        !HyperStatUnlocked(static_cast<HyperStatField>(field),
                           character.proto().level())) {
      continue;
    }
    for (int level = 1; level <= ceiling; ++level) {
      double paid = RateWith(character, preset,
                             static_cast<HyperStatField>(field), level, rate);
      if (paid <= 0.0) {
        break;  // out of reach, and every level above it is too
      }
      worth.rate[field][level] = paid - bare;
    }
  }
  character.ResetHyperStats(preset);
  return worth;
}

int SpendHyperStats(CharacterInstance& character, StatPreset preset,
                    const HyperWorth& worth) {
  character.ResetHyperStats(preset);
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
      int at = character.hyper_stat_level(named, preset);
      if (at >= character.max_hyper_stat_level()) {
        continue;
      }
      double gain = worth.rate[field][at + 1] - worth.rate[field][at];
      int cost = HyperStatLevelCost(at + 1);
      if (gain <= 0.0 || cost <= 0 ||
          cost > character.hyper_stat_points_left(preset)) {
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
        !character.AllocateHyperStat(best, preset, 1)) {
      return spent;
    }
    spent += best_cost;
  }
}

}  // namespace ms
