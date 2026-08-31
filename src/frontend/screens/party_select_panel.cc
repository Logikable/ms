#include "src/frontend/screens/party_select_panel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/character/job_name.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/multiplayer/protocol.h"

namespace ms {
namespace {

// Column widths of the party list. A name is capped at kMaxUsernameLength, so
// the column is that plus the gap after it.
constexpr int kLeaderWidth = kMaxUsernameLength + 2;
constexpr int kCapacityWidth = 8;

// Column widths of the member list. The job column takes the longest short
// name, "I/L Arch Mage", and its gap.
constexpr int kNameWidth = kMaxUsernameLength + 2;
constexpr int kLevelWidth = 7;
constexpr int kJobWidth = 15;

// The window is one size whatever the lobby holds. A party made or broken
// under the cursor would otherwise move the buttons out from under it.
constexpr int kContentWidth = 56;
constexpr int kListRows = 8;

// The cursor's column, and the crown's beside it.
constexpr char kCursorHere[] = "> ";
constexpr char kCursorAway[] = "  ";
constexpr char kCrown[] = "♛ ";
constexpr char kNoCrown[] = "  ";

// The mark beside a member who has said they are ready, in the Ready column,
// with the panel's column of clearance after it.
ftxui::Element ReadyCell(bool ready) {
  return ftxui::text(ready ? "  ✓   " : "      ") | ftxui::color(kTheme);
}

// Marks the row the frame scrolls to, which is the one holding the cursor.
ftxui::Element Focused(ftxui::Element row, bool on_cursor) {
  return on_cursor ? std::move(row) | ftxui::focus : std::move(row);
}

// The rows of a list, held to kListRows however many there are. A longer list
// scrolls to the row the cursor is on.
ftxui::Element ScrollingList(std::vector<ftxui::Element> rows) {
  return ftxui::vbox(std::move(rows)) | ftxui::vscroll_indicator |
         ftxui::yframe | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kListRows);
}

}  // namespace

PartySelectPanel::PartySelectPanel()
    : menu_({"Inspect", "Kick", "Promote", "Close"}) {
}

void PartySelectPanel::SetSnapshot(const MultiplayerSnapshot& snapshot) {
  snapshot_ = snapshot;
}

void PartySelectPanel::Reset() {
  cursor_ = 0;
  button_ = 0;
  CloseMenu();
}

bool PartySelectPanel::in_party() const {
  return !snapshot_.party.id().empty();
}

bool PartySelectPanel::is_leader() const {
  return in_party() &&
         snapshot_.party.leader_account_id() == snapshot_.account_id;
}

bool PartySelectPanel::ready() const {
  if (is_leader()) {
    return true;
  }
  const PartyMember* self = MemberOf(snapshot_.account_id);
  return self != nullptr && self->ready();
}

const PartyMember* PartySelectPanel::MemberOf(
    const std::string& account_id) const {
  for (const PartyMember& member : snapshot_.party.members()) {
    if (member.player().account_id() == account_id) {
      return &member;
    }
  }
  return nullptr;
}

std::vector<const Party*> PartySelectPanel::OpenParties() const {
  std::vector<const Party*> open;
  for (const Party& party : snapshot_.parties.parties()) {
    if (party.members_size() < kMaxPartySize) {
      open.push_back(&party);
    }
  }
  return open;
}

int PartySelectPanel::ListRows() const {
  return in_party() ? snapshot_.party.members_size()
                    : static_cast<int>(OpenParties().size());
}

std::vector<PartyButton> PartySelectPanel::Buttons() const {
  if (!in_party()) {
    return {{"Create a Party", PartyAction::kCreate},
            {"Close", PartyAction::kClose}};
  }
  std::vector<PartyButton> buttons;
  // The leader has no Ready of their own: leading is what says they are.
  if (!is_leader()) {
    buttons.push_back(ready() ? PartyButton{"Unready", PartyAction::kUnready}
                              : PartyButton{"Ready", PartyAction::kReady});
  }
  buttons.push_back({"Leave Party", PartyAction::kLeave});
  buttons.push_back({"Close", PartyAction::kClose});
  return buttons;
}

int PartySelectPanel::Cursor() const {
  return std::clamp(cursor_, 0, ListRows());
}

bool PartySelectPanel::on_buttons() const {
  return Cursor() >= ListRows();
}

void PartySelectPanel::MoveCursor(int delta) {
  // The buttons are the last stop of the ring, so Down off the last row lands
  // on them and Down again comes back to the top of the list.
  cursor_ = StepCursor(Cursor(), delta, ListRows() + 1);
}

void PartySelectPanel::MoveButton(int delta) {
  if (!on_buttons()) {
    return;
  }
  int count = static_cast<int>(Buttons().size());
  button_ = std::clamp(button_ + delta, 0, count - 1);
}

PartyAction PartySelectPanel::Chosen() const {
  if (on_buttons()) {
    std::vector<PartyButton> buttons = Buttons();
    int at = std::clamp(button_, 0, static_cast<int>(buttons.size()) - 1);
    return buttons[at].action;
  }
  if (!in_party()) {
    return PartyAction::kJoin;
  }
  // A member's row. Everyone may read whoever is on it; what else the menu
  // offers is the leader's business, and OpenMenu decides that.
  return PartyAction::kMemberMenu;
}

std::string PartySelectPanel::selected_party_id() const {
  std::vector<const Party*> open = OpenParties();
  if (in_party() || on_buttons()) {
    return "";
  }
  return open[Cursor()]->id();
}

