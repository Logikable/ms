/* What a level buys: which parts of the game are open, and how fast the idle
 * clock runs.
 *
 * Both are one table apiece rather than a condition spread across the panels
 * that honour them, so the whole shape of the early game can be read -- and
 * retuned -- in one place.
 *
 * A feature opens for the account, not for the character: the level asked of
 * is the higher of the one being played and the furthest any character on the
 * account has reached. A player who has been through the early game once does
 * not walk their next character through it again.
 */
#ifndef MS_SRC_CHARACTER_PROGRESSION_H_
#define MS_SRC_CHARACTER_PROGRESSION_H_

#include <string>
#include <vector>

#include "src/account.h"
#include "src/character/character.h"

namespace ms {

// Parts of the game held back from a new account and handed over as it
// levels. A locked feature is not shown at all: no greyed menu entry, no empty
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
  // Recovery is not here: it needs a trace, and a trace only exists after an
  // item exploded, which no level reaches on its own. The item is the gate.
  // Tabs. Skills is the one gate that is not level alone -- see Unlocked.
  kSkills,
  kShop,
  // The menu panel in the bottom-right corner, and the Boss entry on it. The
  // menu takes the corner over from the hotkeys tip the level the tip
  // retires, so the corner is never empty and never holds both.
  kMenu,
  kBoss,
  // The combat stat block on the Character panel, in two halves. Gated on the
  // advancement rather than the level: what fills those rows is a job's
  // passives and the gear a job can wear, so a Beginner has nothing to read
  // there however high they climb -- unless another character on the account
  // advanced, which opens the rows for all of them. Only the panel is held
  // back: the All Stats screen behind it lists everything, and the first half
  // is what opens the way to it.
  kCombatStats,
  kDamageStats,
  kAdvancedStats,
};

// Whether `feature` is open: either `character` has reached it or another
// character on `account` did.
bool Unlocked(Feature feature, const CharacterInstance& character,
              const AccountInstance& account);

// The earliest level `feature` can open at. Several carry a second condition
// on top of it -- Skills wants a job, and the stat block wants an advancement,
// which a player can always put off -- so ask Unlocked rather than comparing
// against this yourself.
int UnlockLevel(Feature feature);

// What a feature is called on screen: "Scrolling", "Star Force".
std::string FeatureName(Feature feature);

// The upgrades a climb from `from_level` to `to_level` opened, in the order
// they arrive. Asked of the span rather than of the level landed on, because
// one idle stretch can carry a character past several thresholds.
//
// `account_level` is the furthest any character on the account has reached.
// Ground it has already covered opens nothing: the upgrade is not news to the
// player a second time, and it was never locked for this character.
//
// Only the item-menu upgrades. A panel or a tab lights itself gold when it
// arrives; these are actions two keypresses deep in a menu with nothing of
// their own to light, so the level-up card says their names instead.
std::vector<Feature> UpgradesUnlockedBetween(int from_level, int to_level,
                                             int account_level);

/* The gold trail that leads a player to a newly unlocked upgrade.
 *
 * The card names it, and then two signposts stand until they are walked past:
 * the equipped weapon's name is gold until the player opens its item menu, and
 * the entry on that menu is gold until they press Enter on it. Each step
 * latches in the account's seen-key list, so it survives a restart and never
 * comes back.
 *
 * One pair of steps per upgrade, which is what makes the trail run again when
 * the next one arrives rather than being spent on the first. Not every upgrade
 * takes both steps: star force arrives long after the item menu stopped being
 * news, so its trail is the entry alone.
 *
 * The latches are the account's: a player who has been led to star force once
 * is not led there again by their next character.
 */

// Whether the equipped weapon's name should be gold: something that starts at
// the weapon has opened, and the player has not gone and looked at it.
bool LeadToWeapon(const CharacterInstance& character,
                  const AccountInstance& account);

// Records that they did. Puts out the weapon step of every upgrade open right
// now -- they opened the menu, and all of it was in front of them -- but not
// of one that has yet to arrive.
void FollowedToWeapon(const CharacterInstance& character,
                      AccountInstance& account);

// Whether `feature`'s entry on an item menu should be gold. False for a
// feature with no trail of its own.
bool LeadToAction(Feature feature, const CharacterInstance& character,
                  const AccountInstance& account);

// Records that the player pressed Enter on that entry, wherever they did it.
void FollowedToAction(Feature feature, AccountInstance& account);

// The level the hotkeys tip stops being drawn at. Not a Feature: the enum
// above is for things that open and stay open, and this is the one thing that
// expires, so folding it in would make Unlocked read backwards.
int HotkeysTipRetireLevel();

// Whether the hotkeys tip still has a place on screen. It teaches the controls
// while the game is small enough that there is nothing else to learn, so a
// returning player's next character never sees it: the menu has already taken
// the corner.
bool HotkeysTipVisible(const CharacterInstance& character,
                       const AccountInstance& account);

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
