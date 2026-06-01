/* BagPanel shows the inventory as a navigable menu. Each entry displays the
 * item index, name, required level, and applicable job categories. Enter equips
 * the selected item and auto-switches panel focus if the bag becomes empty.
 *
 * Call MakeComponent() exactly once; the returned Component captures references
 * to internal state, so the panel object must outlive the Component.
 */
#ifndef MS_SRC_FRONTEND_BAG_PANEL_H_
#define MS_SRC_FRONTEND_BAG_PANEL_H_

#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "src/character.h"
#include "src/protos/equip.pb.h"

namespace ms {

class BagPanel {
 public:
  BagPanel(CharacterInstance& character, int& panel_focus);
  ftxui::Component MakeComponent();

 private:
  static std::string PadRight(const std::string& s, int width);
  static std::string PadLeft(const std::string& s, int width);
  static std::string FormatJobCategories(const EquipPrototype& proto);

  CharacterInstance& character_;
  int& panel_focus_;
  int selected_ = 0;
  std::vector<std::string> entries_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_BAG_PANEL_H_
