/* PartySelectPanel is the screen for finding a party or being in one. It is one
 * window with two views, depending on whether the player is in a party.
 *
 * Outside a party it lists the parties open to join (who leads each and how
 * full it is) and offers to create one. Inside a party it lists the members,
 * with a crown beside the leader and a mark beside everyone who is ready.
 *
 * The cursor moves through the list and then the row of buttons below it, all
 * one ring, and Left and Right move between buttons only once the cursor is
 * there. Enter on a member opens a menu beside that row: Inspect, Trade and
 * Close for anyone, plus Kick and Promote for the leader.
 *
 * The panel only displays. It draws the last snapshot it was given and never
 * asks the connection for anything: the controller reads what the cursor is on
 * and does the asking.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_PARTY_SELECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_PARTY_SELECT_PANEL_H_

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/item_menu.h"
#include "src/multiplayer/client.h"

namespace ms {

// What pressing Enter where the cursor is would do.
enum class PartyAction {
  // A party's row, when not in a party.
  kJoin,
  // A member's row. The menu it opens depends on who is asking.
  kMemberMenu,
  kCreate,
  kReady,
  kUnready,
  kLeave,
  kClose,
};

// One button below the list, and what pressing it does.
struct PartyButton {
  std::string label;
  PartyAction action;
};

class PartySelectPanel {
 public:
  PartySelectPanel();

  // The lobby as the panel should draw it. Passed in every frame.
  void SetSnapshot(const MultiplayerSnapshot& snapshot);
  // Puts the cursor back at the top of the list. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` stops, wrapping at the ends. The button row is the
  // last stop in the ring, so Down from the last member lands on the buttons
  // and Down again wraps to the top of the list.
  void MoveCursor(int delta);
  // Moves `delta` buttons along the row, stopping at its ends. Does nothing
  // while the cursor is in the list.
  void MoveButton(int delta);
  ftxui::Element Render() const;

  bool in_party() const;
  // Whether this player leads their party.
  bool is_leader() const;
  // Whether this player is ready. Always true for the leader, since leading
  // means being ready.
  bool ready() const;
  // What Enter would do where the cursor is.
  PartyAction Chosen() const;
  // The party under the cursor, for joining it. Empty unless Chosen() is kJoin.
  std::string selected_party_id() const;
  // The account of the member under the cursor, and the name to use when asking
  // the player about them. Empty unless the cursor is on a member's row.
  std::string selected_member() const;
  std::string selected_member_name() const;

  // The member menu. A player who isn't the leader gets Inspect, Trade and
  // Close. The leader also gets Kick and Promote, dimmed on their own row since
  // there is nobody there to apply them to. Trade is left out entirely on your
  // own row rather than dimmed.
  void OpenMenu();
  void CloseMenu();
  bool menu_open() const {
    return menu_open_;
  }
  void MoveMenuCursor(int delta);
  // The menu entry under the cursor, as a PartyMenuItem.
  int menu_selected() const;

 private:
  // The number of rows in the list: one per party, or one per member.
  int ListRows() const;
  // The cursor's position, brought back into the ring. The list can shrink
  // under it (a member leaves, a party fills up), and a cursor past the end
  // would waste the keypress that should have moved it.
  int Cursor() const;
  // The buttons below the list, left to right, for the panel's current view.
  std::vector<PartyButton> Buttons() const;
  // Whether the cursor is on the button row rather than in the list.
  bool on_buttons() const;
  // The member playing under `account_id`, or null.
  const PartyMember* MemberOf(const std::string& account_id) const;
  // The parties worth listing: every open one that isn't full.
  std::vector<const Party*> OpenParties() const;

  ftxui::Element RenderPartyList() const;
  ftxui::Element RenderMembers() const;
  ftxui::Element RenderButtons() const;
  // The row the member menu opens at, measured from the top of the window.
  int MenuRow() const;

  MultiplayerSnapshot snapshot_;
  // The cursor's position: one stop per list row, then the button row.
  int cursor_ = 0;
  // The button under the cursor once it is on the button row. Kept while the
  // cursor is in the list, so moving back down returns to the same button.
  int button_ = 0;
  ItemMenu menu_;
  bool menu_open_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_PARTY_SELECT_PANEL_H_
