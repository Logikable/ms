/* CharacterPanel renders the character pane as a tabbed panel: a "Lv X <job>"
 * title, a Stats/Skills tab bar, and the selected tab's content. The Stats tab
 * shows base stats (HP/MP/STR/DEX/INT/LUK) including equipment bonuses and the
 * equipment-derived combat stats (ATT/MATT); the Skills tab shows the unlocked
 * job-advancement tabs (I/II/...) and their SP, with the skill list still to
 * come.
 *
 * Focus moves top-to-bottom through zones, Down descending and Up ascending.
 * The top zone is the Stats/Skills tab bar: there Left/Right switch tabs (they
 * act only on the focused tab bar) and the active tab is drawn white to show
 * the row is selected. Down enters the active tab's content. On the Stats tab
 * the content zone is the four AP-allocatable rows (STR/DEX/INT/LUK): Up/Down
 * move between them, Up off STR returns to the tab bar, the selected row's [+]
 * is highlighted, and Enter fires on_allocate for that stat while there is
 * unspent AP. On the Skills tab the content zone is the advancement tab bar,
 * where Left/Right switch advancement tabs and Up returns to the outer tab bar.
 * Produces a new ftxui Element on each Render() call.
 */
#ifndef MS_SRC_FRONTEND_CHARACTER_PANEL_H_
#define MS_SRC_FRONTEND_CHARACTER_PANEL_H_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
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
  explicit CharacterPanel(const CharacterInstance& character, int& panel_focus,
                          std::map<std::string, Skill> skills = {});
  ftxui::Element Render() const;
  // on_allocate(field) fires when Enter is pressed on a Stats-tab stat's [+]
  // button while there is unspent AP. on_learn(skill) fires on a Skills-tab
  // skill's [+] when it can still take a point. Either pops an amount entry.
  ftxui::Component MakeComponent(
      std::function<void(StatField)> on_allocate,
      std::function<void(const Skill&)> on_learn = {});

 private:
  // Vertical focus zones, top to bottom. From the shared outer tab bar, Down
  // enters the active tab's first content zone: the stat rows on Stats, the
  // advancement tab bar on Skills, from which Down descends to the skill rows.
  enum Zone { kZoneTabs, kZoneStatRows, kZoneAdvTabs, kZoneSkillRows };

  // Per-focus-area event handlers, dispatched from MakeComponent by zone. Each
  // returns whether it consumed the event. OnTabsEvent drives the shared outer
  // tab bar (kZoneTabs); the other two own their tab's content zones -- the
  // stat rows for Stats, the advancement bar and skill rows for Skills.
  bool OnTabsEvent(const ftxui::Event& event);
  bool OnStatsTabEvent(const ftxui::Event& event,
                       const std::function<void(StatField)>& on_allocate);
  bool OnSkillsTabEvent(const ftxui::Event& event,
                        const std::function<void(const Skill&)>& on_learn);

  // Renders the Stats/Skills tab bar. When row_selected the active tab is drawn
  // white (the tab bar holds focus); otherwise it keeps the theme highlight.
  ftxui::Element RenderTabBar(bool row_selected) const;
  ftxui::Element RenderStatsTab(bool content_focused) const;
  // Renders the Skills tab: the advancement tab bar (I/II/... for unlocked
  // stages) with SP right-aligned, then the selected stage's skill rows.
  // bar_focused draws the active advancement tab white; rows_focused highlights
  // the selected skill's [+]. A stage-0 Beginner has neither.
  ftxui::Element RenderSkillsTab(bool bar_focused, bool rows_focused) const;
  // The advancement tab bar: one chip per unlocked stage (1..stages), the
  // selected one highlighted, with "SP: N" for that stage right-aligned.
  ftxui::Element RenderAdvTabBar(int stages, bool bar_focused) const;
  // The skills of the given job stage, in catalog order. Empty if none.
  std::vector<const Skill*> SkillsForStage(int stage) const;
  // Renders one skill row: "name  level/max" on the left, a [+] button on the
  // right. The [+] is dimmed when the skill is maxed or its stage has no SP,
  // and inverted on the selected row while the skill rows hold focus.
  ftxui::Element RenderSkillRow(const Skill& skill, int index,
                                bool rows_focused) const;
  // The MP row with unspent AP right-aligned as "N AP".
  ftxui::Element MpRow(int mp, int ap) const;
  // Renders one allocatable stat row: label/value on the left, a [+] button on
  // the right. The [+] is dimmed when there is no AP to spend, and inverted on
  // the selected row while the content zone holds focus (its Enter target).
  ftxui::Element AllocRow(const std::string& label, int base, int bonus,
                          int index, bool content_focused) const;

  const CharacterInstance& character_;
  std::map<std::string, Skill> skills_;
  int& panel_focus_;
  int active_tab_ = 0;     // which tab is shown: Stats (0) or Skills (1)
  Zone zone_ = kZoneTabs;  // which focus zone holds the cursor
  int stat_sel_ = 0;       // selected Stats-content row (0-3 = STR/DEX/INT/LUK)
  int skill_tab_ = 0;      // selected advancement tab (0-based stage index)
  int skill_sel_ = 0;      // selected skill row within the current stage
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CHARACTER_PANEL_H_
