/* The character panel: the player's name, a "Lv X <job>" title, a tab bar, and
 * the selected tab's content. Stats shows the base stats and combat stats,
 * including equipment and learned passives. Skills shows one page per job
 * advancement, with its SP and skills. Hyper, Ability and Buffs appear as the
 * character reaches them, and an Advance tab appears only while an advancement
 * is pending.
 *
 * Focus moves top to bottom through zones, with Down going down and Up going
 * up. The username row is the top zone, and the tab bar is the only place Left
 * and Right switch tabs. The OnXEvent handlers below say what each zone does
 * with a key. Running out of AP or SP blocks only spending: every row stays
 * reachable, since it is still worth reading.
 */
#ifndef MS_SRC_FRONTEND_PANELS_CHARACTER_PANEL_H_
#define MS_SRC_FRONTEND_PANELS_CHARACTER_PANEL_H_

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/account.h"
#include "src/character/character.h"
#include "src/character/consumables.h"
#include "src/character/hyper_stats.h"
#include "src/frontend/panel_widths.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/marquee.h"
#include "src/frontend/widgets/text_field.h"
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// What Enter does on the Character panel, depending on where it lands:
// `allocate` on a stat's [+], `learn` on a skill's, `advance` on a job (which
// should ask for confirmation first, since the panel advances nothing itself),
// `menu` on a skill's name, and `all_stats` on the row below the stats. Only
// the last two are never gated.
//
// A struct rather than a long argument list, because tabs keep being added and
// a caller naming one action shouldn't have to count empty braces.
struct CharacterPanelActions {
  // The Stats tab.
  std::function<void(StatField)> allocate;
  std::function<void()> all_stats;
  // The Skills tab.
  std::function<void(const Skill&)> learn;
  std::function<void(const Skill&)> menu;
  // The Link Skills row at the bottom of the beginner page.
  std::function<void()> link_skills;
  // The V page's [Reset], which empties the whole matrix.
  std::function<void()> v_reset;
  // The Advance tab.
  std::function<void(Job)> advance;
  // The Hyper tab. `hyper_allocate` buys the stat's next level and
  // `hyper_lower` gives the last one back.
  std::function<void(HyperStatField)> hyper_allocate;
  std::function<void(HyperStatField)> hyper_lower;
  std::function<void()> hyper_reset;
  std::function<void(HyperStatField)> hyper_inspect;
  // The Ability tab. `ability_lock` takes the index of the line under the
  // cursor and toggles its lock. The panel doesn't know which way it goes.
  std::function<void(int)> ability_lock;
  std::function<void()> ability_reroll;
  // The Buffs tab. Enter on a row opens the buff's menu, which holds all of its
  // actions, including turning it on.
  std::function<void(ConsumableType)> buff_menu;
  // The preset row, on the tabs that spend into a preset slot. The row naming
  // the two activities has no actions and opens nothing.
  std::function<void(PresetKind, StatPreset)> preset_menu;
};

class CharacterPanel {
 public:
  // The width a skill row leaves for its name. Public because the game's skill
  // names are tested against it: a name too long for the widest panel is cut
  // off. `has_plus` is the narrow case and the default; a page with no [+]
  // gives those columns to the name.
  static int SkillNameWidth(int level_width, int row_width,
                            bool has_plus = true);

  // `skills` is the loaded catalog, keyed by file stem. The Skills tab lists
  // the entries whose stage matches the selected page.
  CharacterPanel(CharacterInstance& character, AccountInstance& account,
                 int& panel_focus, std::map<std::string, Skill> skills = {});
  ftxui::Element Render() const;

  // The most rows the panel may take, borders included; 0 means no limit. Past
  // this the Stats tab drops extra stats, which keeps the combat panel on
  // screen. It isn't read from the terminal, because only Tui knows what shares
  // the column, and a test draws at whatever size it likes.
  void SetMaxRows(int rows) {
    max_rows_ = rows;
  }
  // The width the panel may take, which the layout computes from the
  // terminal's. Extra width goes to the Skills tab's name column. The Stats tab
  // keeps its alignment either way. Set by the layout for the same reason as
  // SetMaxRows.
  void SetWidth(int width) {
    width_ = width;
  }
  // The panel as a component, responding to Enter with `actions`. An unset
  // action makes that key do nothing, so a caller that only displays passes
  // none.
  ftxui::Component MakeComponent(CharacterPanelActions actions = {});

