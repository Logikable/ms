/* The live fight: the player auto-attacking a map's mobs, draining HP, clearing
 * the queue, then idling until the next respawn beat refills it -- while the
 * mob at the front of that queue hits back.
 *
 * The mobs form a queue. A respawn beat tops it back up to a full roster,
 * appending the missing spawns in random order and leaving whatever is still
 * standing alone -- so a fight that outlasts a beat keeps its progress. One
 * swing hits the first params.attack_targets of them at once -- the chosen
 * attack's reach -- each taking its own type's damage; a mob at 0 HP leaves the
 * queue and the ones behind slide forward. A single-target attack (reach 1) is
 * the one-at-a-time special case.
 *
 * The player has HP here and nowhere else, because it never outlives a fight:
 * there is no regeneration, and instead a full heal every time the map is
 * cleared or changed, or the character levels. Clearing a map is the breather,
 * so a map the player cannot clear is one their HP only ever falls on -- which
 * is the whole reason a map far above their level is dangerous.
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

// One HP bar for the combat panel: a mob type in the current engaged window
// (the front mobs the next swing will hit), with its members merged.
// hp_fraction is the average of that type's engaged mobs' HP fractions; count
// is how many of them are in the window.
struct EngagedGroup {
  std::string name;
  int level = 0;
  int count = 0;
  double hp_fraction = 0.0;
};

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
  // The name of the swing being charged (the attack skill's, or "Attack" for
  // the bare poke). Empty while inactive or respawning -- with nothing up,
  // there is no swing coming to name.
  const std::string& attack_name() const {
    return attack_name_;
  }
  // The player's remaining HP, rounded up so a sliver still reads as 1 rather
  // than as death. 0 while inactive.
  int player_hp() const;
  // What that HP tops out at, carried through from the params so a caller
  // drawing the pair need not resolve the character's stats again.
  int player_max_hp() const {
    return player_max_hp_;
  }
  // That HP as a fraction in [0, 1] of what the params say it tops out at.
  double player_hp_fraction() const {
    return player_hp_fraction_;
  }
  // True on the one step a hit took the player to 0. Reported rather than
  // acted on, exactly as kills are: what dying costs is the reward layer's
  // business, not the fight's.
  bool died_this_step() const {
    return died_this_step_;
  }
  // Kills recorded during the most recent Advance, indexed to match the
  // params.types passed to that call.
  const std::vector<int64_t>& kills_this_step() const {
    return kills_this_step_;
  }
  // The engaged window as HP bars, one per distinct type the next swing will
  // hit, in the order they appear in the queue. Empty while
  // respawning/inactive.
  const std::vector<EngagedGroup>& engaged_groups() const {
    return engaged_groups_;
  }

 private:
  // A mob waiting in or being fought in the queue: its type (an index into
  // params.types) and its remaining HP.
  struct QueuedMob {
    int type = 0;
    double hp = 0.0;
  };

  // Brings the queue back up to a full roster, adding only the mobs missing
  // from each type. What is already queued keeps its remaining HP and its
  // place: a respawn puts new monsters on the map, it does not heal the one
  // being fought. Clear the queue first to repopulate from scratch.
  //
  // Leaves the swing clock alone -- whether a top-up interrupts the swing in
  // progress depends on why it happened, so the callers decide.
  void TopUp(const CombatParams& params);
  // The attack that would land the most damage on the queue as it stands, or
  // null when there is nothing to hit. Reach is worth nothing past the number
  // of mobs actually queued, so a wide, weak-per-target skill loses to the
  // plain swing once the map thins out.
  const AttackOption* BestAttack(const CombatParams& params) const;

  bool active_ = false;
  bool initialized_ = false;
  bool respawning_ = false;
  // Map the queue was filled from. Its type indices only mean anything for that
  // map, so a change here invalidates them.
  std::string map_;
  std::vector<QueuedMob> queue_;  // remaining mobs this cycle, front = engaged
  double attack_phase_ = 0.0;     // seconds into the current swing
  double respawn_phase_ = 0.0;    // seconds into the current respawn cycle
  double player_hp_ = 0.0;        // remaining player HP, refilled on a clear
  double hit_phase_ = 0.0;        // seconds into the engaged mob's next hit

  // Shuffles each batch of arriving mobs so they are fought in mixed order
  // rather than one whole type at a time (see TopUp). Default-seeded, so a sim
  // plays out the same way every run -- which keeps tests reproducible.
  std::mt19937 rng_;

  // Cached render values, refreshed each Advance so accessors need no params.
  std::string target_name_;
  int target_level_ = 0;
  double target_hp_fraction_ = 0.0;
  double attack_fraction_ = 0.0;
  double player_hp_fraction_ = 0.0;
  int player_max_hp_ = 0;
  std::string attack_name_;
  // Reach of the attack the next swing will use -- also the width of the
  // engaged window the UI draws.
  int reach_ = 1;
  std::vector<int64_t> kills_this_step_;
  bool died_this_step_ = false;
  std::vector<EngagedGroup> engaged_groups_;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_FIGHT_H_
