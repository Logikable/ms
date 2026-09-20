/* The character pane: the player's name, a "Lv X <job>" title, a Stats/Skills
 * tab bar, and the selected tab's content. Stats shows the base stats and the
 * combat stats, equipment and learned passives included. Skills shows one page
 * per job advancement, its SP and its skills. A third Advance tab appears only
 * while an advancement is pending.
 *
 * Focus moves top to bottom through zones, Down descending and Up ascending,
 * with the username row as the top zone -- the only place Left/Right switch
 * tabs. What each zone does with a key is in the OnXEvent handlers below.
 * Running out of AP or SP gates the spend alone: every row stays reachable,
 * since they are worth reading either way.
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

// What Enter does on the Character panel, by where it lands: `allocate` on a
// stat's [+], `learn` on a skill's, `advance` on a job (which should confirm
// first -- the panel advances nothing itself), `menu` on a skill's name, and
// `all_stats` on the row below them. Only the last two are never gated.
//
// A struct rather than ten arguments: the tabs are added to, and a caller
// naming one action should not have to count empty braces.
struct CharacterPanelActions {
  // The Stats tab.
  std::function<void(StatField)> allocate;
  std::function<void()> all_stats;
  // The Skills tab.
  std::function<void(const Skill&)> learn;
  std::function<void(const Skill&)> menu;
  // The Link Skills row at the foot of its beginner page.
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
  // The Ability tab. `ability_lock` takes the index of the line the cursor is
  // on, and holds or frees it -- the panel does not know which way that goes.
  std::function<void(int)> ability_lock;
  std::function<void()> ability_reroll;
  // The Buffs tab. Enter on a row raises the buff's menu, which is where every
  // one of its actions lives -- switching it on included.
  std::function<void(ConsumableType)> buff_menu;
  // The preset row, on the tabs that spend into a slot. The row naming the
  // two activities has nothing to offer and raises nothing.
  std::function<void(PresetKind, StatPreset)> preset_menu;
};

class CharacterPanel {
 public:
  // The columns a skill row leaves its name. Public because the shipped names
  // are held against it: a name too long for the widest panel is half drawn.
  // `has_plus` is the narrow case and the default -- a page with no [+] hands
  // those columns to the name.
  static int SkillNameWidth(int level_width, int row_width,
                            bool has_plus = true);

  // `skills` is the loaded catalog, keyed by file stem; the Skills tab lists
  // the entries whose stage matches the selected advancement tab.
  CharacterPanel(CharacterInstance& character, AccountInstance& account,
                 int& panel_focus, std::map<std::string, Skill> skills = {});
  ftxui::Element Render() const;

  // The rows the panel may take, borders included; 0 is no limit. Past this
  // the Stats tab drops extra stats, which is what keeps the combat panel on
  // screen. Not read from the terminal: only Tui knows what shares the
  // column, and a test draws at whatever size it likes.
  void SetMaxRows(int rows) {
    max_rows_ = rows;
  }
  // The columns the panel may take, which the layout works out from the
  // terminal's. What a wide terminal brings goes to the Skills tab's name
  // column; the Stats tab keeps its alignment either way. Set from the layout
  // for SetMaxRows' reason.
  void SetWidth(int width) {
    width_ = width;
  }
  // The panel as a component, answering Enter with `actions`. An unset action
  // is a key that does nothing, so a caller only looking passes none.
  ftxui::Component MakeComponent(CharacterPanelActions actions = {});

  // Which preset slot the panel is reading. The All Stats screen opens on it
  // too, so the two never disagree about whose numbers are shown.
  StatPreset hyper_preset() const {
    return hyper_preset_;
  }

  // What the selected slot stands for, which is what the stats the panel shows
  // are read against -- the Farm chip shows a farming character.
  Activity SelectedActivity() const;

  // Records the active tab as opened, which puts its gold out. Called wherever
  // the bar moves, and when focus arrives: a tab already under the cursor has
  // been seen as surely as one stepped onto. Does nothing on a read-only
  // panel -- see SetReadOnly.
  void MarkActiveTabSeen();

  // Draws somebody else's character rather than the player's: the Inspect
  // screen. Everything that spends comes off -- the [+] and [Max] buttons,
  // both [Reset]s, [Reroll], the Advance tab and the name field -- and with
  // them the stops that only led to one. The Buffs tab goes too: it lists
  // what is in a bag, and a bag is not on the sheet.
  //
  // No gold either way: a tab is news to the player about their own
  // character, and reading a party member's must not spend that news.
  void SetReadOnly(bool read_only) {
    read_only_ = read_only;
  }

  // The screen row the selected skill was drawn on, for anchoring its menu.
  int skill_cursor_row() const {
    return skill_cursor_box_.y_min;
  }

  // The screen row the selected job was drawn on, for anchoring its menu. Read
  // from the RENDER rather than worked out, so the menu need not know the
  // panel's shape. One frame behind, which is right.
  int job_cursor_row() const {
    return job_cursor_box_.y_min;
  }

  // The screen row the selected buff was last drawn on, for anchoring the buff
  // menu beside it. Read from the render, as the two above are.
  int buff_cursor_row() const {
    return buff_cursor_box_.y_min;
  }

  // The screen row the preset row was drawn on, for anchoring its menu. No
  // cursor walks a list here, so the chip's own row is what it hangs from.
  int preset_row() const {
    return preset_row_box_.y_min;
  }

  // True while the name field is taking keys, so Tui lets the player's letters
  // through rather than rewriting them to bound actions.
  bool editing_username() const {
    return username_field_.editing();
  }

  // Lights the border gold while a level-up or advancement is celebrated, this
  // being where what it handed over is spent. No clock of its own: whoever lit
  // it turns it off.
  void SetHighlighted(bool highlighted) {
    highlighted_ = highlighted;
  }

 private:
  // Fixed rows of the Stats tab: two borders, three heading rows, their rule,
  // the tab bar and its rule, HP, MP, the four AP stats and the rule above the
  // extras. Everything else is an extra stat or the View All Stats row.
  //
  // COUNT A NEW HEADING ROW HERE, or the budget overruns and the combat
  // panel's last mob bar falls off a short terminal --
  // ThePanelFitsInsideItsRowBudget says so.
  static constexpr int kStatsTabFixedRows = 15;

  // The same for the Skills tab: two borders, three heading rows, their rule,
  // the tab bar, the advancement bar and its rule; everything else is a skill
  // row. The V page's rule and [Reset] are two more, never given up.
  static constexpr int kSkillsTabFixedRows = 9;

  // The Skills tab's own count, which the V page's foot adds to.
  int SkillsTabFixedRows() const;

  // How many of the Stats tab's four AP rows the cursor stops on. None on a
  // read-only panel: there is no [+] to press, and the View All Stats row
  // under them is the whole reason the block is walked.
  int StatStops() const;

  // The Stats tab's own count, which the Farm/Boss row adds to.
  int StatsTabFixedRows() const;

  // And the Hyper tab's: two borders, three heading rows, their rule, the two
  // tab rows and their rule, then the rule and [Reset] at the foot.
  static constexpr int kHyperTabFixedRows = 11;

  // The Ability and Buffs tabs have no count of their own: a fixed few rows
  // each, none of them ever dropped.

  // Whether the border is currently lit gold. Not part of the panel's own
  // state machine -- it is set from outside and read by Render.
  bool highlighted_ = false;
  // See SetReadOnly.
  bool read_only_ = false;
  // See SetMaxRows. Zero is "as many as it takes".
  int max_rows_ = 0;
  // See SetWidth, and panel_widths.h for where the number comes from.
  int width_ = kLeftColumnMin;

  // Columns inside the window's border.
  int ContentWidth() const {
    return width_ - 2;
  }
  // One row of the Stats tab, at the tab's own width and centred. The tab does
  // not spread with the terminal -- a value chasing the border would strand
  // its label -- so a wide panel is blank either side of the block.
  ftxui::Element StatsAligned(ftxui::Element row) const;

  // The panel's tabs, in bar order. NOT indices into the bar: Hyper and
  // Advance appear only when they have something to offer -- VisibleTabs().
  enum Tab : int {
    kTabStats = 0,
    kTabSkills = 1,
    kTabHyper = 2,
    kTabAbility = 3,
    kTabBuffs = 4,
    kTabAdvance = 5
  };

  // Vertical focus zones, top to bottom. Down off the outer tab bar enters the
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
    // The Hyper tab's fourteen stat rows, and the [Reset] button under them.
    kZoneHyperRows,
    kZoneHyperReset,
    // The Ability tab's three line rows, and the [Reroll] button under them.
    kZoneAbilityRows,
    kZoneAbilityReroll,
    // The Buffs tab's rows. One stop each: everything a buff offers is on the
    // menu Enter raises.
    kZoneBuffRows
  };

  // The two things a skill row offers, left to right. Left/Right move between
  // them; each answers a different Enter.
  enum SkillCol { kColName, kColPlus };

  // Whether the skill rows carry a [+] at all. A read-only sheet has nothing
  // to press, and the beginner's page has nothing to buy.
  bool ShowsSkillPlus() const;
  // skill_col_, corrected for a page with no [+]: the cursor cannot stand on
  // a column that is not drawn. Render reads it, as EffectiveZone is read.
  SkillCol EffectiveSkillCol() const;

  // A Hyper Stat row's three, in screen order: the stat to read about, the
  // level to give back, the point to spend. Left/Right clamp at the ends.
  enum HyperCol { kHyperColName, kHyperColMinus, kHyperColPlus };

  // Follows the panel focus, so tabbing in opens an unnamed character's cursor
  // on the name row. Run from Render, which is what notices.
  void NoteFocus() const;
  // Per-zone event handlers, dispatched from RouteEvent, each returning
  // whether it consumed the event. OnTabsEvent drives the outer tab bar; the
  // other two own their tab's content zones.
  bool RouteEvent(const ftxui::Event& event,
                  const CharacterPanelActions& actions);
  bool OnUsernameEvent(const ftxui::Event& event);
  bool OnTabsEvent(const ftxui::Event& event);
  bool OnStatsTabEvent(const ftxui::Event& event,
                       const CharacterPanelActions& actions);
  // Left/Right on the Farm/Boss row. They clamp at the ends, as every tab bar
  // in this panel does.
  bool OnPresetBarEvent(const ftxui::Event& event,
                        const CharacterPanelActions& actions);
  // Whether the cursor is on the View All Stats row rather than on a stat.
  // It is the one stop in the ring that spends nothing.
  bool OnViewAllStatsRow() const;
  // Whether the Stats tab carries its combat block. Everything below the AP
  // rows hangs off it, that row's stop in the cursor ring included.
  bool ShowsCombatStats() const;
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

  // The tabs on offer, in bar order. The Advance tab is only among them while
  // an advancement is pending, so the count is not a constant.
  std::vector<Tab> VisibleTabs() const;
  // The save key `tab` records being opened under, or "" for a tab that has
  // always been there and has nothing to announce.
  std::string TabKey(Tab tab) const;
  // The tab actually shown: the selected one unless it has since disappeared,
  // as taking the advancement closes the tab it was on.
  Tab ActiveTab() const;
  // zone_, corrected for a tab bar that changed under it: a stranded zone
  // resolves back to the tab bar, which every tab has. Render reads it, so the
  // cursor is right on the first frame; the handler writes it back.
  Zone EffectiveZone() const;
  // Places to stand in the active tab's vertical ring. The outer tab bar is
  // stop 0 in every tab; what follows is the tab's own.
  int RingStops() const;
  // The ring stop the first AP stat row takes, which the Farm/Boss row moves
  // down by one when it is there.
  int FirstStatStop() const;
  // Where the cursor stands in that ring.
  int CursorStop() const;
  // Puts the cursor on stop `stop`, setting the zone that stop belongs to.
  void SetCursorStop(int stop);
  // Moves the cursor `delta` stops around the ring, WRAPPING at both ends, so
  // neither end needs a rule of its own.
  void MoveCursor(int delta);
  // The name row at the top of the panel: the username, inverted while the
  // cursor rests on it, or what is being typed while the field is open.
  ftxui::Element RenderUsername(bool row_selected) const;
  // The Stats/Skills tab bar. `row_selected` draws the active tab white,
  // meaning the bar holds focus; otherwise it keeps the theme highlight.
  ftxui::Element RenderTabBar(bool row_selected) const;
  // The Farm/Boss row: which allocation the tab under it reads, straight under
  // the outer tab bar with no rule between. `trailing` is right-aligned -- the
  // points the allocation has left, or the honor pool.
  ftxui::Element RenderPresetBar(bool bar_focused,
                                 const std::string& trailing) const;
  // Whether the active tab carries a preset row: from level 140, on a tab
  // whose numbers come out of an allocation, and on Stats only while the
  // autoswap has two to tell apart.
  bool ShowsPresetBar() const;
  // Whether that row names the preset slots, which the Hyper and Ability tabs
  // spend into, rather than the two activities the Stats tab shows.
  bool PresetBarNamesSlots() const;
  // How many chips the row draws, and which the cursor is on. The selection
  // survives a tab change, clamped to what the row has.
  int PresetChips() const;
  StatPreset PresetBarSelection() const;
  // Whether the active tab has a second row of tabs. The rule under the outer
  // bar is dropped for one, the second row separating just as well.
  bool ShowsSecondTabRow() const;
  ftxui::Element RenderStatsTab(bool bar_focused, bool rows_focused) const;
  // The Hyper tab: the Farm/Boss row with the spare points, the stat rows,
  // then a rule and [Reset], which always draw -- the ROWS give way.
  ftxui::Element RenderHyperTab(bool bar_focused, bool rows_focused,
                                bool reset_focused) const;
  // The Ability tab: the Farm/Boss row with the honor pool, the three lines,
  // then a rule, the reroll's price and [Reroll]. Nothing scrolls.
  ftxui::Element RenderAbilityTab(bool bar_focused, bool rows_focused,
                                  bool reroll_focused) const;
  // One Inner Ability line: its name and grant as one centred phrase in its
  // rank's colour, then the lock. The lock inverts, Enter answering it.
  ftxui::Element RenderAbilityRow(const AbilityLine& line, int index,
                                  bool rows_focused) const;
  // The lines the selected allocation is holding, which is what the tab's
  // cursor ring is measured in.
  int AbilityRows() const;

  // Renders the Buffs tab: one row per buff this character has reached. Nothing
  // scrolls -- the buffs are few enough that the tab never outgrows them.
  ftxui::Element RenderBuffsTab(bool rows_focused) const;
  // One buff row: the tag saying whether it is rented or owned, its name, and
  // the mark at the end that says it is switched on. A buff that is off dims.
  ftxui::Element RenderBuffRow(const ConsumableInfo& info, int index,
                               bool rows_focused) const;
  // The buffs this character has reached, in the order they open. One below
  // its level is not listed: a greyed row would only advertise it.
  std::vector<const ConsumableInfo*> BuffsShown() const;
  // Whether the honor pool covers a reroll of the selected allocation.
  bool CanRerollAbility() const;

  // One Hyper Stat row: name, level, [+], the cursor's column inverted as on a
  // skill row. What the stat is WORTH is on the card Enter opens.
  ftxui::Element RenderHyperRow(HyperStatField field, int index,
                                bool rows_focused, int row_width) const;
  // Whether a point can go into `field` at all: not maxed, not held shut by
  // the character's level, and the next rung paid for.
  bool CanRaiseHyperStat(HyperStatField field) const;
  // Whether the stat has a level to give back, which is the whole of what
  // the [-] asks: the points return by themselves.
  bool CanLowerHyperStat(HyperStatField field) const;
  // How many Hyper Stat rows the row budget leaves room for, of the fourteen.
  int HyperRowsShown() const;
  // The first row of the window -- ScrollWindowStart, which keeps the
  // selection in the middle of it.
  int FirstHyperRow(int visible) const;
  // The Skills tab: the page bar (the beginner's circle, I/II/..., then H for
  // hypers and V for the matrix) with that page's points right-aligned, its
  // skill rows, and on the V page a rule and [Reset].
  ftxui::Element RenderSkillsTab(bool bar_focused, bool rows_focused,
                                 bool reset_focused) const;
  // Whether the page under the cursor carries the [Reset] at its foot, which
  // only the V page does: the SP books are spent for good.
  bool ShowsVReset() const;
  // Renders the Advance tab: the jobs on offer, one per row, the selected one
  // marked with a caret while the list holds focus.
  ftxui::Element RenderAdvanceTab(bool content_focused) const;
  // The page bar: one chip per page, the selected one highlighted, with the
  // points that page is bought with right-aligned.
  ftxui::Element RenderAdvTabBar(bool bar_focused) const;
  // The page the bar is on -- see skill_tab_.
  int SelectedSkillPage() const;
  // How many pages the Skills tab offers: the beginner's, then one per
  // advancement taken, then the Hyper page once the character has reached it.
  int SkillPages() const;
  // How many of those are numbered -- one per advancement taken, less the 5th
  // while no node has been written for this character's job.
  int NumberedSkillPages() const;
  // The advancement whose Hyper Skills this character holds, which is their
  // 4th job's whether or not they have taken a 5th.
  JobAdvancement HyperAdvancement() const;
  // Whether the Hyper page is one of them: once a Hyper Skill of this
  // character's own book is within reach of their level.
  bool HasHyperPage() const;
  // Whether page `page` (0-based, as SelectedSkillPage is) is the Hyper
  // page.
  bool IsHyperPage(int page) const;
  // Whether it is the beginner's page, which is always the first: the book
  // every character is born holding. Nothing on it is bought.
  bool IsBeginnerPage(int page) const {
    return page == 0;
  }
  // Whether the V page is offered, which it is once the character has a
  // matrix and the catalog holds a node they reach.
  bool HasVPage() const;
  // Whether page `page` is the V page, which is always the last one.
  bool IsVPage(int page) const;
  // The advancement whose own nodes the V page lists beside the common ones.
  JobAdvancement VAdvancement() const;
  // The pool counter on the page bar, in the units the page spends.
  std::string PoolText() const;

  // The skills of page `page`, in the order it lists them. Empty if none.
  std::vector<const Skill*> SkillsForPage(int page) const;
  // Whether the Link Skills row stands under the beginner's book. It is not a
  // skill: nothing is bought on it and no book lists it, so it carries no kind
  // tag and no level -- what it does is open a screen.
  bool ShowsLinkRow() const;
  // The stops the page under the cursor has: its skills, and that row.
  int SkillRowCount() const;
  // The Link Skills row itself, gold until the player has pressed Enter on it.
  ftxui::Element RenderLinkRow(bool selected) const;
  // Whether the character may not learn `skill` yet -- a skill below it still
  // to be taught, or a level still to be reached.
  bool SkillLocked(const Skill& skill) const;
  // How the level column is drawn for one page of skills.
  struct LevelColumn {
    // The levels this book lends every opened skill. Whether a given skill
    // takes any is its own business -- see LevelWithBonus.
    int bonus = 0;
    // The column's width, gutters included. Measured from the widest level
    // ACTUALLY on the page, so an unopened book is a thin column of 0s and
    // the room goes to the skill names beside it.
    int width = 3;
  };
  LevelColumn MeasureLevelColumn(const std::vector<const Skill*>& skills) const;

  // One skill row: a kind tag, then "name    20 (+2)", then a [+]. The
  // cursor's column inverts -- the tag is never one -- and the [+] dims when
  // the skill is maxed or the stage has no SP. The NAME never dims.
  // `row_width` is the content width, less one while the scroll bar holds a
  // column beside it.
  ftxui::Element RenderSkillRow(const Skill& skill, int index,
                                const LevelColumn& column, bool rows_focused,
                                int row_width) const;
  // How many skill rows the row budget leaves room for, of the `total` on the
  // page. All of them when no budget is set, and never fewer than one.
  int SkillRowsShown(int total) const;
  // The first drawn line of the skill window -- ScrollWindowStart, as above.
  // `selected` is a LINE rather than a skill, a divider taking one of its own.
  int FirstSkillRow(int total, int selected, int visible) const;
  // The lines the page draws, in order: each skill's index, and -1 where a
  // rule goes. Only a V page has any; every other book is one list.
  std::vector<int> SkillLines(int page,
                              const std::vector<const Skill*>& skills) const;
  // How many of the `total` extra stats the row budget leaves room for, once
  // the View All Stats row is paid for. All of them with no budget.
  int ExtraStatsShown(int total) const;
  // Whether the rule above the extra stats is drawn. It is dropped at the
  // tightest budgets, where nothing follows it but the way out.
  bool ShowsExtrasRule() const;
  // The MP row with unspent AP right-aligned as "N AP".
  ftxui::Element MpRow(int mp, int ap) const;
  // One allocatable stat row: label and value left, [+] right. The [+] dims
  // with no AP to spend and inverts under the cursor, being Enter's target.
  ftxui::Element AllocRow(const std::string& label, int base, int bonus,
                          int index, bool content_focused) const;

  CharacterInstance& character_;
  // Not const: opening a tab is recorded on the account, so the panel writes
  // as well as reads.
  AccountInstance& account_;
  std::map<std::string, Skill> skills_;
  int& panel_focus_;
  int active_tab_ = 0;  // index into VisibleTabs(): the selected tab
  // Which focus zone holds the cursor. The tab bar, until NoteFocus opens an
  // unnamed character on the name row instead.
  mutable Zone zone_ = kZoneTabs;
  // Whether the panel held focus last frame, and whether the cursor has moved
  // yet. Mutable because NoteFocus runs from the render.
  mutable bool was_focused_;
  bool cursor_moved_ = false;
  int stat_sel_ = 0;  // selected Stats-content row (0-3 = STR/DEX/INT/LUK)
  // The page the player moved the bar to, and whether they have moved it at
  // all. READ THROUGH SelectedSkillPage, which answers for the page nobody
  // has chosen yet and for a bar that has since grown.
  int skill_tab_ = 0;
  bool skill_page_chosen_ = false;
  int skill_sel_ = 0;              // selected skill row within the current page
  SkillCol skill_col_ = kColName;  // selected column of that row
  // How long the cursor has sat on the selected skill row, for the name
  // scroll. Mutable because the render is what notices the row moved.
  mutable SelectionClock name_clock_;
  int job_sel_ = 0;                     // selected Advance-tab job row
  int hyper_sel_ = 0;                   // selected Hyper-tab stat row
  HyperCol hyper_col_ = kHyperColName;  // selected column of that row
  int ability_sel_ = 0;                 // selected Ability-tab line row
  int buff_sel_ = 0;                    // selected Buffs-tab row
  // How long the cursor has sat on the selected buff, for the name scroll. Its
  // own clock: the two tabs share row numbers, and one clock would carry a
  // slide from one to the other.
  mutable SelectionClock buff_clock_;
  // Which allocation the Farm/Boss row is on -- see hyper_preset().
  StatPreset hyper_preset_ = StatPreset::kFirst;
  TextField username_field_{kMaxUsernameLength};
  // Written by ftxui::reflect on the selected job row each render.
  mutable ftxui::Box job_cursor_box_;
  mutable ftxui::Box preset_row_box_;
  mutable ftxui::Box skill_cursor_box_;
  mutable ftxui::Box buff_cursor_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_CHARACTER_PANEL_H_