  // The preset slot the panel is reading. The All Stats screen opens on it too,
  // so the two always show the same numbers.
  StatPreset hyper_preset() const {
    return hyper_preset_;
  }

  // The activity the selected slot is for. The panel's stats are computed for
  // it, so the Farm chip shows a farming character.
  Activity SelectedActivity() const;

  // Marks the active tab as opened, which turns off its gold. Called whenever
  // the bar moves and when the panel gets focus, since a tab already under the
  // cursor has been seen. Does nothing on a read-only panel (see SetReadOnly).
  void MarkActiveTabSeen();

  // Shows someone else's character instead of the player's, for the Inspect
  // screen. Everything that spends is removed (the [+] and [Max] buttons, both
  // [Reset]s, [Reroll], the Advance tab and the name field), along with the
  // cursor stops that only led to them. The Buffs tab is removed too, since it
  // lists what is in a bag, and the bag isn't part of the character sheet.
  //
  // No gold either way: new tabs are news to the player about their own
  // character, and reading a party member's sheet must not mark them seen.
  void SetReadOnly(bool read_only) {
    read_only_ = read_only;
  }

  // The screen row the selected skill was drawn on, for placing its menu.
  int skill_cursor_row() const {
    return skill_cursor_box_.y_min;
  }

  // The screen row the selected job was drawn on, for placing its menu. Read
  // from the render instead of computed, so the menu doesn't need to know the
  // panel's layout. It is one frame behind, which is fine.
  int job_cursor_row() const {
    return job_cursor_box_.y_min;
  }

  // The screen row the selected buff was last drawn on, for placing the buff
  // menu beside it. Read from the render, like the two above.
  int buff_cursor_row() const {
    return buff_cursor_box_.y_min;
  }

  // The screen row the preset row was drawn on, for placing its menu. There is
  // no list cursor here, so the menu hangs from the chip's own row.
  int preset_row() const {
    return preset_row_box_.y_min;
  }

  // True while the name field is taking keys, so Tui passes the player's
  // letters through instead of mapping them to bound actions.
  bool editing_username() const {
    return username_field_.editing();
  }

  // Turns the border gold during a level-up or advancement celebration, since
  // this is where what it granted is spent. The panel keeps no timer: whoever
  // turned it on turns it off.
  void SetHighlighted(bool highlighted) {
    highlighted_ = highlighted;
  }

 private:
  // The Stats tab's fixed rows: two borders, three heading rows and their rule,
  // the tab bar and its rule, HP, MP, the four AP stats, and the rule above the
  // extras. Everything else is an extra stat or the View All Stats row.
  //
  // Count any new heading row here, or the budget overruns and the combat
  // panel's last mob bar falls off a short terminal.
  // ThePanelFitsInsideItsRowBudget catches it.
  static constexpr int kStatsTabFixedRows = 15;

  // The same for the Skills tab: two borders, three heading rows and their
  // rule, the tab bar, the page bar and its rule. Everything else is a skill
  // row. The V page adds its rule and [Reset], which are never dropped.
  static constexpr int kSkillsTabFixedRows = 9;

  // The Skills tab's count, plus the V page's bottom rows.
  int SkillsTabFixedRows() const;

  // How many of the Stats tab's four AP rows the cursor stops on. None on a
  // read-only panel: there is no [+] to press, and the block is only moved
  // through to reach the View All Stats row below it.
  int StatStops() const;

  // The Stats tab's count, plus the Farm/Boss row.
  int StatsTabFixedRows() const;

  // The Hyper tab's: two borders, three heading rows and their rule, the two
  // tab rows and their rule, then the rule and [Reset] at the bottom.
  static constexpr int kHyperTabFixedRows = 11;

  // The Ability and Buffs tabs have no count of their own: each has a few fixed
  // rows, none ever dropped.

  // Whether the border is gold. Set from outside and read by Render. It isn't
  // part of the panel's own state.
  bool highlighted_ = false;
  // See SetReadOnly.
  bool read_only_ = false;
  // See SetMaxRows. Zero means no limit.
  int max_rows_ = 0;
  // See SetWidth, and panel_widths.h for where the number comes from.
  int width_ = kLeftColumnMin;

