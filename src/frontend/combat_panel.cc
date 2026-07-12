#include "src/frontend/combat_panel.h"

#include <map>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/combat/fight.h"
#include "src/frontend/colors.h"
#include "src/frontend/panel_util.h"
#include "src/game_state.h"
#include "src/protos/map.pb.h"

namespace ms {

CombatPanel::CombatPanel(const GameState& state, const CombatSim& sim)
    : state_(state), sim_(sim) {
}

std::string CombatPanel::MapName() const {
  std::map<std::string, MapData>::const_iterator it =
      state_.maps.find(state_.current_map);
  return it == state_.maps.end() ? "-" : it->second.name();
}

ftxui::Element CombatPanel::Render() const {
  // The header fixes the panel's width: the map name padded to the full content
  // width. The bars below stretch to match.
  ftxui::Element header = ftxui::text(PadRight(MapName(), kContentWidth));

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

}  // namespace ms
