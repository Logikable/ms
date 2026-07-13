/* What the player is fighting: the current map's mobs, how hard each one is
 * hit, and how fast the swings and respawns come.
 *
 * ComputeCombatParams() reads all of that off a GameState once, into a plain
 * CombatParams. The fight then steps purely from those params (see fight.h), so
 * it needs no game state of its own and cannot quietly disagree with the
 * encounter it is meant to be playing out. All durations are in game-scaled
 * seconds (x kGameSpeedFactor).
 */
#ifndef MS_SRC_COMBAT_ENCOUNTER_H_
#define MS_SRC_COMBAT_ENCOUNTER_H_

#include <string>
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
  bool active = false;  // false when not farming (no map/weapon/mobs)
  // The map these params describe. The fight watches this to know when it is
  // playing out a different encounter than the one it holds a roster for.
  std::string map;
  double swing_seconds = 0.0;     // time between auto-attacks (game-scaled)
  double respawn_seconds = 0.0;   // time between full-roster respawn beats
  std::vector<CombatType> types;  // in map order
};

// Reads `state`'s current map/character into a CombatParams. active is false
// (and types empty) when there is no current map, no equipped weapon, or no
// loaded mobs.
CombatParams ComputeCombatParams(const GameState& state);

}  // namespace ms

#endif  // MS_SRC_COMBAT_ENCOUNTER_H_
