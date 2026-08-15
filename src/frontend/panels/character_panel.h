/* The character pane: a "Lv X <job>" title, a Stats/Skills tab bar, and the
 * selected tab's content. Stats shows the base stats and the combat stats,
 * equipment and learned passives included. Skills shows one page per job
 * advancement, its SP and its skills. A third Advance tab appears only while
 * an advancement is pending.
 *
 * Focus moves top to bottom through zones, Down descending and Up ascending,
 * with the outer tab bar as the top zone -- the only place Left/Right switch
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
#include "src/character/character.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

class CharacterPanel {
 public:
  // Wide enough to seat the Stats-tab [+]/[Max] allocation buttons; also the
  // width CombatPanel derives from, so keep it roomy for its map row.
  static constexpr int kTotalWidth = 35;

  // `skills` is the loaded skill catalog (keyed by file stem); the Skills tab
  // lists the entries whose stage matches the selected advancement tab. It is
  // copied because the catalog is fixed after load.
  explicit CharacterPanel(CharacterInstance& character, int& panel_focus,
                          std::map<std::string, Skill> skills = {});
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

  // Lights the panel's border gold, to send the player's eye here while a
  // level-up or advancement is being celebrated -- this is where the AP, SP
  // and job the moment handed over are spent. The panel keeps no clock of its
  // own: whoever lit it turns it off again.
  void SetHighlighted(bool highlighted) {
    highlighted_ = highlighted;
  }

 private:
  // Fixed rows of the Stats tab: the window's two borders, the two heading
  // rows, the rule under them, the tab bar, its rule, HP, MP, the four AP
  // stats, and the rule above the extras. Everything else on the tab is an
  // extra stat or the View All Stats row.
  static constexpr int kStatsTabFixedRows = 14;

  // Whether the border is currently lit gold. Not part of the panel's own
  // state machine -- it is set from outside and read by Render.
  bool highlighted_ = false;
  // See SetMaxRows. Zero is "as many as it takes".
  int max_rows_ = 0;

  // The panel's tabs, in bar order. Advance is only on the bar while an
  // advancement is pending, so these are not indices into it -- VisibleTabs().
  enum Tab : int { kTabStats = 0, kTabSkills = 1, kTabAdvance = 2 };

  // Vertical focus zones, top to bottom. From the shared outer tab bar, Down
  // enters the active tab's first content zone: the stat rows on Stats, the
  // advancement tab bar on Skills, from which Down descends to the skill rows.
  enum Zone {
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
  ftxui::Element RenderTabBar(bool row_selected) const;
  ftxui::Element RenderStatsTab(bool content_focused) const;
  // Renders the Skills tab: the advancement tab bar (I/II/... for unlocked
  // stages) with SP right-aligned, then the selected stage's skill rows.
  // bar_focused draws the active advancement tab white; rows_focused highlights
  // the selected skill's [+]. A stage-0 Beginner has neither.
  ftxui::Element RenderSkillsTab(bool bar_focused, bool rows_focused) const;
  // Renders the Advance tab: the jobs on offer, one per row, the selected one
  // marked with a caret while the list holds focus.
  ftxui::Element RenderAdvanceTab(bool content_focused) const;
  // The advancement tab bar: one chip per unlocked stage (1..stages), the
  // selected one highlighted, with that stage's "N SP" right-aligned.
  ftxui::Element RenderAdvTabBar(int stages, bool bar_focused) const;
  // The skills of the given job stage, in catalog order. Empty if none.
  std::vector<const Skill*> SkillsForStage(int stage) const;
  // Renders one skill row: a kind tag, then "name  level/max" on the left, and
  // a [+] button on the right. Whichever column the cursor is on inverts, on
  // the selected row while the skill rows hold focus -- the tag is not one of
  // them. The [+] is dimmed when the skill is maxed or its stage has no SP;
  // the name never dims, since it can always be read.
  ftxui::Element RenderSkillRow(const Skill& skill, int index,
                                bool rows_focused) const;
  // How many of the `total` extra stats the row budget leaves room for, once
  // the View All Stats row under them has been paid for. All of them when no
  // budget is set.
  int ExtraStatsShown(int total) const;
  // The MP row with unspent AP right-aligned as "N AP".
  ftxui::Element MpRow(int mp, int ap) const;
  // Renders one allocatable stat row: label/value on the left, a [+] button on
  // the right. The [+] is dimmed when there is no AP to spend, and inverted on
  // the selected row while the content zone holds focus (its Enter target).
  ftxui::Element AllocRow(const std::string& label, int base, int bonus,
                          int index, bool content_focused) const;

  // Not const: opening a tab is recorded on the character, so the panel
  // writes as well as reads.
  CharacterInstance& character_;
  std::map<std::string, Skill> skills_;
  int& panel_focus_;
  int active_tab_ = 0;     // index into VisibleTabs(): the selected tab
  Zone zone_ = kZoneTabs;  // which focus zone holds the cursor
  int stat_sel_ = 0;       // selected Stats-content row (0-3 = STR/DEX/INT/LUK)
  int skill_tab_ = 0;      // selected advancement tab (0-based stage index)
  int skill_sel_ = 0;      // selected skill row within the current stage
  SkillCol skill_col_ = kColName;  // selected column of that row
  // When the selected skill row last changed, for the name scroll.
  std::chrono::steady_clock::time_point skill_selected_at_ =
      std::chrono::steady_clock::now();
  int job_sel_ = 0;  // selected Advance-tab job row
  // Written by ftxui::reflect on the selected job row each render.
  mutable ftxui::Box job_cursor_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_CHARACTER_PANEL_H_
