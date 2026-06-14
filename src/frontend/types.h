#ifndef MS_SRC_FRONTEND_TYPES_H_
#define MS_SRC_FRONTEND_TYPES_H_

#include <string>

#include "src/equip_instance.h"

namespace ms {

enum Screen : int {
  kMain,
  kItemMenu,
  kInspect,
  kScrollSelect,
  kScrollResult,
  kApAlloc,
  kStarForce,
  kStarForceResult,
  kTraceRecover,
  kTraceRecoverResult,
};
enum Panel : int { kEquipPanel = 0, kBagPanel = 1, kCharPanel = 2, kNumPanels };
enum MenuItem : int {
  kMenuAction = 0,
  kMenuInspect = 1,
  kMenuScroll = 2,
  kMenuStarForce = 3,
  kMenuRecover = 4,
};
struct ScrollResult {
  ScrollOutcome outcome;
  std::string equip_name;
  std::string scroll_name;
  int slots_remaining = 0;
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
