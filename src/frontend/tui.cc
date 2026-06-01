#include "src/frontend/tui.h"

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/frontend/bag_panel.h"
#include "src/frontend/character_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/item_menu.h"
#include "src/game_state.h"

namespace ms {

namespace {

enum TuiMode { kMain, kItemMenu };

}  // namespace

void RunTui(GameState& state) {
  int panel_focus = 0;
  TuiMode mode = kMain;

  CharacterPanel char_panel(state.character);
  EquippedPanel equip_panel(state.character, panel_focus);
  BagPanel bag_panel(state.character, panel_focus);

  ItemMenu equip_menu({"Unequip", "Inspect", "Scroll"});
  ItemMenu bag_menu({"Equip", "Inspect", "Scroll"});
  ItemMenu* active_menu = &equip_menu;

  ftxui::Component equip_component = equip_panel.MakeComponent([&]() {
    mode = kItemMenu;
    active_menu = &equip_menu;
    equip_menu.Reset();
  });
  ftxui::Component bag_component = bag_panel.MakeComponent([&]() {
    mode = kItemMenu;
    active_menu = &bag_menu;
    bag_menu.Reset();
  });

  ftxui::Component panels =
      ftxui::Container::Tab({equip_component, bag_component}, &panel_focus);

  ftxui::Component root = ftxui::CatchEvent(
      ftxui::Renderer(
          panels,
          [&]() -> ftxui::Element {
            ftxui::Element layout = ftxui::vbox({
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
            if (mode != kItemMenu) {
              return layout;
            }
            int menu_row = 0;
            if (panel_focus == 0) {
              menu_row = equip_panel.selected();
            } else {
              menu_row = static_cast<int>(state.character.equipped().size()) +
                         bag_panel.selected();
            }
            return ftxui::dbox({layout, active_menu->Render(menu_row, 22)});
          }),
      [&](ftxui::Event event) -> bool {
        if (mode == kItemMenu) {
          if (event == ftxui::Event::Escape) {
            mode = kMain;
            return true;
          }
          if (event == ftxui::Event::ArrowUp) {
            active_menu->Up();
            return true;
          }
          if (event == ftxui::Event::ArrowDown) {
            active_menu->Down();
            return true;
          }
          if (event == ftxui::Event::Return) {
            if (panel_focus == 0 && active_menu->selected() == 0) {
              state.character.Unequip(equip_panel.selected_slot());
              if (state.character.equipped().empty()) {
                panel_focus = 1;
              }
            }
            if (panel_focus == 1 && active_menu->selected() == 0) {
              state.character.Equip(bag_panel.selected());
              if (state.character.inventory().empty()) {
                panel_focus = 0;
              }
            }
            mode = kMain;
            return true;
          }
          return true;
        }
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
