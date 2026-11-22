/* The rules some boss phases put on their monsters: zones that only their own
 * players reach, windows when a monster can't be hit, HP it holds at until
 * others die, shields, and the penalty for crowding a zone.
 *
 * Slots are numbered as the server numbers them: each spawn's monsters in
 * order, skipping spawns whose mob the catalog lacks. Callers pass the spawn
 * behind each slot. Everything here reads only the phase, the fight's clock and
 * who is alive and where, so a solo run and the server reach the same answer.
 */
#ifndef MS_SRC_COMBAT_BOSS_PHASE_RULES_H_
#define MS_SRC_COMBAT_BOSS_PHASE_RULES_H_

#include <vector>

#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// How one player's hits treat one slot.
struct SlotRule {
  bool reachable = true;
  double damage_taken = 1.0;
  // A fraction of max HP.
  double hp_floor = 0.0;
};

// Whether any of `phase`'s monsters has a rule here, so phases without one
// skip the work.
bool HasSlotRules(const BossPhase& phase);

// The zone `spot` is in, counting from 1, or 0 for none.
int ZoneOfSpot(const BossPhase& phase, int spot);

// Whether `spawn` can't be hit `fight_seconds` after the countdown ended, its
// phase having begun at `phase_began` on the same clock.
bool Invulnerable(const Spawn& spawn, double fight_seconds, double phase_began);

// The fraction of max HP slot `slot` holds at, given which slots are alive.
double HpFloor(const BossPhase& phase, const std::vector<int>& slot_spawns,
               const std::vector<bool>& alive, int slot);

// Each slot's rule for a player on `spot`, with the present party on `spots`
// (the player included). A dead slot's rule means nothing.
std::vector<SlotRule> SlotRules(const BossPhase& phase,
                                const std::vector<int>& slot_spawns,
                                const std::vector<bool>& alive,
                                double fight_seconds, double phase_began,
                                int spot, const std::vector<int>& spots);

// The shields that land in fight time (from, to]: for each slot, the fraction
// of its max HP it gains. Only living givers give and only living slots gain.
std::vector<double> ShieldsBetween(const BossPhase& phase,
                                   const std::vector<int>& slot_spawns,
                                   const std::vector<bool>& alive, double from,
                                   double to);

}  // namespace ms

#endif  // MS_SRC_COMBAT_BOSS_PHASE_RULES_H_
