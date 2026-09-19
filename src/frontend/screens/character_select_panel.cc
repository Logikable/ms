#include "src/frontend/screens/character_select_panel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/job_name.h"
#include "src/frontend/panel_widths.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/stat_rows.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/roster.h"

namespace ms {
namespace {

// The list's columns. A name is capped at kMaxUsernameLength and the job
// column takes the longest short name, "I/L Arch Mage"; each carries the gap
// after it. The check's column is the last, where the party list keeps its
// own mark.
constexpr int kNameWidth = kMaxUsernameLength + 2;
constexpr int kJobWidth = 15;
constexpr int kLevelWidth = 7;
constexpr int kOfflineWidth = 7;

// The list window, one size whatever the account holds, and the card beside
// it at the width the Character panel reads at its narrowest -- its own rows
// are what this draws.
constexpr int kListWidth =
    2 + kNameWidth + kJobWidth + kLevelWidth + kOfflineWidth + 1;
constexpr int kCardWidth = kLeftColumnMin - 2;

constexpr char kCursorHere[] = "> ";
constexpr char kCursorAway[] = "  ";

// The buttons under the list, in the order the cursor walks them.
constexpr char kCreateLabel[] = "Create";
constexpr char kQuitLabel[] = "Quit";

// The check beside the character who farms while the game is closed, in its
// column and in the theme's own colour, as the party list's ready mark is.
ftxui::Element OfflineCell(bool offline) {
  return ftxui::text(offline ? "   ✓   " : "       ") | ftxui::color(kTheme);
}

// Marks the row the frame scrolls to, which is the one holding the cursor.
// The cursor's row: the band behind it, and the mark the frame scrolls to.
// A table row wide enough that the caret alone leaves the far cell unclaimed
// -- see HighlightRow in chrome.h.
ftxui::Element Focused(ftxui::Element row, bool on_cursor) {
  row = HighlightRow(std::move(row), on_cursor);
  return on_cursor ? std::move(row) | ftxui::focus : std::move(row);
}

// One line of the card's heading block, centred over its width.
ftxui::Element CardTitle(const std::string& text) {
  int pad = std::max(0, (kCardWidth - TextColumns(text)) / 2);
  return ftxui::text(PadRight(std::string(pad, ' ') + text, kCardWidth));
}

// One stat of the card: the label, and the value against the right edge a
// gutter shy of the border. The Character panel's own row.
ftxui::Element CardRow(const std::string& label, const std::string& value) {
  int gap = kCardWidth - 2 - TextColumns(value);
  return ftxui::text(" " + PadRight(label, std::max(0, gap)) + value + " ");
}

}  // namespace

CharacterSelectPanel::CharacterSelectPanel(GameState& state)
    : state_(state),
      menu_({"Play", "Set Offline", "Delete", "Close"}),
      preview_(state.rng, Character()) {
  Reset();
}

void CharacterSelectPanel::Reset() {
  Refresh();
  button_ = 0;
  // On the character being played, who is the top row until the player has
  // played somebody else this session.
  cursor_ = 0;
  for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
    if (rows_[i].played) {
      cursor_ = i;
    }
  }
}

void CharacterSelectPanel::Refresh() {
  bool on_row = !on_buttons();
  rows_ = Roster(state_);
  CloseMenu();
  cursor_ = Cursor();
  // A cursor that was on a character stays on one: deleting the last row
  // lands on the row above it rather than dropping onto the buttons, which
  // would leave the card describing somebody who is gone.
  if (on_row && !rows_.empty()) {
    cursor_ = std::min(cursor_, static_cast<int>(rows_.size()) - 1);
  }
  // A delete renumbers the slots, so what the card is holding may no longer
  // be the character the cursor is on.
  preview_slot_ = -1;
}

int CharacterSelectPanel::Cursor() const {
  return std::clamp(cursor_, 0, static_cast<int>(rows_.size()));
}

bool CharacterSelectPanel::on_buttons() const {
  return Cursor() >= static_cast<int>(rows_.size());
}

void CharacterSelectPanel::MoveCursor(int delta) {
  // The button row is the last stop of the ring, so Down off the last
  // character lands on it and Down again comes back to the top.
  cursor_ = StepCursor(Cursor(), delta, static_cast<int>(rows_.size()) + 1);
}

void CharacterSelectPanel::MoveButton(int delta) {
  if (!on_buttons()) {
    return;
  }
  button_ = std::clamp(button_ + delta, 0, 1);
}

CharacterAction CharacterSelectPanel::Chosen() const {
  if (!on_buttons()) {
    return CharacterAction::kMenu;
  }
  return button_ == 0 ? CharacterAction::kCreate : CharacterAction::kQuit;
}

int CharacterSelectPanel::selected_slot() const {
  return on_buttons() ? -1 : rows_[Cursor()].slot;
}

std::string CharacterSelectPanel::selected_name() const {
  return on_buttons() ? "" : rows_[Cursor()].name;
}

void CharacterSelectPanel::OpenMenu() {
  menu_open_ = true;
  menu_.Reset();
  if (on_buttons()) {
    return;
  }
  // Play is not dimmed on the character already in play: it is the only way
  // back into the game, and on that row it resumes them.
  if (rows_[Cursor()].offline) {
    menu_.Disable(kCharacterMenuSetOffline);
  }
  if (rows_.size() <= 1) {
    // There has to be somebody to play.
    menu_.Disable(kCharacterMenuDelete);
  }
}

void CharacterSelectPanel::CloseMenu() {
  menu_open_ = false;
}

