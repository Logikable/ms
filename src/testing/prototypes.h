/* Prototypes that several tests build the same way.
 *
 * A fixture belongs here once a second test writes it identically: two copies
 * drift, and a field added to one stops being tested in the other. A prototype
 * only one test uses stays in that test, next to the assertions that read its
 * numbers.
 *
 * Prototypes only: no GameState and no character. Keeping this library to
 * protos keeps it out of everyone's dependency graph.
 */
#ifndef MS_SRC_TESTING_PROTOTYPES_H_
#define MS_SRC_TESTING_PROTOTYPES_H_

#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// A one-handed sword strong enough for a test fight to finish in time. Magic
// attack matches, so the same weapon works for a mage.
EquipPrototype PlainSword();

// A modest weapon any job can hold, for tests about something other than
// damage, such as what a party member is wearing.
EquipPrototype IronSword();

// The first Arcane Symbol, including the two upgrades a symbol can't take.
EquipPrototype VanishingJourneySymbol();

// Iron Body as the wiki states it: DEF +10*L, Max HP +L%, damage taken -L/2%.
Skill IronBody();

// A passive granting one level of every damage stat the stats tab reports, plus
// two stages of attack speed.
Skill LeverPassive();

// A weak mob: one PlainSword hit kills it, worth 3 EXP, always drops a shell.
Mob SnailMob();

// The snail's drop, as the item catalog holds it.
ItemPrototype GreenSnailShell();

// A field of snails with plenty of spawn slots.
MapData SnailMap();

// A mob no starting character can kill or survive: far too much HP, and an
// attack far beyond what their base DEF can reduce.
Mob OgreMob();

// A field holding one of them.
MapData OgreMap();

// Town: somewhere to be sent back to, with nothing to fight.
MapData HomeMap();

}  // namespace ms

#endif  // MS_SRC_TESTING_PROTOTYPES_H_
