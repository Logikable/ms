#include "src/frontend/screens/player_list_panel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/keys.h"

namespace ms {
namespace {

// Column widths. A name is capped at kMaxUsernameLength, so the column is
// that plus the gap after it.
constexpr int kNameWidth = kMaxUsernameLength + 2;
constexpr int kLevelWidth = 7;

// The window is one size however many are online, so somebody arriving does
// not move the button out from under the cursor.
constexpr int kContentWidth = 34;
constexpr int kListRows = 8;

constexpr char kCursorHere[] = "> ";
constexpr char kCursorAway[] = "  ";

}  // namespace

void PlayerListPanel::SetSnapshot(const MultiplayerSnapshot& snapshot) {
  snapshot_ = snapshot;
}

void PlayerListPanel::Reset() {
  cursor_ = 0;
}

int PlayerListPanel::Cursor() const {
  return std::clamp(cursor_, 0, snapshot_.online.players_size());
}

bool PlayerListPanel::on_close() const {
  return Cursor() >= snapshot_.online.players_size();
}

void PlayerListPanel::MoveCursor(int delta) {
  cursor_ = StepCursor(Cursor(), delta, snapshot_.online.players_size() + 1);
}

std::string PlayerListPanel::selected_account() const {
  if (on_close()) {
    return "";
  }
  return snapshot_.online.players(Cursor()).account_id();
}

std::string PlayerListPanel::selected_name() const {
  if (on_close()) {
    return "";
  }
  return snapshot_.online.players(Cursor()).name();
}

ftxui::Element PlayerListPanel::Render() const {
  std::vector<ftxui::Element> rows;
  if (snapshot_.online.players_size() == 0) {
    rows.push_back(EmptyState("empty"));
  }
  for (int i = 0; i < snapshot_.online.players_size(); ++i) {
    const PlayerInfo& player = snapshot_.online.players(i);
    bool on_cursor = !on_close() && i == Cursor();
    std::string row = on_cursor ? kCursorHere : kCursorAway;
    row += PadRight(player.name(), kNameWidth);
    row += std::to_string(player.level());
    ftxui::Element line = ftxui::text(row);
    rows.push_back(on_cursor ? std::move(line) | ftxui::focus
                             : std::move(line));
  }
  ftxui::Element body =
      ftxui::vbox({
          ftxui::text("  " + PadRight("Name", kNameWidth) +
                      PadRight("Level", kLevelWidth)),
          ThemedSeparator(),
          ftxui::vbox(std::move(rows)) | ftxui::vscroll_indicator |
              ftxui::yframe |
              ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kListRows),
          ThemedSeparator(),
          ftxui::hbox({
              ftxui::text(" "),
              ActionButton("Close", on_close()),
              ftxui::text(" "),
          }) | ftxui::hcenter,
      }) |
      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth);
  return ThemedWindow(" Online Players ", std::move(body));
}

}  // namespace ms
