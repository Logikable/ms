#include "src/frontend/panels/combat_panel.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/combat/fight.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/marquee.h"
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

int CombatPanel::Height() const {
  // Border, map row, rule, then either the "Not fighting" line or the two bars
  // the player always has, one bar per mob type they are fighting, and the
  // respawn bar below them.
  int rows = 2 + 1 + 1;
  if (!sim_.active()) {
    return rows + 1;
  }
  rows += 2;
  rows += sim_.respawning()
              ? 1
              : static_cast<int>(sim_.view().engaged_groups.size());
  return rows + (sim_.view().respawns ? 1 : 0);
}

ftxui::Element CombatPanel::Render() const {
  // The header sets the panel's width: a cursor column plus the map name,
  // padded to the full content width. The bars below stretch to match. The
  // cursor belongs to the map (Enter on it travels), so it shows only while the
  // panel has focus, as in the equip and inventory lists.
  bool focused = panel_focus_ == kCombatPanel;
  // The longest map name is a column wider than the row, so it scrolls while
  // the panel has focus.
  name_clock_.Follow(
      static_cast<int>(std::hash<std::string>{}(state_.current_map)), focused);
  ftxui::Element header = ftxui::text(
      (focused ? "> " : "  ") +
      ScrollingWindow(MapName(), ContentWidth() - 2, name_clock_.Elapsed()));
  if (focused) {
    header = header | ftxui::focus;
  }

  if (!sim_.active()) {
    return ThemedWindow(" Combat ",
                        ftxui::vbox({
                            header,
                            ThemedSeparator(),
                            ftxui::text(" Not fighting"),
                        }),
                        focused);
  }

  // The player's own bar comes first, above the attack they are charging and
  // the mobs they are attacking. Read down, it goes: the player, their attack,
  // then what is hitting back. It is green so it can't be mistaken for the red
  // mob bars below it.
  std::string hp_label = "HP " + std::to_string(sim_.view().player_hp) + " / " +
                         std::to_string(sim_.view().player_max_hp);
  // Fills over one attack, and a full bar is when the hit lands. It is labelled
  // with the attack being charged ("Attack" for the basic attack, otherwise the
  // skill), with dots at the ends for the active buffs.
  std::vector<ftxui::Element> rows = {
      header,
      ThemedSeparator(),
      ProgressBar(static_cast<float>(sim_.view().player_hp_fraction), kGreen,
                  hp_label, ftxui::Color::White),
      ProgressBar(
          static_cast<float>(sim_.view().attack_fraction), kTheme,
          sim_.view().attack_name, ftxui::Color::White,
          state_.account.buff_indicators() ? sim_.view().buff_count : 0),
  };
  if (sim_.respawning()) {
    rows.push_back(ftxui::text(" Respawning..."));
  } else {
    // One HP bar per engaged mob type, with white text all the way across
    // instead of dark text on the fill. White reads well on kRed, and the name
    // shouldn't change colour a letter at a time as health drains. The level
    // comes before the name, and "xN" follows when several mobs of the type
    // share the bar's average.
    for (const EngagedGroup& group : sim_.view().engaged_groups) {
      std::string label =
          "Lv." + std::to_string(group.level) + " " + group.name;
      if (group.count > 1) {
        label += " x" + std::to_string(group.count);
      }
      rows.push_back(ProgressBar(static_cast<float>(group.hp_fraction), kRed,
                                 label, ftxui::Color::White));
    }
  }
  // The respawn bar comes last, under the mobs it is about to refill. It is
  // orange because the other three colours are taken, and it is the one timer
  // here that belongs to the map rather than to a combatant.
  if (sim_.view().respawns) {
    rows.push_back(ProgressBar(static_cast<float>(sim_.view().respawn_fraction),
                               kOrange, "Respawn", ftxui::Color::White));
  }

  return ThemedWindow(" Combat ", ftxui::vbox(std::move(rows)), focused,
                      state_.account.panel_title_blink());
}

ftxui::Component CombatPanel::MakeComponent(std::function<void()> on_travel) {
  // The Renderer(bool) overload is Focusable(), unlike Renderer(). It is needed
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
