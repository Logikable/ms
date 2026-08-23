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

std::string SettingsEntryLabel(SettingsEntry entry) {
  switch (entry) {
    case SettingsEntry::kKeybinds:
      return "Keybinds";
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

std::vector<SettingsEntry> MenuPanel::SettingsEntries() const {
  return {SettingsEntry::kKeybinds};
}

void MenuPanel::OpenSettings() {
  settings_open_ = true;
  settings_cursor_ = -1;
}

void MenuPanel::CloseSettings() {
  settings_open_ = false;
  settings_cursor_ = -1;
}

void MenuPanel::MoveSettingsCursor(int delta) {
  // The box stands above the menu row, so the ring runs from the row up
  // through the entries and back round. Stop 0 is the row itself.
  int count = static_cast<int>(SettingsEntries().size());
  int at = 0;
  if (settings_cursor_ >= 0) {
    at = count - settings_cursor_;
  }
  at = StepCursor(at, delta, count + 1);
  settings_cursor_ = -1;
  if (at > 0) {
    settings_cursor_ = count - at;
  }
}

SettingsEntry MenuPanel::selected_settings_entry() const {
  std::vector<SettingsEntry> entries = SettingsEntries();
  int at =
      std::clamp(settings_cursor_, 0, static_cast<int>(entries.size()) - 1);
  return entries[at];
}

ftxui::Element MenuPanel::RenderSettingsBox() const {
  std::vector<SettingsEntry> entries = SettingsEntries();
  ftxui::Elements rows;
  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    ftxui::Element row =
        ftxui::text(" " + SettingsEntryLabel(entries[i]) + " ");
    if (i == settings_cursor_) {
      row = std::move(row) | ftxui::inverted;
    }
    rows.push_back(std::move(row));
  }
  // Cleared under, so the box covers the interior of whatever it stands on
  // rather than letting the panel below show through it.
  return ThemedWindow(" Settings ", ftxui::vbox(std::move(rows))) |
         ftxui::clear_under;
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
    if (entries[i] == MenuEntry::kBoss && !state_.account.Seen(kBossSeenKey)) {
      // Gold until the player has been there once, the same way a new tab is.
      button = std::move(button) | ftxui::color(kYellow);
    }
    // The cursor is in one place at a time: with the box open and the cursor
    // in it, the entry it came from stops being the highlighted one.
    if (focused && i == at && settings_cursor_ < 0) {
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
