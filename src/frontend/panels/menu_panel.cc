#include "src/frontend/panels/menu_panel.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/progression.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

std::string EntryLabel(MenuEntry entry) {
  switch (entry) {
    case MenuEntry::kBoss:
      return "Boss";
    case MenuEntry::kSettings:
      return "Settings";
  }
  return "";
}

}  // namespace

MenuPanel::MenuPanel(const GameState& state, int& panel_focus)
    : state_(state), panel_focus_(panel_focus) {
}

std::vector<MenuEntry> MenuPanel::Entries() const {
  std::vector<MenuEntry> entries;
  if (Unlocked(Feature::kBoss, state_.character)) {
    entries.push_back(MenuEntry::kBoss);
  }
  entries.push_back(MenuEntry::kSettings);
  return entries;
}

MenuEntry MenuPanel::selected() const {
  std::vector<MenuEntry> entries = Entries();
  int at = std::clamp(cursor_, 0, static_cast<int>(entries.size()) - 1);
  return entries[at];
}

void MenuPanel::MoveCursor(int delta) {
  cursor_ = StepCursor(cursor_, delta, static_cast<int>(Entries().size()));
}

ftxui::Element MenuPanel::Render() const {
  std::vector<MenuEntry> entries = Entries();
  bool focused = panel_focus_ == kMenuPanel;
  int at = std::clamp(cursor_, 0, static_cast<int>(entries.size()) - 1);
  ftxui::Elements row;
  row.push_back(ftxui::text(" "));
  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    if (i > 0) {
      row.push_back(ftxui::text("  "));
    }
    // No brackets: the panel is small enough that the entries read as a menu
    // on their own, and the cursor is the inverted one.
    ftxui::Element button = ftxui::text(EntryLabel(entries[i]));
    if (entries[i] == MenuEntry::kBoss &&
        !state_.character.TabSeen(kBossSeenKey)) {
      // Gold until the player has been there once, the same way a new tab is.
      button = std::move(button) | ftxui::color(kYellow);
    }
    if (focused && i == at) {
      button = std::move(button) | ftxui::inverted;
    }
    row.push_back(std::move(button));
  }
  row.push_back(ftxui::text(" "));
  return ThemedWindow(" Menu ", ftxui::hbox(std::move(row)), focused);
}

ftxui::Component MenuPanel::MakeComponent(
    std::function<void(MenuEntry)> on_open) {
  // The Renderer(bool) overload is Focusable(), unlike Renderer() -- required
  // so Container::Tab's Focused() check passes on kMenuPanel.
  ftxui::Component renderer =
      ftxui::Renderer([this](bool /*focused*/) { return Render(); });
  return ftxui::CatchEvent(renderer, [this, on_open](ftxui::Event event) {
    if (panel_focus_ != kMenuPanel) {
      return false;
    }
    if (event == ftxui::Event::ArrowLeft) {
      MoveCursor(-1);
      return true;
    }
    if (event == ftxui::Event::ArrowRight) {
      MoveCursor(1);
      return true;
    }
    if (IsForward(event)) {
      on_open(selected());
      return true;
    }
    return false;
  });
}

}  // namespace ms
