/* Where players can stand in a boss arena, and when.
 *
 * A phase's spots are its fixed `player_spots` followed by its timed ones, and
 * a spot's index in that list is what FightMember::spot and the wire carry.
 * Everything here reads only the phase and the fight's clock, so each client
 * and the server reach the same answer without sending anything.
 */
#ifndef MS_SRC_COMBAT_ARENA_SPOTS_H_
#define MS_SRC_COMBAT_ARENA_SPOTS_H_

#include <vector>

#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// Every spot in `phase`, open or not: the fixed ones, then the timed ones.
std::vector<ArenaSpot> AllPlayerSpots(const BossPhase& phase);

// The arena's size in cells. Where the phase leaves a dimension unset, it is
// the furthest cell anything stands on, with no margin.
ArenaSpot ArenaSize(const BossPhase& phase);

// Starts `phase`'s own clock, if it has one: cuts `seconds_left` to the
// phase's limit and returns the seconds taken off, which the fight's elapsed
// time must not lose.
double CutToPhaseClock(const BossPhase& phase, double& seconds_left);

// Whether the timed spots are open `fight_seconds` after the countdown ended.
bool TimedSpotsOpen(const BossPhase& phase, double fight_seconds);

// The indices nobody may stand on or move to at `fight_seconds`: the timed
// spots while they are closed.
std::vector<int> ClosedSpots(const BossPhase& phase, double fight_seconds);

// Where everyone stands once closed spots have dropped their players.
// `standing` holds each player's spot in party order, -1 for nobody. Each
// player on a closed spot, in that order, takes the nearest open spot no one
// else holds, a tie going to the one nearer the arena's middle column. With
// none free they stay put.
std::vector<int> DropFromClosedSpots(const BossPhase& phase,
                                     double fight_seconds,
                                     std::vector<int> standing);

// Which spot a key press moves to. Picks the nearest spot in the pressed
// direction, measured along that direction and then across it; a spot further
// across than along doesn't count as that direction. Returns `from` when no
// spot lies that way or two spots tie. Spots in `taken` are skipped, and the
// search continues past them.
int NextPlayerSpot(const BossPhase& phase, int from, int dx, int dy,
                   const std::vector<int>& taken);
int NextPlayerSpot(const BossPhase& phase, int from, int dx, int dy);

}  // namespace ms

#endif  // MS_SRC_COMBAT_ARENA_SPOTS_H_