  // The width inside the window's border.
  int ContentWidth() const {
    return width_ - 2;
  }
  // One row of the Stats tab at the tab's own width, centred. The tab doesn't
  // stretch with the terminal, since a value pushed to the border would leave
  // its label behind, so a wide panel has blank space on each side of the
  // block.
  ftxui::Element StatsAligned(ftxui::Element row) const;

  // The panel's tabs, in bar order. These aren't indices into the bar, because
  // Hyper and Advance appear only when they have something to offer (see
  // VisibleTabs()).
  enum Tab : int {
    kTabStats = 0,
    kTabSkills = 1,
    kTabHyper = 2,
    kTabAbility = 3,
    kTabBuffs = 4,
    kTabAdvance = 5
  };

  // Vertical focus zones, top to bottom. Down from the outer tab bar enters the
  // active tab's first content zone.
  enum Zone {
    kZoneUsername,
    kZoneTabs,
    // The Farm/Boss row, shared by every tab that reads an allocation.
    kZonePresets,
    kZoneStatRows,
    kZoneAdvTabs,
    kZoneSkillRows,
    // The [Reset] under the V page's nodes. Only that page has one.
    kZoneVReset,
    kZoneJobRows,
    // The Hyper tab's fourteen stat rows and the [Reset] button under them.
    kZoneHyperRows,
    kZoneHyperReset,
    // The Ability tab's three line rows and the [Reroll] button under them.
    kZoneAbilityRows,
    kZoneAbilityReroll,
    // The Buffs tab's rows. One stop each, since everything a buff offers is on
    // the menu Enter opens.
    kZoneBuffRows
  };

  // The two parts of a skill row, left to right. Left and Right move between
  // them, and Enter does something different on each.
  enum SkillCol { kColName, kColPlus };

  // Whether skill rows have a [+]. A read-only sheet has nothing to press, and
  // the beginner page has nothing to buy.
  bool ShowsSkillPlus() const;
  // skill_col_, corrected for a page with no [+], since the cursor can't be on
  // a column that isn't drawn. Render reads it, like EffectiveZone.
  SkillCol EffectiveSkillCol() const;

  // A Hyper Stat row's three parts in screen order: the stat to read about, the
  // level to give back, and the point to spend. Left and Right stop at the
  // ends.
  enum HyperCol { kHyperColName, kHyperColMinus, kHyperColPlus };

  // Tracks panel focus, so tabbing into the panel with an unnamed character
  // puts the cursor on the name row. Called from Render, which is where the
  // change is noticed.
  void NoteFocus() const;
  // Per-zone event handlers, called from RouteEvent, each returning whether it
  // consumed the event. OnTabsEvent handles the outer tab bar, and the others
  // handle their tab's content zones.
  bool RouteEvent(const ftxui::Event& event,
                  const CharacterPanelActions& actions);
  bool OnUsernameEvent(const ftxui::Event& event);
  bool OnTabsEvent(const ftxui::Event& event);
  bool OnStatsTabEvent(const ftxui::Event& event,
                       const CharacterPanelActions& actions);
  // Left and Right on the Farm/Boss row. They stop at the ends, like every tab
  // bar in this panel.
  bool OnPresetBarEvent(const ftxui::Event& event,
                        const CharacterPanelActions& actions);
  // Whether the cursor is on the View All Stats row rather than a stat. It is
  // the one stop in the ring that spends nothing.
  bool OnViewAllStatsRow() const;
  // Whether the Stats tab shows its combat block. Everything below the AP rows
  // depends on it, including that row's stop in the cursor ring.
  bool ShowsCombatStats() const;
  // Moves the page bar one page, stopping at either end.
  void StepSkillPage(int delta);
  bool OnSkillsTabEvent(const ftxui::Event& event,
                        const CharacterPanelActions& actions);
  bool OnAdvanceTabEvent(const ftxui::Event& event,
                         const CharacterPanelActions& actions);
  bool OnHyperTabEvent(const ftxui::Event& event,
                       const CharacterPanelActions& actions);
  bool OnAbilityTabEvent(const ftxui::Event& event,
                         const CharacterPanelActions& actions);
  bool OnBuffsTabEvent(const ftxui::Event& event,
                       const CharacterPanelActions& actions);

