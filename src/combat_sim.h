/* The live combat engine: the player auto-attacking a map's mobs one at a
 * time, draining each one's HP, clearing the roster, then idling until the next
 * respawn beat refills it.
 *
 * This is the single engine behind both halves of combat. The kills it reports
 * each step (kills_this_step) are what the reward layer grants EXP, drops and
 * meso for, and the same step drives the combat panel's animation -- so what
 * the player watches and what they are paid for cannot drift apart.
 *
 * ComputeCombatParams() reads the current encounter (map, mobs, weapon,
 * offense) off a GameState into a plain CombatParams. Splitting this out keeps
 * the sim itself free of game state: it steps purely from these params, so it
 * is easy to test. All durations are in game-scaled seconds (x
 * kGameSpeedFactor).
 */
#ifndef MS_SRC_COMBAT_SIM_H_
#define MS_SRC_COMBAT_SIM_H_

#include <cstdint>
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
  bool active = false;            // false when not farming (no map/weapon/mobs)
  double swing_seconds = 0.0;     // time between auto-attacks (game-scaled)
  double respawn_seconds = 0.0;   // time between full-roster respawn beats
  std::vector<CombatType> types;  // in map order
};

// Reads `state`'s current map/character into a CombatParams. active is false
// (and types empty) when there is no current map, no equipped weapon, or no
// loaded mobs.
CombatParams ComputeCombatParams(const GameState& state);

class CombatSim {
 public:
  // Advances combat by elapsed_seconds of real time under `params`. Larger
  // gaps are clamped to one swing, so a long stall costs progress rather than
  // paying out a burst of kills the player never watched.
  void Advance(const CombatParams& params, double elapsed_seconds);

  // True while a valid encounter is being fought.
  bool active() const {
    return active_;
  }
  // True when the whole roster is dead and the sim is idling until the next
  // respawn beat.
  bool respawning() const {
    return respawning_;
  }
  // The current target's name (empty while respawning or inactive).
  const std::string& target_name() const {
    return target_name_;
  }
  // The current target's remaining HP as a fraction in [0, 1].
  double target_hp_fraction() const {
    return target_hp_fraction_;
  }
  // Progress toward the next auto-attack as a fraction in [0, 1].
  double attack_fraction() const {
    return attack_fraction_;
  }
  // Kills recorded during the most recent Advance, indexed to match the
  // params.types passed to that call.
  const std::vector<int64_t>& kills_this_step() const {
    return kills_this_step_;
  }

 private:
  // Repopulates the roster to a full map clear and targets the first mob.
  void Refill(const CombatParams& params);

  bool active_ = false;
  bool initialized_ = false;
  bool respawning_ = false;
  int type_count_ = 0;         // roster type indices are valid while this holds
  std::vector<int> roster_;    // remaining mobs this cycle, as type indices
  double target_hp_ = 0.0;     // current target's remaining HP (absolute)
  double attack_phase_ = 0.0;  // seconds into the current swing
  double delay_remaining_ = 0.0;  // inter-kill pause countdown
  double respawn_phase_ = 0.0;    // seconds into the current respawn cycle

  // Cached render values, refreshed each Advance so accessors need no params.
  std::string target_name_;
  double target_hp_fraction_ = 0.0;
  double attack_fraction_ = 0.0;
  std::vector<int64_t> kills_this_step_;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_SIM_H_
