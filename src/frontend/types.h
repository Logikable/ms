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
  // The stackable counterpart of kInspect, drawn by the same InspectPanel.
  kItemInspect,
  kScrollSelect,
  kScrollResult,
  kApAlloc,
  kSkillLearn,
  // Enter on a skill in the Character panel's Skills tab: read it, switch it
  // on or off where it is a toggle, or walk away.
  kSkillMenu,
  kSkillInspect,
  // [Reset] at the foot of the Skills tab's V page: the free way back to an
  // unspent matrix.
  kVMatrixReset,
  // Enter on the Link Skills row of the Character panel's beginner page: the
  // skill this character's line hands them, the twelve they carry, and what
  // the account has left to offer. The menu is anchored to a row of it, so
  // the panel puts that up itself.
  kLinkSkills,
  kLinkSkillMenu,
  // Every stat on one screen, from the Character panel's last stats row.
  kAllStats,
  // The Hyper tab's two questions: one rung of one stat, and the free reset
  // that takes a whole allocation back.
  kHyperReset,
  // Enter on a Hyper Stat's name: what it is worth now and what the next
  // level would buy. Nothing to do but read it, as the skill card is.
  kHyperStatInspect,
  // Enter on the preset row: put one in use, move one, or walk away. Move
  // opens the second, which lists the presets to swap the first one with.
  kPresetMenu,
  kPresetMove,
  // [Reroll] on the Ability tab: the question, over the lines it would throw
  // away. Locking a line asks nothing and has no screen of its own.
  kAbilityReroll,
  // Enter on a buff's name in the Character panel's Buffs tab: read it, buy it
  // outright, or walk away. The switch beside the name asks nothing -- it
  // takes effect where it is pressed.
  kBuffMenu,
  kBuffInfo,
  kBuffBuy,
  // Enter on a job in the Character panel's Advance tab: read it, take it, or
  // walk away. The screen it leads to reads the job's book without taking it.
  kJobMenu,
  kJobInspect,
  kJobAdvance,
  kStarForce,
  kStarForceResult,
  // Cubing on an item menu: the shelf of cubes and the item's card, with the
  // question one cube asks in the middle. The one screen whose Confirm does
  // not close what it is standing on.
  kCubing,
  // Hammer on the item menu: the question a piece with a hammer left in it
  // is asked. The entry is greyed once both are in.
  kHammer,
  kTraceRecover,
  kTraceRecoverResult,
  kSell,
  // Sell many items in one go: the bag with a mark down every row. Reached
  // from Multi-Sell, which sits under Sell on both item menus.
  kMultiSell,
  // The equip tab's counterpart to kSell. Equipment does not stack, so it
  // asks a yes/no question rather than for an amount.
  kSellEquip,
  // Level Up on the Symbols tab: what the next rung costs, and the question.
  kSymbolLevel,
  // Combine on a spare symbol in the bag: how many to feed the worn one.
  kSymbolCombine,
  kMapSelect,
  // Enter on a map: go there, read what stands there, or walk away.
  kMapMenu,
  // The bestiary, from that menu: the map's mobs and everything known about
  // whichever one the cursor is on.
  kMobInspect,
  // Players on the menu panel's Multiplayer box: everyone connected, by name
  // and level.
  kPlayerList,
  // Enter on a player in that list: read them, or ask them to trade.
  kPlayerMenu,
  // Party on that box: the parties open to be joined, or the one the player
  // is in.
  kPartySelect,
  // Enter on a member: read them, and for the leader kick them or hand the
  // party on.
  kPartyMenu,
  // Trade on either menu: what each side is putting up, over the bag.
  kTrade,
  // Enter on one of your own currencies there: how much of it to offer.
  kTradeAmount,
  // Enter on a row of any of the trade's three windows.
  kTradeMenu,
  // Both sides have accepted: the last question before the exchange.
  kTradeConfirm,
  // Escape on the trade screen, which ends it for both: asked first.
  kTradeLeave,
  // Offer on a stack in the bag: how many of it to put up.
  kTradeItemAmount,
  // Inspect on any of those rows. Their items are not in any bag this client
  // can point at, so the card is drawn from a copy built off the wire.
  kTradeInspect,
  // The player behind Inspect, reached from either list: their stats, and what
  // they are wearing.
  kPlayerInspect,
  // Enter on one of those worn items: its card, the same one the player's own
  // items get.
  kPlayerItemInspect,
  // The View All Stats row on the member's Character panel: every stat they
  // have, on the screen their own last stats row opens.
  kPlayerAllStats,
  // The question one of those asks, and the one leaving a party asks.
  kPartyConfirm,
  // Enter on the menu panel's Dailies entry: what today's claim pays, over
  // whatever screen raised it. The notice is the day already spent.
  kDailies,
  kDailiesNotice,
  // Enter on the menu panel's Boss entry: pick a fight, then fight it.
  kBossSelect,
  kBossConfirm,
  // Enter on a fight that cannot be taken -- no weapon, or its reset has not
  // come round. A notice, not a question.
  kBossNotice,
  kBossFight,
  kBossAbort,
  // The card a cleared fight ends on: what it paid, [Continue] to leave it by
  // and [Analysis] for the next screen.
  kBossClear,
  // [Analysis] on that card: the fight's damage by player and by skill.
  kBossAnalysis,
  // Bank on the bag's tab bar: the character's bag over the account's bank,
  // and the three questions moving something across can ask.
  kBank,
  kBankMenu,
  // Enter on a balance in either half: how much of it to move the other way.
  kBankAmount,
  // Inspect on a row of either half.
  kBankInspect,
  kShop,
  kShopMenu,
  kShopInspect,
  kShopBuy,
  // Enter on a menu panel entry that lists what it leads to: a box over the
  // corner, and the screens it opens.
  kMenuBox,
  kKeybinds,
  // The switches the player can throw, from the same box.
  kOptions,
  // The whole OST, from the same box: what is playing, and what plays next.
  kJukebox,
  // Battle Analysis, from that box: what the stretch being measured is worth,
  // over the main screen it is being measured on.
  kAnalysis,
  // Enter on the menu panel's Characters entry: the account's characters, and
  // the card of whoever the cursor is on. Farming stops while it is up, and
  // the only way back into the game is to play one of them.
  kCharacterSelect,
  // Enter on one of them: play them, hand them the offline check, delete
  // them, or walk away.
  kCharacterMenu,
  // What Delete asks, over the list. The one question in the game whose
  // answer cannot be taken back.
  kCharacterDelete,
  // The card a returning player is met with: what their character earned while
  // the game was closed. Raised at launch and dismissed with one key.
  kOffline,
  // Escape on the main screen. Last because it is the one screen reachable
  // from the main view rather than from something on it.
  kQuit,
};
// Focusable panels of the main screen, in Tab order: clockwise from the
// top-left corner. The values INDEX Container::Tab's component list, so the
// two must stay in the same order.
enum Panel : int {
  // Nobody is looking at a panel at all: the screen in front of the player is
  // a shop, a list or a dialog. Sorts before the real panels, so a range check
  // rejects it.
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
  // An Arcane Symbol's own action, and the only entry here no other item ever
  // shows. It trades places with Equip: a spare goes on while that area's slot
  // is empty, and is fed to what is already worn once it is not.
  kMenuCombine = 2,
  kMenuScroll = 3,
  kMenuHammer = 4,
  kMenuStarForce = 5,
  // The last upgrade a piece goes through, under the three that shape it.
  kMenuCube = 6,
  kMenuRecover = 7,
  // The two that part with the item, above Close. Neither sits where the
  // cursor lands: they are the entries on this menu there is no undoing.
  kMenuSell = 8,
  kMenuMultiSell = 9,
};
// Entries of the worn-gear context menu, on Enter in the Equipped panel's Gear
// tab. Shorter than the bag's: nothing worn is sold or combined, and only a
// bag item can be a trace to recover.
enum GearMenuItem : int {
  kGearMenuUnequip = 0,
  kGearMenuInspect = 1,
  // The four upgrades in the order a piece goes through them: scrolls fill
  // the shelf, a hammer widens it, the stars wait for it to be full, and a
  // cube is what is left to do once it is finished.
  kGearMenuScroll = 2,
  kGearMenuHammer = 3,
  kGearMenuStarForce = 4,
  kGearMenuCube = 5,
  kGearMenuClose = 6,
};
// Entries of the Arcane Symbol context menu, on Enter in the Symbols tab. The
// first two share their places with the item menu's, so the two branches that
// unequip and inspect need not ask which menu is open.
enum SymbolMenuItem : int {
  kSymbolMenuUnequip = 0,
  kSymbolMenuInspect = 1,
  kSymbolMenuLevelUp = 2,
  kSymbolMenuClose = 3,
};
// Entries of the map context menu, on Enter in the map list. Move leads: it is
// what the list is for, and Inspect is the detour.
enum MapMenuItem : int {
  kMapMenuMove = 0,
  kMapMenuInspect = 1,
  kMapMenuClose = 2,
};
// Entries of the party member menu, on Enter in the party list. Anyone may
// raise it to read a member; only the leader is offered the two that act on
// one, and for everybody else they are not on the menu at all.
enum PartyMenuItem : int {
  kPartyMenuInspect = 0,
  kPartyMenuTrade = 1,
  kPartyMenuKick = 2,
  kPartyMenuPromote = 3,
  kPartyMenuClose = 4,
};
// Entries of the player menu, on Enter in the Players list. The same two
// anybody is offered in a party, since the list holds everyone online and a
// party is only some of them. Trade is not on your own row at all.
enum PlayerMenuItem : int {
  kPlayerMenuInspect = 0,
  kPlayerMenuTrade = 1,
  kPlayerMenuClose = 2,
};
// The skill menu's entries. The middle one is a toggle skill's alone -- for
// every other skill it is hidden, there being nothing to switch.
enum SkillMenuItem : int {
  kSkillMenuInspect = 0,
  kSkillMenuToggle = 1,
  kSkillMenuClose = 2,
};
// Entries of the job context menu, on Enter in the Advance tab. Advance sits
// under Inspect for the reason Sell sits low on the item menu: it is the entry
// there is no coming back from, so the cursor does not land on it.
enum JobMenuItem : int {
  kJobMenuInspect = 0,
  kJobMenuAdvance = 1,
  kJobMenuClose = 2,
};
// Entries of the preset menu, on Enter in the preset row. Use is dimmed while
// the autoswap is on -- which preset is read is the fight's to say then -- and
// on the one already in use.
enum PresetMenuItem : int {
  kPresetMenuUse = 0,
  kPresetMenuMove = 1,
  kPresetMenuClose = 2,
};
// Entries of the buff context menu, on Enter in the Buffs tab. Buy Perm sits
// under Inspect for the reason Advance does: it is the entry that spends, and
// it is not where the cursor lands. It dims once the buff is owned.
enum BuffMenuItem : int {
  // The switch, whose label is the state it would leave the buff in.
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