  // The available tabs, in bar order. The Advance tab is included only while an
  // advancement is pending, so the count varies.
  std::vector<Tab> VisibleTabs() const;
  // The save key recording that `tab` was opened, or "" for a tab that has
  // always been there and has nothing to announce.
  std::string TabKey(Tab tab) const;
  // The tab actually shown: the selected one, unless it has disappeared, as the
  // Advance tab does once the advancement is taken.
  Tab ActiveTab() const;
  // zone_, corrected for a tab bar that changed under it: a zone that no longer
  // exists falls back to the tab bar, which every tab has. Render reads it so
  // the cursor is right on the first frame, and the handler writes it back.
  Zone EffectiveZone() const;
  // The number of stops in the active tab's vertical ring. The outer tab bar is
  // stop 0 on every tab, and the rest belong to the tab.
  int RingStops() const;
  // The ring stop of the first AP stat row, which moves down one when the
  // Farm/Boss row is shown.
  int FirstStatStop() const;
  // The cursor's position in that ring.
  int CursorStop() const;
  // Puts the cursor on `stop` and sets the zone that stop belongs to.
  void SetCursorStop(int stop);
  // Moves the cursor `delta` stops around the ring, wrapping at both ends, so
  // neither end needs its own rule.
  void MoveCursor(int delta);
  // The name row at the top of the panel: the username, inverted while the
  // cursor is on it, or the text being typed while the field is open.
  ftxui::Element RenderUsername(bool row_selected) const;
  // The outer tab bar. `row_selected` draws the active tab white, meaning the
  // bar has focus. Otherwise it keeps the theme highlight.
  ftxui::Element RenderTabBar(bool row_selected) const;
  // The Farm/Boss row: which allocation the tab below reads, directly under the
  // outer tab bar with no rule between. `trailing` is right-aligned: the points
  // the allocation has left, or the honor pool.
  ftxui::Element RenderPresetBar(bool bar_focused,
                                 const std::string& trailing) const;
  // Whether the active tab shows a preset row: once Hyper Stats unlock, on a
  // tab whose numbers come from an allocation, and on Stats only while the
  // autoswap has two allocations to tell apart.
  bool ShowsPresetBar() const;
  // Whether that row names the preset slots, which the Hyper and Ability tabs
  // spend into, rather than the two activities the Stats tab shows.
  bool PresetBarNamesSlots() const;
  // How many chips the row draws. The selection survives a tab change, clamped
  // to the chips the row has.
  int PresetChips() const;
  StatPreset PresetBarSelection() const;
  // Whether the active tab has a second row of tabs. The rule under the outer
  // bar is dropped when it does, since the second row separates just as well.
  bool ShowsSecondTabRow() const;
  ftxui::Element RenderStatsTab(bool bar_focused, bool rows_focused) const;
  // The Hyper tab: the Farm/Boss row with the spare points, the stat rows, then
  // a rule and [Reset]. The rule and [Reset] always draw; the stat rows give
  // way.
  ftxui::Element RenderHyperTab(bool bar_focused, bool rows_focused,
                                bool reset_focused) const;
  // The Ability tab: the Farm/Boss row with the honor pool, the three lines,
  // then a rule, the reroll's price and [Reroll]. Nothing scrolls.
  ftxui::Element RenderAbilityTab(bool bar_focused, bool rows_focused,
                                  bool reroll_focused) const;
  // One Inner Ability line: its name and value as one centred phrase in its
  // rank's colour, then the lock. The lock inverts under the cursor, and Enter
  // toggles it.
  ftxui::Element RenderAbilityRow(const AbilityLine& line, int index,
                                  bool rows_focused) const;
  // How many lines the selected allocation has, which sets the size of the
  // tab's cursor ring.
  int AbilityRows() const;

  // The Buffs tab: one row per buff this character has reached. Nothing
  // scrolls, since there are few enough buffs to always fit.
  ftxui::Element RenderBuffsTab(bool rows_focused) const;
  // One buff row: a tag saying whether it is rented or owned, its name, and a
  // mark at the end when it is on. A buff that is off is dimmed.
  ftxui::Element RenderBuffRow(const ConsumableInfo& info, int index,
                               bool rows_focused) const;
  // The buffs this character has reached, in unlock order. A buff above the
  // character's level isn't listed, since a grey row would only advertise it.
  std::vector<const ConsumableInfo*> BuffsShown() const;
  // Whether the honor pool covers a reroll of the selected allocation.
  bool CanRerollAbility() const;

