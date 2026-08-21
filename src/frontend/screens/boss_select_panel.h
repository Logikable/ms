/* BossSelectPanel is the screen for picking a boss fight. The left half lists
 * the fights, one per row, with the difficulty the cursor has chosen for each;
 * the right half describes whichever is highlighted -- its level, what each
 * phase is holding, the defence it stands behind, the clock, and how often it
 * comes back.
 *
 * Up and Down move between fights, Left and Right between difficulties. A
 * difficulty is remembered per fight, so walking down the list and back comes
 * back to the one that was chosen.
 *
 * The panel is a view: it moves its own cursor and never writes to the game
 * state. The controller reads selected_boss() when the player confirms.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BOSS_SELECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BOSS_SELECT_PANEL_H_

#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// What one phase is holding, for the detail panel and for anything else that
// wants to price a fight without walking the spawn list itself.
int64_t PhaseHp(const GameState& state, const BossPhase& phase);
// The highest level among the mobs a difficulty spawns, which is the level the
// fight is fought at. 0 for one whose spawns the catalog does not know.
int BossLevel(const GameState& state, const BossDifficulty& difficulty);

class BossSelectPanel {
 public:
  explicit BossSelectPanel(const GameState& state);

  // Puts the cursor back on the first fight. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` fights, coming out the other end.
  void MoveCursor(int delta);
  // Moves `delta` difficulties within the highlighted fight, clamped to its
  // ends -- there is no wrapping here, because a difficulty ladder has a top
  // and a bottom the player should feel.
  void ChangeDifficulty(int delta);
  ftxui::Element Render() const;

  // Key into GameState::bosses of the highlighted fight; empty when there are
  // none.
  const std::string& selected_boss() const;
  // Index into that boss's difficulties.
  int selected_difficulty() const;
  // The highlighted fight's difficulty, or null when there is no fight.
  const BossDifficulty* selected() const;
  // What the confirmation asks about: "Normal Zakum".
  std::string selected_title() const;
  // Whether the highlighted fight can be entered right now, which is to say
  // its last clear has expired.
  bool selected_available() const;
  // Whether the character has reached the level the highlighted difficulty
  // opens at. A locked fight can still be highlighted -- that is how the
  // player reads the level it wants -- but not entered.
  bool selected_unlocked() const;
  // The level the highlighted difficulty opens at, 0 for one with no gate of
  // its own.
  int selected_unlock_level() const;
  // How often the highlighted fight comes back, for a notice that has to say
  // when. RESET_PERIOD_UNSPECIFIED when there is no fight.
  ResetPeriod selected_reset() const;

 private:
  ftxui::Element RenderBossList() const;
  ftxui::Element RenderDetail() const;
  // Appends the reward rows -- the clear's meso, then a name and a chance per
  // drop -- to the detail panel.
  void RenderRewards(std::vector<ftxui::Element>& rows,
                     const BossDifficulty& difficulty) const;
  // What a drop is called, out of whichever catalog holds it. Empty for one
  // neither does, which is a drop nothing would be granted for.
  std::string RewardName(const MobDrop& drop) const;

  // Whether `character` has reached the level `difficulty` opens at.
  bool Unlocked(const BossDifficulty& difficulty) const;

  const GameState& state_;
  // Boss keys in display order: by the level they open at, then by the level
  // they are fought at, then by name so equal fights hold a stable order. The
  // unlock leads because that is the order a player meets them in, and two
  // fights of the same level can open decades apart. Fixed at construction,
  // since the catalog is static data.
  std::vector<std::string> bosses_;
  // The difficulty chosen for each, parallel to bosses_.
  std::vector<int> difficulties_;
  int selected_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BOSS_SELECT_PANEL_H_
