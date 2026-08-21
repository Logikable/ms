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
  kSkillInspect,
  // Every stat on one screen, from the Character panel's last stats row.
  kAllStats,
  // Enter on a job in the Character panel's Advance tab: read it, take it, or
  // walk away. The screen it leads to reads the job's book without taking it.
  kJobMenu,
  kJobInspect,
  kJobAdvance,
  kStarForce,
  kStarForceResult,
  kTraceRecover,
  kTraceRecoverResult,
  kSell,
  // The equip tab's counterpart to kSell. Equipment does not stack, so it
  // asks a yes/no question rather than for an amount.
  kSellEquip,
  kMapSelect,
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
  kMenuScroll = 2,
  kMenuStarForce = 3,
  kMenuRecover = 4,
  // Second to last, above Close. It is the one entry on this menu that
  // destroys the item, so it does not sit where the cursor lands.
  kMenuSell = 5,
};
// Entries of the job context menu, on Enter in the Advance tab. Advance sits
// under Inspect for the same reason Sell sits low on the item menu: it is the
// entry there is no coming back from, so it is not where the cursor lands.
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
