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

#include "src/combat/damage.h"
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

// One block of lines inside a swing, and what makes it vary. A swing is one of
// these plus one per extra hit the skill lands: the parts differ in line count
// and in how often they crit, so each rolls on its own.
struct HitGroup {
  std::vector<double> damage;  // per target type, parallel to CombatParams
  SwingRolls rolls;
};

// One burn a swing leaves on the enemies it reaches: what one tick is worth
// per target type, the clock it burns on, and how it takes hold. A swing
// carries one of these per source that marks what it hits. Game-scaled, like
// every other duration here.
struct DotApplication {
  std::vector<double> damage;  // per target type, one tick's worth
  SwingRolls rolls;
  double interval_seconds = 0.0;
  double duration_seconds = 0.0;
  // Chance it takes hold on each enemy reached. 1 for a burn a swing simply
  // leaves, which is all of them bar the poison on a rogue's claw.
  double chance = 1.0;
  // Helpings one monster can carry at once, each ticking for the whole damage.
  int max_stacks = 1;
  // Which mark this is, so two burns on one monster do not overwrite each
  // other: an index into the slots every mob carries. Assigned per SOURCE, so
  // the poison a character keeps on their claw writes one slot whichever swing
  // applied it -- and the numbering is the same in every buffed set, so a slot
  // the fight is holding means the same thing however the buffs come and go.
  int slot = -1;
};

// One Final Attack following a swing: a chance, rolled once per enemy the
// swing reached, that one more hit lands on that enemy. `count` is above 1
// only for a source that rides the LINES rather than the swing -- the meso a
// Chief Bandit knocks loose is rolled once per line.
struct FinalAttackRoll {
  double chance = 0.0;
  int count = 1;
  std::vector<double> damage;  // per target type, one hit's worth
  SwingRolls rolls;            // how that hit itself varies
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
  // What this swing gains for every enemy it has already gone through, as a
  // fraction -- compounding, so the k'th enemy it reaches takes (1 + this)^k.
  // 0 for a swing that hits everything it reaches alike, which is all of them
  // but Piercing Arrow. See Skill::pierce_gain_pct.
  double pierce_gain_pct = 0.0;
  // Expected damage per target, parallel to CombatParams::types.
  std::vector<double> damage_per_hit;
  // The same swing broken into the blocks that roll. Their expected damages
  // sum to damage_per_hit, so a reader after the average never looks in here.
  // Empty lands the average itself, which is what a caller building an attack
  // by hand wants.
  std::vector<HitGroup> groups;
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
  // How many of the reached mobs the opening hit lands on, healthiest first.
  // 1 for the Rogue's shape, which strikes one and spreads; more for a swing
  // whose second half simply reaches fewer enemies than its first.
  int lead_enemies = 1;
  // What the opening hit varies by. Its own, because it lands on its own line
  // count rather than the swing's.
  SwingRolls lead_rolls;
  // Expected Final Attack damage per target type, landing on every mob the
  // swing reached: it rolls separately for each of them. Empty for a character
  // with no Final Attack, for a swing none of theirs follows, and for the
  // skills that fire on their own clock -- those are not the character's swing.
  std::vector<double> final_attack_damage;
  // The same, one entry per source, as what actually rolls. Empty lands the
  // average above -- what a caller building an attack by hand wants.
  std::vector<FinalAttackRoll> final_attack_rolls;
  // Blizzard's shape: the pair above, for the sources that roll ONCE for the
  // whole swing and land on one of the enemies it reached. Kept apart rather
  // than flagged inside the vectors above, because the difference is where the
  // damage is added -- once per swing here, once per enemy there.
  std::vector<double> single_final_attack_damage;
  std::vector<FinalAttackRoll> single_final_attack_rolls;
  // The burns this swing leaves on the enemies it reaches. Empty for every
  // swing that leaves none, which is most of them.
  std::vector<DotApplication> dots;
  // Share of the player's HP pool this option puts back instead of dealing
  // damage (1.00 == all of it). 0 for every attack, which is all of them bar
  // the Cleric's Heal. An option carrying this deals no damage at all, and the
  // fight picks it by need rather than by rate -- see CombatSim::HealToCast.
  double heal_fraction = 0.0;
  // Share of the pool a landed swing of THIS attack puts back, on top of
  // whatever the character's passives recover on any swing. 0 for all of them
  // bar Angel Ray, which heals the Bishop as it lands. Costs no swing, unlike
  // heal_fraction above -- the damage still goes out.
  double hp_recover_pct = 0.0;
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
  // Which of the character's buffs has to be standing for this to fire at all,
  // as an index into CombatParams::buffs, or -1 for a clock that runs on its
  // own. Puncture's wound is the case it exists for: what ticks is the wound,
  // so it ticks only where one was left. Off-clock attacks only -- a swing is
  // chosen rather than fired.
  int needs_buff = -1;
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
  // How many distinct burns the character can leave, and so how many slots a
  // monster needs. 0 for everyone who leaves none.
  int dot_count = 0;
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
