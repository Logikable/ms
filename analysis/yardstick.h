/* What the shopper ranks a purchase by: the damage it adds against the fight
 * the character is aimed at.
 *
 * Combat power was the yardstick here until 2026-09-14, and the trouble with
 * it was never the metric -- a shopper only ever compares a character against
 * themselves, which is exactly what combat power is for. The trouble is that
 * it has NO TARGET. Ignored defence is a fact about the monster, so CombatPower
 * leaves it out, and every caller that needed it bolted it back on by hand:
 * cube_plan carried its own DefenceFactor, normalised by its own copy of the
 * character's, and a divide-by-zero in that hand-fold quietly valued every
 * cube at nothing for anybody under a defence wall.
 *
 * So the target comes in instead. ExpectedAttackDamage is the game's own damage
 * chain and it takes the monster, which means ignored defence, the boss flag,
 * elemental resistance and the level gap are all simply read rather than
 * approximated. Nothing has to be folded in by hand again.
 *
 * Still a closed form, and still no clock: a cooldown, a buff's uptime and a
 * summon are invisible here as they were before. What a swing is worth over
 * TIME is a measured fight -- see //src/combat:measure -- and that is far too
 * dear to ask once per candidate per slot.
 */
#ifndef MS_ANALYSIS_YARDSTICK_H_
#define MS_ANALYSIS_YARDSTICK_H_

#include "src/combat/damage.h"
#include "src/game_state.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// The fight a plan is aimed at, reduced to the two things the damage chain
// needs: what is being hit, and what is hitting it.
struct Yardstick {
  // The body of the aimed fight's objective phase. A monster of the
  // character's own level standing for it where there is no fight to aim at,
  // so a character below the first boss still ranks gear by damage rather than
  // by nothing.
  Mob target;
  // The attack they would really swing, which is where the lines and the skill
  // percentage come from. Null for a character with no attack at all, whose
  // every candidate then ranks equal -- which is correct, since nothing they
  // buy changes a damage they cannot deal.
  const Skill* swing = nullptr;
  int swing_level = 0;
};

// Works out both, once. Dear enough to want holding: it builds the fight's
// params to find out which attack the character would settle on.
Yardstick YardstickFor(const GameState& state);

// What `offense` lands on the yardstick's target, per attack. The number every
// candidate on the shelf is ranked by, and the one a drop that sells for
// nothing is priced in.
double Worth(const Yardstick& yard, const OffenseStats& offense);

// `stats` and `passives` read as a whole attack against the yardstick: the
// character's own numbers with the swing folded in. The one door, so no caller
// has to remember that ExpectedAttackDamage reads its lines off the skill.
double WorthOf(const GameState& state, const Yardstick& yard,
               const EquipStats& stats, const PassiveOffense& passives);

}  // namespace ms

#endif  // MS_ANALYSIS_YARDSTICK_H_
