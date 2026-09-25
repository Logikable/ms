/* MobInspectPanel is the bestiary: a map's monsters, one at a time. The left
 * half lists the map's mobs (the same name, level and count as the map select
 * screen), and the right half shows what the player might want to know about
 * the one under the cursor: its description, what killing it gives, and what it
 * drops.
 *
 * The description at the top always takes four lines, so the numbers below stay
 * still as the cursor moves. Below the rule the panel splits in two: stats on
 * the left, where a label and number need little room, and drops on the right,
 * where a name needs more. Both columns can grow downwards, since that is the
 * end of the panel and nothing the eye is tracking moves when they do.
 *
 * The panel only displays. The controller tells it which map to list and moves
 * its cursor, and it never changes the game state.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_MOB_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_MOB_INSPECT_PANEL_H_

#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/game_state.h"
#include "src/protos/mob.pb.h"

namespace ms {

// The width of the bestiary description and the rows it always takes. A data
// test keeps every description within them, because one that overflowed would
// push that mob's stats down.
inline constexpr int kFlavourWidth = 50;
inline constexpr int kFlavourLines = 4;

class MobInspectPanel {
 public:
  explicit MobInspectPanel(const GameState& state);

  // Lists the mobs of `map` and puts the cursor on the first. Call when the
  // screen opens.
  void SetMap(const std::string& map);
  // Moves the cursor `delta` mobs, wrapping at the ends.
  void MoveCursor(int delta);
  ftxui::Element Render() const;

  // The mob under the cursor, by data file stem, or empty when the map has none
  // the catalog knows.
  std::string selected_mob() const;

 private:
  ftxui::Element RenderMobList() const;
  // The map's force requirement, what the character has against it, and what
  // that does to the fight. Adds nothing on a map that requires none.
  void RenderForce(std::vector<ftxui::Element>& rows) const;
  ftxui::Element RenderInfo() const;
  // The description, padded to kFlavourLines rows.
  void RenderFlavour(std::vector<ftxui::Element>& rows, const Mob& mob) const;
  // The left column: Level, HP, EXP, Attack, and the value of one meso drop.
  ftxui::Element RenderStats(const Mob& mob) const;
  // The right column: the meso's own chance first, then a name and chance per
  // drop.
  ftxui::Element RenderDrops(const Mob& mob) const;

  const GameState& state_;
  std::string map_;
  // Mob keys in spawn order, each with how many spawn on the map. Stored rather
  // than recomputed because the catalog may not know every spawn, and then a
  // row per spawn would number the cursor differently from the list.
  std::vector<std::pair<std::string, int>> mobs_;
  int selected_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_MOB_INSPECT_PANEL_H_
