/* What a character's level buys them: which parts of the game are open, and
 * how fast the idle clock runs.
 *
 * Both are one table apiece rather than a condition spread across the panels
 * that honour them, so the whole shape of the early game can be read -- and
 * retuned -- in one place.
 */
#ifndef MS_SRC_CHARACTER_PROGRESSION_H_
#define MS_SRC_CHARACTER_PROGRESSION_H_

#include <string>
#include <vector>

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

// What a feature is called on screen: "Scrolling", "Star Force".
std::string FeatureName(Feature feature);

// The upgrades a climb from `from_level` to `to_level` opened, in the order
// they arrive. Asked of the span rather than of the level landed on, because
// one idle stretch can carry a character past several thresholds.
//
// Only the item-menu upgrades. A panel or a tab lights itself gold when it
// arrives; these are actions two keypresses deep in a menu with nothing of
// their own to light, so the level-up card says their names instead.
std::vector<Feature> UpgradesUnlockedBetween(int from_level, int to_level);

/* The gold trail that leads a player to a newly unlocked upgrade.
 *
 * The card names it, and then two signposts stand until they are walked past:
 * the equipped weapon's name is gold until the player opens its item menu, and
 * the entry on that menu is gold until they press Enter on it. Each step
 * latches in the character's seen-key list, so it survives a restart and never
 * comes back.
 *
 * One pair of steps per upgrade, which is what makes the trail run again when
 * the next one arrives rather than being spent on the first. Only the upgrades
 * a worn weapon can actually take: recovery applies to a destroyed item's
 * trace, so pointing at the weapon for it would lead nowhere.
 */

// Whether the equipped weapon's name should be gold: something has opened that
// the player has not gone and looked at their weapon for.
bool LeadToWeapon(const CharacterInstance& character);

// Records that they did. Puts out the weapon step of every upgrade open right
// now -- they opened the menu, and all of it was in front of them -- but not
// of one that has yet to arrive.
void FollowedToWeapon(CharacterInstance& character);

// Whether `feature`'s entry on an item menu should be gold. False for a
// feature with no trail of its own.
bool LeadToAction(Feature feature, const CharacterInstance& character);

// Records that the player pressed Enter on that entry, wherever they did it.
void FollowedToAction(Feature feature, CharacterInstance& character);

// The level the hotkeys tip stops being drawn at. Not a Feature: the enum
// above is for things that open and stay open, and this is the one thing that
// expires, so folding it in would make Unlocked read backwards.
int HotkeysTipRetireLevel();

// Whether the hotkeys tip still has a place on screen. It teaches the controls
// while the game is small enough that there is nothing else to learn.
bool HotkeysTipVisible(const CharacterInstance& character);

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
