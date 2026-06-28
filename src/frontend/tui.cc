#include "src/frontend/tui.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character.h"
#include "src/equip_instance.h"
#include "src/exp_table.h"
#include "src/frontend/bag_panel.h"
#include "src/frontend/character_panel.h"
#include "src/frontend/colors.h"
#include "src/frontend/equipped_panel.h"
#include "src/frontend/item_menu.h"
#include "src/frontend/scroll_panel.h"
#include "src/frontend/tui_controller.h"
#include "src/game_state.h"

namespace ms {
namespace {

std::string FormatCommas(int64_t n) {
  std::string s = std::to_string(n);
  int pos = static_cast<int>(s.size()) - 3;
  while (pos > 0) {
    s.insert(pos, ",");
    pos -= 3;
  }
  return s;
}

// Returns decimal places for EXP percentage display, scaled by job tier.
int ExpPctDecimals(int level) {
  if (level < 60) {
    return 0;
  }
  if (level < 100) {
    return 1;
  }
  if (level < 200) {
    return 2;
  }
  if (level < 260) {
    return 3;
  }
  return 4;
}

// Custom element: renders a progress bar by writing directly to screen pixels.
// Fills with background colors (filled=kTheme, unfilled=dark) so the gauge and
// label don't interfere with each other via dbox character overwriting.
class ExpBarNode : public ftxui::Node {
 public:
  ExpBarNode(float frac, std::string label)
      : frac_(frac), label_(std::move(label)) {
  }

  void ComputeRequirement() override {
    requirement_.min_x = 1;
    requirement_.min_y = 1;
  }

  void Render(ftxui::Screen& screen) override {
    const int y = box_.y_min;
    const int width = box_.x_max - box_.x_min + 1;
    const int fill_end = box_.x_min + static_cast<int>(frac_ * width);

    for (int x = box_.x_min; x <= box_.x_max; ++x) {
      ftxui::Pixel& px = screen.PixelAt(x, y);
      px.character = " ";
      px.background_color =
          x < fill_end ? kTheme : ftxui::Color::RGB(20, 35, 55);
    }

    const int label_len = static_cast<int>(label_.size());
    const int label_x = box_.x_min + (width - label_len) / 2;
    for (int i = 0; i < label_len; ++i) {
      int x = label_x + i;
      if (x < box_.x_min || x > box_.x_max) {
        continue;
      }
      ftxui::Pixel& px = screen.PixelAt(x, y);
      px.character = std::string(1, label_[i]);
      px.foreground_color =
          x < fill_end ? ftxui::Color::Black : ftxui::Color::White;
    }
  }

 private:
  float frac_;
  std::string label_;
};

}  // namespace

Tui::Tui(GameState& state)
    : state_(state),
      last_farming_update_(std::chrono::steady_clock::now()),
      char_panel_(state.character, panel_focus_),
      equip_panel_(state.character, panel_focus_),
      bag_panel_(state.character, panel_focus_),
      scroll_panel_(state.scrolls),
      ap_alloc_panel_(state.character),
      trace_recover_panel_(state.character),
      controller_(state, equip_panel_, bag_panel_, scroll_panel_,
                  ap_alloc_panel_, star_force_panel_, trace_recover_panel_,
                  panel_focus_) {
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

  ftxui::Component root =
      ftxui::CatchEvent(base, [this](ftxui::Event event) -> bool {
        if (event.is_mouse()) {
          return true;
        }
        return OnEvent(event);
      });

  ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();

  // Drive the idle game: wake periodically, advance farming on the loop thread
  // (so state mutation stays single-threaded), and redraw.
  std::atomic<bool> running = true;
  std::thread ticker([this, &screen, &running]() {
    while (running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      screen.Post([this]() { AdvanceFarmingTick(); });
      screen.PostEvent(ftxui::Event::Custom);
    }
  });

  screen.Loop(root);
  running = false;
  ticker.join();
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
    EquipInstance preview = trace_recover_panel_.PreviewResult();
    trace_inspect_panel_.SetItem(&preview);
    int base_idx = trace_recover_panel_.selected_index();
    inspect_panel_.SetItem(
        base_idx >= 0 ? &state_.character.inventory()[base_idx] : nullptr);
    ftxui::Element right_col = ftxui::vbox({
        trace_recover_panel_.RenderTabs(),
        inspect_panel_.Render(),
        trace_recover_panel_.RenderBelow(),
    });
    return ftxui::hbox({trace_inspect_panel_.Render() | ftxui::flex,
                        std::move(right_col) | ftxui::flex});
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
  return RenderMain();
}

ftxui::Element Tui::RenderMain() {
  ftxui::Element layout = ftxui::vbox({
      ftxui::hbox({
          char_panel_.Render(),
          ftxui::vbox({
              equip_component_->Render(),
              bag_component_->Render(),
          }) | ftxui::flex,
      }),
      ftxui::filler(),
      RenderExpBar(),
  });
  if (controller_.screen() != kItemMenu) {
    return layout;
  }
  int menu_row = 0;
  if (panel_focus_ == kEquipPanel) {
    // +3 for equip column header + sub-header + separator above items.
    menu_row = 3 + equip_panel_.selected();
  } else {
    int equip_count = static_cast<int>(state_.character.equipped().size());
    // Non-empty equip panel adds header + sub-header + separator; empty has
    // neither.
    int equip_rows = std::max(1, equip_count) + (equip_count > 0 ? 3 : 0);
    // +5: equip borders (2) + bag column header + sub-header + separator (3).
    menu_row = equip_rows + 5 + bag_panel_.selected();
  }
  ItemMenu& menu =
      panel_focus_ == kEquipPanel ? equip_panel_.menu() : bag_panel_.menu();
  // Offset past char panel border, menu cursor, name column, slot column, and
  // separators so the menu covers stats rather than item names.
  constexpr int kMenuCol =
      CharacterPanel::kTotalWidth + 1 + 2 + 18 + 2 + 10 + 2;
  return ftxui::dbox({layout, menu.Render(menu_row, kMenuCol)});
}

ftxui::Element Tui::RenderExpBar() {
  const Character& p = state_.character.proto();
  std::string label;
  float frac;
  if (p.level() >= kMaxLevel) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.*f%%", ExpPctDecimals(p.level()), 0.0);
    label = std::string("0 (") + buf + ")";
    frac = 0.0f;
  } else {
    int64_t exp = p.exp();
    int64_t tnl = ExpToNextLevel(p.level());
    frac = tnl > 0 ? static_cast<float>(exp) / static_cast<float>(tnl) : 0.0f;
    double pct =
        tnl > 0 ? static_cast<double>(exp) * 100.0 / static_cast<double>(tnl)
                : 0.0;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.*f%%", ExpPctDecimals(p.level()), pct);
    label = FormatCommas(exp) + " (" + buf + ")";
  }
  return std::make_shared<ExpBarNode>(frac, std::move(label));
}

void Tui::AdvanceFarmingTick() {
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed = now - last_farming_update_;
  last_farming_update_ = now;
  state_.AdvanceFarming(elapsed.count());
}

bool Tui::OnEvent(ftxui::Event event) {
  return controller_.OnEvent(event);
}

}  // namespace ms
