#include "src/frontend/tui.h"

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/frontend/bag_panel.h"
#include "src/frontend/character_panel.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/item_menu.h"
#include "src/frontend/scroll_panel.h"
#include "src/game_state.h"

namespace ms {

Tui::Tui(GameState& state)
    : state_(state),
      char_panel_(state.character),
      equip_panel_(state.character, panel_focus_),
      bag_panel_(state.character, panel_focus_),
      equip_menu_({"Unequip", "Inspect", "Scroll"}),
      bag_menu_({"Equip", "Inspect", "Scroll"}),
      active_menu_(&equip_menu_),
      scroll_panel_(state.scrolls) {
}

void Tui::Run() {
  equip_component_ = equip_panel_.MakeComponent([this]() {
    screen_ = kItemMenu;
    active_menu_ = &equip_menu_;
    equip_menu_.Reset();
  });
  bag_component_ = bag_panel_.MakeComponent([this]() {
    screen_ = kItemMenu;
    active_menu_ = &bag_menu_;
    bag_menu_.Reset();
  });

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
  if (screen_ == kScrollSelect) {
    return scroll_component_->Render() | ftxui::flex;
  }
  equip_panel_.SetShowSelection(screen_ == kMain);
  bag_panel_.SetShowSelection(screen_ == kMain);
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
  if (screen_ != kItemMenu) {
    return layout;
  }
  int menu_row = 0;
  if (panel_focus_ == kEquipPanel) {
    menu_row = equip_panel_.selected();
  } else {
    menu_row = static_cast<int>(state_.character.equipped().size()) +
               bag_panel_.selected();
  }
  return ftxui::dbox({layout, active_menu_->Render(menu_row, 22)});
}

bool Tui::OnEvent(ftxui::Event event) {
  if (screen_ == kItemMenu) {
    if (event == ftxui::Event::Escape) {
      screen_ = kMain;
      return true;
    }
    if (event == ftxui::Event::ArrowUp) {
      active_menu_->Up();
      return true;
    }
    if (event == ftxui::Event::ArrowDown) {
      active_menu_->Down();
      return true;
    }
    if (event == ftxui::Event::Return) {
      if (panel_focus_ == kEquipPanel && active_menu_->selected() == 0) {
        state_.character.Unequip(equip_panel_.selected_slot());
        if (state_.character.equipped().empty()) {
          panel_focus_ = kBagPanel;
        }
        screen_ = kMain;
      } else if (panel_focus_ == kBagPanel && active_menu_->selected() == 0) {
        state_.character.Equip(bag_panel_.selected());
        if (state_.character.inventory().empty()) {
          panel_focus_ = kEquipPanel;
        }
        screen_ = kMain;
      } else if (panel_focus_ == kEquipPanel && active_menu_->selected() == 2) {
        scroll_slot_ = equip_panel_.selected_slot();
        screen_ = kScrollSelect;
      } else if (panel_focus_ == kBagPanel && active_menu_->selected() == 2) {
        scroll_index_ = bag_panel_.selected();
        screen_ = kScrollSelect;
      } else {
        screen_ = kMain;
      }
      return true;
    }
    return true;
  }
  if (screen_ == kScrollSelect) {
    if (event == ftxui::Event::Escape) {
      screen_ = kItemMenu;
      return true;
    }
    if (event == ftxui::Event::Return) {
      if (panel_focus_ == kEquipPanel) {
        state_.character.ScrollEquipped(scroll_slot_,
                                        scroll_panel_.selected_scroll());
      } else {
        state_.character.ScrollInventory(scroll_index_,
                                         scroll_panel_.selected_scroll());
      }
      screen_ = kMain;
      return true;
    }
    scroll_component_->OnEvent(event);
    return true;
  }
  if (event == ftxui::Event::Tab) {
    if (panel_focus_ == kEquipPanel) {
      panel_focus_ = kBagPanel;
    } else {
      panel_focus_ = kEquipPanel;
    }
    return true;
  }
  return false;
}

}  // namespace ms
