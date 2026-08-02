/* What a character's level buys them: which parts of the game are open, and
 * how fast the idle clock runs.
 *
 * Both are one table apiece rather than a condition spread across the panels
 * that honour them, so the whole shape of the early game can be read -- and
 * retuned -- in one place.
 */
#ifndef MS_SRC_CHARACTER_PROGRESSION_H_
#define MS_SRC_CHARACTER_PROGRESSION_H_

#include "src/character/character.h"

namespace ms {

// Parts of the game held back from a new character and handed over as they
// level. A locked feature is not shown at all: no greyed menu entry, no empty
// panel. The player meets each one at the point it first has something to do.
enum class Feature {
  // The panels down the right of the main screen. Equipped comes first
  // because the player starts out wearing something.
  kEquipped,
  kBag,
  // Entries of the item context menu. Unequip waits for the bag: taking
  // something off before there is anywhere to put it would drop it into a
  // panel the player cannot see.
  kUnequip,
  kScrolling,
  kStarForce,
  // Recovering a destroyed item's trace. Last, because the star force levels
  // that can destroy an item in the first place need equipment this late.
  kRecovery,
  // Tabs. Skills is the one gate that is not level alone -- see Unlocked.
  kSkills,
  kShop,
};

// Whether `character` has reached `feature` yet.
bool Unlocked(Feature feature, const CharacterInstance& character);

// The level `feature` opens at. Skills carries a second condition on top of
// this one, so ask Unlocked rather than comparing against this yourself.
int UnlockLevel(Feature feature);

// How many times slower than GMS the game runs at `level`. The one global
// pacing knob, and the reason it lives here: it is not a constant. The game
// stretches out as the player climbs, from twice GMS's clock at the start to
// five times that by level 100.
//
// Everything with a duration is multiplied by it -- swing intervals, the
// respawn beat, and so the kill rate and everything paid out per kill.
double GameSpeedFactor(int level);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_PROGRESSION_H_
