#include "src/frontend/equipped_panel.h"

#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/frontend/util.h"
#include "src/protos/equip.pb.h"

namespace ms {

ftxui::Component MakeEquippedPanel(CharacterInstance& character,
                                    int& panel_focus,
                                    EquippedPanelState& state) {
  ftxui::MenuOption opt;
  opt.on_enter = [&character, &panel_focus, &state]() {
    if ((int)state.slots.size() > state.selected) {
      character.Unequip(state.slots[state.selected]);
      state.selected = std::min(state.selected,
          std::max(0, (int)character.equipped().size() - 1));
      if (character.equipped().empty()) { panel_focus = 1; }
    }
  };
  ftxui::Component menu = ftxui::Menu(&state.entries, &state.selected, opt);

  return ftxui::Renderer(menu, [&character, &state, menu]() -> ftxui::Element {
    state.entries.clear();
    state.slots.clear();
    for (const std::pair<const EquipSlot, EquipInstance>& kv : character.equipped()) {
      state.slots.push_back(kv.first);
      const EquipInstance& item = kv.second;
      const EquipStats stats = item.stats();
      std::string line = PadRight(item.prototype().name(), 18) + "  ";
      AppendStat(line, stats.attack(), "ATT");
      AppendStat(line, stats.magic_attack(), "MATT");
      AppendStat(line, stats.str(), "STR");
      AppendStat(line, stats.dex(), "DEX");
      AppendStat(line, stats.int_(), "INT");
      AppendStat(line, stats.luk(), "LUK");
      line += "  " + std::to_string(item.proto().remaining_upgrade_slots()) + " slots";
      state.entries.push_back(line);
    }
    if (state.entries.empty()) {
      return ftxui::window(ftxui::text(" Equipped "), ftxui::text("(empty)"));
    }
    return ftxui::window(ftxui::text(" Equipped "), menu->Render());
  });
}

}  // namespace ms
