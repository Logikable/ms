#include "src/farming.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "src/character.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/combat/loot.h"
#include "src/game_state.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

void AdvanceCombat(GameState& state, CombatSim& sim, double elapsed_seconds) {
  CombatParams params = ComputeCombatParams(state);
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
    // Meso pools across every mob type into one balance, so all kills bank into
    // the shared meso_progress accumulator.
    int64_t meso = FlushDrops(ExpectedMesoPerKill(mob, player_level), kills[i],
                              &state.meso_progress);
    if (meso > 0) {
      character.AddMeso(meso);
    }
    for (const MobDrop& drop : mob.drops()) {
      int64_t dropped = FlushDrops(drop.per_kill(), kills[i],
                                   &state.drop_progress[drop.item()]);
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
    character.AddExp(exp_gained);
  }
}

}  // namespace ms
