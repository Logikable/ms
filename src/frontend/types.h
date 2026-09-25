#ifndef MS_SRC_FRONTEND_TYPES_H_
#define MS_SRC_FRONTEND_TYPES_H_

#include <string>

#include "src/item/equip_instance.h"
#include "src/protos/scroll.pb.h"

namespace ms {

enum Screen : int {
  kMain,
  kItemMenu,
  kInspect,
  // The stackable version of kInspect, drawn by the same InspectPanel.
  kItemInspect,
  kScrollSelect,
  kScrollResult,
  kApAlloc,
  kSkillLearn,
  // Enter on a skill in the Character panel's Skills tab: read it, turn it on
  // or off if it is a toggle, or close.
  kSkillMenu,
  kSkillInspect,
  // [Reset] at the bottom of the Skills tab's V page: the free way to unspend
  // the matrix.
  kVMatrixReset,
  // Enter on the Link Skills row of the Character panel's beginner page: the
  // link skill from this character's own line, the twelve equipped, and what
  // the account has left to offer. The menu is anchored to one of its rows, so
  // the panel draws it.
  kLinkSkills,
  kLinkSkillMenu,
  // Every stat on one screen, from the Character panel's last stats row.
  kAllStats,
  // The Hyper tab's two questions: one level of one stat, and the free reset
  // that refunds a whole allocation.
  kHyperReset,
  // Enter on a Hyper Stat's name: its current value and what the next level
  // would give. Read-only, like the skill card.
  kHyperStatInspect,
  // Enter on the preset row: use one, move one, or close. Move opens a second
  // menu listing the presets to swap it with.
  kPresetMenu,
  kPresetMove,
  // [Reroll] on the Ability tab: the confirmation, shown over the lines it
  // would replace. Locking a line needs no confirmation and has no screen.
  kAbilityReroll,
  // Enter on a buff's name in the Character panel's Buffs tab: read it, buy it
  // permanently, or close. The switch beside the name needs no confirmation; it
  // takes effect when pressed.
  kBuffMenu,
  kBuffInfo,
  kBuffBuy,
  // Enter on a job in the Character panel's Advance tab: read it, take it, or
  // close. The screen it opens shows the job's book without taking the job.
  kJobMenu,
  kJobInspect,
  kJobAdvance,
  kStarForce,
  kStarForceResult,
  // Cubing from an item menu: the cube shelf and the item's card, with the
  // cube's confirmation between them. The only screen whose Confirm doesn't
  // close the screen under it.
  kCubing,
  // Hammer on the item menu: the confirmation for a piece that can still take a
  // hammer. The entry is greyed out once both hammers are used.
  kHammer,
  kTraceRecover,
  kTraceRecoverResult,
  kSell,
  // Sell many items at once: the bag with a checkbox on every row. Opened from
  // Multi-Sell, under Sell on both item menus.
  kMultiSell,
  // The equip tab's version of kSell. Equipment doesn't stack, so it asks yes
  // or no instead of an amount.
  kSellEquip,
  // Level Up on the Symbols tab: the next level's cost and the confirmation.
  kSymbolLevel,
  // Combine on a spare symbol in the bag: how many to feed into the worn one.
  kSymbolCombine,
  kMapSelect,
  // Enter on a map: go there, see what spawns there, or close.
  kMapMenu,
  // The bestiary, from that menu: the map's mobs and everything known about the
  // one under the cursor.
  kMobInspect,
  // Players on the menu panel's Multiplayer box: everyone connected, by name
  // and level.
  kPlayerList,
  // Enter on a player in that list: inspect them, or ask them to trade.
  kPlayerMenu,
  // Party on that box: the parties open to join, or the one the player is in.
  kPartySelect,
  // Enter on a member: inspect them and, for the leader, kick them or pass
  // leadership.
  kPartyMenu,
  // Trade on either menu: what each side is offering, above the bag.
  kTrade,
  // Enter on one of your own currencies there: how much of it to offer.
  kTradeAmount,
  // Enter on a row in any of the trade's three windows.
  kTradeMenu,
  // Both sides have accepted: the last confirmation before the exchange.
  kTradeConfirm,
  // Escape on the trade screen, which ends the trade for both players, so it
  // asks first.
  kTradeLeave,
  // Offer on a stack in the bag: how many to offer.
  kTradeItemAmount,
  // Inspect on any of those rows. The items aren't in any bag this client can
  // point at, so the card is drawn from a copy built from network data.
  kTradeInspect,
  // The screen behind Inspect, from either list: the player's stats and what
  // they are wearing.
  kPlayerInspect,
  // Enter on one of those worn items: its card, the same one used for the
  // player's own items.
  kPlayerItemInspect,
  // The View All Stats row on the member's Character panel: all their stats, on
  // the same screen the player's own last stats row opens.
  kPlayerAllStats,
  // The confirmation for those member actions, and for leaving a party.
  kPartyConfirm,
  // Enter on the menu panel's Dailies entry: what today's claim pays, over the
  // current screen. If already claimed, it says so.
  kDailies,
  kDailiesNotice,
  // Enter on the menu panel's Boss entry: pick a fight, then fight it.
  kBossSelect,
  kBossConfirm,
  // Enter on a fight that can't be started (no weapon, or it hasn't reset yet).
  // A notice, not a question.
  kBossNotice,
  kBossFight,
  kBossAbort,
  // The card shown after clearing a fight: what it paid, [Continue] to leave,
  // and [Analysis] for the next screen.
  kBossClear,
  // [Analysis] on that card: the fight's damage by player and by skill.
  kBossAnalysis,
  // Bank on the bag's tab bar: the character's bag above the account's bank,
  // plus the three questions moving something between them can ask.
  kBank,
  kBankMenu,
  // Enter on a balance in either half: how much to move to the other side.
  kBankAmount,
  // Inspect on a row in either half.
  kBankInspect,
  kShop,
  kShopMenu,
  kShopInspect,
  kShopBuy,
  // Enter on a menu panel entry with sub-items: a box over the corner, and the
  // screens it opens.
  kMenuBox,
  kKeybinds,
  // The player's options, from the same box.
  kOptions,
  // The whole soundtrack, from the same box: what is playing and what plays
  // next.
  kJukebox,
  // Battle Analysis, from that box: the measured stretch's numbers, over the
  // main screen where it is being measured.
  kAnalysis,
  // Enter on the menu panel's Characters entry: the account's characters, and
  // the card of the one under the cursor. Farming stops while it is open, and
  // the only way back into the game is to play one of them.
  kCharacterSelect,
  // Enter on one of them: play them, give them the offline rewards, delete
  // them, or close.
  kCharacterMenu,
  // Delete's confirmation, over the list. The only action in the game that
  // can't be undone.
  kCharacterDelete,
  // The card a returning player sees: what their character earned while the
  // game was closed. Shown at launch and dismissed with one key.
  kOffline,
  // Escape on the main screen. Last because it is the only screen reached from
  // the main view itself rather than from something on it.
  kQuit,
};
// Focusable panels of the main screen, in Tab order: clockwise from the
// top-left corner. The values index Container::Tab's component list, so both
// must stay in the same order.
enum Panel : int {
  // No panel is in view: the screen in front of the player is a shop, a list or
  // a dialog. It sorts before the real panels, so a range check rejects it.
  kNoPanel = -1,
  kCharPanel = 0,
  kEquipPanel = 1,
  kInventoryPanel = 2,
  kMenuPanel = 3,
  kCombatPanel = 4,
  kNumPanels
};
enum MenuItem : int {
  kMenuAction = 0,
  kMenuInspect = 1,
  // An Arcane Symbol's own action, which no other item shows. It swaps places
  // with Equip: a spare is equipped while that area's slot is empty, and fed
  // into the worn one once it isn't.
  kMenuCombine = 2,
  kMenuScroll = 3,
  kMenuHammer = 4,
  kMenuStarForce = 5,
  // The last upgrade a piece gets, after the three that shape it.
  kMenuCube = 6,
  kMenuRecover = 7,
  // The two entries that get rid of the item, above Close. The cursor doesn't
  // start on them, since they can't be undone.
  kMenuSell = 8,
  kMenuMultiSell = 9,
};
// Entries of the worn-gear context menu, from Enter in the Equipped panel's
// Gear tab. Shorter than the bag's: worn items can't be sold or combined, and
// only a bag item can be a trace to restore.
enum GearMenuItem : int {
  kGearMenuUnequip = 0,
  kGearMenuInspect = 1,
  // The four upgrades in the order a piece gets them: scrolls fill the slots, a
  // hammer adds a slot, stars wait for the slots to be full, and a cube is
  // what's left once it is finished.
  kGearMenuScroll = 2,
  kGearMenuHammer = 3,
  kGearMenuStarForce = 4,
  kGearMenuCube = 5,
  kGearMenuClose = 6,
};
// Entries of the Arcane Symbol context menu, from Enter in the Symbols tab. The
// first two are at the same positions as on the item menu, so the unequip and
// inspect code doesn't need to check which menu is open.
enum SymbolMenuItem : int {
  kSymbolMenuUnequip = 0,
  kSymbolMenuInspect = 1,
  kSymbolMenuLevelUp = 2,
  kSymbolMenuClose = 3,
};
// Entries of the map context menu, from Enter in the map list. Move is first,
// since it is what the list is for; Inspect is secondary.
enum MapMenuItem : int {
  kMapMenuMove = 0,
  kMapMenuInspect = 1,
  kMapMenuClose = 2,
};
// Entries of the party member menu, from Enter in the party list. Anyone can
// open it to inspect a member; only the leader sees the two actions on a
// member, and they are hidden for everyone else.
enum PartyMenuItem : int {
  kPartyMenuInspect = 0,
  kPartyMenuTrade = 1,
  kPartyMenuKick = 2,
  kPartyMenuPromote = 3,
  kPartyMenuClose = 4,
};
// Entries of the player menu, from Enter in the Players list. The same two
// anyone gets in a party, since the list holds everyone online and a party is
// only some of them. Trade isn't shown on your own row.
enum PlayerMenuItem : int {
  kPlayerMenuInspect = 0,
  kPlayerMenuTrade = 1,
  kPlayerMenuClose = 2,
};
// The skill menu's entries. The middle one is only for toggle skills and is
// hidden for every other skill.
enum SkillMenuItem : int {
  kSkillMenuInspect = 0,
  kSkillMenuToggle = 1,
  kSkillMenuClose = 2,
};
// Entries of the job context menu, from Enter in the Advance tab. Advance is
// under Inspect for the same reason Sell is low on the item menu: it can't be
// undone, so the cursor doesn't start on it.
enum JobMenuItem : int {
  kJobMenuInspect = 0,
  kJobMenuAdvance = 1,
  kJobMenuClose = 2,
};
// Entries of the preset menu, from Enter in the preset row. Use is dimmed while
// autoswap is on (the activity decides which preset is used then) and on the
// preset already in use.
enum PresetMenuItem : int {
  kPresetMenuUse = 0,
  kPresetMenuMove = 1,
  kPresetMenuClose = 2,
};
// Entries of the buff context menu, from Enter in the Buffs tab. Buy Perm is
// under Inspect for the same reason as Advance: it spends meso, so the cursor
// doesn't start on it. It is dimmed once the buff is owned.
enum BuffMenuItem : int {
  // The on/off switch. Its label shows the state it would switch the buff to.
  kBuffMenuToggle = 0,
  kBuffMenuInspect = 1,
  kBuffMenuBuyPerm = 2,
  kBuffMenuClose = 3,
};
// Entries of the Etc stackable context menu.
enum StackMenuItem : int {
  kStackInspect = 0,
  kStackSell = 1,
  kStackMultiSell = 2,
  kStackClose = 3,
};
struct ScrollResult {
  ScrollOutcome outcome;
  std::string equip_name;
  std::string scroll_name;
  int slots_remaining = 0;
  ScrollCategory scroll_category = SCROLL_CATEGORY_UNSPECIFIED;
};

struct StarForceResult {
  StarForceOutcome outcome = kStarForceFail;
  std::string equip_name;
  int stars_before = 0;
  int stars_after = 0;
};

struct TraceRecoveryResult {
  std::string equip_name;
  int stars_recovered = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TYPES_H_
