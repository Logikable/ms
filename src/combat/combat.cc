#include "src/combat/combat.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/character/consumables.h"
#include "src/character/honor.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/combat/loot.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// The most units handed to the bag in one call. GrantDrop's count is an int64
// because an offline stretch can drop more of something than an int holds; the
// bag counts one addition in an int, so a big yield goes in in pieces.
constexpr int64_t kStackChunk = 1000000;

// Adds `count` of `name` to the tally, of which `discarded` were thrown away.
// One line per item however many mob types dropped it.
void TallyItem(RewardTally& tally, const std::string& name, int64_t count,
               int64_t discarded) {
  for (RewardItem& item : tally.items) {
    if (item.name == name) {
      item.count += count;
      item.discarded += discarded;
      return;
    }
  }
  tally.items.push_back({name, count, discarded});
}

}  // namespace

std::string DropName(const GameState& state, const MobDrop& drop) {
  if (drop.has_equip()) {
    std::map<std::string, EquipPrototype>::const_iterator it =
        state.equips.find(drop.equip());
    return it == state.equips.end() ? "" : it->second.name();
  }
  std::map<std::string, ItemPrototype>::const_iterator it =
      state.items.find(drop.item());
  return it == state.items.end() ? "" : it->second.name();
}

int64_t GrantDrop(GameState& state, const MobDrop& drop, int64_t count) {
  if (drop.has_equip()) {
    std::map<std::string, EquipPrototype>::const_iterator it =
        state.equips.find(drop.equip());
    if (it == state.equips.end()) {
      return 0;
    }
    // One at a time: every copy is its own item with its own slots and stars,
    // and a full equip tab stops the rest of them.
    for (int64_t i = 0; i < count; ++i) {
      if (!state.character.PickUp(
              std::make_unique<EquipInstance>(it->second))) {
        return i;
      }
    }
    return count;
  }
  std::map<std::string, ItemPrototype>::const_iterator it =
      state.items.find(drop.item());
  if (it == state.items.end()) {
    return 0;
  }
  int64_t added = 0;
  while (count > 0) {
    int chunk = static_cast<int>(std::min<int64_t>(count, kStackChunk));
    int took = state.character.AddStackable(it->second, chunk);
    added += took;
    if (took < chunk) {
      return added;  // the tab is full; the rest is lost
    }
    count -= chunk;
  }
  return added;
}

RewardTally AwardCombatRewards(GameState& state, const CombatParams& params,
                               const std::vector<int64_t>& kills) {
  CharacterInstance& character = state.character;
  RewardTally tally;
  int64_t exp_gained = 0;
  for (std::size_t i = 0; i < params.types.size(); ++i) {
    if (i >= kills.size() || kills[i] <= 0) {
      continue;
    }
    const Mob& mob = *params.types[i].mob;
    // A boss pays out of its own table and not out of its level band: what
    // Zakum's eight arms are worth is a design decision, not a side effect of
    // being level 110 monsters. Its EXP and meso are the fight's, paid once
    // for the clear, so a body killed here is worth neither.
    if (!mob.boss()) {
      exp_gained += kills[i] * mob.exp();
      // The bonus multiplies the purse rather than each drop in it: a share of
      // a sum is the share of its parts, and the passives are already resolved
      // here.
      int64_t meso = static_cast<int64_t>(
          RollMeso(mob, kills[i], params.item_drop_pct, state.rng) *
          (1.0 + params.meso_pct) * params.meso_final_mult);
      if (meso > 0) {
        character.AddMeso(meso);
        tally.meso += meso;
      }
      // Honor is the monster's own and nothing multiplies it -- neither the
      // meso bonus, which buys nothing here, nor drop rate.
      int64_t honor = RollMobHonor(kills[i], state.rng);
      if (honor > 0) {
        character.AddHonor(honor);
        tally.honor += honor;
      }
    }
    for (const MobDrop& drop : mob.drops()) {
      // Drop rate raises the rate itself. A rate past one is not capped the
      // way the meso chance is: RollDrops already reads it as one drop every
      // kill plus a chance at another.
      int64_t dropped = RollDrops(
          drop.per_kill() * (1.0 + params.item_drop_pct), kills[i], state.rng);
      if (dropped <= 0) {
        continue;
      }
      int64_t taken = GrantDrop(state, drop, dropped);
      std::string name = DropName(state, drop);
      if (!name.empty()) {
        TallyItem(tally, name, taken, dropped - taken);
      }
    }
  }
  if (exp_gained > 0) {
    // The EXP passives land here rather than in the fight: what they buy is
    // the climb, not the swing. Truncated, so a kill worth 1 EXP with a 50%
    // bonus is still worth 1 -- the same rounding every other reward takes.
    tally.exp = static_cast<int64_t>(exp_gained * (1.0 + params.exp_pct)) *
                state.exp_multiplier;
    int before = character.proto().level();
    character.AddExp(tally.exp);
    GrantLevelRewards(state, before, character.proto().level());
  }
  return tally;
}

RewardTally AdvanceCombat(GameState& state, CombatSim& sim,
                          double elapsed_seconds) {
  return AdvanceCombat(state, sim, ComputeCombatParams(state), elapsed_seconds);
}

RewardTally AdvanceCombat(GameState& state, CombatSim& sim,
                          const CombatParams& params, double elapsed_seconds) {
  sim.Advance(params, elapsed_seconds);
  RewardTally tally = AwardCombatRewards(state, params, sim.kills_this_step());
  // Charged for the seconds farmed, and for those alone: a player standing in
  // town or watching a boss is not drinking it. Taken after the kills are
  // paid, so a second's farming can cover a second's drink.
  if (params.active) {
    tally.consumable_cost = state.character.ChargeConsumable(
        CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION, elapsed_seconds);
  }
  if (sim.died_this_step()) {
    // Dying costs the trip home and nothing else -- no EXP, no meso. The
    // kills already banked above stand: they happened. Moving the map is all
    // it takes to be whole again, since the fight heals whoever arrives
    // somewhere new (see fight.h).
    state.current_map = kHomeMap;
  }
  return tally;
}

}  // namespace ms
