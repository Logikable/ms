#include "src/combat/combat.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/combat/loot.h"
#include "src/game_state.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

void AdvanceCombat(GameState& state, CombatSim& sim, double elapsed_seconds) {
  AdvanceCombat(state, sim, ComputeCombatParams(state), elapsed_seconds);
}

void AdvanceCombat(GameState& state, CombatSim& sim, const CombatParams& params,
                   double elapsed_seconds) {
  sim.Advance(params, elapsed_seconds);
  const std::vector<int64_t>& kills = sim.kills_this_step();

  CharacterInstance& character = state.character;
  int player_level = character.proto().level();
  int64_t exp_gained = 0;
  for (std::size_t i = 0; i < params.types.size(); ++i) {
    if (kills[i] <= 0) {
      continue;
    }
    const Mob& mob = *params.types[i].mob;
    exp_gained += kills[i] * mob.exp();
    // The bonus multiplies the purse rather than each drop in it: a share of a
    // sum is the share of its parts, and the passives are already resolved
    // here.
    int64_t meso =
        static_cast<int64_t>(RollMeso(mob, player_level, kills[i], state.rng) *
                             (1.0 + params.meso_pct));
    if (meso > 0) {
      character.AddMeso(meso);
    }
    for (const MobDrop& drop : mob.drops()) {
      int64_t dropped = RollDrops(drop.per_kill(), kills[i], state.rng);
      if (dropped <= 0) {
        continue;
      }
      std::map<std::string, ItemPrototype>::const_iterator item_it =
          state.items.find(drop.item());
      if (item_it == state.items.end()) {
        continue;  // Drop references an unloaded item; skip it.
      }
      character.AddStackable(item_it->second, static_cast<int>(dropped));
    }
  }
  if (exp_gained > 0) {
    // Holy Symbol's share lands here rather than in the fight: what it buys is
    // the climb, not the swing. Truncated, so a kill worth 1 EXP with a 50%
    // bonus is still worth 1 -- the same rounding every other reward takes.
    character.AddExp(static_cast<int64_t>(exp_gained * (1.0 + params.exp_pct)) *
                     state.exp_multiplier);
  }
  if (sim.died_this_step()) {
    // Dying costs the trip home and nothing else -- no EXP, no meso. The
    // kills already banked above stand: they happened. Moving the map is all
    // it takes to be whole again, since the fight heals whoever arrives
    // somewhere new (see fight.h).
    state.current_map = kHomeMap;
  }
}

}  // namespace ms
