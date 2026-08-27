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
#include "src/frontend/panel_widths.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/text_field.h"
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

class CharacterPanel {
 public:
  // The columns a skill row leaves its name, given the level column beside it
  // and the row's own width. Public because the shipped names are held
  // against it: a name too long for the widest panel is a name half drawn.
  static int SkillNameWidth(int level_width, int row_width);

  // `skills` is the loaded skill catalog (keyed by file stem); the Skills tab
  // lists the entries whose stage matches the selected advancement tab. It is
  // copied because the catalog is fixed after load.
  CharacterPanel(CharacterInstance& character, AccountInstance& account,
                 int& panel_focus, std::map<std::string, Skill> skills = {});
  ftxui::Element Render() const;

  // The rows the panel may take, borders included. Anything past this and the
  // Stats tab starts dropping extra stats off the bottom of its list, which is
  // what keeps the combat panel below it on screen. Zero means no limit.
  //
  // Not read from the terminal here: the panel is drawn in tests at whatever
  // size they choose, and only Tui knows what else is sharing the column.
  void SetMaxRows(int rows) {
    max_rows_ = rows;
  }
  // The columns the panel may take, borders included -- its column's width,
  // which the layout works out from the terminal's. The extra a wide terminal
  // brings goes to the Skills tab's name column, which is what a long Hyper
  // Skill name needs; the Stats tab keeps its own alignment either way.
  //
  // Set from the layout rather than read from the terminal here, for the same
  // reason as SetMaxRows: a test draws the panel at whatever size it likes.
  void SetWidth(int width) {
    width_ = width;
  }
  // What Enter does, by where it lands: on_allocate on a stat's [+] with AP to
  // spend, on_learn on a skill's [+] with SP, on_advance on a job (which
  // should confirm first -- the panel does not advance anything itself), and
  // on_inspect on a skill's name, and on_all_stats on the View All Stats row
  // below them. Only the last two are never gated: a maxed skill with no SP
  // behind it is still worth reading about, and so are the stats.
  ftxui::Component MakeComponent(
      std::function<void(StatField)> on_allocate,
      std::function<void(const Skill&)> on_learn = {},
      std::function<void(Job)> on_advance = {},
      std::function<void(const Skill&)> on_inspect = {},
      std::function<void()> on_all_stats = {});

  // Records the active tab as opened, which is what puts its gold out. Called
  // wherever the tab bar moves, and by the controller when focus arrives on
  // the panel -- a tab already open under the cursor has been seen just as
  // surely as one stepped onto.
  void MarkActiveTabSeen();

  // The screen row the selected job was last drawn on, for anchoring the job
  // menu beside it. Read from the render rather than worked out from the rows
  // above it, so the menu does not have to know the panel's shape. One frame
  // behind, which is right: opening the menu does not move the list.
  int job_cursor_row() const {
    return job_cursor_box_.y_min;
  }

  // True while the name field is taking keys. Tui asks so the player's own
  // letters reach the field instead of being rewritten to the actions they
  // are bound to -- see TranslateKeys.
  bool editing_username() const {
    return username_field_.editing();
  }

  // Lights the panel's border gold, to send the player's eye here while a
  // level-up or advancement is being celebrated -- this is where the AP, SP
  // and job the moment handed over are spent. The panel keeps no clock of its
  // own: whoever lit it turns it off again.
  void SetHighlighted(bool highlighted) {
    highlighted_ = highlighted;
  }

 private:
  // Fixed rows of the Stats tab: the window's two borders, the three heading
  // rows (name, level and job, combat power), the rule under them, the tab
  // bar, its rule, HP, MP, the four AP stats, and the rule above the extras.
  // Everything else on the tab is an extra stat or the View All Stats row.
  //
  // A heading row added without this counted a row too few overruns the budget
  // and pushes the combat panel's last mob bar off a short terminal --
  // ThePanelFitsInsideItsRowBudget is the test that says so. The rule itself
  // goes when the budget cannot pay for it: see ShowsExtrasRule.
  static constexpr int kStatsTabFixedRows = 15;

  // The same for the Skills tab: the two borders, the three heading rows, the
  // rule under them, the tab bar, its rule, the advancement bar and the rule
  // under that. Everything else on the tab is a skill row.
  static constexpr int kSkillsTabFixedRows = 10;

  // Whether the border is currently lit gold. Not part of the panel's own
  // state machine -- it is set from outside and read by Render.
  bool highlighted_ = false;
  // See SetMaxRows. Zero is "as many as it takes".
  int max_rows_ = 0;
  // See SetWidth, and panel_widths.h for where the number comes from.
  int width_ = kLeftColumnMin;

