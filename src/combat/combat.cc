#include "src/combat/combat.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/character/character.h"
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

// Hands `count` copies of one rolled drop to the character. A drop names
// either a stackable or an equip, so this asks which and takes the matching
// path; a name neither catalog knows is skipped rather than guessed at.
void GrantDrop(GameState& state, const MobDrop& drop, int64_t count) {
  if (!drop.equip().empty()) {
    std::map<std::string, EquipPrototype>::const_iterator it =
        state.equips.find(drop.equip());
    if (it == state.equips.end()) {
      return;
    }
    // One at a time: every copy is its own item with its own slots and stars,
    // and a full equip tab stops the rest of them.
    for (int64_t i = 0; i < count; ++i) {
      if (!state.character.PickUp(
              std::make_unique<EquipInstance>(it->second))) {
        return;
      }
    }
    return;
  }
  std::map<std::string, ItemPrototype>::const_iterator it =
      state.items.find(drop.item());
  if (it == state.items.end()) {
    return;
  }
  state.character.AddStackable(it->second, static_cast<int>(count));
}

}  // namespace

void AdvanceCombat(GameState& state, CombatSim& sim, double elapsed_seconds) {
  AdvanceCombat(state, sim, ComputeCombatParams(state), elapsed_seconds);
}

void AdvanceCombat(GameState& state, CombatSim& sim, const CombatParams& params,
                   double elapsed_seconds) {
  sim.Advance(params, elapsed_seconds);
  const std::vector<int64_t>& kills = sim.kills_this_step();

  CharacterInstance& character = state.character;
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
    int64_t meso = static_cast<int64_t>(RollMeso(mob, kills[i], state.rng) *
                                        (1.0 + params.meso_pct));
    if (meso > 0) {
      character.AddMeso(meso);
    }
    for (const MobDrop& drop : mob.drops()) {
      int64_t dropped = RollDrops(drop.per_kill(), kills[i], state.rng);
      if (dropped > 0) {
        GrantDrop(state, drop, dropped);
      }
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
