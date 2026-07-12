#include "src/frontend/combat_panel.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/combat/constants.h"
#include "src/combat/fight.h"
#include "src/frontend/colors.h"
#include "src/frontend/panel_util.h"
#include "src/game_state.h"
#include "src/protos/map.pb.h"

namespace ms {
namespace {

// The heartbeat flips once per game tick: the 30ms action grain, stretched by
// the game's slowdown. It is a sign of life, not a readout -- it keeps ticking
// while the roster is dead and nothing else on the panel is moving.
constexpr int kHeartbeatMs = static_cast<int>(kTickMs * kGameSpeedFactor);

bool HeartbeatOn() {
  std::chrono::steady_clock::duration since_epoch =
      std::chrono::steady_clock::now().time_since_epoch();
  int64_t ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch)
          .count();
  return (ms / kHeartbeatMs) % 2 == 0;
}

}  // namespace

CombatPanel::CombatPanel(const GameState& state, const CombatSim& sim)
    : state_(state), sim_(sim) {
}

std::string CombatPanel::MapName() const {
  std::map<std::string, MapData>::const_iterator it =
      state_.maps.find(state_.current_map);
  return it == state_.maps.end() ? "-" : it->second.name();
}

ftxui::Element CombatPanel::Render() const {
  // The header fixes the panel's width: the map name padded out to everything
  // but the heartbeat's cell. The bars below stretch to match.
  ftxui::Element header = ftxui::hbox({
      ftxui::text(PadRight(MapName(), kContentWidth - 1)),
      ftxui::text(HeartbeatOn() ? "*" : " ") | ftxui::color(kTheme),
  });

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
