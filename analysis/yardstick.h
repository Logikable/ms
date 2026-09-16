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
 * The clock came in next. A closed form over ONE settled swing cannot see a
 * cooldown, a buff's uptime or a summon, so a purchase was ranked on the swing
 * the character leans on and on nothing else they do. Measuring a fight per
 * candidate would answer it and is far too dear -- so the fight is played ONCE
 * a pass instead, and what comes back is a profile: every attack that landed,
 * weighted by how often it really landed. A candidate is then re-scored
 * through the closed form on each strand of it, which costs what it always did.
 *
 * What is left is the INTERACTION. The weights are solved against the
 * character as they stand, so a candidate lifting a stat one of their buffs
 * multiplies still scores at the rate the buff was up for rather than at the
 * rate it would then be worth. Narrower than it was, not closed.
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

// One thread of what a character really does over a fight: a swing, and how
// often it landed per second of one.
//
// `per_second` is SOLVED rather than counted -- the measured damage the swing
// came to, over what the closed form says one landing is worth -- so it
// carries what the closed form cannot: the share of the fight its cooldown
// left it, the crowd it reached, and the buffs that were standing.
struct Strand {
  const Skill* swing = nullptr;
  int level = 0;
  double per_second = 0.0;
};

// The fight a plan is aimed at, reduced to the two things the damage chain
// needs: what is being hit, and what is hitting it.
struct Yardstick {
  // The body of the aimed fight's objective phase. A monster of the
  // character's own level standing for it where there is no fight to aim at,
  // so a character below the first boss still ranks gear by damage rather than
  // by nothing.
  Mob target;
  // Everything they really swing at it, and how often. Empty for a character
  // with no attack at all, whose every candidate then ranks equal -- which is
  // correct, since nothing they buy changes a damage they cannot deal.
  std::vector<Strand> strands;
};

// Works out both. Dear: it plays the aimed fight out to find what the
// character really does in it, which is why HeldYardstick exists.
Yardstick YardstickFor(const GameState& state);

// `stats` and `passives` read as a whole fight against the yardstick: every
// strand's damage chain at the rate it is swung. What every candidate on the
// shelf is ranked by, and what a drop that sells for nothing is priced in.
//
// THE ONE DOOR. A caller folding its own attack is one whose numbers no longer
// compare with the shelf's -- cube and star offers are sorted against each
// other, and were once in different units for exactly that reason.
double WorthOf(const GameState& state, const Yardstick& yard,
               const EquipStats& stats, const PassiveOffense& passives);

// The yardstick held across one shopping pass. Taking one plays a fight, far
// too dear to do per purchase, and the answer only moves when the KIT does:
// what is worn, what is learned, what it is aimed at. Stars, scrolls and cubes
// move the numbers without moving the plan. ONE per character, never shared.
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
