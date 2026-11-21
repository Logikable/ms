/* Where a phase with zones puts its players: as it starts, and again each time
 * a zone's monsters all die. Players may still walk anywhere in between; this
 * is only where they are sent.
 */
#ifndef MS_SRC_COMBAT_BOSS_PLACEMENT_H_
#define MS_SRC_COMBAT_BOSS_PLACEMENT_H_

#include <random>
#include <vector>

#include "src/protos/boss.pb.h"

namespace ms {

// Whether each of `phase`'s zones (index z - 1) still has a monster standing.
// `slot_spawns` and `alive` are per slot, as in phase_rules.h.
std::vector<bool> ZonesAlive(const BossPhase& phase,
                             const std::vector<int>& slot_spawns,
                             const std::vector<bool>& alive);

// Which zone each player goes to, counting from 1. `current` is each player's
// zone now, all 0 as a phase starts; `alive` is ZonesAlive. Nobody is sent past
// two to a zone, and a player already in a living zone within its share stays.
// One left over stays in a zone whose monsters are dead, their own if they have
// one, or else the first; with none dead, 0. `rng` picks who goes first when
// nobody has a zone yet.
std::vector<int> PlacePlayers(ZonePlacement mode,
                              const std::vector<bool>& alive,
                              const std::vector<int>& current,
                              std::mt19937& rng);

// The first of zone `zone`'s spots not in `taken`, or -1.
int FreeSpotInZone(const BossPhase& phase, int zone,
                   const std::vector<int>& taken);

}  // namespace ms

#endif  // MS_SRC_COMBAT_BOSS_PLACEMENT_H_
