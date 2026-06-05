#include "src/frontend/tui.h"

#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/frontend/bag_panel.h"
#include "src/frontend/character_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/item_menu.h"
#include "src/frontend/scroll_panel.h"
#include "src/frontend/tui_controller.h"
#include "src/game_state.h"

namespace ms {

Tui::Tui(GameState& state)
    : state_(state),
      char_panel_(state.character),
      equip_panel_(state.character, panel_focus_),
      bag_panel_(state.character, panel_focus_),
      scroll_panel_(state.scrolls),
      controller_(state, equip_panel_, bag_panel_, scroll_panel_,
                  panel_focus_) {
}

void Tui::Run() {
  equip_component_ =
      equip_panel_.MakeComponent([this]() { controller_.OpenEquipMenu(); });
  bag_component_ =
      bag_panel_.MakeComponent([this]() { controller_.OpenBagMenu(); });

  scroll_component_ = scroll_panel_.MakeComponent();

  ftxui::Component panels =
      ftxui::Container::Tab({equip_component_, bag_component_}, &panel_focus_);

  ftxui::Component base = ftxui::Renderer(
      panels, [this]() -> ftxui::Element { return RenderFrame(); });

  ftxui::Component root = ftxui::CatchEvent(
      base, [this](ftxui::Event event) -> bool { return OnEvent(event); });

  ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
  screen.Loop(root);
}

ftxui::Element Tui::RenderFrame() {
  if (controller_.screen() == kScrollSelect) {
    return scroll_component_->Render() | ftxui::flex;
  }
  equip_panel_.SetShowSelection(controller_.screen() == kMain);
  bag_panel_.SetShowSelection(controller_.screen() == kMain);
  ftxui::Element layout = ftxui::vbox({
      ftxui::hbox({
          char_panel_.Render() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 22),
          ftxui::vbox({
              equip_component_->Render(),
              bag_component_->Render(),
          }) | ftxui::flex,
      }),
      ftxui::filler(),
  });
  if (controller_.screen() == kScrollResult) {
    const ScrollResult& r = controller_.scroll_result();
    ftxui::Element dialog = ftxui::window(
        ftxui::text(" Result "),
        ftxui::vbox({
            ftxui::text(r.equip_name + "  |  " + r.scroll_name),
            ftxui::separator(),
            ftxui::text(r.success ? "SUCCESS" : "FAILED") | ftxui::hcenter,
            ftxui::text(std::to_string(r.slots_remaining) + " slots remaining") |
                ftxui::hcenter,
            ftxui::text(""),
            ftxui::text("Press Enter to continue"),
        }));
    return ftxui::dbox({layout, ftxui::center(dialog) | ftxui::clear_under});
  }
  if (controller_.screen() != kItemMenu) {
    return layout;
  }
  int menu_row = 0;
  if (panel_focus_ == kEquipPanel) {
    menu_row = equip_panel_.selected();
  } else {
    menu_row = static_cast<int>(state_.character.equipped().size()) +
               bag_panel_.selected();
  }
  return ftxui::dbox({layout, controller_.active_menu().Render(menu_row, 22)});
}

bool Tui::OnEvent(ftxui::Event event) {
  if (!controller_.OnEvent(event)) {
    if (controller_.screen() == kScrollSelect) {
      scroll_component_->OnEvent(event);
      return true;
    }
    return false;
  }
  return true;
}

}  // namespace ms
