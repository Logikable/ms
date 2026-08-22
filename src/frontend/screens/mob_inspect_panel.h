/* MobInspectPanel is the bestiary: what a map's monsters are, read one at a
 * time. The left half lists the mobs of one map -- the same name, level and
 * count the map select screen shows -- and the right half is everything the
 * player might want to know about whichever the cursor is on: what the world
 * says about it, what it is worth killing, and what falls off it.
 *
 * The blurb at the top is held to a fixed four lines whether the mob has one
 * or not, so the numbers under it stand still as the cursor walks the list.
 * Below the drops the panel is free to grow: that is the end of it, and
 * nothing the eye is holding its place in moves when it does.
 *
 * The panel is a view. The controller tells it which map to list and moves its
 * cursor; it never writes to the game state.
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

// Columns the bestiary blurb is set in, and the rows it always takes. A data
// test holds every shipped description inside them, because a blurb that
// overruns would push the stats down for that one mob.
inline constexpr int kFlavourWidth = 50;
inline constexpr int kFlavourLines = 4;

class MobInspectPanel {
 public:
  explicit MobInspectPanel(const GameState& state);

  // Lists the mobs of `map` and puts the cursor on the first of them. Call
  // when the screen opens.
  void SetMap(const std::string& map);
  // Moves the cursor `delta` mobs, coming out the other end.
  void MoveCursor(int delta);
  ftxui::Element Render() const;

  // The mob the cursor is on, by data file stem; empty when the map stands
  // none the catalog knows.
  std::string selected_mob() const;

 private:
  ftxui::Element RenderMobList() const;
  ftxui::Element RenderInfo() const;
  // The blurb, padded out to kFlavourLines rows.
  void RenderFlavour(std::vector<ftxui::Element>& rows, const Mob& mob) const;
  // Level, HP, EXP, Attack, and what one meso drop is worth.
  void RenderStats(std::vector<ftxui::Element>& rows, const Mob& mob) const;
  // The meso's own chance first, then a name and a chance per drop.
  void RenderDrops(std::vector<ftxui::Element>& rows, const Mob& mob) const;

  const GameState& state_;
  std::string map_;
  // Mob keys in spawn order, each with how many of it stand on the map. Held
  // rather than walked because the catalog may not know every spawn, and a row
  // per spawn would then number the cursor differently from the list.
  std::vector<std::pair<std::string, int>> mobs_;
  int selected_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_MOB_INSPECT_PANEL_H_
