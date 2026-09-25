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
#include "src/character/v_matrix.h"
#include "src/combat/drop.h"
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

// Largest count handed to the bag in one call. GrantDrop takes an int64 because
// offline progress can exceed an int, but the bag takes an int.
constexpr int64_t kStackChunk = 1000000;

// Adds `count` of `name` to the tally, `discarded` of which were thrown away.
// Keeps one entry per item, even when several mob types drop it.
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

int64_t GrantDrop(GameState& state, const MobDrop& drop, int64_t count) {
  if (drop.has_equip()) {
    std::map<std::string, EquipPrototype>::const_iterator it =
        state.equips.find(drop.equip());
    if (it == state.equips.end()) {
      return 0;
    }
    // Add equips one at a time: each copy is its own item with its own slots
    // and stars, and a full equip tab stops the rest.
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
    int took = state.character.AddItem(it->second, chunk);
    added += took;
    if (took < chunk) {
      return added;  // the bag is full; the rest is lost
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
    // Bosses pay EXP and meso once per clear, from their own table, so a boss
    // kill here pays neither.
    if (!mob.boss()) {
      exp_gained += kills[i] * mob.exp();
      // Applying the meso bonus to the total is the same as applying it to each
      // drop.
      int64_t meso = static_cast<int64_t>(
          RollMeso(mob, kills[i], params.item_drop_pct, state.rng) *
          (1.0 + params.meso_pct) * params.meso_final_mult);
      if (meso > 0) {
        character.AddMeso(meso);
        tally.meso += meso;
      }
      // Nothing multiplies honor: not meso bonus, not drop rate.
      int64_t honor = RollMobHonor(kills[i], state.rng);
      if (honor > 0) {
        character.AddHonor(honor);
        tally.honor += honor;
      }
      // V Points count as a drop, so drop rate raises them. Only Arcane River
      // and Grandis mobs drop them. A character without the 5th job
      // advancement still banks them.
      if (params.pays_v_points) {
        int64_t points =
            RollMobVPoints(kills[i], params.item_drop_pct, state.rng);
        if (points > 0) {
          character.AddVPoints(points);
          tally.v_points += points;
        }
      }
    }
    for (const MobDrop& drop : mob.drops()) {
      // Drop rate is uncapped here, unlike the meso chance. RollDrops treats a
      // rate above 1 as a guaranteed drop plus a chance at another.
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
    // EXP bonuses apply here, not in the fight, since they don't affect damage.
    // Truncated like every other reward.
    tally.exp = static_cast<int64_t>(exp_gained * (1.0 + params.exp_pct)) *
                state.exp_multiplier;
    AwardExp(state, tally.exp);
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
  RewardTally tally =
      AwardCombatRewards(state, params, sim.view().kills_this_step);
  // Charge potions only for seconds spent farming, and after paying for kills,
  // so a second of farming can pay for itself.
  if (params.active) {
    tally.consumable_cost =
        state.character.ChargeFarmingConsumables(elapsed_seconds);
  }
  if (sim.view().died_this_step) {
    // Dying sends the character home but keeps the kills paid above. Changing
    // maps is enough to recover: the fight heals whoever arrives on a new map.
    state.current_map = kHomeMap;
  }
  return tally;
}

}  // namespace ms
