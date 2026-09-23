/* LinkSkillPanel is the Link Skills screen: the skill this character's own job
 * line hands them, the twelve they carry, and everything else the account has
 * climbed.
 *
 * Three windows down the screen, their columns lined up under the one header
 * the top window carries. Tab and Shift+Tab walk the windows and Up and Down
 * walk the rows of whichever holds the cursor. The middle window's top row is
 * its preset bar: Left and Right pick which set of twelve is being read, and
 * Enter there raises the preset menu the Hyper tab raises.
 *
 * The panel reads and writes the character's presets and puts up its own
 * menus. It asks nothing: what an entry does is the controller's, which is
 * also where the skill card and the notification live.
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

// The three windows, in the order Tab walks them.
enum class LinkZone {
  kMine,
  kEnabled,
  kAll,
};

// What the cursor is standing on.
struct LinkCursor {
  enum class Kind {
    // The preset bar, which only the middle window has.
    kPreset,
    kSkill,
    // The window under the cursor has nothing to stand on: an empty preset,
    // or a character whose own line hands them nothing yet.
    kNothing,
  };
  Kind kind = Kind::kNothing;
  // The skill under the cursor, null for every other kind.
  const Skill* skill = nullptr;
};

// What the entry under the menu cursor does. The three row menus share Inspect
// and Close and differ only by the entry between them, so the controller reads
// the choice rather than which menu is open.
enum class LinkMenuChoice {
  kInspect,
  kAdd,
  kRemove,
  kClose,
};

class LinkSkillPanel {
 public:
  // `skills` is the loaded catalog; the panel lists the entries carrying a
  // Skill.link_line.
  LinkSkillPanel(CharacterInstance& character,
                 const std::map<std::string, Skill>& skills);
  // The catalog is held by reference, so a temporary one would dangle.
  LinkSkillPanel(CharacterInstance& character,
                 std::map<std::string, Skill>&& skills) = delete;

  // The cursor on the top window, reading the preset the character is
  // playing, with no menu up.
  void Reset();

  // Tab and Shift+Tab. The ring comes round at both ends.
  void NextZone(int delta);
  // Up and Down inside the window holding the cursor, wrapping at both ends --
  // in the middle window the preset bar is the stop above the first row.
  void MoveRow(int delta);
  // Left and Right on that bar, which clamp at the ends as every tab bar in
  // the game does.
  void MovePreset(int delta);

  LinkZone zone() const {
    return zone_;
  }
  LinkCursor cursor() const;
  // The preset being read, which is the one every add and remove lands in.
  StatPreset preset() const {
    return preset_;
  }

  // Puts the skill under the cursor into that preset. False when it already
  // holds kMaxEquippedLinkSkills, which is the one refusal worth a word.
  bool AddSelected();
  // Takes it back off. Does nothing on a cursor holding no skill.
  void RemoveSelected();

  // The level the skill under the cursor stands at: what the account has
  // climbed on its line, whether or not this character carries it yet.
  int SelectedLevel() const;

  // Raises the menu for the window the cursor is in, or the preset menu on the
  // bar. Neither happens on a cursor with nothing under it.
  void OpenMenu();
  void CloseMenu();
  bool menu_open() const {
    return menu_open_;
  }
  bool preset_menu_open() const {
    return preset_menu_open_;
  }
  void MoveMenuCursor(int delta);
  // What Enter on the open row menu does.
  LinkMenuChoice menu_choice() const;
  // And on the open preset menu, as a PresetMenuItem.
  int preset_menu_selected() const {
    return preset_menu_.selected();
  }

  ftxui::Element Render() const;

 private:
  // One row of a list, laid out in the three columns the header names.
  ftxui::Element RenderRow(const Skill& skill, bool on_cursor,
                           ftxui::Box& box) const;
  // What `skill` is worth to this character: the rungs the account climbed,
  // plus whatever their book lends a skill -- the level its card heads with.
  int LevelOf(const Skill& skill) const;
  // That level as the column prints it, the lent part in brackets.
  std::string LevelText(const Skill& skill) const;
  // The header the top window carries for all three, over the game's usual
  // rule: the other two sit under it and need no second copy.
  ftxui::Element RenderHeader() const;
  ftxui::Element RenderMine() const;
  ftxui::Element RenderEnabled() const;
  ftxui::Element RenderAll() const;
  // The preset bar: one chip per slot, named for the activity it answers while
  // the autoswap is on and numbered while it is off.
  ftxui::Element RenderPresetBar() const;
  // A blank row of the right width, for the slots a preset has not filled.
  ftxui::Element BlankRow() const;

  // The skill this character's own line hands them, null while they have no
  // line -- a Beginner, or a job line with no link skill written.
  const Skill* MineSkill() const;
  // The skills the selected preset carries that pay something, in the order
  // it holds them.
  std::vector<const Skill*> EnabledSkills() const;
  // Everything else the account has climbed a rung on, catalog order.
  std::vector<const Skill*> AllSkills() const;
  // The list the cursor is walking, empty in a window with no rows.
  std::vector<const Skill*> RowsHere() const;
  // The stops in the window holding the cursor: its rows, and the preset bar
  // above them in the middle one.
  int StopsHere() const;
  // The row of `rows` the cursor is on, held inside it.
  int ClampedRow(const std::vector<const Skill*>& rows) const;
  // Where a menu hangs: one row back from the row it is about, in the panel's
  // own coordinates.
  int MenuRow() const;

  // Which row of the window holding the cursor it is on, before clamping.
  int RowHere() const;

  CharacterInstance& character_;
  const std::map<std::string, Skill>& skills_;

  LinkZone zone_ = LinkZone::kMine;
  StatPreset preset_ = StatPreset::kFirst;
  // The cursor's row in the two lists, and whether the middle window's cursor
  // is on its rows rather than on the preset bar.
  int enabled_row_ = 0;
  int all_row_ = 0;
  bool enabled_in_list_ = true;

  bool menu_open_ = false;
  bool preset_menu_open_ = false;
  ItemMenu mine_menu_{{"Inspect", "Close"}};
  ItemMenu enabled_menu_{{"Inspect", "Remove", "Close"}};
  ItemMenu all_menu_{{"Inspect", "Add", "Close"}};
  ItemMenu preset_menu_{{"Use", "Move", "Close"}};

  // The menu the open window uses, so the three need no switch at every call.
  ItemMenu& OpenMenuHere();
  const ItemMenu& OpenMenuHere() const;

  // A name or an effect too wide for its column slides under it while the row
  // is selected. One clock: only one row is ever selected.
  mutable SelectionClock name_clock_;
  // Where the cursor and the preset bar were drawn, for anchoring a menu
  // beside them -- read from the render, as every other screen's is.
  mutable ftxui::Box cursor_box_;
  mutable ftxui::Box bar_box_;
  mutable ftxui::Box panel_box_;
  mutable ftxui::Box scratch_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_LINK_SKILL_PANEL_H_
