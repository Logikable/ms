#ifndef MS_SRC_FRONTEND_TYPES_H_
#define MS_SRC_FRONTEND_TYPES_H_

#include <string>

namespace ms {

enum Screen : int { kMain, kItemMenu, kInspect, kScrollSelect, kScrollResult };
enum Panel : int { kEquipPanel = 0, kBagPanel = 1 };
enum MenuItem : int { kMenuAction = 0, kMenuInspect = 1, kMenuScroll = 2 };
enum ScrollOutcome : int { kScrollSuccess, kScrollFail, kScrollNoSlots };

struct ScrollResult {
  ScrollOutcome outcome;
  std::string equip_name;
  std::string scroll_name;
  int slots_remaining = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TYPES_H_
