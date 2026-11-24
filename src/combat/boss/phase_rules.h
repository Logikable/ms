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

#include "src/combat/guard_times.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// How one player's hits treat one slot.
struct SlotRule {
  bool reachable = true;
  double damage_taken = 1.0;
  // A fraction of max HP.
  double hp_floor = 0.0;
  GuardTimes guard;
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

// When each slot's guard (Spawn.guard) last opened, in fight seconds, and how
// many of its HP openings are spent. Whoever owns the HP keeps these: the solo
// run, or the server, which sends them on.
struct GuardState {
  // Negative before the first opening.
  double opened_at = -1.0;
  int hp_opens_spent = 0;
};

// Moves `guards` (one per slot) up to `fight_seconds`, given each slot's HP
// fraction now: the openings its interval has brought, then one its HP has.
void AdvanceGuards(const BossPhase& phase, const std::vector<int>& slot_spawns,
                   const std::vector<double>& hp_fractions,
                   double fight_seconds, std::vector<GuardState>& guards);

// Whether a guard that last opened at `opened_at` is open at `fight_seconds`.
bool GuardOpen(const SpawnGuard& guard, double opened_at, double fight_seconds);

// When a guard that last opened at `opened_at` next opens and how long it
// holds, from `fight_seconds`, for the fight to save a burst for it.
GuardTimes GuardTimesAt(const SpawnGuard& guard, double opened_at,
                        double fight_seconds);

// When the phase's first guarded slot last opened, for the timed spots that
// follow it; negative when none has.
double PhaseGuardOpenedAt(const BossPhase& phase,
                          const std::vector<int>& slot_spawns,
                          const std::vector<GuardState>& guards);

// One span a self shield (Spawn.self_shield) holds for, in fight seconds.
struct ShieldSpan {
  double start = 0.0;
  double end = 0.0;
};

// The spans of `shield` that overlap fight time [from, to], its phase having
// begun at `phase_began`.
std::vector<ShieldSpan> SelfShieldSpans(const BossPhase& phase,
                                        const SelfShield& shield,
                                        double phase_began, double from,
                                        double to);

// The living slots whose Spawn.vanish has come, given each slot's HP fraction
// (0 for a dead one).
std::vector<bool> Vanishing(const BossPhase& phase,
                            const std::vector<int>& slot_spawns,
                            const std::vector<double>& hp_fractions);

// Each slot's rule for a player on `spot`, with the present party on `spots`
// (the player included). `shields_left` is each slot's shield, as a fraction
// of max HP. A dead slot's rule means nothing.
std::vector<SlotRule> SlotRules(const BossPhase& phase,
                                const std::vector<int>& slot_spawns,
                                const std::vector<bool>& alive,
                                const std::vector<GuardState>& guards,
                                const std::vector<double>& shields_left,
                                double fight_seconds, double phase_began,
                                int spot, const std::vector<int>& spots);

// The shields that land in fight time (from, to]: for each slot, the fraction
// of its max HP it gains. Only living givers give and only living slots gain.
// A self shield is raised as its span starts, and grows while it holds:
// `shields_left` says whether it still does.
std::vector<double> ShieldsBetween(const BossPhase& phase,
                                   const std::vector<int>& slot_spawns,
                                   const std::vector<bool>& alive,
                                   const std::vector<double>& shields_left,
                                   double phase_began, double from, double to);

// The slots whose self shield ends in fight time (from, to], whatever is left
// of it then healing them unless it vanishes (ShieldVanishes). One held until
// broken never ends.
std::vector<bool> SelfShieldsEnding(const BossPhase& phase,
                                    const std::vector<int>& slot_spawns,
                                    double phase_began, double from, double to);

// Whether what is left of the slot's self shield is lost as it ends instead of
// healing it.
bool ShieldVanishes(const BossPhase& phase, const std::vector<int>& slot_spawns,
                    int slot);

// The other slots sharing `slot`'s HP (Spawn.shares_hp): a hit on one is a hit
// on every one of them.
std::vector<int> HpPartners(const BossPhase& phase,
                            const std::vector<int>& slot_spawns, int slot);

}  // namespace ms

#endif  // MS_SRC_COMBAT_BOSS_PHASE_RULES_H_
