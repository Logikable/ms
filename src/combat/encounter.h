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

#include <memory>
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
  // Landed swings between casts, for an attack clocked by the character's
  // attacking rather than by the clock. 0 for everything else. An attack
  // carries this or interval_seconds, never both.
  int attacks_per_cast = 0;
  // What one landed swing of this attack counts toward the field above, on
  // whichever attacks are so clocked. 1 for an ordinary swing; less for one
  // that lands several times a second.
  double count_weight = 1.0;
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
  // The bigger swing that takes the PLACE of every empowered_every'th swing of
  // this one, and how often that is. Null and 0 for every attack but the one
  // the Sniper's Empowered Arrows names. Shared rather than owned outright
  // because an AttackOption is copied freely, and the form never changes.
  std::shared_ptr<const AttackOption> empowered;
  int empowered_every = 0;
  // Whether the count runs per ENEMY rather than per swing. Set, nothing is
  // replaced: the swing marks each mob it reaches, and the form lands on top
  // of the ordinary strike for whichever of them came due. Only Divine
  // Judgment sets it.
  bool brands_enemies = false;
};

// Everything the character can attack with, as it stands under one particular
// set of buffs. The same attacks in the same order in every set -- what
// differs is the damage -- so an index the fight is holding stays good however
// the buffs come and go.
struct AttackSet {
  std::vector<AttackOption> attacks;
  std::vector<AttackOption> auto_attacks;
  std::vector<AttackOption> triggered_attacks;
};

// A buff the character puts up for a while, on a wait of its own. What it
// GRANTS is not here: it is already folded into the buffed attack sets,
// because a lever like ignored defence cannot be applied to a damage number
// after that number has been worked out.
struct BuffOption {
  std::string name;
  // All game-scaled, like every other duration here.
  double duration_seconds = 0.0;
  double cooldown_seconds = 0.0;
  // Seconds a landed swing takes off the wait for the next cast.
  double cooldown_reduction_seconds = 0.0;
  // Share of the pool the cast puts back at once (1.00 == all of it).
  double heal_fraction = 0.0;
  // Index into AttackSet::attacks of the swing that lays this buff, or -1 for
  // one the character raises on its own wait. A buff hanging off an ATTACK is
  // inseparable from the swing that delivers it -- Puncture's wound is left by
  // puncturing something -- so the fight has to spend a swing to put it up.
  //
  // An index rather than a name, because the attacks are the same in the same
  // order in every buffed set: one index stays good however the buffs come and
  // go.
  int laid_by_attack = -1;
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
  // The character's level. The fight watches it for the level-up fill, which
  // cannot be read off max_player_hp: a skill point, a scroll or a swapped hat
  // all widen the pool too, and none of them is a reason to be healed.
  int player_level = 0;
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
  // Extra EXP every kill yields, as a fraction of what the mob was worth. Read
  // by AwardCombatRewards rather than by the fight -- Holy Symbol is the one
  // skill whose payment is not made in the fight it was cast in.
  double exp_pct = 0.0;
  // Share added to the meso every kill yields, from the character's passives.
  // 0 for everyone but a Chief Bandit.
  double meso_pct = 0.0;
  // Share of the HP pool a fountain puts back every second, already stretched
  // by the pacing band. Runs whether or not the character is swinging.
  double regen_pct_per_second = 0.0;
  // Seconds between one revival and the next, game-scaled. Above 0 for a
  // character whose passives revive them: the hit that would have killed them
  // fills the pool instead, and the wait starts over. 0 for everyone else.
  double revive_cooldown_seconds = 0.0;
  std::vector<CombatType> types;  // in map order
  // Every attack available, the bare poke first. Never empty while active.
  std::vector<AttackOption> attacks;
  // Attacks that fire on their own clock beside whatever is being swung --
  // summons and cooldown skills. Not candidates for the swing, so a wide one
  // never crowds out the character's own attack; they simply also happen.
  std::vector<AttackOption> auto_attacks;
  // The same, but clocked by swings landed rather than by seconds passed. Held
  // apart from auto_attacks because the fight has to count something for these
  // and nothing for those, and one list with an empty field on half its
  // entries would hide which half.
  //
  // A cast of one of these does not itself count toward any of them: what the
  // player is being paid for is their own attacking.
  std::vector<AttackOption> triggered_attacks;
  // The timed buffs this character can put up. Empty for everyone but a Dark
  // Knight; the fight runs their clocks and asks for the matching attacks.
  std::vector<BuffOption> buffs;
  // One attack set per combination of those buffs, indexed by the bitmask of
  // which are up, less one -- the three lists above are the set for none of
  // them. Empty when nothing grants a buff.
  std::vector<AttackSet> buffed;

  // The three lists above as they stand with `mask`'s buffs up. Out of range
  // reads as none of them, so a fight one step behind a change in what the
  // character has learned swings unbuffed rather than off the end.
  const std::vector<AttackOption>& Attacks(int mask) const;
  const std::vector<AttackOption>& AutoAttacks(int mask) const;
  const std::vector<AttackOption>& TriggeredAttacks(int mask) const;
};

// Reads `state`'s current map/character into a CombatParams. active is false
// (and types empty) when there is no current map, no equipped weapon, or no
// loaded mobs.
CombatParams ComputeCombatParams(const GameState& state);

}  // namespace ms

#endif  // MS_SRC_COMBAT_ENCOUNTER_H_
