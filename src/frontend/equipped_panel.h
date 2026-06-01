/* EquippedPanel shows currently equipped items as a navigable menu. Each entry
 * displays name, stat bonuses, and remaining upgrade slots. Enter unequips the
 * selected item and auto-switches panel focus if the list becomes empty.
 *
 * Call MakeComponent() exactly once; the returned Component captures references
 * to internal state, so the panel object must outlive the Component.
 */
#ifndef MS_SRC_FRONTEND_EQUIPPED_PANEL_H_
#define MS_SRC_FRONTEND_EQUIPPED_PANEL_H_

#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "src/character.h"
#include "src/protos/equip.pb.h"

namespace ms {

class EquippedPanel {
 public:
  EquippedPanel(CharacterInstance& character, int& panel_focus);
  ftxui::Component MakeComponent();

 private:
  static std::string PadRight(const std::string& s, int width);
  static void AppendStat(std::string& out, int val, const std::string& name);

  CharacterInstance& character_;
  int& panel_focus_;
  int selected_ = 0;
  std::vector<std::string> entries_;
  std::vector<EquipSlot> slots_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_EQUIPPED_PANEL_H_
