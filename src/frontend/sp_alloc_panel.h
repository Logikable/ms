/* SpAllocPanel is the SP (skill point) allocation screen, opened from the SP
 * selector in the character panel's balcony. It will host a tab per job
 * advancement, each listing that stage's skills with allocation controls.
 *
 * For now it is a shell: no skills exist yet, so it shows the available SP for
 * the current job stage and a placeholder. Esc returns to kMain.
 */
#ifndef MS_SRC_FRONTEND_SP_ALLOC_PANEL_H_
#define MS_SRC_FRONTEND_SP_ALLOC_PANEL_H_

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/frontend/types.h"

namespace ms {

class SpAllocPanel {
 public:
  explicit SpAllocPanel(CharacterInstance& character);

  ftxui::Element Render() const;
  // Handles events. Returns kMain on Esc, kSpAlloc otherwise.
  Screen OnEvent(ftxui::Event event);
  // Resets navigation state; call when entering the screen.
  void Reset();

 private:
  CharacterInstance& character_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SP_ALLOC_PANEL_H_
