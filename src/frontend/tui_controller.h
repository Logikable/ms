/* TuiController owns the screen-state machine for the TUI. It handles
 * keyboard events and drives transitions between kMain, kItemMenu, and
 * kScrollSelect. Tui holds a TuiController and delegates event handling;
 * tests can construct TuiController directly without the ftxui event loop.
 *
 * panel_focus is owned by the caller and shared with the panel components
 * so Container::Tab can read it; TuiController mutates it on Tab and when
 * an action leaves one panel empty.
 */
#ifndef MS_SRC_FRONTEND_TUI_CONTROLLER_H_
#define MS_SRC_FRONTEND_TUI_CONTROLLER_H_

#include <string>

#include "ftxui/component/event.hpp"
#include "src/frontend/bag_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/item_menu.h"
#include "src/frontend/scroll_panel.h"
#include "src/game_state.h"
#include "src/protos/equip.pb.h"

namespace ms {

enum Screen : int { kMain, kItemMenu, kScrollSelect, kScrollResult };
enum Panel : int { kEquipPanel = 0, kBagPanel = 1 };
enum MenuItem : int { kMenuAction = 0, kMenuInspect = 1, kMenuScroll = 2 };
enum ScrollOutcome : int { kScrollSuccess, kScrollFail, kScrollNoSlots };

struct ScrollResult {
  ScrollOutcome outcome;
  std::string equip_name;
  std::string scroll_name;
  int slots_remaining = 0;
};

class TuiController {
 public:
  // panel_focus is a reference shared with panel components and
  // Container::Tab; the controller mutates it as focus changes.
  TuiController(GameState& state, EquippedPanel& equip_panel,
                BagPanel& bag_panel, ScrollPanel& scroll_panel,
                int& panel_focus);

  // Open the equip or bag context menu. Called from MakeComponent callbacks.
  void OpenEquipMenu();
  void OpenBagMenu();

  // Returns true if the event was consumed. Returns false for navigation
  // events in kScrollSelect; the caller should forward those to the scroll
  // component.
  bool OnEvent(ftxui::Event event);

  Screen screen() const {
    return screen_;
  }
  const ScrollResult& scroll_result() const {
    return scroll_result_;
  }
  // Non-const: RenderFrame calls active_menu().Render().
  ItemMenu& active_menu() {
    return *active_menu_;
  }

 private:
  // Returns pointers into state.scrolls for scrolls applicable to proto.
  // A scroll matches if its applicable_job_categories overlaps with the
  // prototype's equip_job_categories.
  static std::vector<const Scroll*> FilterScrolls(
      const EquipPrototype& proto,
      const std::map<std::string, Scroll>& scrolls);

  GameState& state_;
  EquippedPanel& equip_panel_;
  BagPanel& bag_panel_;
  ScrollPanel& scroll_panel_;
  int& panel_focus_;
  Screen screen_ = kMain;
  ItemMenu equip_menu_;
  ItemMenu bag_menu_;
  // Points to whichever menu was opened last. Initialized to equip_menu_
  // and reassigned in OpenEquipMenu/OpenBagMenu.
  ItemMenu* active_menu_ = nullptr;
  EquipSlot scroll_slot_ = EQUIP_SLOT_UNSPECIFIED;
  int scroll_index_ = 0;
  ScrollResult scroll_result_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TUI_CONTROLLER_H_
