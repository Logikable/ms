/* LegionPanel is the Legion screen, opened from Characters on the menu: the
 * account's Legion points and who gives what.
 *
 * One window with two tabs. Grid has a preset row straight under them (the
 * same Use and Move menu as the Hyper tab) and the sixteen stats, the base
 * eight above a rule and the eight the Legion's rank opens below it, each with
 * its amount, [-], points out of its cap and [+]. An expanded stat greys out
 * while its cap is 0, and the cursor skips it. [Reset] sits under its own rule.
 * Members lists every ranked character from the highest level down, with the
 * points they give and their job effect; nothing there is selectable, and Up
 * and Down scroll it.
 *
 * The Legion is the account's. The panel writes the account's copy and mirrors
 * it onto the played character, whose stats read it.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_LEGION_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_LEGION_PANEL_H_

#include <map>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "src/character/legion.h"
#include "src/character/stat_preset.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/legion.pb.h"

namespace ms {

enum class LegionTab {
  kGrid,
  kMembers,
};

// What the cursor is on. Members has no stop of its own: the cursor stays on
// the tab row and Up and Down scroll the list.
enum class LegionZone {
  kTabs,
  kPresets,
  kStat,
  kReset,
};

// One row of the Members list.
struct LegionMemberRow {
  std::string name;
  Job job = JOB_UNSPECIFIED;
  int level = 0;
  // Past the Legion's member count: their job effect still counts, but they
  // give no points.
  bool gives_points = true;
};

class LegionPanel {
 public:
  // Fixed, so the window is one size on both tabs. 23 rows fit the smallest
  // supported terminal with the border.
  static constexpr int kContentWidth = 64;
  static constexpr int kContentRows = 23;
  // The Members list's rows: the content less the tab row, the header and
  // their two rules.
  static constexpr int kMemberRows = kContentRows - 4;

  explicit LegionPanel(GameState& state);

  // On Grid, with the cursor on the tab row and the preset the character is
  // farming with on show.
  void Reset();

  // Up and Down. On Grid they move through the stops, wrapping; on Members they
  // scroll the list.
  void MoveRow(int delta);
  // Left and Right: the tab on the tab row, the preset on the preset row, and
  // [-] or [+] on a stat.
  void MoveColumn(int delta);
  // Enter. Spends or refunds a point, or opens the preset menu. Returns true
  // when it lands on [Reset], whose question the controller asks.
  bool Activate();

  void ResetPreset();

  LegionTab tab() const {
    return tab_;
  }
  LegionZone zone() const {
    return zone_;
  }
  LegionStat stat() const {
    return stat_;
  }
  // Whether the cursor on a stat is on [+] rather than [-].
  bool on_plus() const {
    return on_plus_;
  }
  StatPreset preset() const {
    return preset_;
  }
  int first_member() const {
    return first_member_;
  }

  bool preset_menu_open() const {
    return preset_menu_open_;
  }
  void CloseMenu() {
    preset_menu_open_ = false;
  }
  void MoveMenuCursor(int delta);
  // The entry under the menu cursor, as a PresetMenuItem.
  int preset_menu_selected() const {
    return preset_menu_.selected();
  }

  // Every ranked character on the account, the played one included, from the
  // highest level down.
  std::vector<LegionMemberRow> Members() const;

  ftxui::Element Render() const;

 private:
  // The stats the cursor can reach, in order: every base stat, and each
  // expanded one with a cap above 0.
  std::vector<LegionStat> OpenStats() const;
  // Spends `delta` points in the stat under the cursor. A preset holding more
  // than counts (the rank fell since) is first cut to what counts, so the row
  // moves from the number it shows.
  void Spend(int delta);
  // The account's Legion copied onto the character after a write.
  void Mirror();

  // What the shown preset gives, per stat, at the Legion's rank and points.
  std::map<LegionStat, int> Effective() const;
  int PointsLeft() const;

  ftxui::Element RenderTabs() const;
  ftxui::Element RenderGrid() const;
  ftxui::Element RenderPresetBar() const;
  ftxui::Element RenderStat(LegionStat stat,
                            const std::map<LegionStat, int>& effective) const;
  ftxui::Element RenderMembers() const;

  GameState& state_;
  LegionTab tab_ = LegionTab::kGrid;
  LegionZone zone_ = LegionZone::kTabs;
  LegionStat stat_ = LEGION_STAT_STR;
  bool on_plus_ = true;
  StatPreset preset_ = StatPreset::kFirst;
  int first_member_ = 0;

  bool preset_menu_open_ = false;
  ItemMenu preset_menu_{{"Use", "Move", "Close"}};
  // Where the render placed the preset row and the window, so the menu can
  // hang from the row.
  mutable ftxui::Box bar_box_;
  mutable ftxui::Box panel_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_LEGION_PANEL_H_