  // Columns inside the window's border.
  int ContentWidth() const {
    return width_ - 2;
  }
  // One row of the Stats tab, at the tab's own width and against the panel's
  // right gutter. The tab does not spread with the terminal -- a value
  // chasing the border would leave its label at the other end of the row --
  // so the room a wide panel brings goes in front of the block, and what the
  // rows right-align stays in the Skills tab's [+] column.
  ftxui::Element StatsAligned(ftxui::Element row) const;

  // The panel's tabs, in bar order. Advance is only on the bar while an
  // advancement is pending, so these are not indices into it -- VisibleTabs().
  enum Tab : int { kTabStats = 0, kTabSkills = 1, kTabAdvance = 2 };

  // Vertical focus zones, top to bottom. From the shared outer tab bar, Down
  // enters the active tab's first content zone: the stat rows on Stats, the
  // advancement tab bar on Skills, from which Down descends to the skill rows.
  enum Zone {
    kZoneUsername,
    kZoneTabs,
    kZoneStatRows,
    kZoneAdvTabs,
    kZoneSkillRows,
    kZoneJobRows
  };

  // The two things a skill row offers, left to right. Left/Right move between
  // them; each answers a different Enter.
  enum SkillCol { kColName, kColPlus };

  // Per-focus-area event handlers, dispatched from MakeComponent by zone. Each
  // returns whether it consumed the event. OnTabsEvent drives the shared outer
  // tab bar (kZoneTabs); the other two own their tab's content zones -- the
  // stat rows for Stats, the advancement bar and skill rows for Skills.
  bool OnUsernameEvent(const ftxui::Event& event);
  bool OnTabsEvent(const ftxui::Event& event);
  bool OnStatsTabEvent(const ftxui::Event& event,
                       const std::function<void(StatField)>& on_allocate,
                       const std::function<void()>& on_all_stats);
  // Whether the cursor is on the View All Stats row rather than on a stat.
  // It is the one stop in the ring that spends nothing.
  bool OnViewAllStatsRow() const;
  // Whether the Stats tab carries its combat block at all. Everything below
  // the AP rows hangs off this: the rule, the stats, the View All Stats row
  // that leads to the rest of them, and that row's stop in the cursor ring.
  bool ShowsCombatStats() const;
  bool OnSkillsTabEvent(const ftxui::Event& event,
                        const std::function<void(const Skill&)>& on_learn,
                        const std::function<void(const Skill&)>& on_inspect);
  bool OnAdvanceTabEvent(const ftxui::Event& event,
                         const std::function<void(Job)>& on_advance);

  // The tabs on offer, in bar order. The Advance tab is only among them while
  // an advancement is pending, so the count is not a constant.
  std::vector<Tab> VisibleTabs() const;
  // The save key `tab` records being opened under, or "" for a tab that has
  // always been there and has nothing to announce.
  std::string TabKey(Tab tab) const;
  // The tab actually being shown, which is the selected one unless it has
  // since disappeared -- taking the advancement closes the tab the player was
  // standing on.
  Tab ActiveTab() const;
  // zone_, corrected for a tab bar that changed under it: taking the
  // advancement drops the Advance tab, stranding a cursor that was in it. A
  // stranded zone resolves back to the tab bar, which every tab has.
  //
  // Render reads it, so the cursor is visible on the first frame after the
  // advancement; the event handler writes it back before dispatching.
  Zone EffectiveZone() const;
  // How many places there are to stand in the active tab's vertical ring. The
  // outer tab bar is stop 0 in every tab; what follows depends on the tab --
  // the four stat rows, the job rows, or the advancement bar and then the
  // skills of whichever stage it is on.
  int RingStops() const;
  // Where the cursor stands in that ring.
  int CursorStop() const;
  // Puts the cursor on stop `stop`, setting the zone that stop belongs to.
  void SetCursorStop(int stop);
  // Moves the cursor `delta` stops around the ring, wrapping at both ends. So
  // Down off the last row returns to the tab bar and Up off the tab bar goes
  // to the last row, and neither is a rule of its own.
  void MoveCursor(int delta);
  // Sends the selected row's name back to its first character. Called
  // wherever the selection moves, so a new row is read from the head rather
  // than from wherever the last one had slid to.
  void RestartNameScroll();
  // Renders the Stats/Skills tab bar. When row_selected the active tab is drawn
  // white (the tab bar holds focus); otherwise it keeps the theme highlight.
  // The name row at the top of the panel: the username, inverted while the
  // cursor rests on it, or what is being typed while the field is open.
  ftxui::Element RenderUsername(bool row_selected) const;
  ftxui::Element RenderTabBar(bool row_selected) const;
  ftxui::Element RenderStatsTab(bool content_focused) const;
  // Renders the Skills tab: the page bar (I/II/... for unlocked stages, then H
  // for the Hyper Skills) with that page's SP right-aligned, then its skill
  // rows. bar_focused draws the active page white; rows_focused highlights the
  // selected skill's [+]. A stage-0 Beginner has neither.
  ftxui::Element RenderSkillsTab(bool bar_focused, bool rows_focused) const;
  // Renders the Advance tab: the jobs on offer, one per row, the selected one
  // marked with a caret while the list holds focus.
  ftxui::Element RenderAdvanceTab(bool content_focused) const;
  // The page bar: one chip per page, the selected one highlighted, with the
  // points that page is bought with right-aligned.
  ftxui::Element RenderAdvTabBar(bool bar_focused) const;
  // How many pages the Skills tab offers: one per advancement taken, and the
  // Hyper page after them once the character has reached it.
  int SkillPages() const;
  // Whether the Hyper page is one of them, which it is once a Hyper Skill of
  // this character's own book has come within reach of their level. Before
  // that the page could only ever be a list of things they cannot have.
  bool HasHyperPage() const;
  // Whether page `page` (0-based, as skill_tab_ is) is the Hyper page.
  bool IsHyperPage(int page) const;
  // The skills of page `page`, in the order it lists them. Empty if none.
  std::vector<const Skill*> SkillsForPage(int page) const;
  // Whether the character may not learn `skill` yet -- a skill below it still
  // to be taught, or a level still to be reached.
  bool SkillLocked(const Skill& skill) const;
  // How the level column is drawn for one page of skills.
  struct LevelColumn {
    // The levels this character's book lends every skill they have opened.
    // Whether a given skill takes any of them is its own business -- see
    // LevelWithBonus.
    int bonus = 0;
    // The column's width, gutters included. Measured from the widest level
    // actually on the page rather than the widest one that could ever be, so
    // an unopened book is a thin column of 0s and every column it does not
    // need goes to the skill names beside it. The first point spent on a
    // lender's book widens it, and the names give the room back.
    int width = 3;
  };
  LevelColumn MeasureLevelColumn(const std::vector<const Skill*>& skills) const;

