/* Prototypes several tests build the same way.
 *
 * A fixture belongs here once a second test writes it out identically: two
 * copies drift, and a field added to one is the field the other stops
 * exercising. A prototype only one test uses stays in that test, where its
 * numbers sit beside the assertion that reads them.
 *
 * Prototypes only -- no GameState, no character. What a test does with one of
 * these is its own business, and keeping the library to the protos keeps it
 * out of everybody's dependency graph.
 */
#ifndef MS_SRC_TESTING_PROTOTYPES_H_
#define MS_SRC_TESTING_PROTOTYPES_H_

#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// A one-handed sword hitting hard enough that a fight in a test finishes
// inside the clock. Magic attack matches, so the same weapon serves a mage.
EquipPrototype PlainSword();

// A modest weapon any job can hold, for a test about something other than the
// damage it does -- what a party member is wearing, say.
EquipPrototype IronSword();

// The first Arcane Symbol, with the two upgrades a symbol refuses.
EquipPrototype VanishingJourneySymbol();

// Iron Body as the wiki states it: DEF +10*L, Max HP +L%, damage taken -L/2%.
Skill IronBody();

// A passive granting one level's worth of every damage lever the stats tab
// reports, plus two stages of attack speed.
Skill LeverPassive();

}  // namespace ms

#endif  // MS_SRC_TESTING_PROTOTYPES_H_
