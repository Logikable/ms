/* --mode=bis: the best character the game allows at a level.
 *
 * Everything with a maximum is set to it outright by the seeding in
 * game_state.cc: stars to each item's cap, every skill, node and symbol, every
 * familiar at Legendary, a full Legion. What has no maximum -- which item fills
 * a slot, which scroll it takes, which lines its potentials, flame, soul,
 * familiars, Inner Ability and HEXA Stats roll -- is chosen here by measuring
 * the character, one choice at a time, until a whole pass changes nothing.
 *
 * Every choice is one the game's own rolls could give: potential lines at
 * Legendary and prime, flame lines distinct at tier 7, Inner Ability lines
 * distinct with only the top one Legendary, HEXA Stats within GMS's rules.
 */
#ifndef MS_SRC_BIS_SEARCH_H_
#define MS_SRC_BIS_SEARCH_H_

#include <vector>

#include "src/game_state.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

// The upgrade state --mode=bis gives `proto` with `scroll` in every upgrade
// slot (none when null) and stars to the item's cap. Potential and flame are
// left for the caller. Defined in game_state.cc beside the other seeded gear.
Equip BisUpgradedWith(const GameState& state, const EquipPrototype& proto,
                      const Scroll* scroll);

// The scrolls worth trying on `proto`: the biggest attack scroll a boss drop
// pays for, and the best spell traces for the job's main stat and for attack.
// Empty for an item that takes none.
std::vector<const Scroll*> BisScrollChoices(const GameState& state,
                                            const EquipPrototype& proto);

// The starting lines before any measurement: the job's main stat or attack on
// every potential line, the flame max mode would roll at tier 7, Legendary
// familiars and Inner Ability, the HEXA Matrix bought whole.
void DressBisDefaults(GameState& state);

// Every measured choice, ranked by `rate`. Run on the played character once
// the seeding has set the maxima.
void SearchBis(GameState& state, const BisRate& rate);

// One plain swing at the toughest boss the level has unlocked: the rate kBis
// uses when TestOptions gives none.
double BisSwingRate(GameState& state);

}  // namespace ms

#endif  // MS_SRC_BIS_SEARCH_H_
