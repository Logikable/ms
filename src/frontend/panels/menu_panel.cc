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

std::string SettingsEntryName(SettingsEntry entry) {
  switch (entry) {
    case SettingsEntry::kJukebox:
      return "Jukebox";
    case SettingsEntry::kKeybinds:
      return "Keybinds";
    case SettingsEntry::kOptions:
      return "Options";
  }
  return "";
}

std::string EntryLabel(MenuEntry entry) {
  switch (entry) {
    case MenuEntry::kAnalysis:
      return "Analysis";
    case MenuEntry::kDailies:
      return "Dailies";
    case MenuEntry::kBoss:
      return "Boss";
    case MenuEntry::kMultiplayer:
      return "Multiplayer";
    case MenuEntry::kCharacters:
      return "Characters";
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
  entries.push_back(MenuEntry::kAnalysis);
  // The dailies are for the symbols, so the entry appears with them. Below that
  // level there is nothing to claim.
  if (Unlocked(Feature::kSymbols, state_.character, state_.account)) {
    entries.push_back(MenuEntry::kDailies);
  }
  if (Unlocked(Feature::kBoss, state_.character, state_.account)) {
    entries.push_back(MenuEntry::kBoss);
  }
  // A single-player build has nobody to play with, whatever the level.
  if (kMultiplayerEnabled &&
      Unlocked(Feature::kMultiplayer, state_.character, state_.account)) {
    entries.push_back(MenuEntry::kMultiplayer);
  }
  if (Unlocked(Feature::kCharacters, state_.character, state_.account)) {
    entries.push_back(MenuEntry::kCharacters);
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

std::vector<std::string> MenuPanel::BoxEntries(MenuEntry entry) const {
  switch (entry) {
    // These open a screen or a dialog instead of a box.
    case MenuEntry::kBoss:
    case MenuEntry::kCharacters:
      return {};
    case MenuEntry::kMultiplayer:
      return {"Players", "Party"};
    case MenuEntry::kAnalysis:
      return {analysis_.stops_on_press() ? "Stop" : "Start", "View"};
    case MenuEntry::kDailies:
      return {};
    case MenuEntry::kSettings: {
      std::vector<std::string> labels;
      for (SettingsEntry entry : SettingsEntries()) {
        labels.push_back(SettingsEntryName(entry));
      }
      return labels;
    }
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
  // The box sits above the menu row, so the ring runs from the row up through
  // the entries and back round. Stop 0 is the row itself.
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

std::vector<SettingsEntry> MenuPanel::SettingsEntries() {
  // No Jukebox in a build without music: the screen would have nothing to list
  // or play.
  if (!kAudioEnabled) {
    return {SettingsEntry::kKeybinds, SettingsEntry::kOptions};
  }
  return {SettingsEntry::kJukebox, SettingsEntry::kKeybinds,
          SettingsEntry::kOptions};
}

SettingsEntry MenuPanel::selected_settings_entry() const {
  std::vector<SettingsEntry> entries = SettingsEntries();
  return entries[std::clamp(box_cursor_, 0,
                            static_cast<int>(entries.size()) - 1)];
}

MultiplayerEntry MenuPanel::selected_multiplayer_entry() const {
  return box_cursor_ <= 0 ? MultiplayerEntry::kPlayers
                          : MultiplayerEntry::kParty;
}

AnalysisEntry MenuPanel::selected_analysis_entry() const {
  return box_cursor_ <= 0 ? AnalysisEntry::kStartStop : AnalysisEntry::kView;
}

int MenuPanel::BoxWidth() const {
  // The width ThemedWindow will take: the wider of its title and its widest
  // row, plus the two borders. Measured with BoxRow rather than the label, so
  // the caret's columns are counted and the box isn't cut.
  int widest = static_cast<int>(EntryLabel(box_entry_).size()) + 2;
  for (const std::string& entry : BoxEntries(box_entry_)) {
    widest = std::max(widest, static_cast<int>(BoxRow(entry).size()));
  }
  return widest + 2;
}

int MenuPanel::BoxRightMargin() const {
  std::vector<MenuEntry> entries = Entries();
  // Where the word starts, how wide it is, and how wide the panel is, all
  // counted from the panel's left border, where the row starts.
  int word = 0;
  int label = 0;
  int at = 2;  // past the border and the blank column inside it
  for (const MenuEntry& entry : entries) {
    int width = static_cast<int>(EntryLabel(entry).size());
    if (entry == box_entry_) {
      word = at;
      label = width;
    }
    at += width + kEntryGap;
  }
  int panel_width = at - kEntryGap + 2;
  // The box is wider than the word, so it overhangs both sides evenly instead
  // of starting where the word does.
  int left = word + (label - BoxWidth()) / 2;
  // The panel is flush with the right of the screen, so a box that would hang
  // off the edge is pulled back instead of being cut.
  return std::max(panel_width - left - BoxWidth(), 0);
}

std::string MenuPanel::BoxRow(const std::string& entry, bool on_cursor) {
  // A caret, as the item menu and every other dropdown mark their cursor. A box
  // of labels is read top to bottom, and a highlighted row in the middle would
  // look like a state rather than a position.
  return (on_cursor ? "> " : "  ") + entry + " ";
}

ftxui::Element MenuPanel::RenderBox() const {
  std::vector<std::string> entries = BoxEntries(box_entry_);
  ftxui::Elements rows;
  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    rows.push_back(ftxui::text(BoxRow(entries[i], i == box_cursor_)));
  }
  // Cleared underneath, so the box covers whatever it sits on instead of
  // letting the panel below show through.
  ftxui::Element box = ClearUnder(ThemedWindow(
      " " + EntryLabel(box_entry_) + " ", ftxui::vbox(std::move(rows))));
  // The margin places the box over the word that opened it. A filler rather
  // than blanks, so nothing behind the box is painted over.
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
    // No brackets: the panel is small enough that the entries read as a menu on
    // their own, and the cursor is shown inverted.
    ftxui::Element button = ftxui::text(EntryLabel(entries[i]));
    if (entries[i] == MenuEntry::kBoss && !state_.account.Seen(kBossSeenKey)) {
      // Gold until the player has visited once, like a new tab.
      button = std::move(button) | ftxui::color(kYellow);
    }
    // Only one place has the cursor. With the box open and the cursor in it,
    // the entry it came from is no longer highlighted.
    if (focused && i == at && box_cursor_ < 0) {
      button = std::move(button) | ftxui::inverted;
    }
    row.push_back(std::move(button));
  }
  row.push_back(ftxui::text(" "));
  return ThemedWindow(" Menu ", ftxui::hbox(std::move(row)), focused,
                      state_.account.panel_title_blink());
}

ftxui::Component MenuPanel::MakeComponent(
    std::function<void(MenuEntry)> on_open) {
  // The Renderer(bool) overload is Focusable(), unlike Renderer(). It is needed
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
