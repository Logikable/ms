/* The live fight: the player auto-attacking a map's mobs one at a time,
 * draining each one's HP, clearing the roster, then idling until the next
 * respawn beat refills it.
 *
 * This is the single engine behind both halves of combat. The kills it reports
 * each step (kills_this_step) are what the reward layer pays out for, and the
 * same step drives the combat panel's animation -- so what the player watches
 * and what they are paid for cannot drift apart.
 */
#ifndef MS_SRC_COMBAT_FIGHT_H_
#define MS_SRC_COMBAT_FIGHT_H_

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "src/combat/encounter.h"

namespace ms {

class CombatSim {
 public:
  // Advances the fight by elapsed_seconds of real time under `params`. Larger
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
  // The current target's level (0 while respawning or inactive).
  int target_level() const {
    return target_level_;
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
  // Map the roster was filled from. Its type indices only mean anything for
  // that map, so a change here invalidates them.
  std::string map_;
  std::vector<int> roster_;       // remaining mobs this cycle, as type indices
  double target_hp_ = 0.0;        // current target's remaining HP (absolute)
  double attack_phase_ = 0.0;     // seconds into the current swing
  double delay_remaining_ = 0.0;  // inter-kill pause countdown
  double respawn_phase_ = 0.0;    // seconds into the current respawn cycle

  // Shuffles the roster on each refill so the mobs are fought in mixed order
  // rather than one whole type at a time (see Refill). Default-seeded, so a sim
  // plays out the same way every run -- which keeps tests reproducible.
  std::mt19937 rng_;

  // Cached render values, refreshed each Advance so accessors need no params.
  std::string target_name_;
  int target_level_ = 0;
  double target_hp_fraction_ = 0.0;
  double attack_fraction_ = 0.0;
  std::vector<int64_t> kills_this_step_;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_FIGHT_H_
