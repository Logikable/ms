#include "src/frontend/tui.h"

#include <random>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/bag_panel.h"
#include "src/frontend/character_panel.h"
#include "src/frontend/equipped_panel.h"

namespace ms {

void RunTui(CharacterInstance& character,
            const std::vector<Scroll>& /*scrolls*/,
            std::mt19937& /*rng*/) {
  int panel_focus = 0;
  EquippedPanelState equip_state;
  BagPanelState bag_state;

  ftxui::Component equip_panel = MakeEquippedPanel(character, panel_focus, equip_state);
  ftxui::Component bag_panel = MakeBagPanel(character, panel_focus, bag_state);

  ftxui::Component panels = ftxui::Container::Tab(
      {equip_panel, bag_panel}, &panel_focus);

  ftxui::Component root = ftxui::CatchEvent(
      ftxui::Renderer(panels, [&]() -> ftxui::Element {
        return ftxui::vbox({
          ftxui::hbox({
            CharacterElement(character) |
                ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 22),
            ftxui::vbox({
              equip_panel->Render(),
              bag_panel->Render(),
            }) | ftxui::flex,
          }),
          ftxui::filler(),
        });
      }),
      [&](ftxui::Event event) -> bool {
        if (event == ftxui::Event::Tab) {
          panel_focus = (panel_focus + 1) % 2;
          return true;
        }
        return false;
      });

  ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
  screen.Loop(root);
}

}  // namespace ms
