/* BossSelectPanel is the screen for picking a boss fight. The left half is a
 * grid: one row per fight, one column per difficulty, so every fight in the
 * game can be read at once. The right half describes whichever cell is
 * highlighted -- its level, what each phase is holding, the defence it stands
 * behind, the clock, and how often it comes back. An options row runs under
 * both.
 *
 * Every window is a fixed height, so nothing on the screen moves as the cursor
 * walks the list. What does not fit slides: a name too long for its column
 * runs under it, and the rewards scroll inside whatever the fight above them
 * leaves.
 *
 * Tab walks the three windows. On the grid, Up and Down move between fights
 * and Left and Right between difficulties; the column belongs to the grid
 * rather than to a fight, so leaving the cursor on Chaos and moving down lands
 * on the next fight's Chaos, and only a fight with fewer difficulties than the
 * widest pulls it back to its own last one. On the fight card, Up and Down
 * scroll the rewards.
 *
 * The panel is a view: it moves its own cursor and never writes to the game
 * state. The controller reads selected_boss() when the player confirms.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BOSS_SELECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BOSS_SELECT_PANEL_H_

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// The name column of the grid, in cells. A name over it slides under the
// column rather than reaching the Difficulty column beside it.
inline constexpr int kBossNameWidth = 21;
// The detail card beside it: a label, a right-aligned value, and the column of
// clearance on either side that every row here has. The scroll bar's lane
// stands outside it.
inline constexpr int kDetailWidth = 29;

// The rows each of the two panels takes, borders included. Fixed rather than
// fitted, so that walking the list -- where one fight has more drops than the
// next -- moves nothing on the screen.
inline constexpr int kBossPanelHeight = 25;
// The fights the grid has room for: its rows less the header and the rule
// under it. A catalog over this loses its tail, so the data is tested here.
inline constexpr int kBossListCapacity = kBossPanelHeight - 2 - 2;

// Which of the screen's windows the arrows reach. Tab walks the ring.
enum class BossPanel { kList, kFight, kOptions };
inline constexpr int kBossPanelCount = 3;

// What one phase is holding, for the detail panel and for anything else that
// wants to price a fight without walking the spawn list itself.
int64_t PhaseHp(const GameState& state, const BossPhase& phase);
// The highest level among the mobs a difficulty spawns, which is the level the
// fight is fought at. 0 for one whose spawns the catalog does not know.
int BossLevel(const GameState& state, const BossDifficulty& difficulty);

class BossSelectPanel {
 public:
  explicit BossSelectPanel(const GameState& state);

  // Puts the cursor back on the first fight's easiest difficulty, on the grid.
  // Call when the screen opens.
  void Reset();
  // Moves focus `delta` windows round the ring.
  void SwitchPanel(int delta);
  // Up and Down. On the grid they move `delta` fights, coming out the other
  // end; on the fight card they scroll its rewards, which stop at both ends.
  void MoveCursor(int delta);
  // Left and Right: `delta` columns across the grid, clamped to its ends --
  // there is no wrapping here, because a difficulty ladder has a top and a
  // bottom the player should feel. Only the grid reads them.
  void ChangeDifficulty(int delta);
  // `now` drives the sliding names, whose phase comes off the clock so that
  // every one of them slides on the same beat. Tests pass their own.
  ftxui::Element Render(std::chrono::steady_clock::time_point now =
                            std::chrono::steady_clock::now()) const;

  BossPanel focus() const {
    return focus_;
  }

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
  // Whether the highlighted fight is written down but not built yet. Such a
  // fight is listed and readable but can never be entered.
  bool selected_coming_soon() const;
  // How often the highlighted fight comes back, for a notice that has to say
  // when. RESET_PERIOD_UNSPECIFIED when there is no fight.
  ResetPeriod selected_reset() const;

 private:
  ftxui::Element RenderBossList(
      std::chrono::steady_clock::time_point now) const;
  // One cell of the difficulty grid, padded to its column. Blank for a fight
  // with fewer difficulties than the widest one has.
  ftxui::Element RenderDifficultyCell(int boss, int at) const;
  ftxui::Element RenderDetail(std::chrono::steady_clock::time_point now) const;
  // The empty row under both panels, held open so that filling it later does
  // not move the screen.
  ftxui::Element RenderOptions() const;
  // The fight's name across the head of its card, sliding when it is long.
  ftxui::Element RenderDetailTitle(
      std::chrono::steady_clock::time_point now) const;

  // One row of the rewards. A separator spans the whole card, the scroll bar's
  // lane included, so the rule meets the border on both sides.
  struct RewardRow {
    ftxui::Element element;
    bool separator = false;
  };
  // The fight card cut where the scroll bar starts: everything about the fight
  // above, the rewards that scroll under it below. Built by the render and by
  // the scroll, which needs the count to clamp against.
  struct DetailRows {
    std::vector<ftxui::Element> head;
    std::vector<RewardRow> rewards;
  };
  DetailRows BuildDetail(const BossDifficulty& difficulty,
                         std::chrono::steady_clock::time_point now) const;
  // Appends the rewards the window has room for, each with its cell of the
  // scroll bar. `rows` is the head, whose height is what is left to them.
  void AppendRewardWindow(std::vector<ftxui::Element>& rows,
                          std::vector<RewardRow>& rewards) const;
  // Scrolls the rewards `delta` rows, stopping at both ends.
  void ScrollRewards(int delta);
  // Appends one HP row per phase. "HP" alone for a fight that is one monster
  // standing there; a fight with phases numbers them, because then which HP
  // it is matters.
  void RenderPhaseHp(std::vector<ftxui::Element>& rows,
                     const BossDifficulty& difficulty) const;
  // Appends the reward rows to the detail panel: what every clear pays, then
  // the drops -- the shard and its like, then the prizes under a rule of their
  // own, commonest first.
  void RenderRewards(std::vector<RewardRow>& rows,
                     const BossDifficulty& difficulty,
                     std::chrono::steady_clock::time_point now) const;
  // Appends one drop's name and chance, sliding a name too long for the
  // column it stands in.
  void RenderDropRow(std::vector<RewardRow>& rows, const MobDrop& drop,
                     std::chrono::steady_clock::time_point now) const;

  // Columns in the grid: what the fight with the most difficulties has.
  int Columns() const;
  // The difficulty `boss` is standing on: the grid's column, pulled back to
  // the fight's own last one when it has fewer than the widest has.
  int DifficultyAt(int boss) const;

  // Whether `character` has reached the level `difficulty` opens at.
  bool Unlocked(const BossDifficulty& difficulty) const;

  const GameState& state_;
  // Boss keys in display order: the level they open at, then the level they
  // are fought at, then name. The UNLOCK leads, being the order a player meets
  // them in -- two fights of the same level can open decades apart.
  std::vector<std::string> bosses_;
  // The column the cursor is in, held across the whole grid so that moving up
  // and down stays on the difficulty the player chose.
  int column_ = 0;
  int selected_ = 0;
  BossPanel focus_ = BossPanel::kList;
  // The first reward row drawn. Back to the top whenever the cursor moves: a
  // fight with two drops has nothing to show at another fight's offset.
  int scroll_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BOSS_SELECT_PANEL_H_
