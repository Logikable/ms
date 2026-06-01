#include "src/frontend/bag_panel.h"

#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/frontend/util.h"
#include "src/protos/equip.pb.h"

namespace ms {

ftxui::Component MakeBagPanel(CharacterInstance& character, int& panel_focus,
                               BagPanelState& state) {
  ftxui::MenuOption opt;
  opt.on_enter = [&character, &panel_focus, &state]() {
    character.Equip(state.selected);
    state.selected = std::min(state.selected,
        std::max(0, (int)character.inventory().size() - 1));
    if (character.inventory().empty()) { panel_focus = 0; }
  };
  ftxui::Component menu = ftxui::Menu(&state.entries, &state.selected, opt);

  return ftxui::Renderer(menu, [&character, &state, menu]() -> ftxui::Element {
    state.entries.clear();
    for (const EquipInstance& item : character.inventory()) {
      const EquipPrototype& proto = item.prototype();
      int level = proto.required_level() > 0 ? proto.required_level() : 1;
      state.entries.push_back(
          "[" + PadLeft(std::to_string(state.entries.size()), 2) + "] " +
          PadRight(proto.name(), 18) +
          "  Lv" + PadRight(std::to_string(level), 3) +
          "  " + FormatJobCategories(proto));
    }
    if (state.entries.empty()) {
      return ftxui::window(ftxui::text(" Bag "), ftxui::text("(empty)"));
    }
    return ftxui::window(ftxui::text(" Bag "), menu->Render());
  });
}

}  // namespace ms
