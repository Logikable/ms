/* LinkSkillPanel is the Link Skills screen: the skill this character's own job
 * line gives them, the twelve they have equipped, and everything else the
 * account has earned.
 *
 * Three windows, one above the other, with their columns lined up under the
 * single header in the top window. Tab and Shift+Tab move between windows, and
 * Up and Down move through the rows of the one with the cursor. The middle
 * window's top row is its preset bar: Left and Right choose which set of twelve
 * is shown, and Enter there opens the same preset menu as the Hyper tab.
 *
 * The panel reads and writes the character's presets and opens its own menus.
 * It asks nothing itself: the controller decides what an entry does, and also
 * shows the skill card and notifications.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_LINK_SKILL_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_LINK_SKILL_PANEL_H_

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "src/character/character.h"
#include "src/character/stat_preset.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/skill.pb.h"

namespace ms {

// The three windows, in the order Tab moves through them.
enum class LinkZone {
  kMine,
  kEnabled,
  kAll,
};

// What the cursor is on.
struct LinkCursor {
  enum class Kind {
    // The preset bar, which only the middle window has.
    kPreset,
    kSkill,
    // The window with the cursor has nothing to select: an empty preset, or a
    // Beginner, who has no job line of their own.
    kNothing,
  };
  Kind kind = Kind::kNothing;
  // The skill under the cursor, null for every other kind.
  const Skill* skill = nullptr;
};

// What the entry under the menu cursor does. The three row menus share Inspect
// and Close and differ only in the entry between them, so the controller reads
// the choice rather than which menu is open.
enum class LinkMenuChoice {
  kInspect,
  kAdd,
  kRemove,
  kClose,
};

class LinkSkillPanel {
 public:
  // `skills` is the loaded catalog. The panel lists the entries that have a
  // Skill.link_line.
  LinkSkillPanel(CharacterInstance& character,
                 const std::map<std::string, Skill>& skills);
  // The catalog is held by reference, so a temporary one would dangle.
  LinkSkillPanel(CharacterInstance& character,
                 std::map<std::string, Skill>&& skills) = delete;

  // The cursor on the top window, showing the preset the character is using,
  // with no menu open.
  void Reset();

  // Tab and Shift+Tab, wrapping at both ends.
  void NextZone(int delta);
  // Up and Down inside the window with the cursor, wrapping at both ends. In
  // the middle window the preset bar is the stop above the first row.
  void MoveRow(int delta);
  // Left and Right on that bar, which stop at the ends like every tab bar in
  // the game.
  void MovePreset(int delta);

  LinkZone zone() const {
    return zone_;
  }
  LinkCursor cursor() const;
  // The preset being shown, which every add and remove goes into.
  StatPreset preset() const {
    return preset_;
  }

  // Puts the skill under the cursor into that preset. Returns false when it
  // already has kMaxEquippedLinkSkills, the one refusal worth a message.
  bool AddSelected();
  // Removes it again. Does nothing when the cursor has no skill.
  void RemoveSelected();

  // The level of the skill under the cursor: what the account has earned on its
  // line, whether or not this character has it equipped yet.
  int SelectedLevel() const;

  // Opens the menu for the window with the cursor, or the preset menu on the
  // bar. Neither opens when nothing is under the cursor.
  void OpenMenu();
  void CloseMenu();
  bool menu_open() const {
    return menu_open_;
  }
  bool preset_menu_open() const {
    return preset_menu_open_;
  }
  void MoveMenuCursor(int delta);
  // What Enter does on the open row menu.
  LinkMenuChoice menu_choice() const;
  // The same for the open preset menu, as a PresetMenuItem.
  int preset_menu_selected() const {
    return preset_menu_.selected();
  }

  ftxui::Element Render() const;

 private:
  // One row of a list, in the three columns the header names.
  ftxui::Element RenderRow(const Skill& skill, bool on_cursor,
                           ftxui::Box& box) const;
  // What `skill` is worth to this character: the levels the account earned,
  // plus any bonus levels their book gives, which is the level its card shows.
  int LevelOf(const Skill& skill) const;
  // That level as the column shows it, with the bonus in brackets.
  std::string LevelText(const Skill& skill) const;
  // The header in the top window, shared by all three, above the usual rule.
  // The other two sit under it and need no copy.
  ftxui::Element RenderHeader() const;
  ftxui::Element RenderMine() const;
  ftxui::Element RenderEnabled() const;
  ftxui::Element RenderAll() const;
  // The preset bar: one chip per slot, named for its activity while the
  // autoswap is on and numbered while it is off.
  ftxui::Element RenderPresetBar() const;
  // A blank row of the right width, for a preset's unused slots.
  ftxui::Element BlankRow() const;

  // The skill this character's own line gives them, or null while they have no
  // line (a Beginner, or a job line with no link skill defined).
  const Skill* MineSkill() const;
  // The skills in the selected preset that grant something, in the preset's
  // order.
  std::vector<const Skill*> EnabledSkills() const;
  // Every other skill the account has earned at least one level in, in catalog
  // order.
  std::vector<const Skill*> AllSkills() const;
  // The list the cursor is moving through, empty in a window with no rows.
  std::vector<const Skill*> RowsHere() const;
  // The stops in the window with the cursor: its rows, plus the preset bar
  // above them in the middle window.
  int StopsHere() const;
  // The cursor's row in `rows`, clamped to the rows that exist.
  int ClampedRow(const std::vector<const Skill*>& rows) const;
  // Where a menu opens: one row back from the row it is about, in the panel's
  // own coordinates.
  int MenuRow() const;

  // The cursor's row in the window it is in, before clamping.
  int RowHere() const;

  CharacterInstance& character_;
  const std::map<std::string, Skill>& skills_;

  LinkZone zone_ = LinkZone::kMine;
  StatPreset preset_ = StatPreset::kFirst;
  // The cursor's row in each of the two lists, and whether the middle window's
  // cursor is on its rows rather than the preset bar.
  int enabled_row_ = 0;
  int all_row_ = 0;
  bool enabled_in_list_ = true;

  bool menu_open_ = false;
  bool preset_menu_open_ = false;
  ItemMenu mine_menu_{{"Inspect", "Close"}};
  ItemMenu enabled_menu_{{"Inspect", "Remove", "Close"}};
  ItemMenu all_menu_{{"Inspect", "Add", "Close"}};
  ItemMenu preset_menu_{{"Use", "Move", "Close"}};

  // The menu for the open window, so callers don't need a switch every time.
  ItemMenu& OpenMenuHere();
  const ItemMenu& OpenMenuHere() const;

  // A name or effect too wide for its column scrolls while the row is selected.
  // One clock is enough, since only one row is selected at a time.
  mutable SelectionClock name_clock_;
  // Where the cursor and the preset bar were drawn, for placing a menu beside
  // them, read from the render as on every other screen.
  mutable ftxui::Box cursor_box_;
  mutable ftxui::Box bar_box_;
  mutable ftxui::Box panel_box_;
  mutable ftxui::Box scratch_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_LINK_SKILL_PANEL_H_
