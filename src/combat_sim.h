/* The live combat engine's inputs.
 *
 * ComputeCombatParams() reads the current encounter (map, mobs, weapon,
 * offense) off a GameState into a plain CombatParams. Splitting this out keeps
 * the engine itself free of game state: it steps purely from these params, so
 * it is easy to test and cannot quietly disagree with the encounter it is
 * meant to be playing out. All durations are in game-scaled seconds (x
 * kGameSpeedFactor).
 */
#ifndef MS_SRC_COMBAT_SIM_H_
#define MS_SRC_COMBAT_SIM_H_

#include <vector>

#include "src/game_state.h"
#include "src/protos/mob.pb.h"

namespace ms {

// One targetable mob type in the current encounter. `mob` is the single source
// of truth (name, HP, EXP, drops); it is owned by GameState and outlives the
// step it is used in.
struct CombatType {
  const Mob* mob = nullptr;
  double damage_per_hit = 0.0;  // expected damage the player deals to it
  int simultaneous = 0;  // how many spawn at once (spawn_count / type count)
};

// A snapshot of the current encounter's combat parameters.
struct CombatParams {
  bool active = false;            // false when not farming (no map/weapon/mobs)
  double swing_seconds = 0.0;     // time between auto-attacks (game-scaled)
  double respawn_seconds = 0.0;   // time between full-roster respawn beats
  std::vector<CombatType> types;  // in map order
};

// Reads `state`'s current map/character into a CombatParams. active is false
// (and types empty) when there is no current map, no equipped weapon, or no
// loaded mobs.
CombatParams ComputeCombatParams(const GameState& state);

}  // namespace ms

#endif  // MS_SRC_COMBAT_SIM_H_
