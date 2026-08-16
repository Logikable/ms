/* Gearing a character the way a player does, and the swing measurement the
 * choice rests on. Shared by the sims that play a character forward.
 *
 * A player does not read a table to learn what to hold: they try the weapons
 * their job can hold and keep the one that hits hardest. Outfit does the same,
 * which is why no list of jobs appears here -- a branch added tomorrow is
 * geared correctly the day it exists.
 */
#ifndef MS_ANALYSIS_SIM_GEAR_H_
#define MS_ANALYSIS_SIM_GEAR_H_

#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/combat/encounter.h"
#include "src/game_state.h"

namespace ms {

// What one swing of `attack` lands on a lone mob, Final Attack included, and
// an empowered form averaged over the swings it takes the place of -- the same
// reading CombatSim::SwingDamage takes. The form has none of its own, so this
// recurs exactly once.
double SoloDamage(const AttackOption& attack);

// What a run of swings came to.
struct Sequence {
  double damage = 0.0;
  double seconds = 0.0;  // time the swings that landed actually took
  int main_attack = -1;  // index of the one swung most often
  // Share of the run each of the character's buffs spent standing, parallel to
  // CombatParams::buffs. What a pulse gated on one is worth is its own damage
  // times this -- see AttackOption::needs_buff.
  std::vector<double> buff_uptime;
};

// Plays out the swings the fight would actually make against a lone mob, at
// the same step and by the same rule as CombatSim: the best rate available,
// with a recharging skill absent from the choice until it comes back, and the
// timed buffs running beside it -- a swing lands for what it is worth under
// whichever of them happen to be standing.
//
// A closed form cannot answer this once a cooldown exists -- what the skill is
// worth depends on what gets swung while it recharges, and on how much of a
// charge is already wound up when it returns. The buffs are the same problem
// again: a buff worth 25% that stands for half the run is not worth 12.5% of
// every swing, it is worth all of it to half of them.
Sequence PlaySwings(const CombatParams& params, double horizon);

// What the character's summons and pulses add per second, at `speed` (1.0 for
// the game-scaled figure, the speed factor to back the scaling out).
//
// A pulse gated on a buff is worth its own damage times the share of the run
// that buff stood, and is priced off the table where it does stand -- it hits
// harder there, which is the point of the buff it waits for.
double OffClockRate(const CombatParams& params, const Sequence& played,
                    double speed);

// The name of the character's weapon, "-" for empty hands.
std::string HeldWeaponName(const CharacterInstance& character);

// Puts the bag's copy of `name` on, if the character can wear it. Found by
// name rather than by index because equipping shuffles the bag: what is
// displaced goes back into it.
bool EquipByName(CharacterInstance& character, const std::string& name);

// Buys and wears the best gear the character can hold: the weapon, the
// ammunition it draws from, and their branch's off-hand.
//
// Which weapon it is comes out of a measurement, not a list: the top rung of
// every ladder they can hold is tried on and swung at a mob of their own
// level, and the one that hits hardest is bought. Only the top of each ladder
// is tried, because within a type the tiers only climb.
//
// Asked afresh every time, because the answer moves: the weapon a Fighter
// wants at 30 is not the one they want once the book behind it is full, and a
// choice frozen at the advancement would hold them to the wrong one for
// thirty levels.
//
// Only the weapon is measured. Ammunition and off-hands are owned by one
// branch apiece and carry plain stats, so for those there is nothing to
// choose between -- only a tier to reach.
//
// The try-ons are free and the winner is paid for, which is how a player
// shops: looking at the shelf costs nothing, and only one weapon goes home.
// `budget` weighs the price against what the character has -- a sim measuring
// the climb wants it, since affording the weapon is part of what it measures;
// a sim asking whether a build can hold a map does not.
//
// Both shelves are shopped. The Frozen tier is paid for in the tokens the
// fights dropped, so under a budget it is reached by farming rather than by
// saving, and without one it is simply the top rung.
void Outfit(GameState& state, bool budget);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_GEAR_H_
