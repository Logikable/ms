/* ApAllocPanel is the AP allocation screen. It shows a navigable list of
 * stats (STR/DEX/INT/LUK/HP/MP) with their current values formatted as
 * "total (allocated+bonus)". The selected row shows [+1] and [All] buttons;
 * left/right switches focus between them and Enter executes. [All] prompts
 * for confirmation before assigning all available AP to the stat. Esc
 * cancels confirmation or returns to kMain.
 */
#ifndef MS_SRC_FRONTEND_AP_ALLOC_PANEL_H_
#define MS_SRC_FRONTEND_AP_ALLOC_PANEL_H_

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/frontend/types.h"

namespace ms {

class ApAllocPanel {
 public:
  explicit ApAllocPanel(CharacterInstance& character);

  ftxui::Element Render() const;
  // Handles navigation and allocation. Returns kMain on Esc (outside
  // confirmation), kApAlloc otherwise.
  Screen OnEvent(ftxui::Event event);
  // Resets navigation state; call when entering the screen.
  void Reset();

 private:
  CharacterInstance& character_;
  int selected_ = 0;  // stat row index (0-5)
  int button_ = 0;    // 0 = [+1], 1 = [All]
  bool confirming_ = false;
  int confirm_sel_ = 0;  // 0 = Confirm, 1 = Cancel
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_AP_ALLOC_PANEL_H_
