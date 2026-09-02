#include "src/frontend/panels/menu_panel.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/build_config.h"
#include "src/character/progression.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/keys.h"

namespace ms {
namespace {

std::string EntryLabel(MenuEntry entry) {
  switch (entry) {
    case MenuEntry::kBoss:
      return "Boss";
    case MenuEntry::kParty:
      return "Party";
    case MenuEntry::kAnalysis:
      return "Analysis";
    case MenuEntry::kSettings:
      return "Settings";
  }
  return "";
}

// Columns between two entries on the row.
constexpr int kEntryGap = 2;

}  // namespace

MenuPanel::MenuPanel(const GameState& state, const BattleAnalysis& analysis,
                     int& panel_focus)
    : state_(state), analysis_(analysis), panel_focus_(panel_focus) {
}

std::vector<MenuEntry> MenuPanel::Entries() const {
  std::vector<MenuEntry> entries;
  if (Unlocked(Feature::kBoss, state_.character, state_.account)) {
    entries.push_back(MenuEntry::kBoss);
    // Bossing is what a party is for so far, and a build that plays alone has
    // nobody to make one with.
    if (kMultiplayerEnabled) {
      entries.push_back(MenuEntry::kParty);
    }
  }
  entries.push_back(MenuEntry::kAnalysis);
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

std::vector<std::string> MenuPanel::BoxEntries(MenuEntry entry) const {
  switch (entry) {
    // Both open a screen rather than a box.
    case MenuEntry::kBoss:
    case MenuEntry::kParty:
      return {};
    case MenuEntry::kAnalysis:
      return {analysis_.stops_on_press() ? "Stop" : "Start", "View"};
    case MenuEntry::kSettings:
      return {"Keybinds"};
  }
  return {};
}

void MenuPanel::OpenBox(MenuEntry entry) {
  box_open_ = true;
  box_entry_ = entry;
  box_cursor_ = -1;
}

void MenuPanel::CloseBox() {
  box_open_ = false;
  box_cursor_ = -1;
}

void MenuPanel::MoveBoxCursor(int delta) {
  // The box stands above the menu row, so the ring runs from the row up
  // through the entries and back round. Stop 0 is the row itself.
  int count = static_cast<int>(BoxEntries(box_entry_).size());
  int at = 0;
  if (box_cursor_ >= 0) {
    at = count - box_cursor_;
  }
  at = StepCursor(at, delta, count + 1);
  box_cursor_ = -1;
  if (at > 0) {
    box_cursor_ = count - at;
  }
}

SettingsEntry MenuPanel::selected_settings_entry() const {
  // One entry, so the cursor cannot be on anything else.
  return SettingsEntry::kKeybinds;
}

AnalysisEntry MenuPanel::selected_analysis_entry() const {
  return box_cursor_ <= 0 ? AnalysisEntry::kStartStop : AnalysisEntry::kView;
}

int MenuPanel::BoxWidth() const {
  // What ThemedWindow will size itself to: the wider of its title and its
  // widest row, plus the two borders.
  int widest = static_cast<int>(EntryLabel(box_entry_).size()) + 2;
  for (const std::string& entry : BoxEntries(box_entry_)) {
    widest = std::max(widest, static_cast<int>(entry.size()) + 2);
  }
  return widest + 2;
}

int MenuPanel::BoxRightMargin() const {
  std::vector<MenuEntry> entries = Entries();
  // Where the word starts and how wide it is, and how wide the panel around it
  // is. All counted from the panel's left border, which the row is laid out
  // from.
  int word = 0;
  int label = 0;
  int at = 2;  // past the border and the column of clearance inside it
  for (const MenuEntry& entry : entries) {
    int width = static_cast<int>(EntryLabel(entry).size());
    if (entry == box_entry_) {
      word = at;
      label = width;
    }
    at += width + kEntryGap;
  }
  int panel_width = at - kEntryGap + 2;
  // The box is wider than the word, so it hangs off both sides of it evenly
  // rather than starting where the word does.
  int left = word + (label - BoxWidth()) / 2;
  // The panel is flush with the right of the screen, so a box that would hang
  // off the edge is pulled back to it rather than being cut in half.
  return std::max(panel_width - left - BoxWidth(), 0);
}

ftxui::Element MenuPanel::RenderBox() const {
  std::vector<std::string> entries = BoxEntries(box_entry_);
  ftxui::Elements rows;
  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    ftxui::Element row = ftxui::text(" " + entries[i] + " ");
    if (i == box_cursor_) {
      row = std::move(row) | ftxui::inverted;
    }
    rows.push_back(std::move(row));
  }
  // Cleared under, so the box covers the interior of whatever it stands on
  // rather than letting the panel below show through it.
  ftxui::Element box = ClearUnder(ThemedWindow(
      " " + EntryLabel(box_entry_) + " ", ftxui::vbox(std::move(rows))));
  // The margin stands the box over the word that raised it. A filler rather
  // than blanks: nothing behind the box should be painted over.
  return ftxui::hbox({
      std::move(box),
      ftxui::filler() |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, BoxRightMargin()),
  });
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
    if (focused && i == at && box_cursor_ < 0) {
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