  // Renders one skill row: a kind tag, then "name    20 (+2)" on the left, and
  // a [+] button on the right. Whichever column the cursor is on inverts, on
  // the selected row while the skill rows hold focus -- the tag is not one of
  // them. The [+] is dimmed when the skill is maxed or its stage has no SP;
  // the name never dims, since it can always be read.
  // `row_width` is what the row lays out in: the content width, or one less
  // while the scroll bar is holding a column beside it.
  ftxui::Element RenderSkillRow(const Skill& skill, int index,
                                const LevelColumn& column, bool rows_focused,
                                int row_width) const;
  // How many skill rows the row budget leaves room for, of the `total` on the
  // page. All of them when no budget is set, and never fewer than one.
  int SkillRowsShown(int total) const;
  // The first skill row of the window, chosen so the selected one is in it.
  int FirstSkillRow(int total, int visible) const;
  // How many of the `total` extra stats the row budget leaves room for, once
  // the View All Stats row under them has been paid for. All of them when no
  // budget is set.
  int ExtraStatsShown(int total) const;
  // Whether the rule above the extra stats is drawn. It is dropped at the
  // tightest budgets, where nothing follows it but the way out.
  bool ShowsExtrasRule() const;
  // The MP row with unspent AP right-aligned as "N AP".
  ftxui::Element MpRow(int mp, int ap) const;
  // Renders one allocatable stat row: label/value on the left, a [+] button on
  // the right. The [+] is dimmed when there is no AP to spend, and inverted on
  // the selected row while the content zone holds focus (its Enter target).
  ftxui::Element AllocRow(const std::string& label, int base, int bonus,
                          int index, bool content_focused) const;

  CharacterInstance& character_;
  // Not const: opening a tab is recorded on the account, so the panel writes
  // as well as reads.
  AccountInstance& account_;
  std::map<std::string, Skill> skills_;
  int& panel_focus_;
  int active_tab_ = 0;     // index into VisibleTabs(): the selected tab
  Zone zone_ = kZoneTabs;  // which focus zone holds the cursor
  int stat_sel_ = 0;       // selected Stats-content row (0-3 = STR/DEX/INT/LUK)
  int skill_tab_ = 0;      // selected page: a 0-based stage index, then Hyper
  int skill_sel_ = 0;      // selected skill row within the current page
  SkillCol skill_col_ = kColName;  // selected column of that row
  // When the selected skill row last changed, for the name scroll.
  std::chrono::steady_clock::time_point skill_selected_at_ =
      std::chrono::steady_clock::now();
  int job_sel_ = 0;  // selected Advance-tab job row
  TextField username_field_{kMaxUsernameLength};
  // Written by ftxui::reflect on the selected job row each render.
  mutable ftxui::Box job_cursor_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_CHARACTER_PANEL_H_