void CharacterSelectPanel::MoveMenuCursor(int delta) {
  if (delta < 0) {
    menu_.Up();
  } else {
    menu_.Down();
  }
}

int CharacterSelectPanel::menu_selected() const {
  return menu_.selected();
}

ftxui::Element CharacterSelectPanel::RenderList() const {
  std::vector<ftxui::Element> rows;
  for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
    bool on_cursor = !on_buttons() && i == Cursor();
    std::string text = PadRight(rows_[i].name, kNameWidth) +
                       PadRight(rows_[i].job, kJobWidth) +
                       PadRight(std::to_string(rows_[i].level), kLevelWidth);
    rows.push_back(
        Focused(ftxui::hbox({
                    ftxui::text(on_cursor ? kCursorHere : kCursorAway),
                    ftxui::text(std::move(text)),
                    OfflineCell(rows_[i].offline),
                }),
                on_cursor));
  }
  return ftxui::vbox({
      ftxui::text("  " + PadRight("Name", kNameWidth) +
                  PadRight("Job", kJobWidth) + PadRight("Level", kLevelWidth) +
                  "Offline"),
      ThemedSeparator(),
      ftxui::vbox(std::move(rows)) | ftxui::vscroll_indicator | ftxui::yframe |
          ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kCharacterListRows),
  });
}

ftxui::Element CharacterSelectPanel::RenderButtons() const {
  return ftxui::hbox({
             ftxui::text(" "),
             ActionButton(kCreateLabel, on_buttons() && button_ == 0),
             ftxui::text("  "),
             ActionButton(kQuitLabel, on_buttons() && button_ == 1),
             ftxui::text(" "),
         }) |
         ftxui::hcenter;
}

void CharacterSelectPanel::PreviewSelected() const {
  int slot = on_buttons() ? -1 : rows_[Cursor()].slot;
  if (slot < 0 || slot == preview_slot_) {
    return;
  }
  std::vector<CharacterSave> all = AllCharacters(state_);
  if (slot >= static_cast<int>(all.size())) {
    return;
  }
  preview_.RestoreFrom(all[slot].character(), state_.equips, state_.items);
  preview_.UseEquipSets(state_.equip_sets);
  preview_slot_ = slot;
}

ftxui::Element CharacterSelectPanel::RenderCard() const {
  PreviewSelected();
  const Character& p = preview_.proto();
  std::vector<ftxui::Element> rows;
  rows.push_back(CardTitle(p.name()));
  rows.push_back(CardTitle("Lv" + PadLeft(std::to_string(p.level()), 3) + " " +
                           ShortJobName(p.job())));
  rows.push_back(CardTitle(CombatPowerText(
      CharacterCombatPower(preview_, state_.skills, Activity::kFarming))));
  rows.push_back(ThemedSeparator());
  DerivedStats derived =
      DerivedStatsFor(preview_, state_.skills, /*buffs_up=*/{},
                      /*allies=*/{}, Activity::kFarming);
  rows.push_back(CardRow("HP", FormatWithCommas(derived.max_hp)));
  rows.push_back(CardRow("MP", FormatWithCommas(derived.max_mp)));
  for (const StatLine& line :
       MainStatLines(preview_, state_.skills, Activity::kFarming)) {
    rows.push_back(CardRow(line.label, line.value));
  }
  rows.push_back(ThemedSeparator());
  // What is left of the window after the block above, which is what the
  // stats get. The tail is cut rather than the window grown: both windows
  // are one height, and a card that outgrew the list would say so by moving
  // the border.
  std::vector<StatLine> extras =
      ExtraStatLines(preview_, state_.skills, Activity::kFarming);
  int room = kCharacterPanelHeight - 2 - static_cast<int>(rows.size());
  int shown = std::clamp(static_cast<int>(extras.size()), 0, room);
  // A rule with nothing under it reads as a row that failed to draw.
  while (shown > 0 && extras[shown - 1].rule) {
    --shown;
  }
  for (int i = 0; i < shown; ++i) {
    rows.push_back(extras[i].rule ? ThemedSeparator()
                                  : CardRow(extras[i].label, extras[i].value));
  }
  return ThemedWindow(" Character ", ftxui::vbox(std::move(rows))) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kCardWidth + 2) |
         ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kCharacterPanelHeight);
}

int CharacterSelectPanel::MenuRow() const {
  // +3 rows: the window's top border, the column header and its separator.
  // One row back from there, so the entry standing highlighted lands beside
  // the character rather than below them.
  constexpr int kFirstCharacterRow = 3;
  return kFirstCharacterRow + Cursor() - 1;
}

ftxui::Element CharacterSelectPanel::Render() const {
  ftxui::Element body = ftxui::vbox({
                            RenderList(),
                            ThemedSeparator(),
                            RenderButtons(),
                        }) |
                        ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kListWidth);
  ftxui::Element list =
      ThemedWindow(" Character Select ", std::move(body)) |
      ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kCharacterPanelHeight);
  if (menu_open_) {
    // Anchored inside the window rather than on the terminal, the screen
    // being centred and so having no fixed place to measure from. The column
    // clears the cursor and the name, so the menu covers the job rather than
    // who it is about.
    constexpr int kMenuCol = 2 + kNameWidth;
    list = ftxui::dbox({
        std::move(list),
        Floating(menu_.Render(MenuRow(), kMenuCol)),
    });
  }
  return ftxui::hbox({std::move(list), RenderCard()});
}

}  // namespace ms
