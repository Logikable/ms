/* InventoryPanel shows the inventory as a navigable menu. Each entry displays
 * the item name, required level, applicable job categories, and remaining
 * upgrade slots. Enter opens the item context menu via the on_enter callback
 * passed to MakeComponent().
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
  // selected action. Returns the next screen state.
  Screen OnMenuEvent(ftxui::Event event, int& panel_focus,
                     ScrollPanel& scroll_panel);

  ItemMenu& menu() {
    return menu_;
  }
  int selected() const {
    return selected_;
  }

 private:
  ftxui::Element RenderContent(ftxui::Component menu);

  CharacterInstance& character_;
  int& panel_focus_;
  int selected_ = 0;
  std::vector<InventoryRowState> rows_;
  std::vector<std::string>
      entries_;  // labels derived from rows_ for ftxui::Menu
  ItemMenu menu_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_INVENTORY_PANEL_H_
