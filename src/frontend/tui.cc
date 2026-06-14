#include "src/frontend/tui.h"

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
      char_panel_(state.character, panel_focus_),
      equip_panel_(state.character, panel_focus_),
      bag_panel_(state.character, panel_focus_),
      scroll_panel_(state.scrolls),
      ap_alloc_panel_(state.character),
      trace_recover_panel_(state.character),
      controller_(state, equip_panel_, bag_panel_, scroll_panel_,
                  ap_alloc_panel_, trace_recover_panel_, panel_focus_) {
}

void Tui::Run() {
  equip_component_ =
      equip_panel_.MakeComponent([this]() { controller_.OpenEquipMenu(); });
  bag_component_ =
      bag_panel_.MakeComponent([this]() { controller_.OpenBagMenu(); });
  char_component_ =
      char_panel_.MakeComponent([this]() { controller_.OpenApAlloc(); });

  ftxui::Component panels = ftxui::Container::Tab(
      {equip_component_, bag_component_, char_component_}, &panel_focus_);

  ftxui::Component base = ftxui::Renderer(
      panels, [this]() -> ftxui::Element { return RenderFrame(); });

  ftxui::Component root = ftxui::CatchEvent(
      base, [this](ftxui::Event event) -> bool { return OnEvent(event); });

  ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
  screen.Loop(root);
}

ftxui::Element Tui::RenderFrame() {
  if (controller_.screen() == kApAlloc) {
    return ftxui::center(ap_alloc_panel_.Render());
  }
  if (controller_.screen() == kStarForce) {
    star_force_panel_.SetItem(controller_.star_force_item());
    return ftxui::center(star_force_panel_.Render());
  }
  if (controller_.screen() == kStarForceResult) {
    return ftxui::center(
        star_force_panel_.RenderResult(controller_.star_force_result()));
  }
  if (controller_.screen() == kTraceRecover) {
    int base_idx = trace_recover_panel_.selected_index();
    inspect_panel_.SetItem(
        base_idx >= 0 ? &state_.character.inventory()[base_idx] : nullptr);
    return ftxui::hbox({trace_recover_panel_.Render() | ftxui::flex,
                        inspect_panel_.Render() | ftxui::flex});
  }
  if (controller_.screen() == kTraceRecoverResult) {
    return ftxui::center(
        trace_recover_panel_.RenderResult(controller_.trace_recovery_result()));
  }
  if (controller_.screen() == kInspect) {
    inspect_panel_.SetItem(controller_.inspect_item());
    return ftxui::hbox({
        ftxui::filler(),
        inspect_panel_.Render(),
        ftxui::filler(),
    });
  }
  if (controller_.screen() == kScrollSelect ||
      controller_.screen() == kScrollResult) {
    inspect_panel_.SetItem(controller_.scroll_item());
    ftxui::Element scroll_view = scroll_panel_.Render();
    if (controller_.screen() == kScrollResult) {
      ftxui::Element dialog =
          scroll_panel_.RenderResult(controller_.scroll_result());
      scroll_view = ftxui::dbox(
          {scroll_view, ftxui::center(dialog | ftxui::clear_under)});
    }
    return ftxui::hbox(
        {scroll_view | ftxui::flex, inspect_panel_.Render() | ftxui::flex});
  }
  ftxui::Element layout = ftxui::vbox({
      ftxui::hbox({
          char_panel_.Render(),
          ftxui::vbox({
              equip_component_->Render(),
              bag_component_->Render(),
          }) | ftxui::flex,
      }),
      ftxui::filler(),
  });
  if (controller_.screen() != kItemMenu) {
    return layout;
  }
  int menu_row = 0;
  if (panel_focus_ == kEquipPanel) {
    // +2 for equip column header + separator above items.
    menu_row = 2 + equip_panel_.selected();
  } else {
    int equip_count = static_cast<int>(state_.character.equipped().size());
    // Non-empty equip panel adds header + separator; empty panel has neither.
    int equip_rows = std::max(1, equip_count) + (equip_count > 0 ? 2 : 0);
    // +4: equip borders (2) + bag column header + separator (2).
    menu_row = equip_rows + 4 + bag_panel_.selected();
  }
  ItemMenu& menu =
      panel_focus_ == kEquipPanel ? equip_panel_.menu() : bag_panel_.menu();
  // Offset past char panel border, menu cursor, name column, slot column, and
  // separators so the menu covers stats rather than item names.
  constexpr int kMenuCol =
      CharacterPanel::kTotalWidth + 1 + 2 + 18 + 2 + 10 + 2;
  return ftxui::dbox({layout, menu.Render(menu_row, kMenuCol)});
}

bool Tui::OnEvent(ftxui::Event event) {
  if (!controller_.OnEvent(event)) {
    if (controller_.screen() == kScrollSelect) {
      scroll_panel_.OnEvent(event);
      return true;
    }
    return false;
  }
  return true;
}

}  // namespace ms
