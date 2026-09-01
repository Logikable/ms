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

// The one line the chase is for: the type worth the most at `rank`.
//
// Only the top slot ever carries the ability's own rank -- lines two and three
// roll a rank below it -- so this is the single best line the preset will ever
// hold, and everything else on the sheet is filler.
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

// Whether the top line is the one being chased, at the rank that makes it
// worth chasing.
bool GoalLanded(const AbilityPreset& preset, const AbilityWorth& worth) {
  return preset.lines_size() > 0 && preset.lines(0).rank() == preset.rank() &&
         preset.lines(0).type() == BestTypeAt(worth, preset.rank());
}

// The slots worth holding through a reset, best first. A line worth nothing is
// never held: that slot is better spent rolling for one that is.
std::vector<int> BestSlots(const AbilityPreset& preset,
                           const AbilityWorth& worth) {
  std::vector<std::pair<double, int>> ranked;
  for (int i = 0; i < preset.lines_size(); ++i) {
    double value = worth.Of(preset.lines(i));
    if (value > 0.0) {
      ranked.push_back({-value, i});  // negated, so a sort puts the best first
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

// Whether this preset is done being rolled: the rank is climbed, the line it
// was chasing is on top, and nothing under it is dead weight.
//
// The last clause is what stops a finished preset being rolled to pieces. Two
// lines can be held and three are rolled, so one is always live -- and a pool
// spent to the last honor leaves whatever that final roll gave. A sheet with
// no dead line on it is where a player stops and puts the honor into the other
// preset.
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

// What to hold through the next reset. Frees every line first: a third lock is
// refused, so a swap made the other way round would keep the line it meant to
// drop.
//
// Nothing at all while the rank is still being climbed -- a lock buys nothing
// when what the character is short of is a rank, and it makes every roll of
// the ladder dearer. Nothing after that either, while the line being chased is
// not yet on top: a held top line is never rerolled, so holding the wrong one
// there strands the chase for good, and holding the fillers under it only
// raises the price of the roll that matters. Once it lands, it is held and the
// best filler with it.
void HoldForChase(CharacterInstance& character, StatPreset preset,
                  AbilityRank climb_to, const AbilityWorth& worth) {
  const AbilityPreset lines = character.ability(preset);
  for (int i = 0; i < lines.lines_size(); ++i) {
    character.LockAbilityLine(i, false, preset);
  }
  if (lines.rank() < climb_to || !GoalLanded(lines, worth)) {
    return;
  }
  // The goal is on top and safe there -- a held top line already at the
  // ability's rank is never rerolled. Hold the best filler with it and let the
  // last slot keep rolling.
  for (int slot : BestSlots(lines, worth)) {
    character.LockAbilityLine(slot, true, preset);
  }
}

}  // namespace

AbilityWorth MeasureAbilityWorth(GameState& state, StatPreset preset,
                                 const AbilityRate& rate) {
  // ToProto, not proto(): the live containers hold the character's items, and
  // the backing message they were taken out of has none of them.
  const Character before = state.character.ToProto();
  Character trial = before;
  AbilityPreset& setup = PresetOf(*trial.mutable_inner_ability(), preset);

  // Holding nothing, which is what every line below is read against.
  setup.Clear();
  state.character.RestoreFrom(trial, state.equips, state.items);
  const double bare = rate(state);

  AbilityWorth worth;
  for (int t = ABILITY_LINE_TYPE_STR; t < AbilityLineType_ARRAYSIZE; ++t) {
    for (int r = ABILITY_RANK_RARE; r <= ABILITY_RANK_LEGENDARY; ++r) {
      const AbilityLineType type = static_cast<AbilityLineType>(t);
      const AbilityRank rank = static_cast<AbilityRank>(r);
      if (AbilityTypeWeight(type, rank) <= 0) {
        continue;  // a pairing the roll never hands over
      }
      setup.Clear();
      setup.set_rank(rank);  // the top line always carries the ability's rank
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
      return spent;  // the pool is short, or the panel is not open to them yet
    }
    spent += cost;
  }
  return spent;
}

}  // namespace ms
