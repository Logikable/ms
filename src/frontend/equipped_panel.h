#ifndef MS_SRC_FRONTEND_EQUIPPED_PANEL_H_
#define MS_SRC_FRONTEND_EQUIPPED_PANEL_H_

#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "src/character.h"
#include "src/protos/equip.pb.h"

namespace ms {

struct EquippedPanelState {
  int selected = 0;
  std::vector<std::string> entries;
  std::vector<EquipSlot> slots;
};

ftxui::Component MakeEquippedPanel(CharacterInstance& character,
                                    int& panel_focus,
                                    EquippedPanelState& state);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_EQUIPPED_PANEL_H_
