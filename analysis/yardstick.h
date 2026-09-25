/* Ranks purchases by the damage they add against the character's target fight.
 *
 * ExpectedAttackDamage takes the monster, so ignored defence, the boss flag,
 * elemental resistance and the level gap are read directly. Combat power has no
 * target and leaves ignored defence out.
 *
 * The closed form can't see cooldowns, buff uptime or summons, so the fight is
 * played once per pass, producing a profile: every attack that landed, weighted
 * by how often. Each candidate is scored through the closed form on each
 * strand.
 *
 * The weights are solved for the character as they are, so a candidate raising
 * a stat one of their buffs multiplies is scored at the buff's current uptime.
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
// landed per second. Solved rather than counted, so it captures cooldown
// uptime, enemies reached and buffs.
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
// yardstick. Every caller must go through this: cube and star offers are sorted
// against each other, and a caller computing its own damage gives different
// units.
double WorthOf(const GameState& state, const Yardstick& yard,
               const EquipStats& stats, const PassiveOffense& passives);

// The yardstick kept across one shopping pass. It only changes when what is
// worn, learned or targeted does; stars, scrolls and cubes change the numbers
// without changing the plan. One per character; never share it.
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
