/* CharacterPanel renders the character pane as a tabbed panel: a "Lv X <job>"
 * title, a Stats/Skills tab bar, and the selected tab's content. The Stats tab
 * shows base stats (HP/MP/STR/DEX/INT/LUK) including equipment bonuses and the
 * equipment-derived combat stats (ATT/MATT); the Skills tab is a placeholder
 * until skills land. Produces a new ftxui Element on each Render() call.
 *
 * When the panel holds focus, Left/Right switch tabs. On the Stats tab, Enter
 * opens the AP allocation screen; this is a temporary bridge until AP is
 * allocated inline on the tab itself.
 */
#ifndef MS_SRC_FRONTEND_CHARACTER_PANEL_H_
#define MS_SRC_FRONTEND_CHARACTER_PANEL_H_

#include <functional>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/frontend/types.h"

namespace ms {

class CharacterPanel {
 public:
  // Wide enough to seat the Stats-tab [+]/[Max] allocation buttons; also the
  // width CombatPanel derives from, so keep it roomy for its map row.
  static constexpr int kTotalWidth = 35;

  explicit CharacterPanel(const CharacterInstance& character, int& panel_focus);
  ftxui::Element Render() const;
  // on_ap opens the AP allocation screen; fired by Enter on the Stats tab when
  // there is unspent AP.
  ftxui::Component MakeComponent(std::function<void()> on_ap);

 private:
  // Formats one stat row padded to kContentWidth. Shows "(base+bonus)" when
  // bonus > 0; omits the breakdown when bonus is zero.
  static std::string StatRow(const std::string& label, int base, int bonus);
  ftxui::Element RenderTabBar() const;
  ftxui::Element RenderStatsTab() const;
  ftxui::Element RenderSkillsTab() const;

  const CharacterInstance& character_;
  int& panel_focus_;
  int active_tab_ = 0;  // which tab is shown: Stats (0) or Skills (1)
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CHARACTER_PANEL_H_
