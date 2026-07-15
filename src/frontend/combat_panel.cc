#include "src/frontend/combat_panel.h"

#include <functional>
#include <map>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/combat/fight.h"
#include "src/frontend/colors.h"
#include "src/frontend/panel_util.h"
#include "src/frontend/types.h"
#include "src/game_state.h"
#include "src/protos/map.pb.h"

namespace ms {

CombatPanel::CombatPanel(const GameState& state, const CombatSim& sim,
                         int& panel_focus)
    : state_(state), sim_(sim), panel_focus_(panel_focus) {
}

std::string CombatPanel::MapName() const {
  std::map<std::string, MapData>::const_iterator it =
      state_.maps.find(state_.current_map);
  return it == state_.maps.end() ? "-" : it->second.name();
}

ftxui::Element CombatPanel::Render() const {
  // The header fixes the panel's width: a cursor column plus the map name,
  // padded to the full content width. The bars below stretch to match. The
  // cursor is the map's -- Enter on it travels -- so it shows only when the
  // panel holds focus, as in the equip and inventory lists.
  bool focused = panel_focus_ == kCombatPanel;
  ftxui::Element header = ftxui::text((focused ? "> " : "  ") +
                                      PadRight(MapName(), kContentWidth - 2));
  if (focused) {
    header = header | ftxui::focus;
  }

  if (!sim_.active()) {
    return ThemedWindow(" Combat ", ftxui::vbox({
                                        header,
                                        ThemedSeparator(),
                                        ftxui::text("Not fighting"),
                                    }));
  }

  // Charges over one swing; a full bar is the moment a hit lands.
  ftxui::Element attack =
      ProgressBar(static_cast<float>(sim_.attack_fraction()), kTheme, "");
  ftxui::Element target =
      sim_.respawning()
          ? ftxui::text("Respawning...")
          : ProgressBar(static_cast<float>(sim_.target_hp_fraction()), kRed,
                        sim_.target_name());

  return ThemedWindow(" Combat ", ftxui::vbox({
                                      header,
                                      ThemedSeparator(),
                                      attack,
                                      target,
                                  }));
}

ftxui::Component CombatPanel::MakeComponent(std::function<void()> on_travel) {
  // The Renderer(bool) overload is Focusable(), unlike Renderer() -- required
  // so Container::Tab's Focused() check passes on kCombatPanel.
  ftxui::Component renderer =
      ftxui::Renderer([this](bool /*focused*/) { return Render(); });
  return ftxui::CatchEvent(renderer, [this, on_travel](ftxui::Event event) {
    if (panel_focus_ == kCombatPanel && IsForward(event)) {
      on_travel();
      return true;
    }
    return false;
  });
}

}  // namespace ms
