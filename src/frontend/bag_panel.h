/* BagPanel shows the inventory as a navigable menu. Each entry displays the
 * item name, required level, applicable job categories, and remaining upgrade
 * slots. Enter opens the item context menu via the on_enter callback passed to
 * MakeComponent().
 *
 * Call MakeComponent() exactly once; the returned Component captures references
 * to internal state, so the panel object must outlive the Component.
 */
#ifndef MS_SRC_FRONTEND_BAG_PANEL_H_
#define MS_SRC_FRONTEND_BAG_PANEL_H_

#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "src/character.h"
#include "src/frontend/item_menu.h"
#include "src/frontend/types.h"
#include "src/protos/equip.pb.h"

namespace ms {

class ScrollPanel;

class BagPanel {
 public:
  BagPanel(CharacterInstance& character, int& panel_focus);
  ftxui::Component MakeComponent(std::function<void()> on_enter);
  void SetShowSelection(bool show);
  void OpenMenu();
  ItemMenu& menu() {
    return menu_;
  }
  // Handles Up/Down/Escape/Return for the item context menu and executes the
  // selected action. Returns the next screen state.
  Screen OnMenuEvent(ftxui::Event event, int& panel_focus,
                     ScrollPanel& scroll_panel);
  int selected() const {
    return selected_;
  }

 private:
  static std::string FormatJobCategories(const EquipPrototype& proto);

  CharacterInstance& character_;
  int& panel_focus_;
  bool show_selection_ = true;
  int selected_ = 0;
  std::vector<std::string> entries_;
  ItemMenu menu_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_BAG_PANEL_H_