  // One Hyper Stat row: name, level and [+], with the cursor's column inverted
  // as on a skill row. The stat's value is on the card Enter opens.
  ftxui::Element RenderHyperRow(HyperStatField field, int index,
                                bool rows_focused, int row_width) const;
  // Whether a point can go into `field`: not maxed, not blocked by the
  // character's level, and the next level affordable.
  bool CanRaiseHyperStat(HyperStatField field) const;
  // Whether the stat has a level to give back, which is all the [-] needs,
  // since the points return automatically.
  bool CanLowerHyperStat(HyperStatField field) const;
  // How many of the fourteen Hyper Stat rows fit in the row budget.
  int HyperRowsShown() const;
  // The first row of the window, from ScrollWindowStart, which keeps the
  // selection in the middle.
  int FirstHyperRow(int visible) const;
  // The Skills tab: the page bar (the beginner circle, I/II/..., then H for
  // Hyper Skills and V for the matrix) with that page's points right-aligned,
  // its skill rows, and on the V page a rule and [Reset].
  ftxui::Element RenderSkillsTab(bool bar_focused, bool rows_focused,
                                 bool reset_focused) const;
  // Whether the current page has a [Reset] at the bottom. Only the V page does,
  // since SP spent in the books is permanent.
  bool ShowsVReset() const;
  // The Advance tab: the available jobs, one per row, with a caret on the
  // selected one while the list has focus.
  ftxui::Element RenderAdvanceTab(bool content_focused) const;
  // The page bar: one chip per page with the selected one highlighted, and the
  // points that page spends right-aligned.
  ftxui::Element RenderAdvTabBar(bool bar_focused) const;
  // The page the bar is on (see skill_tab_).
  int SelectedSkillPage() const;
  // How many pages the Skills tab has: the beginner page, one per advancement
  // taken, then the Hyper page once the character has reached it.
  int SkillPages() const;
  // How many of those are numbered: one per advancement taken, minus the 5th
  // while no node has been written for this character's job.
  int NumberedSkillPages() const;
  // The advancement whose Hyper Skills this character has, which is their 4th
  // job's whether or not they have taken the 5th.
  JobAdvancement HyperAdvancement() const;
  // Whether the Hyper page is shown: once a Hyper Skill from this character's
  // own book is within reach of their level.
  bool HasHyperPage() const;
  // Whether page `page` (0-based, like SelectedSkillPage) is the Hyper page.
  bool IsHyperPage(int page) const;
  // Whether it is the beginner page, which is always first: the book every
  // character starts with. Nothing on it is bought.
  bool IsBeginnerPage(int page) const {
    return page == 0;
  }
  // Whether the V page is shown, which it is once the character has a matrix
  // and the catalog has a node they can reach.
  bool HasVPage() const;
  // Whether page `page` is the V page, which is always last.
  bool IsVPage(int page) const;
  // The advancement whose own nodes the V page lists beside the common ones.
  JobAdvancement VAdvancement() const;
  // The points counter on the page bar, in the units the page spends.
  std::string PoolText() const;

  // The skills on page `page`, in listing order. Empty if none.
  std::vector<const Skill*> SkillsForPage(int page) const;
  // Whether the Link Skills row appears on the beginner page. It isn't a skill:
  // nothing is bought on it and no book lists it, so it has no kind tag and no
  // level. It opens a screen.
  bool ShowsLinkRow() const;
  // The number of stops on the current page: its skills plus that row.
  int SkillRowCount() const;
  // The skill a cursor row names, as a SkillsForPage index. The Link Skills row
  // comes first on the beginner page, so every skill below it is one row down.
  int SkillIndexFor(int row) const;
  // The Link Skills row, gold until the player has pressed Enter on it.
  ftxui::Element RenderLinkRow(bool selected) const;
  // Whether the character can't learn `skill` yet: a prerequisite skill is
  // still unlearned, or a level still has to be reached.
  bool SkillLocked(const Skill& skill) const;
  // How the level column is drawn for one page of skills.
  struct LevelColumn {
    // The bonus levels this book gives every opened skill. Whether a given
    // skill receives them is up to the skill (see LevelWithBonus).
    int bonus = 0;
    // The column's width, including gutters. Measured from the widest level
    // actually on the page, so an unopened book gets a thin column of 0s and
    // the room goes to the skill names.
    int width = 3;
  };
  LevelColumn MeasureLevelColumn(const std::vector<const Skill*>& skills) const;

