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

// Holds what this preset is worth the most for -- or nothing at all while it
// is still climbing, since a lock buys nothing when what the character is
// short of is a rank. Frees every line first: a third lock is refused, so a
// swap made the other way round would keep the line it meant to drop.
void HoldBestLines(CharacterInstance& character, StatPreset preset,
                   AbilityRank climb_to, const AbilityWorth& worth) {
  const AbilityPreset lines = character.ability(preset);
  for (int i = 0; i < lines.lines_size(); ++i) {
    character.LockAbilityLine(i, false, preset);
  }
  if (lines.rank() < climb_to) {
    return;
  }
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
                            const AbilityWorth& farming,
                            const AbilityWorth& bossing) {
  const std::pair<StatPreset, const AbilityWorth*> setups[] = {
      {StatPreset::kFarming, &farming}, {StatPreset::kBossing, &bossing}};
  int64_t spent = 0;
  // Alternated rather than taken one preset at a time, so a pool that runs out
  // leaves the character with two half-rolled setups rather than one finished
  // one and the lines they started with.
  for (bool rolled = true; rolled;) {
    rolled = false;
    for (const std::pair<StatPreset, const AbilityWorth*>& setup : setups) {
      HoldBestLines(state.character, setup.first, climb_to, *setup.second);
      const int64_t cost = state.character.ability_reset_cost(setup.first);
      if (!state.character.ResetAbility(setup.first)) {
        continue;  // the pool is short, or the panel is not open to them yet
      }
      spent += cost;
      rolled = true;
    }
  }
  return spent;
}

}  // namespace ms
