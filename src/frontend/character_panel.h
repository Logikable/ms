/* CharacterPanel renders the character pane as a tabbed panel: a "Lv X <job>"
 * title, a Stats/Skills tab bar, and the selected tab's content. The Stats tab
 * shows base stats (HP/MP/STR/DEX/INT/LUK) including equipment bonuses and the
 * equipment-derived combat stats (ATT/MATT); the Skills tab is a placeholder
 * until skills land.
 *
 * Focus moves top-to-bottom through zones, Down descending and Up ascending.
 * The top zone is the Stats/Skills tab bar: there Left/Right switch tabs (they
 * do nothing in any other zone) and the active tab is drawn white to show the
 * row is selected. Down enters the active tab's content. On the Stats tab the
 * content zone is the four AP-allocatable rows (STR/DEX/INT/LUK): Up/Down move
 * between them, Up off STR returns to the tab bar, the selected row's [+] is
 * highlighted, and Enter fires on_allocate for that stat while there is unspent
 * AP. Produces a new ftxui Element on each Render() call.
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
  // Renders the Stats/Skills tab bar. When row_selected the active tab is drawn
  // white (the tab bar holds focus); otherwise it keeps the theme highlight.
  ftxui::Element RenderTabBar(bool row_selected) const;
  ftxui::Element RenderStatsTab(bool content_focused) const;
  ftxui::Element RenderSkillsTab() const;
  // The MP row with unspent AP right-aligned as "N AP".
  ftxui::Element MpRow(int mp, int ap) const;
  // Renders one allocatable stat row: label/value on the left, a [+] button on
  // the right. The [+] is dimmed when there is no AP to spend, and inverted on
  // the selected row while the content zone holds focus (its Enter target).
  ftxui::Element AllocRow(const std::string& label, int base, int bonus,
                          int index, bool content_focused) const;

  const CharacterInstance& character_;
  int& panel_focus_;
  int active_tab_ = 0;      // which tab is shown: Stats (0) or Skills (1)
  bool on_tab_bar_ = true;  // focus zone: the tab bar (true) or tab content
  int stat_sel_ = 0;  // selected Stats-content row (0-3 = STR/DEX/INT/LUK)
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CHARACTER_PANEL_H_