std::string PartySelectPanel::selected_member() const {
  if (!in_party() || on_buttons()) {
    return "";
  }
  return snapshot_.party.members(Cursor()).player().account_id();
}

std::string PartySelectPanel::selected_member_name() const {
  if (!in_party() || on_buttons()) {
    return "";
  }
  return snapshot_.party.members(Cursor()).player().name();
}

void PartySelectPanel::OpenMenu() {
  menu_open_ = true;
  menu_.Reset();
  if (!is_leader()) {
    // Hidden rather than dimmed: a member has not been handed these and never
    // will be on this party, so a greyed row would advertise nothing.
    menu_.Hide(kPartyMenuKick);
    menu_.Hide(kPartyMenuPromote);
    return;
  }
  if (selected_member() != snapshot_.account_id) {
    return;
  }
  // The leader's own row. Both actions stay visible and dimmed, so the menu
  // reads the same wherever the leader raises it.
  menu_.Disable(kPartyMenuKick);
  menu_.Disable(kPartyMenuPromote);
}

void PartySelectPanel::CloseMenu() {
  menu_open_ = false;
}

void PartySelectPanel::MoveMenuCursor(int delta) {
  if (delta < 0) {
    menu_.Up();
  } else {
    menu_.Down();
  }
}

int PartySelectPanel::menu_selected() const {
  return menu_.selected();
}

ftxui::Element PartySelectPanel::RenderPartyList() const {
  std::vector<ftxui::Element> rows;
  std::vector<const Party*> open = OpenParties();
  if (open.empty()) {
    rows.push_back(EmptyState("empty"));
  }
  for (int i = 0; i < static_cast<int>(open.size()); ++i) {
    const PartyMember* leader = nullptr;
    for (const PartyMember& member : open[i]->members()) {
      if (member.player().account_id() == open[i]->leader_account_id()) {
        leader = &member;
      }
    }
    std::string row =
        !on_buttons() && i == Cursor() ? kCursorHere : kCursorAway;
    row += PadRight(leader == nullptr ? "" : leader->player().name(),
                    kLeaderWidth);
    row += std::to_string(open[i]->members_size()) + "/" +
           std::to_string(kMaxPartySize);
    rows.push_back(Focused(ftxui::text(row), !on_buttons() && i == Cursor()));
  }
  return ftxui::vbox({
      ftxui::text("  " + PadRight("Leader", kLeaderWidth) +
                  PadRight("Capacity", kCapacityWidth)),
      ThemedSeparator(),
      ScrollingList(std::move(rows)),
  });
}

ftxui::Element PartySelectPanel::RenderMembers() const {
  std::vector<ftxui::Element> rows;
  for (int i = 0; i < snapshot_.party.members_size(); ++i) {
    const PartyMember& member = snapshot_.party.members(i);
    const PlayerInfo& player = member.player();
    bool leader = player.account_id() == snapshot_.party.leader_account_id();
    // The crown and the mark are their own cells so each can keep its colour.
    std::string text = PadRight(player.name(), kNameWidth);
    text += PadRight(std::to_string(player.level()), kLevelWidth);
    text += PadRight(ShortJobName(JobForAdvancement(player.job())), kJobWidth);
    bool on_cursor = !on_buttons() && i == Cursor();
    rows.push_back(Focused(
        ftxui::hbox({
            ftxui::text(on_cursor ? kCursorHere : kCursorAway),
            ftxui::text(leader ? kCrown : kNoCrown) | ftxui::color(kYellow),
            ftxui::text(text),
            ReadyCell(leader || member.ready()),
        }),
        on_cursor));
  }
  return ftxui::vbox({
      ftxui::text(std::string("    ") + PadRight("Name", kNameWidth) +
                  PadRight("Level", kLevelWidth) + PadRight("Job", kJobWidth) +
                  "Ready"),
      ThemedSeparator(),
      ScrollingList(std::move(rows)),
  });
}

ftxui::Element PartySelectPanel::RenderButtons() const {
  std::vector<PartyButton> buttons = Buttons();
  // A column of clearance either side, since the button row is the widest
  // thing here and would otherwise set a window with no margin inside it.
  ftxui::Elements row;
  row.push_back(ftxui::text(" "));
  for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
    if (i > 0) {
      row.push_back(ftxui::text("  "));
    }
    row.push_back(ActionButton(buttons[i].label, on_buttons() && i == button_));
  }
  row.push_back(ftxui::text(" "));
  return ftxui::hbox(std::move(row)) | ftxui::hcenter;
}

int PartySelectPanel::MenuRow() const {
  // +3 rows: the window's top border, the column header and its separator.
  // One row back from there, so the entry standing highlighted lands beside
  // the member rather than below them.
  constexpr int kFirstMemberRow = 3;
  return kFirstMemberRow + Cursor() - 1;
}

ftxui::Element PartySelectPanel::Render() const {
  ftxui::Element list = in_party() ? RenderMembers() : RenderPartyList();
  ftxui::Element body = ftxui::vbox({
                            std::move(list),
                            ThemedSeparator(),
                            RenderButtons(),
                        }) |
                        ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth);
  ftxui::Element window =
      ThemedWindow(in_party() ? " Party " : " Party List ", std::move(body));
  if (!menu_open_) {
    return window;
  }
  // Anchored inside the panel rather than on the terminal, because the screen
  // is centred and so has no fixed place to measure from. The column clears
  // the border, the crown and the name, so the menu covers the level rather
  // than who it is about.
  constexpr int kMenuCol = 4 + kNameWidth;
  return ftxui::dbox({
      std::move(window),
      Floating(menu_.Render(MenuRow(), kMenuCol)),
  });
}

}  // namespace ms
