#include "analysis/ability_plan.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "src/character/character.h"
#include "src/character/inner_ability.h"
#include "src/character/stat_preset.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// The line type worth the most at `rank`: the one line the chase aims for. Only
// the top slot rolls at the ability's own rank, so everything else is filler.
AbilityLineType BestTypeAt(const AbilityWorth& worth, AbilityRank rank) {
  AbilityLineType best = ABILITY_LINE_TYPE_UNSPECIFIED;
  double most = 0.0;
  for (int type = 1; type < AbilityLineType_ARRAYSIZE; ++type) {
    double value = worth.rate[type][rank];
    if (value > most) {
      most = value;
      best = static_cast<AbilityLineType>(type);
    }
  }
  return best;
}

// Whether the top line is the target type, at the preset's own rank.
bool GoalLanded(const AbilityPreset& preset, const AbilityWorth& worth) {
  return preset.lines_size() > 0 && preset.lines(0).rank() == preset.rank() &&
         preset.lines(0).type() == BestTypeAt(worth, preset.rank());
}

// Slots worth locking through a reroll, best first. A line worth nothing is
// never locked, since its slot is better spent rolling for something useful.
std::vector<int> BestSlots(const AbilityPreset& preset,
                           const AbilityWorth& worth) {
  std::vector<std::pair<double, int>> ranked;
  for (int i = 0; i < preset.lines_size(); ++i) {
    double value = worth.Of(preset.lines(i));
    if (value > 0.0) {
      ranked.push_back({-value, i});  // negated so sorting puts the best first
    }
  }
  std::sort(ranked.begin(), ranked.end());
  std::vector<int> slots;
  for (int i = 0;
       i < static_cast<int>(ranked.size()) && i < kMaxLockedAbilityLines; ++i) {
    slots.push_back(ranked[i].second);
  }
  return slots;
}

// Whether this preset is finished: rank reached, target line on top, and no
// dead weight below it. The last check stops a finished preset from being
// rerolled away.
bool Settled(const AbilityPreset& preset, AbilityRank climb_to,
             const AbilityWorth& worth) {
  if (preset.rank() < climb_to || !GoalLanded(preset, worth)) {
    return false;
  }
  for (int i = 1; i < preset.lines_size(); ++i) {
    if (worth.Of(preset.lines(i)) <= 0.0) {
      return false;
    }
  }
  return true;
}

// Sets which lines to lock for the next reroll. Unlocks every line first, since
// a third lock is refused.
//
// Locks nothing while climbing ranks, or until the target line is on top: a
// locked top line is never rerolled, so locking the wrong one ends the chase.
void HoldForChase(CharacterInstance& character, StatPreset preset,
                  AbilityRank climb_to, const AbilityWorth& worth) {
  const AbilityPreset lines = character.ability(preset);
  for (int i = 0; i < lines.lines_size(); ++i) {
    character.LockAbilityLine(i, false, preset);
  }
  if (lines.rank() < climb_to || !GoalLanded(lines, worth)) {
    return;
  }
  // The target is on top, and a locked top line at the ability's rank is never
  // rerolled. Lock the best filler with it and let the last slot keep rolling.
  for (int slot : BestSlots(lines, worth)) {
    character.LockAbilityLine(slot, true, preset);
  }
}

}  // namespace

AbilityWorth MeasureAbilityWorth(GameState& state, StatPreset preset,
                                 const AbilityRate& rate) {
  // Use ToProto, not proto(). The live containers hold the character's items,
  // and the backing message has none of them.
  const Character before = state.character.ToProto();
  Character trial = before;
  AbilityPreset& setup = PresetOf(*trial.mutable_inner_ability(), preset);

  // Baseline with no lines, which every line below is measured against.
  setup.Clear();
  state.character.RestoreFrom(trial, state.equips, state.items);
  const double bare = rate(state);

  AbilityWorth worth;
  for (int t = ABILITY_LINE_TYPE_STR; t < AbilityLineType_ARRAYSIZE; ++t) {
    for (int r = ABILITY_RANK_RARE; r <= ABILITY_RANK_LEGENDARY; ++r) {
      const AbilityLineType type = static_cast<AbilityLineType>(t);
      const AbilityRank rank = static_cast<AbilityRank>(r);
      if (AbilityTypeWeight(type, rank) <= 0) {
        continue;  // a combination the roll never produces
      }
      setup.Clear();
      setup.set_rank(rank);  // the top line always has the ability's rank
      AbilityLine& line = *setup.add_lines();
      line.set_type(type);
      line.set_rank(rank);
      state.character.RestoreFrom(trial, state.equips, state.items);
      worth.rate[t][r] = rate(state) - bare;
    }
  }
  state.character.RestoreFrom(before, state.equips, state.items);
  return worth;
}

int64_t SpendHonorOnAbility(GameState& state, AbilityRank climb_to,
                            StatPreset preset, const AbilityWorth& worth) {
  int64_t spent = 0;
  while (!Settled(state.character.ability(preset), climb_to, worth)) {
    HoldForChase(state.character, preset, climb_to, worth);
    const int64_t cost = state.character.ability_reset_cost(preset);
    if (!state.character.ResetAbility(preset)) {
      return spent;  // not enough honor, or the panel isn't unlocked yet
    }
    spent += cost;
  }
  return spent;
}

}  // namespace ms
