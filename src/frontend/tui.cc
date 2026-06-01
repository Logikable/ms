#include "src/frontend/tui.h"

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/frontend/bag_panel.h"
#include "src/frontend/character_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/game_state.h"

namespace ms {

void RunTui(GameState& state) {
  int panel_focus = 0;

  CharacterPanel char_panel(state.character);
  EquippedPanel equip_panel(state.character, panel_focus);
  BagPanel bag_panel(state.character, panel_focus);

  ftxui::Component equip_component = equip_panel.MakeComponent();
  ftxui::Component bag_component = bag_panel.MakeComponent();

  ftxui::Component panels =
      ftxui::Container::Tab({equip_component, bag_component}, &panel_focus);

  ftxui::Component root = ftxui::CatchEvent(
      ftxui::Renderer(panels,
                      [&]() -> ftxui::Element {
                        return ftxui::vbox({
                            ftxui::hbox({
                                char_panel.Render() |
                                    ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 22),
                                ftxui::vbox({
                                    equip_component->Render(),
                                    bag_component->Render(),
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
