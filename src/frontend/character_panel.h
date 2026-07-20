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
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"

namespace ms {

class CharacterPanel {
 public:
  // Wide enough to seat the Stats-tab [+]/[Max] allocation buttons; also the
  // width CombatPanel derives from, so keep it roomy for its map row.
  static constexpr int kTotalWidth = 35;

  explicit CharacterPanel(const CharacterInstance& character, int& panel_focus);
  ftxui::Element Render() const;
  // on_allocate(field) fires when Enter is pressed on a Stats-tab stat's [+]
  // button while there is unspent AP; the caller pops the amount entry.
  ftxui::Component MakeComponent(std::function<void(StatField)> on_allocate);

 private:
  // Vertical focus zones, top to bottom. From the shared outer tab bar, Down
  // enters the active tab's first content zone: the stat rows on Stats, the
  // advancement tab bar on Skills.
  enum Zone { kZoneTabs, kZoneStatRows, kZoneAdvTabs };

  // Renders the Stats/Skills tab bar. When row_selected the active tab is drawn
  // white (the tab bar holds focus); otherwise it keeps the theme highlight.
  ftxui::Element RenderTabBar(bool row_selected) const;
  ftxui::Element RenderStatsTab(bool content_focused) const;
  // Renders the Skills tab: the advancement tab bar (I/II/... for unlocked
  // stages) with SP right-aligned, then the skill-list placeholder. bar_focused
  // draws the active advancement tab white. A stage-0 Beginner has none.
  ftxui::Element RenderSkillsTab(bool bar_focused) const;
  // The advancement tab bar: one chip per unlocked stage (1..stages), the
  // selected one highlighted, with "SP: N" for that stage right-aligned.
  ftxui::Element RenderAdvTabBar(int stages, bool bar_focused) const;
  // The MP row with unspent AP right-aligned as "N AP".
  ftxui::Element MpRow(int mp, int ap) const;
  // Renders one allocatable stat row: label/value on the left, a [+] button on
  // the right. The [+] is dimmed when there is no AP to spend, and inverted on
  // the selected row while the content zone holds focus (its Enter target).
  ftxui::Element AllocRow(const std::string& label, int base, int bonus,
                          int index, bool content_focused) const;

  const CharacterInstance& character_;
  int& panel_focus_;
  int active_tab_ = 0;     // which tab is shown: Stats (0) or Skills (1)
  Zone zone_ = kZoneTabs;  // which focus zone holds the cursor
  int stat_sel_ = 0;       // selected Stats-content row (0-3 = STR/DEX/INT/LUK)
  int skill_tab_ = 0;      // selected advancement tab (0-based stage index)
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CHARACTER_PANEL_H_
