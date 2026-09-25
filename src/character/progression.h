/* What a level unlocks: which parts of the game are open, and how fast the idle
 * clock runs.
 *
 * Each is one table instead of conditions spread across the panels that use
 * them, so the whole shape of the early game can be read and retuned in one
 * place.
 *
 * Features unlock for the account, not the character: the level checked is the
 * higher of the current character's and the highest any character on the
 * account has reached. A player who has been through the early game once
 * doesn't have to walk their next character through it again.
 */
#ifndef MS_SRC_CHARACTER_PROGRESSION_H_
#define MS_SRC_CHARACTER_PROGRESSION_H_

#include <string>
#include <vector>

#include "src/account.h"
#include "src/character/character.h"

namespace ms {

// Parts of the game hidden from a new account and revealed as it levels. A
// locked feature isn't shown at all: no greyed-out menu entry, no empty panel.
// The player sees each one when it first has something to do.
enum class Feature {
  // The panels along the right of the main screen. Equipped comes first because
  // the player starts out wearing something.
  kEquipped,
  kBag,
  // Item context menu entries. Unequip waits for the bag: taking something off
  // before there's somewhere to put it would move it to a panel the player
  // can't see.
  kUnequip,
  kScrolling,
  kStarForce,
  // The golden hammer, which adds a scroll slot to an item. Unlocked after
  // scrolling and star force, since it's only worth the price on gear the
  // player means to keep.
  kHammer,
  // Cubing, which rerolls an item's potential. Unlocked after the hammer and
  // every other upgrade: it's the last thing a player does to an item, and
  // worth doing again and again.
  kPotential,
  // Recovery isn't listed: it needs a trace, which only exists after an item is
  // destroyed, which no level causes by itself. The item is the gate.
  //
  // Tabs. Skills unlocks on the account's level alone: a character with no job
  // still has the beginner's book.
  kSkills,
  kShop,
  // The menu panel in the bottom-right corner, and its Boss entry. The menu
  // replaces the hotkeys tip in that corner at the level the tip goes away, so
  // the corner is never empty and never has both.
  kMenu,
  kBoss,
  // The Multiplayer entry on that menu. Unlocked once the player is clearly a
  // real character instead of at bossing level: the lobby is where trading
  // happens, which is useful long before a party is.
  kMultiplayer,
  // The bag's Bank tab and its screen. Same level as Characters, for the same
  // reason: shared storage is useless until there's a second character to share
  // with.
  kBank,
  // The Characters entry and the character select behind it. The last menu
  // entry to unlock: a second character is worth making once the first has run
  // out of levels to gain, and the account's unlocks have already opened the
  // way for it.
  kCharacters,
  // The Character panel's Hyper tab and its preset row. Unlocked at Hyper
  // Stats' own level, and by this character's level: the points come from their
  // own levels.
  kHyperStats,
  // The Character panel's Buffs tab. Unlocked at the first potion's level: a
  // tab with only one row the player can't buy is worse than no tab.
  kConsumables,
  // The Equipped panel's Symbols tab. Unlocked at Arcane River's level: below
  // it there are no symbols, and a tab that can only be empty is worse than no
  // tab.
  kSymbols,
  // The gold trail to the Link Skills row, shown the first time the account
  // reaches the last threshold; see kLinkSkillsLevel. The row itself is always
  // on the beginner's page.
  kLinkSkills,
  // The Farm/Boss/Drop row under the Gear tab. Unlocked at cubing's level: a
  // second set of gear is worth keeping once an item is worth more than its
  // tier, which cubing makes true.
  kEquipPresets,
  // The combat stat block on the Character panel, in two halves. Gated by
  // advancement: those rows are filled by a job's passives and gear, so a
  // Beginner has nothing there. Only the panel is hidden; the All Stats screen
  // behind it lists everything.
  kCombatStats,
  kDamageStats,
  kAdvancedStats,
};

// Whether `feature` is open: `character` has reached it, or another character
// on `account` has.
bool Unlocked(Feature feature, const CharacterInstance& character,
              const AccountInstance& account);

// The earliest level `feature` can unlock at. Several also have a second
// condition, so call Unlocked instead of comparing against this.
int UnlockLevel(Feature feature);

// A feature's display name: "Scrolling", "Star Force".
std::string FeatureName(Feature feature);

// The upgrades unlocked going from `from_level` to `to_level`, in unlock order.
// It's a range because one offline period can cross several thresholds.
// `account_level` is the highest any character has reached, and levels already
// covered unlock nothing.
//
// Only the item menu upgrades: a panel or tab highlights itself in gold, but
// these are two keypresses deep with nothing to highlight, so the level-up card
// names them instead.
std::vector<Feature> UpgradesUnlockedBetween(int from_level, int to_level,
                                             int account_level);

/* The gold trail that leads a player to a newly unlocked upgrade.
 *
 * The level-up card names it, and then two markers stay gold until the player
 * visits them: the equipped weapon's name until they open its item menu, and
 * the entry on that menu until they press Enter on it. Each step is recorded in
 * the account's seen-key list, so it survives a restart and never comes back.
 *
 * Each upgrade has its own pair of steps, so the trail runs again for the next
 * upgrade instead of being used up by the first. Not every upgrade uses both
 * steps: star force unlocks long after the item menu stopped being new, so its
 * trail is just the menu entry.
 *
 * The records belong to the account: a player led to star force once isn't led
 * there again by their next character.
 */

// Whether the equipped weapon's name should be gold: something reached through
// the weapon has unlocked, and the player hasn't looked yet.
bool LeadToWeapon(const CharacterInstance& character,
                  const AccountInstance& account);

// Records that they looked. Clears the weapon step of every upgrade unlocked so
// far, since opening the menu showed all of them, but not of upgrades still to
// come.
void FollowedToWeapon(const CharacterInstance& character,
                      AccountInstance& account);

// Whether `feature`'s item menu entry should be gold. False for features
// without a trail.
bool LeadToAction(Feature feature, const CharacterInstance& character,
                  const AccountInstance& account);

// Records that the player pressed Enter on that entry, wherever they did it.
void FollowedToAction(Feature feature, AccountInstance& account);

/* The gold trail that leads a player to the Link Skills screen.
 *
 * Three markers, each shown the first time the account reaches kLinkSkillsLevel
 * and each cleared when the player passes it: the Skills tab, the beginner's
 * page under it, and the row itself. The records belong to the account, like
 * every other trail's, so one character following it clears it for all.
 */
enum class LinkTrailStep {
  kSkillsTab,
  kBeginnerPage,
  kLinkRow,
};

// Whether `step` should be drawn in gold.
bool LeadToLinkSkills(LinkTrailStep step, const CharacterInstance& character,
                      const AccountInstance& account);

// Records that the player followed it. Marking a step twice is harmless.
void FollowedToLinkSkills(LinkTrailStep step, AccountInstance& account);

// The save key `step` is recorded under. It is written into saves, for the same
// reason as TabKey: changing it leads every player down the trail again.
std::string LinkTrailKey(LinkTrailStep step);

// The level where the hotkeys tip stops being shown. Not a Feature: that enum
// is for things that unlock and stay unlocked, and this is the one thing that
// goes away, so including it would make Unlocked mean the opposite.
int HotkeysTipRetireLevel();

// Whether the hotkeys tip should still be shown. It teaches the controls while
// there's nothing else to learn, so a returning player's next character never
// sees it; the menu has already replaced it.
bool HotkeysTipVisible(const CharacterInstance& character,
                       const AccountInstance& account);

// How many times slower than GMS the game runs at `level`: the one global
// pacing setting. It isn't constant: it goes from 2x GMS's clock at level 1 to
// 10x from level 230. Every duration is multiplied by it, so the kill rate and
// every payout depend on it.
double GameSpeedFactor(int level);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_PROGRESSION_H_
