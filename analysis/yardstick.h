/* Ranks purchases by the damage they add against the character's target fight.
 *
 * Combat power used to be the measure here. Comparing a character against
 * itself is fine, but combat power has no target: ignored defence depends on
 * the monster, so CombatPower leaves it out, and every caller that needed it
 * added it back by hand. One of those hand-written versions had a
 * divide-by-zero that valued every cube at nothing for any character below a
 * defence wall.
 *
 * So the target is passed in instead. ExpectedAttackDamage is the game's own
 * damage chain and takes the monster, so ignored defence, the boss flag,
 * elemental resistance and the level gap are read directly rather than
 * approximated.
 *
 * Timing came next. A closed-form formula over one attack can't see cooldowns,
 * buff uptime or summons, so purchases were ranked only on the character's main
 * attack. Simulating a fight per candidate would fix that but costs too much.
 * So the fight is played once per pass, producing a profile: every attack that
 * landed, weighted by how often. Each candidate is then scored through the
 * closed form on each strand, at the old cost.
 *
 * One gap remains: interactions. The weights are solved for the character as
 * they are, so a candidate raising a stat that one of their buffs multiplies is
 * scored at the buff's current uptime, not at what the stat would then be
 * worth.
 */
#ifndef MS_ANALYSIS_YARDSTICK_H_
#define MS_ANALYSIS_YARDSTICK_H_

#include <string>
#include <vector>

#include "src/combat/damage.h"
#include "src/game_state.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// One part of what a character does in a fight: an attack, and how often it
// landed per second.
//
// `per_second` is solved rather than counted: the measured damage the attack
// dealt divided by what the closed form says one landing is worth. That way it
// captures what the closed form can't: how much of the fight its cooldown
// allowed, how many enemies it reached, and which buffs were up.
struct Strand {
  const Skill* swing = nullptr;
  int level = 0;
  double per_second = 0.0;
};

// The target fight, reduced to what the damage chain needs: what is being hit,
// and what is hitting it.
struct Yardstick {
  // The body of the target fight's objective phase. With no fight to aim at, a
  // monster of the character's own level stands in, so a character below the
  // first boss still ranks gear by damage.
  Mob target;
  // Every attack the character actually uses on it, and how often. Empty for a
  // character with no attack, so every candidate ranks equal. That is correct,
  // since no purchase changes damage they can't deal.
  std::vector<Strand> strands;
};

// Computes the yardstick. Expensive: it plays out the target fight to see what
// the character actually does, which is why HeldYardstick exists.
Yardstick YardstickFor(const GameState& state);

// Damage `stats` and `passives` deal over the whole fight against the
// yardstick: every strand's damage chain at the rate it's used. Every candidate
// on the shelf is ranked by this, and drops that don't sell are valued in it.
//
// Every caller must go through this function. Cube and star offers are sorted
// against each other, and a caller computing its own damage would give numbers
// in different units, as once happened.
double WorthOf(const GameState& state, const Yardstick& yard,
               const EquipStats& stats, const PassiveOffense& passives);

// The yardstick kept across one shopping pass. Computing one plays a fight, far
// too expensive to do per purchase, and it only changes when the kit changes:
// what is worn, what is learned, and what the target is. Stars, scrolls and
// cubes change the numbers without changing the plan. Use one per character;
// never share it.
class HeldYardstick {
 public:
  const Yardstick& For(const GameState& state);

 private:
  Yardstick held_;
  std::string key_;
  bool taken_ = false;
};

}  // namespace ms

#endif  // MS_ANALYSIS_YARDSTICK_H_
