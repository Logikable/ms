/* What the player is fighting: the current map's mobs, how hard each one is
 * hit, and how fast the swings and respawns come.
 *
 * ComputeCombatParams() reads all of that off a GameState once, into a plain
 * CombatParams. The fight then steps purely from those params (see fight.h), so
 * it needs no game state of its own and cannot quietly disagree with the
 * encounter it is meant to be playing out. All durations are in game-scaled
 * seconds: real ones stretched by the character's GameSpeedFactor, which grows
 * with their level.
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
  int simultaneous = 0;  // how many spawn at once (spawn_count / type count)
  // Expected damage one hit from this mob does to the player, already through
  // their DEF. Held per type rather than per mob because every member of a
  // type hits alike.
  double damage_to_player = 0.0;
};

// One thing the character could spend a swing on: the bare poke, a learned
// attack skill, or a cast that does something else with the swing entirely.
// Which one is best depends on how many mobs are actually in front of the
// player, so the choice is made per swing by the fight rather than fixed here
// -- a wide skill that does less per target wins on a crowd and loses on the
// last mob standing.
struct AttackOption {
  std::string name = "Attack";  // shown on the charge bar
  int max_enemies = 1;          // front-of-queue mobs one swing reaches
  // Expected damage per target, parallel to CombatParams::types.
  std::vector<double> damage_per_hit;
  // Seconds one swing of this attack takes, game-scaled. Per attack rather
  // than per encounter because the delay belongs to the skill: a slower
  // animation is the price a harder-hitting skill pays. 0 for an attack that
  // fires on its own clock instead -- see interval_seconds.
  double swing_seconds = 0.0;
  // Seconds between casts, for an attack that fires on its own clock rather
  // than on the character's swing. 0 for the swings themselves, which are
  // paced by swing_seconds.
  double interval_seconds = 0.0;
  // Seconds this attack cannot be swung for after it lands, game-scaled. 0 for
  // one that is there every time, which is most of them.
  double cooldown_seconds = 0.0;
  // Expected damage of the opening hit, per target type. It lands on ONE of
  // the mobs the swing reached -- the healthiest of them, because a hit this
  // big is worth least where it overkills. Empty for a swing that lands once,
  // which is most of them.
  std::vector<double> lead_damage;
  // Expected Final Attack damage per target type, landing on every mob the
  // swing reached: it rolls separately for each of them. Empty for a character
  // with no Final Attack, for a swing none of theirs follows, and for the
  // skills that fire on their own clock -- those are not the character's swing.
  std::vector<double> final_attack_damage;
  // Share of the player's HP pool this option puts back instead of dealing
  // damage (1.00 == all of it). 0 for every attack, which is all of them bar
  // the Cleric's Heal. An option carrying this deals no damage at all, and the
  // fight picks it by need rather than by rate -- see CombatSim::HealToCast.
  double heal_fraction = 0.0;
};

// A snapshot of the current encounter's combat parameters.
struct CombatParams {
  bool active = false;  // false when not farming (no map/weapon/mobs)
  // The map these params describe. The fight watches this to know when it is
  // playing out a different encounter than the one it holds a roster for.
  std::string map;
  double respawn_seconds = 0.0;  // time between full-roster respawn beats
  double hit_seconds = 0.0;      // time between mob hits on the player
  int max_player_hp = 0;         // what a full heal fills the player back to
  // Share of that pool the player gets back on every respawn beat, whether or
  // not they cleared the map (0.10 == a tenth of it). What lets a map be
  // survived by outlasting it rather than only by emptying it.
  double beat_heal_fraction = 0.0;
  // Share of every hit the player takes that goes back into the mob that
  // landed it (1.20 == 120% of the damage taken). 0 for a character with no
  // reflection, which is all of them until Spirit Blade is learned.
  double damage_reflect_pct = 0.0;
  // Share of the pool a landed swing puts back, from a passive that heals on
  // attack. Costs no swing, so it stacks with the beat heal rather than
  // replacing it, and pays nothing on an empty map -- there is nothing to hit.
  double hp_recover_pct = 0.0;
  std::vector<CombatType> types;  // in map order
  // Every attack available, the bare poke first. Never empty while active.
  std::vector<AttackOption> attacks;
  // Attacks that fire on their own clock beside whatever is being swung --
  // summons and cooldown skills. Not candidates for the swing, so a wide one
  // never crowds out the character's own attack; they simply also happen.
  std::vector<AttackOption> auto_attacks;
};

// Reads `state`'s current map/character into a CombatParams. active is false
// (and types empty) when there is no current map, no equipped weapon, or no
// loaded mobs.
CombatParams ComputeCombatParams(const GameState& state);

}  // namespace ms

#endif  // MS_SRC_COMBAT_ENCOUNTER_H_
