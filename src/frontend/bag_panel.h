#ifndef MS_SRC_FRONTEND_BAG_PANEL_H_
#define MS_SRC_FRONTEND_BAG_PANEL_H_

#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "src/character.h"

namespace ms {

struct BagPanelState {
  int selected = 0;
  std::vector<std::string> entries;
};

ftxui::Component MakeBagPanel(CharacterInstance& character, int& panel_focus,
                               BagPanelState& state);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_BAG_PANEL_H_