  // One skill row: a kind tag, then "name    20 (+2)", then a [+]. The cursor's
  // column inverts (never the tag), and the [+] dims when the skill is maxed or
  // the stage has no SP. The name never dims. `row_width` is the content width,
  // minus one while the scroll bar takes a column beside it.
  ftxui::Element RenderSkillRow(const Skill& skill, int index,
                                const LevelColumn& column, bool rows_focused,
                                int row_width) const;
  // How many of the page's `total` skill rows fit in the row budget. All of
  // them when no budget is set, and never fewer than one.
  int SkillRowsShown(int total) const;
  // The first drawn line of the skill window, from ScrollWindowStart as above.
  // `selected` is a line rather than a skill, since a divider takes a line of
  // its own.
  int FirstSkillRow(int total, int selected, int visible) const;
  // The lines the page draws, in order: each skill's index, and -1 where a rule
  // goes. Only the V page has rules; every other book is one list.
  std::vector<int> SkillLines(int page,
                              const std::vector<const Skill*>& skills) const;
  // How many of the `total` extra stats fit in the row budget after the View
  // All Stats row. All of them with no budget.
  int ExtraStatsShown(int total) const;
  // Whether the rule above the extra stats is drawn. It is dropped at the
  // tightest budgets, where only the View All Stats row follows it.
  bool ShowsExtrasRule() const;
  // The MP row, with unspent AP right-aligned as "N AP".
  ftxui::Element MpRow(int mp, int ap) const;
  // One stat row: label and value on the left, [+] on the right. The [+] dims
  // with no AP to spend and inverts under the cursor, since Enter acts on it.
  ftxui::Element AllocRow(const std::string& label, int base, int bonus,
                          int index, bool content_focused) const;

  CharacterInstance& character_;
  // Not const, because opening a tab is recorded on the account.
  AccountInstance& account_;
  std::map<std::string, Skill> skills_;
  int& panel_focus_;
  int active_tab_ = 0;  // index into VisibleTabs(): the selected tab
  // Which focus zone has the cursor. The tab bar, unless NoteFocus puts an
  // unnamed character's cursor on the name row.
  mutable Zone zone_ = kZoneTabs;
  // Whether the panel had focus last frame, and whether the cursor has moved
  // yet. Mutable because NoteFocus runs from the render.
  mutable bool was_focused_;
  bool cursor_moved_ = false;
  int stat_sel_ = 0;  // selected Stats row (0-3 = STR/DEX/INT/LUK)
  // The page the player moved the bar to, and whether they have moved it at
  // all. Read it through SelectedSkillPage, which handles the case where nobody
  // has chosen a page yet and a bar that has since grown.
  int skill_tab_ = 0;
  bool skill_page_chosen_ = false;
  int skill_sel_ = 0;              // selected skill row on the current page
  SkillCol skill_col_ = kColName;  // selected column of that row
  // How long the cursor has been on the selected skill row, for scrolling its
  // name. Mutable because the render is where the move is noticed.
  mutable SelectionClock name_clock_;
  int job_sel_ = 0;                     // selected Advance tab job row
  int hyper_sel_ = 0;                   // selected Hyper tab stat row
  HyperCol hyper_col_ = kHyperColName;  // selected column of that row
  int ability_sel_ = 0;                 // selected Ability tab line row
  int buff_sel_ = 0;                    // selected Buffs tab row
  // How long the cursor has been on the selected buff, for scrolling its name.
  // It has its own clock because the two tabs share row numbers, and one clock
  // would carry a scroll from one tab to the other.
  mutable SelectionClock buff_clock_;
  // Which allocation the Farm/Boss row is on (see hyper_preset()).
  StatPreset hyper_preset_ = StatPreset::kFirst;
  TextField username_field_{kMaxUsernameLength};
  // Set by ftxui::reflect on the selected job row each render.
  mutable ftxui::Box job_cursor_box_;
  mutable ftxui::Box preset_row_box_;
  mutable ftxui::Box skill_cursor_box_;
  mutable ftxui::Box buff_cursor_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_CHARACTER_PANEL_H_
