/* BossSelectPanel is the screen for choosing a boss fight. The left half is a
 * grid with one row per fight and one column per difficulty, so every fight in
 * the game can be read at once. The right half describes the highlighted cell:
 * its level, what each phase holds, its defence, the clock, and how often it
 * resets. An options row runs under both.
 *
 * Every window is a fixed height, so nothing on the screen moves as the cursor
 * moves through the list. What doesn't fit scrolls: a name too long for its
 * column scrolls inside it, and the rewards scroll in whatever space the fight
 * details leave.
 *
 * The options row holds the switches a fight is started with (currently only
 * Practice), which the player toggles with Enter and the server applies to the
 * whole party.
 *
 * Tab moves between the three windows. On the grid, Up and Down move between
 * fights and Left and Right between difficulties. The column belongs to the
 * grid rather than to a fight, so moving down from Chaos lands on the next
 * fight's Chaos; only a fight with fewer difficulties pulls the cursor back to
 * its own last one. On the fight card, Up and Down scroll the rewards.
 *
 * The panel only displays: it moves its own cursor and never changes the game
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

// The width of the grid's name column. A longer name scrolls inside the column
// instead of reaching the Difficulty column beside it.
inline constexpr int kBossNameWidth = 21;
// The detail card beside it: a label, a right-aligned value, and the blank
// column on each side that every row here has. The scroll bar's lane is outside
// it.
inline constexpr int kDetailWidth = 29;

// The rows each of the two panels takes, borders included. Fixed rather than
// fitted, so moving through the list (where one fight has more drops than the
// next) doesn't move anything on the screen.
inline constexpr int kBossPanelHeight = 25;
// The number of fights the grid has room for: its rows minus the header and the
// rule under it. A catalog with more loses its end, so the data is tested
// against this.
inline constexpr int kBossListCapacity = kBossPanelHeight - 2 - 2;

// Which window the arrows control. Tab moves around the ring.
enum class BossPanel { kList, kFight, kOptions };
inline constexpr int kBossPanelCount = 3;

// What one phase holds, for the detail panel and anything else that needs a
// fight's HP without walking the spawn list itself.
int64_t PhaseHp(const GameState& state, const BossPhase& phase);
// The highest level among the mobs a difficulty spawns, which is the level the
// fight is fought at. 0 when the catalog doesn't know its spawns.
int BossLevel(const GameState& state, const BossDifficulty& difficulty);

class BossSelectPanel {
 public:
  explicit BossSelectPanel(const GameState& state);

  // Puts the cursor back on the first fight's easiest difficulty, on the grid.
  // Call when the screen opens.
  void Reset();
  // Moves focus `delta` windows around the ring.
  void SwitchPanel(int delta);
  // Up and Down. On the grid they move `delta` fights, wrapping at the ends. On
  // the fight card they scroll its rewards, which stop at both ends.
  void MoveCursor(int delta);
  // Left and Right: `delta` columns across the grid, stopping at the ends.
  // There is no wrapping, since the player should feel that a difficulty ladder
  // has a top and a bottom. Only the grid uses them.
  void ChangeDifficulty(int delta);
  // `now` drives the scrolling names, which all take their position from the
  // clock so they scroll in step. Tests pass their own.
  ftxui::Element Render(std::chrono::steady_clock::time_point now =
                            std::chrono::steady_clock::now()) const;

  BossPanel focus() const {
    return focus_;
  }
  // The option under the cursor, as an index into the row.
  int selected_option() const {
    return option_;
  }
  // Whether the fight would be a practice run: no clear used and no reward
  // paid. Read from the game state, which is what the server is told.
  bool practice() const;

  // The GameState::bosses key of the highlighted fight, or empty when there are
  // none.
  const std::string& selected_boss() const;
  // The index into that boss's difficulties.
  int selected_difficulty() const;
  // The highlighted fight's difficulty, or null when there is no fight.
  const BossDifficulty* selected() const;
  // The name the confirmation asks about: "Normal Zakum".
  std::string selected_title() const;
  // Whether the highlighted fight can be entered now, meaning its last clear
  // has expired.
  bool selected_available() const;
  // Whether the character has reached the level the highlighted difficulty
  // unlocks at. A locked fight can still be highlighted, which is how the
  // player sees the level it needs, but can't be entered.
  bool selected_unlocked() const;
  // The level the highlighted difficulty unlocks at, or 0 if it has no gate of
  // its own.
  int selected_unlock_level() const;
  // Whether the highlighted fight is defined but not built yet. Such a fight is
  // listed and readable but can never be entered.
  bool selected_coming_soon() const;
  // How often the highlighted fight resets, for a notice that has to say when.
  // RESET_PERIOD_UNSPECIFIED when there is no fight.
  ResetPeriod selected_reset() const;

 private:
  ftxui::Element RenderBossList(
      std::chrono::steady_clock::time_point now) const;
  // One cell of the difficulty grid, padded to its column. Blank for a fight
  // with fewer difficulties than the widest one.
  ftxui::Element RenderDifficultyCell(int boss, int at) const;
  ftxui::Element RenderDetail(std::chrono::steady_clock::time_point now) const;
  // The switches under both panels, each a box and its name.
  ftxui::Element RenderOptions() const;
  // The fight's name across the top of its card, scrolling when it is long.
  ftxui::Element RenderDetailTitle(
      std::chrono::steady_clock::time_point now) const;

  // One row of the rewards. A separator spans the whole card, including the
  // scroll bar's lane, so the rule meets the border on both sides.
  struct RewardRow {
    ftxui::Element element;
    bool separator = false;
  };
  // The fight card split where the scroll bar starts: everything about the
  // fight above, and the scrolling rewards below. Built by both the render and
  // the scroll, which needs the count to clamp against.
  struct DetailRows {
    std::vector<ftxui::Element> head;
    std::vector<RewardRow> rewards;
  };
  DetailRows BuildDetail(const BossDifficulty& difficulty,
                         std::chrono::steady_clock::time_point now) const;
  // Appends the rewards the window has room for, each with its scroll bar cell.
  // `rows` is the top part, whose height decides how much room is left.
  void AppendRewardWindow(std::vector<ftxui::Element>& rows,
                          std::vector<RewardRow>& rewards) const;
  // Scrolls the rewards `delta` rows, stopping at both ends.
  void ScrollRewards(int delta);
  // Appends one HP row per phase. A fight with a single monster shows just
  // "HP"; a fight with phases numbers them, since then which HP it is matters.
  void RenderPhaseHp(std::vector<ftxui::Element>& rows,
                     const BossDifficulty& difficulty) const;
  // Appends the reward rows to the detail panel: what every clear pays, then
  // the drops (the shard and similar), then the prizes under their own rule,
  // most common first.
  void RenderRewards(std::vector<RewardRow>& rows,
                     const BossDifficulty& difficulty,
                     std::chrono::steady_clock::time_point now) const;
  // Appends one drop's name and chance, scrolling a name too long for its
  // column.
  void RenderDropRow(std::vector<RewardRow>& rows, const MobDrop& drop,
                     std::chrono::steady_clock::time_point now) const;

  // The number of grid columns: as many as the fight with the most
  // difficulties.
  int Columns() const;
  // The difficulty `boss` is on: the grid's column, pulled back to the fight's
  // own last one when it has fewer.
  int DifficultyAt(int boss) const;

  // Whether `character` has reached the level `difficulty` unlocks at.
  bool Unlocked(const BossDifficulty& difficulty) const;

  const GameState& state_;
  // Boss keys in display order: by the easiest difficulty's unlock level, then
  // its HP, then name. The unlock level comes first because that is the order
  // a player meets them in.
  std::vector<std::string> bosses_;
  // The column the cursor is in, kept across the whole grid so moving up and
  // down stays on the difficulty the player chose.
  int column_ = 0;
  int selected_ = 0;
  BossPanel focus_ = BossPanel::kList;
  // The switch on the options row under the cursor.
  int option_ = 0;
  // The first reward row drawn. Reset to the top whenever the cursor moves,
  // since a fight with two drops has nothing to show at another fight's offset.
  int scroll_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BOSS_SELECT_PANEL_H_
