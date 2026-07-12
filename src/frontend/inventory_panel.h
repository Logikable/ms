/* InventoryPanel shows the character's inventory as three tabs: Equip (equip-
 * tab items as a navigable menu), Use, and Etc (read-only stackable lists).
 * Left/Right switch tabs. On the Equip tab, Enter opens the item context menu
 * via the on_enter callback passed to MakeComponent(); the Use and Etc tabs
 * have no actions yet.
 *
 * Call MakeComponent() exactly once; the returned Component captures references
 * to internal state, so the panel object must outlive the Component.
 */
#ifndef MS_SRC_FRONTEND_INVENTORY_PANEL_H_
#define MS_SRC_FRONTEND_INVENTORY_PANEL_H_

#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "src/character.h"
#include "src/frontend/item_menu.h"
#include "src/frontend/scroll_panel.h"
#include "src/frontend/types.h"
#include "src/protos/equip.pb.h"

namespace ms {

struct InventoryRowState {
  std::string label;
  bool is_trace;
  bool level_ok;
  bool job_ok;
};

class InventoryPanel {
 public:
  InventoryPanel(CharacterInstance& character, int& panel_focus);
  ftxui::Component MakeComponent(std::function<void()> on_enter);
  void OpenMenu();
  // Handles Up/Down/Escape/Return for the item context menu and executes the
  // selected action. Returns the next screen state. On the Equip tab this
  // drives the equip menu; on Use/Etc it drives the {Sell, Close} menu.
  Screen OnMenuEvent(ftxui::Event event, int& panel_focus,
                     ScrollPanel& scroll_panel);

  // The context menu for the active tab: the equip menu on Equip, the sell menu
  // on Use/Etc.
  ItemMenu& menu();
  int selected() const {
    return selected_;
  }

 private:
  // Wraps the active tab's body in the titled window with the tab bar on top.
  ftxui::Element RenderContent(ftxui::Component menu);
  // Rebuilds rows_/entries_ from the equip inventory and returns the Equip tab
  // body (column headers + the navigable menu, or "(empty)").
  ftxui::Element RenderEquipList(ftxui::Component menu);

  CharacterInstance& character_;
  int& panel_focus_;
  int selected_ = 0;        // selected row on the Equip tab (ftxui::Menu index)
  int selected_stack_ = 0;  // selected row on the active Use/Etc tab
  int active_tab_ = 0;      // 0 = Equip, 1 = Use, 2 = Etc
  std::vector<InventoryRowState> rows_;
  std::vector<std::string>
      entries_;         // labels derived from rows_ for ftxui::Menu
  ItemMenu menu_;       // Equip tab context menu.
  ItemMenu sell_menu_;  // Use/Etc tab context menu.
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_INVENTORY_PANEL_H_
