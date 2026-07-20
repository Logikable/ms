#ifndef MS_SRC_FRONTEND_TYPES_H_
#define MS_SRC_FRONTEND_TYPES_H_

#include <string>

#include "src/equip_instance.h"
#include "src/protos/scroll.pb.h"

namespace ms {

enum Screen : int {
  kMain,
  kItemMenu,
  kInspect,
  kScrollSelect,
  kScrollResult,
  kApConfirm,
  kStarForce,
  kStarForceResult,
  kTraceRecover,
  kTraceRecoverResult,
  kSell,
  kMapSelect,
};
// Focusable panels of the main screen, in Tab order: clockwise from the
// top-left corner of the layout (Character, Equipped, Inventory, Combat). The
// values index Container::Tab's component list, so the two must stay in the
// same order.
enum Panel : int {
  kCharPanel = 0,
  kEquipPanel = 1,
  kInventoryPanel = 2,
  kCombatPanel = 3,
  kNumPanels
};
enum MenuItem : int {
  kMenuAction = 0,
  kMenuInspect = 1,
  kMenuScroll = 2,
  kMenuStarForce = 3,
  kMenuRecover = 4,
};
// Entries of the Use/Etc stackable context menu.
enum SellMenuItem : int {
  kSellSell = 0,
  kSellClose = 1,
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
