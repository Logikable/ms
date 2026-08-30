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
  // The stackable counterpart of kInspect: see ItemInspectPanel.
  kItemInspect,
  kScrollSelect,
  kScrollResult,
  kApAlloc,
  kSkillLearn,
  // Enter on a skill in the Character panel's Skills tab: read it, switch it
  // on or off where it is a toggle, or walk away.
  kSkillMenu,
  kSkillInspect,
  // Every stat on one screen, from the Character panel's last stats row.
  kAllStats,
  // The Hyper tab's two questions: one rung of one stat, and the free reset
  // that takes a whole allocation back.
  kHyperAlloc,
  kHyperReset,
  // Enter on a Hyper Stat's name: what it is worth now and what the next
  // level would buy. Nothing to do but read it, as the skill card is.
  kHyperStatInspect,
  // Enter on a job in the Character panel's Advance tab: read it, take it, or
  // walk away. The screen it leads to reads the job's book without taking it.
  kJobMenu,
  kJobInspect,
  kJobAdvance,
  kStarForce,
  kStarForceResult,
  // Hammer on the item menu: the question, and the notice an item that will
  // take no more hammers answers with.
  kHammer,
  kHammerNotice,
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
  // Enter on the menu panel's Party entry: the parties open to be joined, or
  // the one the player is in.
  kPartySelect,
  // Enter on a member: read them, and for the leader kick them or hand the
  // party on.
  kPartyMenu,
  // The member behind Inspect: their stats, and what they are wearing.
  kPartyInspect,
  // Enter on one of those worn items: its card, the same one the player's own
  // items get.
  kPartyItemInspect,
  // The question one of those asks, and the one leaving a party asks.
  kPartyConfirm,
  // Enter on the menu panel's Boss entry: pick a fight, then fight it.
  kBossSelect,
  kBossConfirm,
  // Enter on a fight that cannot be taken -- no weapon, or its reset has not
  // come round. A notice, not a question.
  kBossNotice,
  kBossFight,
  kBossAbort,
  // The card a cleared fight ends on: what it paid, and one button to leave
  // it by.
  kBossClear,
  kShop,
  kShopMenu,
  kShopInspect,
  kShopBuy,
  // Enter on a menu panel entry that lists what it leads to: a box over the
  // corner, and the screens it opens.
  kMenuBox,
  kKeybinds,
  // Battle Analysis, from that box: what the stretch being measured is worth,
  // over the main screen it is being measured on.
  kAnalysis,
  // The card a returning player is met with: what their character earned while
  // the game was closed. Raised at launch and dismissed with one key.
  kOffline,
  // Escape on the main screen. Last because it is the one screen reachable
  // from the main view rather than from something on it.
  kQuit,
};
// Focusable panels of the main screen, in Tab order: clockwise from the
// top-left corner of the layout (Character, Equipped, Inventory, Menu,
// Combat). The values index Container::Tab's component list, so the two must
// stay in the same order.
enum Panel : int {
  // Not one of them: nobody is looking at a panel at all, because the screen
  // in front of the player is the shop, or map select, or a dialog over the
  // top of everything. Sorts before the real panels so a range check on them
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
  kMenuRecover = 6,
  // The two that part with the item, above Close. Neither sits where the
  // cursor lands: they are the entries on this menu there is no undoing.
  kMenuSell = 7,
  kMenuMultiSell = 8,
};
// Entries of the worn-gear context menu, on Enter in the Equipped panel's Gear
// tab. Shorter than the bag's: nothing worn is sold or combined, and only a
// bag item can be a trace to recover.
enum GearMenuItem : int {
  kGearMenuUnequip = 0,
  kGearMenuInspect = 1,
  // The three upgrades in the order a piece goes through them: scrolls fill
  // the shelf, a hammer widens it, and the stars wait for it to be full.
  kGearMenuScroll = 2,
  kGearMenuHammer = 3,
  kGearMenuStarForce = 4,
  kGearMenuClose = 5,
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
// Entries of the job context menu, on Enter in the Advance tab. Advance sits
// under Inspect for the same reason Sell sits low on the item menu: it is the
// entry there is no coming back from, so it is not where the cursor lands.
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
  kPartyMenuKick = 1,
  kPartyMenuPromote = 2,
  kPartyMenuClose = 3,
};
// The skill menu's entries. The middle one is a toggle skill's alone -- for
// every other skill it is hidden, there being nothing to switch.
enum SkillMenuItem : int {
  kSkillMenuInspect = 0,
  kSkillMenuToggle = 1,
  kSkillMenuClose = 2,
};
enum JobMenuItem : int {
  kJobMenuInspect = 0,
  kJobMenuAdvance = 1,
  kJobMenuClose = 2,
};
// Entries of the Use/Etc stackable context menu.
enum StackMenuItem : int {
  kStackInspect = 0,
  kStackUse = 1,
  kStackSell = 2,
  kStackMultiSell = 3,
  kStackClose = 4,
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
