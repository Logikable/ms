/* PartyInspectPanel reads a party member: the sheet they sent, drawn the way
 * they see it themselves.
 *
 * Two windows, one over the other. The top is the All Stats screen's own
 * heading and stat columns, so the numbers a member is shown and the numbers
 * they see cannot come from two places. The bottom is what they are wearing,
 * with a cursor the player walks; Enter on a row opens the item's card.
 *
 * The panel rebuilds the member's character from their sheet against this
 * build's own catalogs -- a sheet names its items rather than describing them.
 * It is a view: the controller reads the cursor and decides what to open.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_PARTY_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_PARTY_INSPECT_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/frontend/widgets/marquee.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

class PartyInspectPanel {
 public:
  // The Equipped row is the widest thing on the screen, so it sets the width
  // and the stat columns are centred over it.
  static constexpr int kContentWidth = 82;
  // How many worn items show at once when the terminal is not the constraint.
  // A full set of gear is longer than this and the frame scrolls to the
  // cursor.
  static constexpr int kListRows = 8;
  // The rows everything but the item list takes: both windows' borders, the
  // three heading rows, the two rules between the stat blocks, the three
  // rows of main stats, the eight of extras, and the Equipped column header
  // with its rule.
  static constexpr int kFixedRows = 22;
  // The shortest the item list is ever squeezed to. Past this the screen is
  // clipped instead: a list of one row says less than the terminal is small.
  static constexpr int kLeastListRows = 3;

  explicit PartyInspectPanel(GameState& state);

  // Points the panel at a party member. An item their build has and this one
  // does not is dropped, the way a save loaded against changed catalogs is.
  //
  // Called every tick with whatever the lobby last said, so a member levelling
  // or re-gearing under the reader shows it. A player who has not changed is
  // not rebuilt, and the cursor only goes back to the top when the panel is
  // pointed at somebody else.
  void SetPlayer(const PlayerInfo& player);
  // Puts the cursor on the first worn item. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` items, coming out the other end.
  void MoveCursor(int delta);
  // The rows the screen may take. The item list gives way to what is left
  // after the two stat blocks, which are the point of the screen. Zero means
  // no limit.
  void SetMaxRows(int rows) {
    max_rows_ = rows;
  }
  ftxui::Element Render() const;

  // The member as this build reads them, for whoever needs their stats.
  const CharacterInstance& character() const {
    return character_;
  }
  // The item the cursor is on, or null with nothing worn.
  const EquipInstance* selected_item() const;

 private:
  ftxui::Element RenderEquipped() const;
  // How many item rows to draw at once, for the terminal and the list there
  // are to fit.
  int VisibleRows(int items) const;
  // How many items are worn, which is how many stops the cursor has.
  int ItemCount() const;

  GameState& state_;
  // The member, rebuilt from their sheet. Held rather than rebuilt per frame:
  // a sheet arrives when something about them changes, not every tick.
  CharacterInstance character_;
  // The member as the lobby last described them, so a tick that changed
  // nothing does not rebuild them.
  PlayerInfo shown_;
  int cursor_ = 0;
  // See SetMaxRows. Zero is "as many as it takes".
  int max_rows_ = 0;
  // When the cursor last moved, for sliding a long name under its column.
  SelectionClock name_clock_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_PARTY_INSPECT_PANEL_H_
