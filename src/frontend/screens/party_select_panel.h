/* PartySelectPanel is the screen for finding a party or standing in one. It is
 * one window with two faces, and which one it wears is whether the player is
 * in a party.
 *
 * Out of a party it lists the parties open to be joined -- who leads each and
 * how full it is -- and offers to make one. In a party it lists who is in it,
 * with a crown beside the leader and a mark beside everyone who has said they
 * are ready.
 *
 * The cursor walks the list and then the row of buttons under it, all one
 * ring, and Left and Right move between the buttons only once it is down
 * there. The leader may press Enter on a member for a menu -- Kick, Promote,
 * Close -- anchored to that row; everyone else may move the cursor and nothing
 * more.
 *
 * The panel is a view. It draws whatever snapshot it was last handed and never
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

// What pressing Enter where the cursor stands would do.
enum class PartyAction {
  // Nothing: a member's row, for a player who is not the leader.
  kNone,
  // A party's row, out of a party.
  kJoin,
  // A member's row, for the leader.
  kMemberMenu,
  kCreate,
  kReady,
  kUnready,
  kLeave,
  kClose,
};

// One button under the list, and what pressing it does.
struct PartyButton {
  std::string label;
  PartyAction action;
};

class PartySelectPanel {
 public:
  PartySelectPanel();

  // The lobby as the panel should draw it. Handed in every frame.
  void SetSnapshot(const MultiplayerSnapshot& snapshot);
  // Puts the cursor back on the top of the list. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` stops, coming out the other end. The button row
  // is the last stop of the ring, so Down off the last member lands on the
  // buttons and Down again wraps to the top of the list.
  void MoveCursor(int delta);
  // Moves `delta` buttons along the row, clamped to its ends. Does nothing
  // while the cursor is still up in the list.
  void MoveButton(int delta);
  ftxui::Element Render() const;

  bool in_party() const;
  // Whether this player leads the party they are in.
  bool is_leader() const;
  // Whether this player has said they are ready. Always true for the leader,
  // who is ready by leading.
  bool ready() const;
  // What Enter would do from where the cursor stands.
  PartyAction Chosen() const;
  // The party the cursor is on, for joining it. Empty unless Chosen() is
  // kJoin.
  std::string selected_party_id() const;
  // The account of the member the cursor is on, and the name to ask the
  // player about them by. Empty unless the cursor is on a member's row.
  std::string selected_member() const;
  std::string selected_member_name() const;

  // The member menu, which only the leader raises. On the leader's own row
  // Kick and Promote are dimmed: there is nobody there to do either to.
  void OpenMenu();
  void CloseMenu();
  bool menu_open() const {
    return menu_open_;
  }
  void MoveMenuCursor(int delta);
  // Which entry the menu cursor is on, as a PartyMenuItem.
  int menu_selected() const;

 private:
  // Rows the list holds, which is one per party or one per member.
  int ListRows() const;
  // The buttons under the list, left to right, for the state the panel is in.
  std::vector<PartyButton> Buttons() const;
  // Whether the cursor is on the button row rather than in the list.
  bool on_buttons() const;
  // The member playing under `account_id`, or null.
  const PartyMember* MemberOf(const std::string& account_id) const;
  // The parties worth listing: every open one that is not already full.
  std::vector<const Party*> OpenParties() const;

  ftxui::Element RenderPartyList() const;
  ftxui::Element RenderMembers() const;
  ftxui::Element RenderButtons() const;
  // The row the member menu opens on, measured from the top of the window.
  int MenuRow() const;

  MultiplayerSnapshot snapshot_;
  // Where the cursor stands: one stop per list row, then the button row.
  int cursor_ = 0;
  // Which button the cursor is on once it is down there. Held while it is up
  // in the list, so stepping down comes back to the button it left.
  int button_ = 0;
  ItemMenu menu_;
  bool menu_open_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_PARTY_SELECT_PANEL_H_
