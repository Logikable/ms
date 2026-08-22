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

#include <map>
#include <set>
#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/combat/encounter.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

// What one swing of `attack` lands on `enemies` mobs standing together: every
// rider the swing carries, and an empowered form averaged over the swings it
// takes the place of -- the same reading CombatSim::SwingDamage takes. The
// form has none of its own, so this recurs exactly once.
//
// Which riders count per enemy and which count once is the whole of what an
// enemy count changes: a Final Attack that falls on one enemy is worth the
// same against twelve, and a burn is worth twelve times as much.
//
// `charge_burns` false leaves the burns out entirely, for a caller keeping its
// own burn clocks and landing their ticks as they fall due -- see PlaySwings.
// True charges them at the rate the swing can sustain, which is right for
// anything relit on a clock of its own and for measuring a swing on its own.
double CrowdDamage(const AttackOption& attack, int enemies,
                   bool charge_burns = true);

// The same against a lone mob, which is what a weapon comparison wants.
double SoloDamage(const AttackOption& attack);

// What a run of swings came to.
struct Sequence {
  double damage = 0.0;
  double seconds = 0.0;  // time the swings that landed actually took
  int main_attack = -1;  // index of the one swung most often
  // What each swing came to over the run, parallel to CombatParams::attacks.
  // Sums to `damage`, so a share is one entry over that.
  std::vector<double> damage_by_attack;
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
Sequence PlaySwings(const CombatParams& params, double horizon,
                    int enemies = 1);

// What the character's summons and pulses add per second, at `speed` (1.0 for
// the game-scaled figure, the speed factor to back the scaling out).
//
// A pulse gated on a buff is worth its own damage times the share of the run
// that buff stood, and is priced off the table where it does stand -- it hits
// harder there, which is the point of the buff it waits for.
double OffClockRate(const CombatParams& params, const Sequence& played,
                    double speed, int enemies = 1);

// The name of the character's weapon, "-" for empty hands.
std::string HeldWeaponName(const CharacterInstance& character);

// Puts the bag's copy of `name` on, if the character can wear it. Found by
// name rather than by index because equipping shuffles the bag: what is
// displaced goes back into it.
bool EquipByName(CharacterInstance& character, const std::string& name);

// Buys and wears the best gear the character can hold: the weapon, the
// ammunition it draws from, and their branch's off-hand. Both shelves are
// shopped, the Frozen tier included.
//
// Which weapon comes out of a measurement rather than a list -- the top rung
// of every ladder they can hold is swung at a mob of their own level, and the
// hardest hitter is bought. Asked afresh every time, because the answer moves
// as the book behind the weapon fills.
//
// `budget` weighs the price against the purse: a sim measuring the climb wants
// it, since affording the weapon is part of what it measures; one asking
// whether a build can hold a map does not.
void Outfit(GameState& state, bool budget);

// Outfit with the choice already made: the top rung of `type` the character
// can hold, what it draws from, and their branch's off-hand. For a sim that
// settles the weapon elsewhere -- measuring it needs a book, and a book is
// not always bought by the time the weapon has to be in hand.
void OutfitWeapon(GameState& state, EquipType type);

// Wears the best of every slot the shop does not stock: the armour, the
// accessories and the pocket, which in this game drop rather than sell. What
// a player who had cleared everything would be standing in.
//
// `skip` names catalog keys to leave off, for a sim asking whether a fight can
// be won without what only that fight pays -- a boss cannot be beaten in its
// own drop. Within a slot the highest rung wins, the way a shop ladder is
// climbed: there is nothing to measure while each slot holds one item.
void OutfitDrops(GameState& state, const std::set<std::string>& skip = {});

// Wears the best of what the bag is already holding, in the slots the shop
// does not stock -- the armour, the accessories and the pocket, which drop
// rather than sell. A piece is put on when its slot is empty or when it
// outranks what is in it, so a second copy of what is worn never displaces the
// scrolls and stars on the first.
//
// The drop half of Outfit: that one shops, this one opens the bag. A sim
// playing a climb forward needs both, since a player wears what falls.
void WearBestFromBag(CharacterInstance& character);

// Puts everything worn at its ceiling: every upgrade slot filled with the
// scroll that measures best on the item, and stars up to the item's own
// maximum. Nothing is rolled and nothing is paid for -- a sim asking what a
// build can reach wants the ceiling, not one draw from it.
//
// Which scroll is best is measured rather than listed, for the reason Outfit
// measures the weapon: a thief's weapon takes three 15% traces that differ
// only in which stat rides the attack, and only a swing says which.
//
// `star_cap` holds every item below its own maximum, for a ceiling a player
// would actually stop at -- past 15 an attempt can destroy the item, and a
// piece only one boss drops has no second copy to reach for.
void FullyUpgrade(GameState& state, int star_cap = kMaxStarForce);

// Which scroll each worn slot wants: the one the character measures best in
// when it fills every slot of the item, chosen from those the item takes that
// succeed `success_rate` of the time. A slot no scroll helps is absent.
//
// Restores the character afterwards, so asking wears nothing and buys
// nothing.
std::map<EquipSlot, const Scroll*> ChooseScrolls(GameState& state,
                                                 int success_rate);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_GEAR_H_
